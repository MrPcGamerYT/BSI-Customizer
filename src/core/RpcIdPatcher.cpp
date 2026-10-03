#include "RpcIdPatcher.h"

#include "PatchFile.h"
#include "RpcIdScan.h"
#include "VersionResource.h"
#include "ZlibUtils.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <optional>
#include <string_view>

namespace RpcIdPatcher {
namespace {

using namespace RpcIdScan;

struct State
{
    QString original;
    QString previous;
    QString current;
    QStringList history;
    // The unchanging bytes around the last-written imm64 slot, captured the
    // moment it was written. Once a value has been found and patched, its
    // surroundings almost never change even across builds where the value's
    // own offset does, so this relocates the exact slot again directly -
    // without needing to know (or re-guess) what value is stored there now.
    QString sigName;
    QString sigPre;
    QString sigPost;
    int sigGap = 8;
};

std::string_view view(const QByteArray &data)
{
    return {data.constData(), static_cast<std::size_t>(data.size())};
}

bool readFile(const QString &path, QByteArray *data, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("Could not read %1: %2").arg(QFileInfo(path).fileName(), file.errorString());
        return false;
    }
    *data = file.readAll();
    return true;
}

State loadState(const QString &path)
{
    State state;
    QFile file(statePath(path));
    if (!file.open(QIODevice::ReadOnly))
        return state;
    const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    state.original = object.value(QStringLiteral("original_id")).toString();
    state.previous = object.value(QStringLiteral("previous_id")).toString();
    state.current = object.value(QStringLiteral("current_id")).toString();
    for (const QJsonValue &value : object.value(QStringLiteral("history")).toArray()) {
        const QString id = value.toString();
        if (!id.isEmpty() && !state.history.contains(id))
            state.history << id;
    }
    for (const QString &id : {state.original, state.previous, state.current})
        if (!id.isEmpty() && !state.history.contains(id)) state.history << id;
    const QJsonObject sig = object.value(QStringLiteral("sig")).toObject();
    if (!sig.isEmpty()) {
        state.sigName = sig.value(QStringLiteral("name")).toString();
        state.sigPre = sig.value(QStringLiteral("pre")).toString();
        state.sigPost = sig.value(QStringLiteral("post")).toString();
        state.sigGap = sig.value(QStringLiteral("gap")).toInt(8);
    }
    return state;
}

bool saveState(const QString &path, const QString &original, const QString &current,
               int occurrences, QString *warning, const RpcIdScan::Signature *sig = nullptr)
{
    State old = loadState(path);
    QStringList history = old.history;
    for (const QString &id : {original, current})
        if (!id.isEmpty() && !history.contains(id)) history << id;
    QJsonArray array;
    for (const QString &id : history) array.append(id);
    QJsonObject object{
        {QStringLiteral("version"), 1},
        {QStringLiteral("tool"), QStringLiteral("BSI Customizer native Rich Presence patcher")},
        {QStringLiteral("original_id"), old.original.isEmpty() ? original : old.original},
        {QStringLiteral("current_id"), current},
        {QStringLiteral("previous_id"), original},
        {QStringLiteral("history"), array},
        {QStringLiteral("occurrences"), occurrences},
        {QStringLiteral("updated"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}
    };
    // A freshly captured signature replaces the remembered one; otherwise the
    // previous one (if any) is carried forward instead of being dropped.
    const QString sigName = sig ? QString::fromStdString(sig->name) : old.sigName;
    const QString sigPre = sig ? QString::fromStdString(sig->pre) : old.sigPre;
    const QString sigPost = sig ? QString::fromStdString(sig->post) : old.sigPost;
    const int sigGap = sig ? sig->gap : old.sigGap;
    if (!sigPre.isEmpty() || !sigPost.isEmpty()) {
        object.insert(QStringLiteral("sig"), QJsonObject{
            {QStringLiteral("name"), sigName}, {QStringLiteral("pre"), sigPre},
            {QStringLiteral("post"), sigPost}, {QStringLiteral("gap"), sigGap}
        });
    }
    const QByteArray json = QJsonDocument(object).toJson(QJsonDocument::Indented);
    QSaveFile file(statePath(path));
    if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size() || !file.commit()) {
        if (warning) *warning = QStringLiteral("The ID was changed, but its patch state could not be saved.");
        return false;
    }
    return true;
}

QString currentId(const QByteArray &data, const State &state)
{
    if (!state.current.isEmpty()
        && !sitesForId(view(data), state.current.toStdString()).empty())
        return state.current;
    return {};
}

QStringList leftovers(const QByteArray &data, const State &state, const QStringList &keep)
{
    QStringList result;
    for (const QString &id : state.history) {
        if (id.isEmpty() || keep.contains(id) || result.contains(id)) continue;
        if (!sitesForId(view(data), id.toStdString()).empty()) result << id;
    }
    return result;
}

QString siteSummary(const Candidate &candidate)
{
    QStringList forms;
    for (const Site &site : candidate.sites) {
        const QString form = QString::fromLatin1(kindLabel(site.kind));
        if (!forms.contains(form)) forms << form;
    }
    return QStringLiteral("%1 — %2 occurrence(s): %3; created %4")
        .arg(QString::fromStdString(candidate.id)).arg(candidate.refs)
        .arg(forms.join(QStringLiteral(", "))).arg(QString::fromStdString(candidate.date));
}

/**
 * Look for a decimal Application ID stashed inside a zlib-compressed blob
 * embedded in the file (a packed resource, an update manifest, ...). This is
 * informational only - a `packed` Site refuses to be written back to, since
 * "patch the compressed bytes in place" would just corrupt the stream - but
 * it tells the user an ID exists somewhere the ordinary byte scan can't see
 * into, and what it is.
 */
QStringList scanPackedBlobs(const QByteArray &data, int limit = 60)
{
    QStringList lines;
    QSet<QString> seen;
    int tried = 0;
    for (const qint64 offset : ZlibUtils::findMagicOffsets(data)) {
        if (tried >= limit) break;
        // Compressed application resources are at most a few hundred KB in
        // practice; bounding the slice keeps one early false-positive magic
        // byte near the start of a huge file from copying the whole thing.
        const QByteArray slice = data.mid(offset, 16 * 1024 * 1024);
        if (slice.size() < 32) continue;
        const QByteArray inflated = ZlibUtils::inflateLoose(slice, -1);
        if (inflated.size() < 32) continue;
        ++tried;
        for (const Candidate &record : harvestText(view(inflated))) {
            const QString id = QString::fromStdString(record.id);
            if (seen.contains(id)) continue;
            seen.insert(id);
            lines << QStringLiteral("Packed: %1 inside a compressed blob at offset %2 (not patchable in place).")
                         .arg(id).arg(offset);
            if (seen.size() >= 10) return lines;
        }
    }
    return lines;
}

struct Selection { QString id; std::vector<Site> sites; QString source; };

/**
 * Relocate the id slot from the unchanging bytes around it instead of from
 * its value: the remembered signature from an earlier patch, a known build's
 * exact byte layout, or (last resort) whatever single short run differs from
 * the untouched `.rpcid-bak` backup. This is what makes a file find its slot
 * again even when the remembered current ID text was itself since changed by
 * something else, or no state file survived at all.
 */
std::optional<Selection> locateByPosition(const QString &path, const QByteArray &data,
                                          const State &state)
{
    const std::string_view bytes = view(data);
    const auto fromOffset = [&](std::int64_t off, const QString &source) -> std::optional<Selection> {
        Site site;
        site.kind = Kind::Imm64;
        site.off = off;
        const std::string value = siteValue(bytes, site);
        if (value.empty() || !isAppIdText(value))
            return std::nullopt;
        return Selection{ QString::fromStdString(value), { site }, source };
    };

    if (!state.sigPre.isEmpty() || !state.sigPost.isEmpty()) {
        const Signature sig{ state.sigName.toStdString(), state.sigPre.toStdString(),
                             state.sigPost.toStdString(), state.sigGap };
        const SignatureMatch match = findSignature(bytes, sig);
        if (match.offsets.size() == 1) {
            if (auto found = fromOffset(match.offsets.front(), QStringLiteral("remembered signature")))
                return found;
        }
    }
    for (const Signature &sig : builtinSignatures()) {
        const SignatureMatch match = findSignature(bytes, sig);
        if (match.offsets.size() == 1) {
            if (auto found = fromOffset(match.offsets.front(),
                                        QStringLiteral("built-in signature (%1)")
                                            .arg(QString::fromStdString(sig.name))))
                return found;
        }
    }
    QByteArray backup;
    QString ignoredError;
    if (readFile(backupPath(path), &backup, &ignoredError) && backup.size() == data.size()) {
        for (const std::int64_t off : diffRuns(bytes, view(backup))) {
            if (auto found = fromOffset(off, QStringLiteral("difference against the backup")))
                return found;
        }
    }
    return std::nullopt;
}

std::vector<Selection> select(const QByteArray &data, const QString &path,
                              const QStringList &oldIds, QStringList *notes)
{
    std::vector<Selection> selected;
    if (!oldIds.isEmpty()) {
        for (const QString &raw : oldIds) {
            const QString id = raw.trimmed();
            if (!isAppIdText(id.toStdString())) {
                notes->append(QStringLiteral("%1 is not a valid Discord Application ID.").arg(id));
                continue;
            }
            auto sites = sitesForId(view(data), id.toStdString());
            if (sites.empty()) notes->append(QStringLiteral("%1 is not present in the file.").arg(id));
            else selected.push_back({id, std::move(sites), QStringLiteral("manual entry")});
        }
        return selected;
    }
    const State state = loadState(path);
    const QString remembered = currentId(data, state);
    if (!remembered.isEmpty()) {
        selected.push_back({remembered, sitesForId(view(data), remembered.toStdString()),
                            QStringLiteral("previous patch")});
        return selected;
    }
    if (auto found = locateByPosition(path, data, state)) {
        selected.push_back(*found);
        return selected;
    }
    Pe pe(view(data));
    const auto records = harvest(view(data), &pe);
    const Pick pick = pickCandidate(records);
    if (pick.index >= 0) {
        const Candidate &record = records[static_cast<std::size_t>(pick.index)];
        auto sites = sitesForId(view(data), record.id);
        if (sites.empty()) sites = record.sites;
        selected.push_back({QString::fromStdString(record.id), std::move(sites), QStringLiteral("scan")});
    } else if (!pick.strong.empty()) {
        QStringList ids;
        for (int i : pick.strong) {
            ids << QString::fromStdString(records[static_cast<std::size_t>(i)].id);
            if (ids.size() == 3) break;
        }
        notes->append(QStringLiteral("Possible IDs were found (%1), but none is confirmed. Enter the current ID in the advanced field.").arg(ids.join(QStringLiteral(", "))));
    }
    return selected;
}

} // namespace

QString backupPath(const QString &exePath) { return exePath + QStringLiteral(".rpcid-bak"); }
QString statePath(const QString &exePath) { return exePath + QStringLiteral(".rpcid.json"); }

Status status(const QString &path)
{
    Status out;
    out.hasBackup = QFileInfo::exists(backupPath(path));
    if (!QFileInfo(path).isFile()) {
        out.msg = QStringLiteral("Select an executable.");
        return out;
    }
    QByteArray data;
    QString error;
    if (!readFile(path, &data, &error)) { out.msg = error; return out; }
    const State state = loadState(path);
    out.originalId = state.original;
    const QString remembered = currentId(data, state);
    if (auto found = remembered.isEmpty() ? locateByPosition(path, data, state) : std::nullopt) {
        out.ok = true;
        out.patched = true;
        out.currentId = found->id;
        out.msg = QStringLiteral("Patched to %1 (relocated by %2).").arg(found->id, found->source);
    } else if (!remembered.isEmpty()) {
        out.ok = true;
        out.patched = true;
        out.currentId = remembered;
        out.msg = QStringLiteral("Patched to %1.").arg(remembered);
    } else {
        Pe pe(view(data));
        const auto records = harvest(view(data), &pe);
        const Pick pick = pickCandidate(records);
        for (int index : pick.strong) {
            const Candidate &record = records[static_cast<std::size_t>(index)];
            out.candidates.append({QString::fromStdString(record.id), QString::fromStdString(record.known),
                                   QString::fromStdString(record.date), record.score, record.refs, record.confident});
            if (out.candidates.size() == 6) break;
        }
        if (pick.index >= 0) {
            out.ok = true;
            out.currentId = QString::fromStdString(records[static_cast<std::size_t>(pick.index)].id);
            out.msg = QStringLiteral("Detected %1.").arg(out.currentId);
        } else if (!pick.strong.empty()) {
            out.ok = true;
            out.msg = QStringLiteral("%1 possible ID(s) found; none confirmed. Use Find to review them.").arg(pick.strong.size());
        } else out.msg = QStringLiteral("No Discord Application ID found in this file.");
    }
    out.known = QString::fromStdString(knownName(out.currentId.toStdString()));
    out.leftovers = leftovers(data, state, {remembered, out.currentId});
    if (!out.leftovers.isEmpty())
        out.msg += QStringLiteral(" %1 older ID copy/copies remain.").arg(out.leftovers.size());
    return out;
}

Result find(const QString &path)
{
    Result result;
    QFileInfo info(path);
    QStringList paths;
    if (info.isFile()) paths << info.absoluteFilePath();
    else if (info.isDir()) {
        QDir dir(info.absoluteFilePath());
        const QFileInfoList files = dir.entryInfoList({QStringLiteral("*.exe"), QStringLiteral("*.dll")}, QDir::Files, QDir::Name);
        for (const QFileInfo &file : files)
            if (file.size() <= (512LL << 20)) paths << file.absoluteFilePath();
    }
    if (paths.isEmpty()) return {false, QStringLiteral("Nothing to scan at the selected path."), {}};

    QStringList lines;
    int findings = 0;
    for (const QString &filePath : paths) {
        QByteArray data; QString error;
        if (!readFile(filePath, &data, &error)) { lines << error; continue; }
        Pe pe(view(data));
        const auto records = harvest(view(data), &pe);
        const Pick pick = pickCandidate(records);
        lines << QStringLiteral("%1 — %2, %3 bytes%4")
                     .arg(QFileInfo(filePath).fileName(), pe.ok() ? QString::fromLatin1(pe.arch()) : QStringLiteral("not a PE image"))
                     .arg(data.size()).arg(pe.ok() && pe.isSigned() ? QStringLiteral(", signed") : QString());
        const State state = loadState(filePath);
        const QString remembered = currentId(data, state);
        if (!remembered.isEmpty())
            lines << QStringLiteral("Already patched: current ID %1 (original %2).")
                         .arg(remembered, state.original.isEmpty() ? QStringLiteral("unknown") : state.original);
        if (pick.strong.empty()) {
            lines << QStringLiteral("No Application ID found.");
        } else {
            for (int index : pick.strong) {
                const Candidate &record = records[static_cast<std::size_t>(index)];
                lines << siteSummary(record);
                if (!record.known.empty()) lines << QStringLiteral("Known: %1").arg(QString::fromStdString(record.known));
                result.ids << QString::fromStdString(record.id);
                ++findings;
                if (findings >= 20) break;
            }
        }
        // A separate, bounded sweep for IDs that only exist inside a
        // zlib-compressed blob (e.g. a packed resource) - informational, and
        // independent of whether the plain-bytes scan above found anything.
        lines << scanPackedBlobs(data);
    }
    result.ok = true;
    result.msg = lines.join(QLatin1Char('\n'));
    return result;
}

Result replace(const QString &path, const QString &newIdRaw, const QStringList &oldIds,
               bool dryRun, bool all)
{
    const QString newId = newIdRaw.trimmed();
    if (!isAppIdText(newId.toStdString()))
        return {false, QStringLiteral("Enter a valid Discord Application ID of 17 to 20 digits."), {}};
    if (!QFileInfo(path).isFile()) return {false, QStringLiteral("Select an executable."), {}};
    QByteArray data; QString error;
    if (!readFile(path, &data, &error)) return {false, error, {}};
    const State state = loadState(path);
    QStringList notes;
    auto selected = select(data, path, oldIds, &notes);

    QStringList keep{newId};
    for (const Selection &item : selected) keep << item.id;
    for (const QString &id : leftovers(data, state, keep))
        selected.push_back({id, sitesForId(view(data), id.toStdString()), QStringLiteral("leftover from an earlier patch")});

    if (selected.empty()) {
        const auto existing = sitesForId(view(data), newId.toStdString());
        if (!oldIds.size() && !existing.empty()) {
            QString warning; if (!dryRun) saveState(path, state.original.isEmpty() ? newId : state.original, newId, int(existing.size()), &warning);
            return {true, QStringLiteral("%1 is already the Application ID in this file (%2 occurrence(s)).").arg(newId).arg(existing.size()), {newId}};
        }
        notes << QStringLiteral("Could not find an Application ID in that file. If you know it, enter it in the advanced field.");
        return {false, notes.join(QLatin1Char('\n')), {}};
    }
    int primary = 0;
    for (const Selection &item : selected) if (item.source != QStringLiteral("leftover from an earlier patch")) ++primary;
    if (primary > 1 && !all)
        return {false, QStringLiteral("Several different IDs were found. Enter the current one in the advanced field."), {}};

    QStringList report = notes;
    std::vector<Site> allSites;
    for (const Selection &item : selected) {
        report << QStringLiteral("%1 → %2 (%3 occurrence(s), via %4)").arg(item.id, newId).arg(item.sites.size()).arg(item.source);
        allSites.insert(allSites.end(), item.sites.begin(), item.sites.end());
    }
    if (dryRun) { report << QStringLiteral("Preview only: nothing was written."); return {true, report.join(QLatin1Char('\n')), {newId}}; }

    QByteArray patched = data;
    const WriteReport writes = writeSites(patched.data(), static_cast<std::size_t>(patched.size()), allSites, newId.toStdString());
    if (writes.done.empty()) return {false, QStringLiteral("Nothing could be written without changing the file size."), {}};
    if (!QFileInfo::exists(backupPath(path)) && !QFile::copy(path, backupPath(path)))
        return {false, QStringLiteral("Could not create the original-file backup. No changes were made."), {}};
    if (!PatchFile::writeVerified(path, data, patched, &error)) return {false, error, {}};

    QByteArray fresh;
    if (!readFile(path, &fresh, &error)) return {false, QStringLiteral("The file was written but could not be verified: %1").arg(error), {}};
    auto hits = sitesForId(view(fresh), newId.toStdString());
    // sitesForId() only recognizes one contiguous run of bytes (or text), so
    // it can never see a Split32 pair - its two halves sit a few bytes apart
    // with an opcode in between. Every site actually written is checked
    // directly instead, the same way status() reads a slot back, so a
    // 32-bit build's two-halves write is verified just like any other kind.
    bool allWritten = true;
    for (const Site &site : writes.done) {
        if (siteValue(view(fresh), site) != newId.toStdString()) {
            allWritten = false;
            continue;
        }
        bool already = false;
        for (const Site &found : hits)
            if (found.kind == site.kind && found.off == site.off) { already = true; break; }
        if (!already) hits.push_back(site);
    }
    QStringList history = state.history;
    for (const Selection &item : selected) if (!history.contains(item.id)) history << item.id;
    QStringList stale;
    for (const QString &id : history)
        if (id != newId && !sitesForId(view(fresh), id.toStdString()).empty()) stale << id;
    if (hits.empty() || !allWritten) return {false, QStringLiteral("Could not verify the new ID after writing."), {}};

    // Remember the bytes around one of the exact slots just written, so a
    // later run can relocate it directly even if this ID is then changed by
    // something else (another tool, a BlueStacks self-update that keeps the
    // same layout, ...). Imm64 is the only form this is worth doing for: it
    // is a single fixed-width slot, unlike a text form's variable slack or a
    // Split32 pair's two independent halves.
    std::optional<Signature> freshSignature;
    for (const Site &site : writes.done) {
        if (site.kind != Kind::Imm64) continue;
        freshSignature = makeSignature(view(fresh), site.off, 8, 16,
                                       "captured after patching " + newId.toStdString());
        break;
    }

    QString warning;
    const QString original = state.original.isEmpty() ? selected.front().id : state.original;
    saveState(path, original, newId, int(hits.size()), &warning,
             freshSignature ? &*freshSignature : nullptr);
    report << QStringLiteral("Wrote and verified %1 occurrence(s); %2 skipped.").arg(writes.done.size()).arg(writes.skipped.size());
    report << QStringLiteral("The original file is backed up at %1.").arg(QFileInfo(backupPath(path)).fileName());
    if (!stale.isEmpty()) report << QStringLiteral("Incomplete: older ID copies remain (%1). Use a shorter ID or Undo.").arg(stale.join(QStringLiteral(", ")));
    if (!warning.isEmpty()) report << warning;
    return {true, report.join(QLatin1Char('\n')), {newId}};
}

Result undo(const QString &path)
{
    const QString backup = backupPath(path);
    if (!QFileInfo::exists(backup)) return {false, QStringLiteral("No Rich Presence backup was found for this file."), {}};
    QByteArray current, original; QString error;
    if (!readFile(path, &current, &error) || !readFile(backup, &original, &error)) return {false, error, {}};
    if (!PatchFile::writeVerified(path, current, original, &error)) return {false, error, {}};
    QFile::remove(statePath(path));
    return {true, QStringLiteral("Restored the original file from its Rich Presence backup."), {}};
}

// ------------------------------------------------------- version-resource name

Names names(const QString &path)
{
    Names out;
    QByteArray data;
    QString error;
    if (!QFileInfo(path).isFile()) {
        out.msg = QStringLiteral("Select an executable.");
        return out;
    }
    if (!readFile(path, &data, &error)) { out.msg = error; return out; }
    const VersionResource::Info info = VersionResource::read(view(data));
    if (!info.ok) {
        out.msg = QStringLiteral("%1: %2.").arg(QFileInfo(path).fileName(),
                                                QString::fromStdString(info.error));
        return out;
    }
    for (const VersionResource::StringEntry &entry : info.strings) {
        out.entries.append({ QString::fromStdString(entry.key),
                             QString::fromStdString(entry.value),
                             entry.cap - 1 });
    }
    out.ok = true;
    out.msg = QStringLiteral("%1 editable name(s) in %2.")
                  .arg(out.entries.size()).arg(QFileInfo(path).fileName());
    return out;
}

Result setName(const QString &path, const QString &textRaw, const QString &key, bool dryRun)
{
    const QString text = textRaw.trimmed();
    if (text.isEmpty()) return { false, QStringLiteral("Enter the name to write."), {} };
    if (!QFileInfo(path).isFile()) return { false, QStringLiteral("Select an executable."), {} };

    QByteArray data;
    QString error;
    if (!readFile(path, &data, &error)) return { false, error, {} };
    const VersionResource::Info info = VersionResource::read(view(data));
    if (!info.ok)
        return { false, QStringLiteral("%1: %2.").arg(QFileInfo(path).fileName(),
                                                      QString::fromStdString(info.error)), {} };

    const QStringList keys = key.trimmed().isEmpty()
        ? QStringList{ QStringLiteral("FileDescription"), QStringLiteral("ProductName") }
        : QStringList{ key.trimmed() };

    QByteArray patched = data;
    QStringList written;
    QStringList refused;
    for (const QString &name : keys) {
        const auto matches = VersionResource::withKey(info, name.toStdString());
        if (matches.empty()) {
            refused << QStringLiteral("%1 is not in this file.").arg(name);
            continue;
        }
        for (const VersionResource::StringEntry *entry : matches) {
            std::string why;
            if (VersionResource::setString(patched.data(), static_cast<std::size_t>(patched.size()),
                                           *entry, text.toStdString(), &why)) {
                if (!written.contains(name)) written << name;
            } else {
                refused << QStringLiteral("%1: %2.").arg(name, QString::fromStdString(why));
            }
        }
    }
    if (written.isEmpty()) {
        refused << QStringLiteral("Nothing was changed.");
        return { false, refused.join(QLatin1Char('\n')), {} };
    }

    QStringList report = refused;
    if (dryRun) {
        report << QStringLiteral("Preview only: %1 would become \"%2\". Nothing was written.")
                      .arg(written.join(QStringLiteral(", ")), text);
        return { true, report.join(QLatin1Char('\n')), {} };
    }
    if (!QFileInfo::exists(backupPath(path)) && !QFile::copy(path, backupPath(path)))
        return { false, QStringLiteral("Could not create the original-file backup. No changes were made."), {} };
    if (!PatchFile::writeVerified(path, data, patched, &error))
        return { false, error, {} };

    QByteArray fresh;
    if (!readFile(path, &fresh, &error))
        return { false, QStringLiteral("The file was written but could not be verified: %1").arg(error), {} };
    const VersionResource::Info after = VersionResource::read(view(fresh));
    bool verified = after.ok && fresh.size() == data.size();
    for (const QString &name : written) {
        for (const VersionResource::StringEntry *entry : VersionResource::withKey(after, name.toStdString()))
            verified = verified && QString::fromStdString(entry->value) == text;
    }
    if (!verified)
        return { false, QStringLiteral("The new name could not be verified after writing."), {} };

    report << QStringLiteral("%1 is now \"%2\" (file size unchanged); the original is backed up at %3.")
                  .arg(written.join(QStringLiteral(", ")), text, QFileInfo(backupPath(path)).fileName());
    return { true, report.join(QLatin1Char('\n')), {} };
}

} // namespace RpcIdPatcher
