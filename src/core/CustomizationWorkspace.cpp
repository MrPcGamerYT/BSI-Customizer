#include "CustomizationWorkspace.h"
#include "AboutPatcher.h"
#include "BsDetector.h"
#include "ByteUtils.h"
#include "EmbeddedResources.h"
#include "GifRpc.h"
#include "PatchFile.h"
#include "PlayerAppearancePatcher.h"
#include "AddonPacks.h"
#include "PeUtils.h"
#include "QtResources.h"
#include "ThemeEngine.h"
#include <QBuffer>
#include <QColor>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QJsonParseError>
#include <QLocalSocket>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <QUrl>
#include <functional>

namespace CustomizationWorkspace {
namespace {
constexpr qint64 maxImage = 256LL * 1024 * 1024;
constexpr qint64 maxResource = 32LL * 1024 * 1024;
QVariantMap fail(const QString &s) { return {{"ok", false}, {"msg", s}}; }
bool readBounded(const QString &p, QByteArray *out, qint64 max = maxImage)
{
    QFile f(p);
    if (!f.open(QIODevice::ReadOnly) || f.size() <= 0 || f.size() > max) return false;
    *out = f.read(max + 1);
    return !out->isEmpty() && out->size() <= max && f.error() == QFileDevice::NoError;
}
bool save(const QString &p, const QByteArray &data)
{
    QSaveFile f(p); f.setDirectWriteFallback(false);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size() && f.commit();
}
bool safeTarget(const QString &p)
{
    QFileInfo f(p);
    if (f.suffix().compare(QLatin1String("exe"), Qt::CaseInsensitive) != 0 || !f.isFile() || f.isSymLink() || f.canonicalFilePath().isEmpty()) return false;
#ifdef Q_OS_WIN
    const QString win = QFileInfo(qEnvironmentVariable("SystemRoot")).canonicalFilePath();
    if (!win.isEmpty() && (f.canonicalFilePath().compare(win, Qt::CaseInsensitive) == 0
        || f.canonicalFilePath().startsWith(win + QLatin1Char('/'), Qt::CaseInsensitive))) return false;
#endif
    return true;
}
QString rootPath(const QString &requested)
{
    return requested.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/patch-history") : requested;
}
bool safeRoot(const QString &p)
{
    // No symlinked journal directories or ancestors. The normal root is AppData.
    QFileInfo f(QDir::cleanPath(QFileInfo(p).absoluteFilePath()));
    while (true) {
        if (f.isSymLink() || f.isJunction()) return false;
        const QString parent = f.absolutePath();
        if (parent == f.absoluteFilePath()) break;
        f.setFile(parent);
    }
    return true;
}
QByteArray json(const QVariantMap &m) { return QJsonDocument::fromVariant(m).toJson(QJsonDocument::Indented); }
QVariantMap loadJson(const QString &p, qint64 max)
{
    QByteArray bytes; if (!readBounded(p, &bytes, max)) return {};
    QJsonParseError err; const auto d = QJsonDocument::fromJson(bytes, &err);
    return err.error == QJsonParseError::NoError && d.isObject() ? d.toVariant().toMap() : QVariantMap{};
}
bool validHash(const QString &s)
{
    static const QRegularExpression re(QStringLiteral("^[0-9a-f]{64}$"));
    return re.match(s).hasMatch();
}
QMap<QString, QString> colorMap(const QVariantMap &m)
{
    QMap<QString, QString> out; for (auto it = m.cbegin(); it != m.cend(); ++it) { const QColor c(it.value().toString()); out[it.key()] = c.isValid() ? c.name(c.alpha() == 255 ? QColor::HexRgb : QColor::HexArgb) : it.value().toString(); }
    return out;
}
}
QString digest(const QByteArray &bytes)
{ return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()); }

QVariantMap inspect(const QString &path, const QVariantMap &edits)
{
    QByteArray bytes; if (!safeTarget(path) || !readBounded(path, &bytes)) return fail(QStringLiteral("Select a readable, non-system EXE (maximum 256 MB)."));
    if (PeUtils::sections(bytes).isEmpty()) return fail(QStringLiteral("Invalid PE section table."));
    const auto t = ThemeEngine::detectTheme(bytes, QStringLiteral("qml"));
    const auto entries = ThemeEngine::parseQmlEntries(t.qml);
    QVariantList changes;
    for (const auto &e : entries) if (edits.contains(e.name)) changes << QVariantMap{{"name", e.name}, {"before", e.value}, {"after", edits.value(e.name)}};
    QStringList skipped;
    const QString proposed = ThemeEngine::buildQml(t.qml, entries, colorMap(edits), t.mode == QLatin1String("zlib"), &skipped);
    QString why;
    const qint64 capacity = t.hasQrcNode ? QtResources::inPlaceCapacity(bytes, t.entryOff, t.resource.sourceName, &why) : -1;
    const qint64 required = t.mode == QLatin1String("zlib") ? qCompress(proposed.toLatin1(), 9).size() : proposed.size();
    bool colorsValid = true;
    for (auto it = edits.cbegin(); it != edits.cend(); ++it) colorsValid &= QColor(it.value().toString()).isValid();
    const bool supported = t.found && !t.ambiguous && colorsValid && skipped.isEmpty()
        && (t.mode == QLatin1String("plain") || t.hasQrcNode);
    const auto imports = PeUtils::importedModules(bytes);
    int qt = 0; for (const auto &m : imports.modules) { if (m.startsWith(QLatin1String("Qt6"), Qt::CaseInsensitive)) qt = 6; else if (!qt && m.startsWith(QLatin1String("Qt5"), Qt::CaseInsensitive)) qt = 5; }
    const auto about = AboutPatcher::analyze(bytes);
    return {{"ok", true}, {"sha256", digest(bytes)}, {"bytes", bytes.size()}, {"qtMajor", qt},
        {"themeFound", t.found}, {"themeSupported", supported}, {"ambiguous", t.ambiguous},
        {"mode", t.mode}, {"capacity", capacity}, {"required", required}, {"used", t.storedSize},
        {"originalDecodedBytes", t.qml.size() * (t.mode == QLatin1String("plain") ? t.mult : 1)},
        {"replacementDecodedBytes", proposed.size() * (t.mode == QLatin1String("plain") ? t.mult : 1)},
        {"originalCompressedBytes", t.compSize},
        {"replacementCompressedBytes", t.mode == QLatin1String("zlib") ? required - 4 : 0},
        {"relocationSupport", t.hasQrcNode ? QStringLiteral("Verified mapping; exact free storage must pass Dry Run") : QStringLiteral("Unsupported/unverified mapping")},
        {"relocationRequired", capacity >= 0 && required > capacity}, {"changes", changes},
        {"aboutSupported", about.supported}, {"mapping", t.resource.detail},
        {"msg", supported ? QStringLiteral("Theme mapping is verified. Dry Run checks the exact write before Apply.")
            : QStringLiteral("Theme writing is blocked: %1. No file was changed.").arg(!colorsValid ? QStringLiteral("invalid color") : (!skipped.isEmpty() ? skipped.join(QStringLiteral("; ")) : (t.resource.detail.isEmpty() ? QStringLiteral("missing or ambiguous active resource") : t.resource.detail)))}};
}

QVariantMap validateProfile(const QVariantMap &profile)
{
    const QSet<QString> allowed{QStringLiteral("schema"), QStringLiteral("version"), QStringLiteral("name"), QStringLiteral("sourceSha256"), QStringLiteral("theme"), QStringLiteral("boot"), QStringLiteral("gifRpc"), QStringLiteral("about"), QStringLiteral("appearance")};
    for (auto it = profile.cbegin(); it != profile.cend(); ++it) if (!allowed.contains(it.key())) return fail(QStringLiteral("Unknown profile field: %1. Credentials and raw offsets are not accepted.").arg(it.key()));
    if (profile.value("schema").toString() != QLatin1String("bsi.customization.profile") || profile.value("version").toDouble() != 1.0 || profile.value("version").metaType().id() == QMetaType::Bool || profile.value("version").metaType().id() == QMetaType::QString) return fail(QStringLiteral("Unsupported profile schema/version."));
    if (json(profile).size() > 256 * 1024 || profile.value("name").toString().size() > 128) return fail(QStringLiteral("Profile exceeds the size limits."));
    for (const QString &key : {QStringLiteral("theme"), QStringLiteral("boot"), QStringLiteral("gifRpc"), QStringLiteral("about"), QStringLiteral("appearance")})
        if (profile.contains(key) && profile.value(key).metaType().id() != QMetaType::QVariantMap) return fail(QStringLiteral("Profile section %1 must be an object.").arg(key));
    for (const QString &key : {QStringLiteral("schema"), QStringLiteral("name"), QStringLiteral("sourceSha256")})
        if (profile.contains(key) && profile.value(key).metaType().id() != QMetaType::QString) return fail(QStringLiteral("Profile %1 must be text.").arg(key));
    const auto colors = profile.value("theme").toMap();
    if (colors.size() > 1024) return fail(QStringLiteral("Too many theme colors."));
    static const QRegularExpression name(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    for (auto it = colors.cbegin(); it != colors.cend(); ++it)
        if (!name.match(it.key()).hasMatch() || it.value().metaType().id() != QMetaType::QString || !QColor(it.value().toString()).isValid()) return fail(QStringLiteral("Invalid profile color: %1.").arg(it.key()));
    const auto appearance = profile.value("appearance").toMap();
    if (appearance.size() > 512) return fail(QStringLiteral("Too many appearance tokens in one profile."));
    static const QRegularExpression appearanceKey(QStringLiteral("^[^#\\s]{1,512}#root\\.[A-Za-z0-9_.]+$"));
    for (auto it = appearance.cbegin(); it != appearance.cend(); ++it) {
        if (!appearanceKey.match(it.key()).hasMatch() || it.key().contains(QStringLiteral("..")))
            return fail(QStringLiteral("Invalid appearance token key: %1.").arg(it.key()));
        const int type = it.value().metaType().id();
        bool ok = false; const int number = it.value().toInt(&ok);
        if (!ok || type == QMetaType::Bool || type == QMetaType::QString || it.value().toDouble() != number || number < 0 || number > 1000000)
            return fail(QStringLiteral("Appearance profile values must be whole numbers: %1.").arg(it.key()));
    }
    const auto boot = profile.value("boot").toMap();
    for (auto it = boot.cbegin(); it != boot.cend(); ++it) if (it.key() != QLatin1String("holdMs")) return fail(QStringLiteral("Unknown boot setting."));
    bool holdOk = true; if (boot.contains("holdMs")) boot.value("holdMs").toInt(&holdOk);
    if (boot.contains("holdMs") && (!holdOk || boot.value("holdMs").metaType().id() == QMetaType::QString || boot.value("holdMs").metaType().id() == QMetaType::Bool || boot.value("holdMs").toDouble() != boot.value("holdMs").toInt() || boot.value("holdMs").toInt() < 0 || boot.value("holdMs").toInt() > 15000)) return fail(QStringLiteral("Boot hold must be 0–15000 ms."));
    const auto rpc = profile.value("gifRpc").toMap(); QString why;
    const QSet<QString> rpcKeys{ "AppId", "State", "Details", "LargeImage", "LargeText", "SmallImage", "SmallText", "Button1Label", "Button1Url", "Button2Label", "Button2Url", "Enabled", "Timestamp", "Interval" };
    for (auto it = rpc.cbegin(); it != rpc.cend(); ++it) if (!rpcKeys.contains(it.key())) return fail(QStringLiteral("Unknown GIF RPC setting: %1.").arg(it.key()));
    for (const QString &key : {QStringLiteral("Enabled"), QStringLiteral("Timestamp")})
        if (rpc.contains(key) && rpc.value(key).metaType().id() != QMetaType::Bool) return fail(QStringLiteral("RPC boolean settings must be true/false."));
    if (rpc.contains("AppId") && rpc.value("AppId").metaType().id() != QMetaType::QString) return fail(QStringLiteral("Store Discord application IDs as strings to avoid numeric precision loss."));
    if (rpc.contains("Interval") && (rpc.value("Interval").metaType().id() == QMetaType::Bool || rpc.value("Interval").metaType().id() == QMetaType::QString || rpc.value("Interval").toDouble() != rpc.value("Interval").toInt())) return fail(QStringLiteral("RPC interval must be a whole number."));
    if (!rpc.isEmpty() && !GifRpc::validate(rpc, &why)) return fail(why);
    const auto about = profile.value("about").toMap();
    for (auto it = about.cbegin(); it != about.cend(); ++it) if (it.key() != QLatin1String("text") && it.key() != QLatin1String("showSections") && it.key() != QLatin1String("links")) return fail(QStringLiteral("Unknown About setting."));
    if (about.contains("showSections") && about.value("showSections").metaType().id() != QMetaType::Bool) return fail(QStringLiteral("About showSections must be true/false."));
    if (about.contains("text") && about.value("text").metaType().id() != QMetaType::QString) return fail(QStringLiteral("About text must be text."));
    const auto links = about.value("links").toList();
    if (links.size() > 32 || (about.contains("links") && about.value("links").metaType().id() != QMetaType::QVariantList)) return fail(QStringLiteral("About links must be an array with at most 32 entries."));
    QSet<QString> linkIds;
    for (const auto &v : links) {
        if (v.metaType().id() != QMetaType::QVariantMap) return fail(QStringLiteral("About link entries must be objects."));
        const auto link = v.toMap(); const QString id = link.value("id").toString(), target = link.value("url").toString();
        for (auto it = link.cbegin(); it != link.cend(); ++it)
            if (it.key() != QLatin1String("id") && it.key() != QLatin1String("name") && it.key() != QLatin1String("url") && it.key() != QLatin1String("label") && it.key() != QLatin1String("hidden")) return fail(QStringLiteral("Unknown About link field."));
        for (const QString &key : {QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("url"), QStringLiteral("label")})
            if (link.contains(key) && (link.value(key).metaType().id() != QMetaType::QString || link.value(key).toString().contains(QChar(0)))) return fail(QStringLiteral("About link text fields must contain text without NUL bytes."));
        if (link.contains("hidden") && link.value("hidden").metaType().id() != QMetaType::Bool) return fail(QStringLiteral("About link hidden must be true/false."));
        if (QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f]")).match(target).hasMatch()) return fail(QStringLiteral("About link URLs cannot contain control characters."));
        if (!name.match(id).hasMatch() || linkIds.contains(id) || id.size() > 128 || link.value("name").toString().size() > 256 || link.value("label").toString().size() > 256 || target.size() > 2048) return fail(QStringLiteral("Invalid or duplicate About link."));
        linkIds.insert(id);
        const QUrl url(target);
        if (!target.isEmpty() && (!url.isValid() || !url.userInfo().isEmpty() || (url.scheme() != QLatin1String("https") && url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("mailto")) || ((url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https")) && url.host().isEmpty()))) return fail(QStringLiteral("About profile links must be HTTP(S) or mailto URLs, never executable/local-file URLs."));
    }
    if (about.value("text").toString().size() > 4096 || about.value("text").toString().contains(QChar(0))) return fail(QStringLiteral("About text is too long."));
    const QString hash = profile.value("sourceSha256").toString();
    if (!hash.isEmpty() && !validHash(hash)) return fail(QStringLiteral("Invalid profile source fingerprint."));
    return {{"ok", true}, {"profile", profile}, {"msg", QStringLiteral("Profile validated. Loading stages settings only; nothing is deployed automatically.")}};
}
QVariantMap readProfile(const QString &path)
{ return validateProfile(loadJson(path, 256 * 1024)); }
QVariantMap saveProfile(const QString &path, const QVariantMap &profile)
{
    const auto r = validateProfile(profile); if (!r.value("ok").toBool()) return r;
    if (QFileInfo(path).suffix().compare(QLatin1String("json"), Qt::CaseInsensitive) != 0) return fail(QStringLiteral("Profiles must be saved as .json, never over an EXE."));
    return save(path, json(profile)) ? r : fail(QStringLiteral("Could not save the profile atomically."));
}
QVariantMap stageResource(const QString &path, const QString &replacement)
{
    QByteArray data; if (!readBounded(replacement, &data, maxResource)) return fail(QStringLiteral("Replacement must be a readable, non-empty file up to 32 MB."));
    if (path.isEmpty() || path.size() > 1024) return fail(QStringLiteral("Select an exact resource path from a manual scan."));
    return {{"ok", true}, {"operation", QVariantMap{{"kind", "resource"}, {"path", path}, {"data", QString::fromLatin1(data.toBase64())}, {"sha256", digest(data)}}},
        {"preview", EmbeddedResources::previewDataUrl(data, QFileInfo(replacement).suffix(), 512)},
        {"text", EmbeddedResources::decodeText(data).left(16384)}, {"msg", QStringLiteral("Replacement staged in memory. Run Dry Run before applying.")}};
}
QVariantMap inspectAppearance(const QString &path)
{
    QByteArray bytes;
    if (!safeTarget(path) || !readBounded(path, &bytes)) return fail(QStringLiteral("Select a readable, non-system EXE (maximum 256 MB)."));
    if (PeUtils::sections(bytes).isEmpty()) return fail(QStringLiteral("Invalid PE section table."));
    auto out = PlayerAppearancePatcher::toMap(PlayerAppearancePatcher::analyze(bytes));
    out.insert(QStringLiteral("sha256"), digest(bytes));
    return out;
}
QVariantMap stageAppearance(const QString &path, const QVariantMap &edits)
{
    QByteArray bytes;
    if (!safeTarget(path) || !readBounded(path, &bytes)) return fail(QStringLiteral("Select a readable, non-system EXE (maximum 256 MB)."));
    if (PeUtils::sections(bytes).isEmpty()) return fail(QStringLiteral("Invalid PE section table."));
    const auto validation = PlayerAppearancePatcher::validateEdits(bytes, edits);
    if (!validation.value(QStringLiteral("ok")).toBool()) return validation;
    const auto normalized = validation.value(QStringLiteral("edits")).toMap();
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("operation"), QVariantMap{{QStringLiteral("kind"), QStringLiteral("appearance")}, {QStringLiteral("edits"), normalized}}},
            {QStringLiteral("resources"), validation.value(QStringLiteral("resources"))},
            {QStringLiteral("msg"), QStringLiteral("Appearance edits staged in memory. Run Dry Run before applying.")}};
}
QVariantMap simulate(const QString &path, const QVariantList &operations, const QString &expectedHash, QByteArray *prepared)
{
    if (prepared) prepared->clear();
    QByteArray original; if (!safeTarget(path) || !readBounded(path, &original)) return fail(QStringLiteral("The selected EXE is not a safe readable target."));
    if (!validHash(expectedHash) || digest(original) != expectedHash) return fail(QStringLiteral("The EXE changed or has not been inspected. Inspect again and rebuild the plan."));
    if (operations.isEmpty() || operations.size() > 32) return fail(QStringLiteral("A batch must contain 1–32 operations."));
    QTemporaryDir tmp; if (!tmp.isValid()) return fail(QStringLiteral("Cannot create an isolated dry-run directory."));
    const QString stage = tmp.filePath(QStringLiteral("HD-Player.exe"));
    QByteArray working = original; QSet<QString> touched; QVariantList steps; qint64 total = 0;
    // Only unique resource paths, one theme operation and one appearance
    // operation are accepted. Aliased records are checked by payload offset too;
    // raw theme + theme edits and appearance + same-payload edits conflict.
    QSet<qint64> payloads;
    const auto theme = ThemeEngine::detectTheme(original, QStringLiteral("qml"));
    for (const auto &v : operations) {
        const auto op = v.toMap(); const QString kind = op.value("kind").toString();
        const QString key = kind == QLatin1String("theme") ? QStringLiteral("@theme")
            : (kind == QLatin1String("appearance") ? QStringLiteral("@appearance") : op.value("path").toString());
        if (touched.contains(key)) return fail(QStringLiteral("Conflicting duplicate operation: %1.").arg(key));
        touched.insert(key);
        if (!save(stage, working)) return fail(QStringLiteral("Could not stage the dry-run image."));
        QByteArray next; bool ok = false; QString msg;
        if (kind == QLatin1String("theme")) {
            if (payloads.contains(theme.entryOff)) return fail(QStringLiteral("Theme edits conflict with a raw replacement of the same payload."));
            payloads.insert(theme.entryOff);
            const auto info = ThemeEngine::detectTheme(working, QStringLiteral("qml"));
            const auto r = ThemeEngine::patchTheme(stage, info, ThemeEngine::parseQmlEntries(info.qml), colorMap(op.value("edits").toMap()), {}, &next);
            ok = r.ok; msg = r.msg;
            if (ok && op.contains("pack")) {
                const auto *pack = AddonPacks::find(op.value("pack").toString());
                if (!pack) return fail(QStringLiteral("Unknown add-on pack."));
                const auto originalAnalysis = PlayerAppearancePatcher::analyze(original);
                const auto originalEdits = AddonPacks::designEdits(*pack, originalAnalysis);
                for (const auto &token : originalAnalysis.tokens) {
                    if (!originalEdits.contains(token.key)) continue;
                    int matches = 0; EmbeddedResources::Entry selected;
                    for (const auto &e : EmbeddedResources::list(original, true))
                        if (e.path == token.resource) { selected = e; ++matches; }
                    if (matches != 1 || touched.contains(token.resource))
                        return fail(QStringLiteral("Ambiguous or conflicting add-on design resource."));
                    if (selected.entryOff != theme.entryOff && payloads.contains(selected.entryOff))
                        return fail(QStringLiteral("Add-on design conflicts with an earlier resource edit."));
                }
                const auto design = AddonPacks::designEdits(*pack, PlayerAppearancePatcher::analyze(next));
                if (design.isEmpty()) return fail(QStringLiteral("Supported add-on design tokens disappeared after palette preparation."));
                QString error;
                ok = PlayerAppearancePatcher::apply(next, design, &error);
                if (!ok) msg = error;
                else msg += QStringLiteral(" Add-on button geometry verified; animations and non-QML artwork unchanged.");
                for (const auto &token : originalAnalysis.tokens) if (originalEdits.contains(token.key)) {
                    for (const auto &e : EmbeddedResources::list(original, true))
                        if (e.path == token.resource) payloads.insert(e.entryOff);
                    touched.insert(token.resource);
                }
            }
        } else if (kind == QLatin1String("appearance")) {
            const auto validation = PlayerAppearancePatcher::validateEdits(working, op.value("edits").toMap());
            if (!validation.value("ok").toBool()) return fail(validation.value("msg").toString());
            const QStringList affected = validation.value("resources").toStringList();
            for (const QString &resource : affected) {
                int matches = 0; EmbeddedResources::Entry selected;
                // Conflict identity belongs to the inspected original image. A
                // preceding palette/resource write may legitimately relocate a
                // record into a mapped gap; comparing that new offset with the
                // original theme offset would miss a same-payload conflict.
                for (const auto &e : EmbeddedResources::list(original, true)) if (e.path == resource) { selected = e; ++matches; }
                if (matches != 1 || payloads.contains(selected.entryOff) || touched.contains(resource))
                    return fail(QStringLiteral("Appearance edits conflict with another staged edit of %1.").arg(resource));
                if (touched.contains(QStringLiteral("@theme")) && selected.entryOff == theme.entryOff)
                    return fail(QStringLiteral("Appearance edits conflict with palette edits of the same theme payload."));
                payloads.insert(selected.entryOff);
                touched.insert(resource);
            }
            QByteArray preparedAppearance = working;
            QString appearanceError;
            ok = PlayerAppearancePatcher::apply(preparedAppearance, op.value("edits").toMap(), &appearanceError);
            next = preparedAppearance;
            msg = ok ? QStringLiteral("%1 appearance token(s) rewritten and read back in memory.").arg(op.value("edits").toMap().size()) : appearanceError;
        } else if (kind == QLatin1String("resource")) {
            const QString encoded = op.value("data").toString();
            if (encoded.size() > (maxResource * 4 / 3 + 8)) return fail(QStringLiteral("Encoded replacement exceeds 32 MB."));
            const auto decoded = QByteArray::fromBase64Encoding(encoded.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
            const QByteArray data = decoded.decoded;
            total += data.size();
            if (!decoded || data.isEmpty() || data.size() > maxResource || total > 64 * 1024 * 1024 || digest(data) != op.value("sha256").toString()) return fail(QStringLiteral("Invalid or oversized staged replacement."));
            int matches = 0; EmbeddedResources::Entry selected;
            for (const auto &e : EmbeddedResources::list(original, true)) if (e.path == key) { selected = e; ++matches; }
            if (matches != 1 || payloads.contains(selected.entryOff)) return fail(QStringLiteral("Missing, ambiguous or aliased resource conflict: %1.").arg(key));
            payloads.insert(selected.entryOff);
            if (touched.contains(QStringLiteral("@theme")) && selected.entryOff == theme.entryOff) return fail(QStringLiteral("Raw theme replacement conflicts with palette edits."));
            const auto r = EmbeddedResources::replaceBytes(stage, key, data, &next); ok = r.ok; msg = r.msg;
        } else return fail(QStringLiteral("Unsupported batch operation. Only theme, resource and appearance edits belong to this single-EXE transaction."));
        QVariantMap step{{"operation", key}, {"ok", ok}, {"msg", msg}};
        if (op.contains("pack")) step["pack"] = op.value("pack");
        steps << step;
        if (!ok || next.isEmpty()) return {{"ok", false}, {"msg", msg}, {"steps", steps}};
        if (next.size() > maxImage) return fail(QStringLiteral("Prepared EXE exceeds the 256 MB recovery limit; no files changed."));
        working = next;
    }
    if (prepared) *prepared = working;
    return {{"ok", true}, {"sha256", expectedHash}, {"outputSha256", digest(working)}, {"bytesBefore", original.size()}, {"bytesAfter", working.size()}, {"steps", steps},
        {"msg", QStringLiteral("All %1 operations verified on an isolated copy. No emulator files, backups or caches were changed.").arg(operations.size())}};
}
QVariantList history(const QString &requested)
{
    QVariantList out; const QString root = rootPath(requested); if (!safeRoot(root)) return out;
    const auto files = QDir(root).entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Time);
    for (const auto &name : files.mid(0, 200)) {
        if (QFileInfo(root + QLatin1Char('/') + name).isSymLink()) continue;
        const auto m = loadJson(root + QLatin1Char('/') + name, 64 * 1024);
        if (!m.isEmpty()) out << m;
    }
    return out;
}
QVariantMap apply(const QString &path, const QVariantList &operations, const QString &expectedHash, const QString &expectedOutputHash, const QString &requested)
{
    if (!safeTarget(path)) return fail(QStringLiteral("Select a safe readable EXE target before applying."));
    QLockFile lock(QFileInfo(path).canonicalFilePath() + QStringLiteral(".bsi-workspace.lock"));
    if (!lock.tryLock(0)) return fail(QStringLiteral("Another Workspace writer is using this EXE, or its directory is not writable."));
    if (BsDetector::isProcessRunning(QStringLiteral("HD-Player.exe"))) return fail(QStringLiteral("Close all HD-Player instances before applying the batch."));
    QByteArray changed; const auto r = simulate(path, operations, expectedHash, &changed);
    if (!r.value("ok").toBool()) return r;
    if (!validHash(expectedOutputHash) || digest(changed) != expectedOutputHash) return fail(QStringLiteral("Plan changed since Dry Run. Review a fresh Dry Run before applying."));
    QByteArray original; if (!readBounded(path, &original) || digest(original) != expectedHash) return fail(QStringLiteral("Target changed during preflight."));
    if (original == changed) return fail(QStringLiteral("The batch makes no byte changes."));
    const QString root = rootPath(requested);
    if (!safeRoot(root) || !QDir().mkpath(root)) return fail(QStringLiteral("Cannot create a safe recovery directory."));
    const QString id = QUuid::createUuid().toString(QUuid::Id128);
    QVariantList summary;
    for (const QVariant &value : operations) {
        const QVariantMap op = value.toMap();
        QVariantMap description{{"kind",op.value("kind")}};
        if (op.value("kind").toString() == QLatin1String("theme")) {
            const QStringList names = op.value("edits").toMap().keys();
            if (op.contains("pack")) description["pack"] = op.value("pack");
            description["colorCount"] = names.size();
            description["properties"] = names.mid(0,64);
        } else if (op.value("kind").toString() == QLatin1String("appearance")) {
            const QStringList names = op.value("edits").toMap().keys();
            description["tokenCount"] = names.size();
            description["tokens"] = names.mid(0,64);
        } else {
            description["path"] = op.value("path");
            description["replacementSha256"] = digest(QByteArray::fromBase64(op.value("data").toByteArray()));
        }
        summary.append(description);
    }
    QVariantMap entry{{"id", id}, {"path", QFileInfo(path).canonicalFilePath()}, {"before", expectedHash}, {"after", expectedOutputHash},
        {"time", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}, {"state", "prepared"}, {"operationCount", operations.size()}, {"operations", summary}};
    if (json(entry).size() > 60*1024) return fail(QStringLiteral("Recovery metadata exceeds the safe journal limit; no EXE was written."));
    if (!save(root + QLatin1Char('/') + id + QStringLiteral(".bin"), original)
        || !save(root + QLatin1Char('/') + id + QStringLiteral(".json"), json(entry))) return fail(QStringLiteral("Could not preserve verified recovery data; batch cancelled."));
    QString error;
    if (!ThemeEngine::backup(path) || !PatchFile::writeVerified(path, original, changed, &error)) return fail(QStringLiteral("Commit/verification failed; the EXE may already have been replaced. Recovery journal %1 is retained. %2").arg(id, error));
    entry["state"] = QStringLiteral("applied");
    const bool journal = save(root + QLatin1Char('/') + id + QStringLiteral(".json"), json(entry));
    QtResources::clearPlayerQmlCache(path);
    return {{"ok", true}, {"id", id}, {"msg", journal ? QStringLiteral("Batch committed and verified. Recovery snapshot: %1. Restart the emulator.").arg(id)
        : QStringLiteral("Batch committed; recovery data is preserved but the journal status could not be updated.")}};
}
QVariantMap restore(const QString &path, const QString &id, const QString &requested)
{
    static const QRegularExpression re(QStringLiteral("^[0-9a-f]{32}$"));
    const QString root = rootPath(requested);
    if (!re.match(id).hasMatch() || !safeRoot(root) || !safeTarget(path)) return fail(QStringLiteral("Invalid recovery target or identifier."));
    QLockFile lock(QFileInfo(path).canonicalFilePath() + QStringLiteral(".bsi-workspace.lock"));
    if (!lock.tryLock(0)) return fail(QStringLiteral("Another Workspace writer is using this EXE, or its directory is not writable."));
    if (BsDetector::isProcessRunning(QStringLiteral("HD-Player.exe"))) return fail(QStringLiteral("Close HD-Player before restoring."));
    const QString meta = root + QLatin1Char('/') + id + QStringLiteral(".json"), snapshot = root + QLatin1Char('/') + id + QStringLiteral(".bin");
    if (QFileInfo(meta).isSymLink() || QFileInfo(snapshot).isSymLink()) return fail(QStringLiteral("Linked recovery files are not accepted."));
    auto entry = loadJson(meta, 64 * 1024); QByteArray before, now;
    if (entry.value("id").toString() != id || entry.value("path").toString() != QFileInfo(path).canonicalFilePath()
        || !readBounded(snapshot, &before) || digest(before) != entry.value("before").toString()
        || !readBounded(path, &now) || digest(now) != entry.value("after").toString()) return fail(QStringLiteral("Recovery hash/path mismatch. The EXE was updated or modified; automatic restore is blocked."));
    QString error; if (!PatchFile::writeVerified(path, now, before, &error)) return fail(error);
    entry["state"] = QStringLiteral("restored"); const bool journal = save(meta, json(entry));
    QtResources::clearPlayerQmlCache(path);
    return {{"ok", true}, {"msg", journal ? QStringLiteral("Recovery snapshot restored and verified.") : QStringLiteral("Restored and verified; journal status update failed.")}};
}
QVariantMap exportDiagnostics(const QString &path, const QString &destination)
{
    if (QFileInfo(destination).suffix().compare(QLatin1String("txt"), Qt::CaseInsensitive) != 0 || QFileInfo(path).absoluteFilePath() == QFileInfo(destination).absoluteFilePath()) return fail(QStringLiteral("Choose a separate .txt report file."));
    QByteArray data; if (!readBounded(path, &data)) return fail(QStringLiteral("Cannot read the selected EXE."));
    const auto i = inspect(path);
    const QByteArray report = ThemeEngine::probeReport(path).toUtf8() + "\nCompatibility summary\n" + json(i);
    return save(destination, report) ? QVariantMap{{"ok", true}, {"msg", QStringLiteral("Read-only diagnostic report exported. Review paths and resource previews before sharing.")}} : fail(QStringLiteral("Report could not be saved."));
}
QVariantMap prepareImage(const QString &source, const QString &destination, int width, int height, bool crop, int quality)
{
    if (width < 1 || height < 1 || width > 8192 || height > 8192 || qint64(width) * height > 16000000 || quality < 1 || quality > 100) return fail(QStringLiteral("Image dimensions must be 1–8192, at most 16 megapixels; quality must be 1–100."));
    if (QFileInfo(source).canonicalFilePath() == QFileInfo(destination).canonicalFilePath() || QFileInfo(source).absoluteFilePath() == QFileInfo(destination).absoluteFilePath()) return fail(QStringLiteral("Save the prepared image to a new file, not over its source."));
    const QString format = QFileInfo(destination).suffix().toLower();
    if (format != QLatin1String("png") && format != QLatin1String("jpg") && format != QLatin1String("jpeg") && format != QLatin1String("webp")) return fail(QStringLiteral("Prepared images must be PNG, JPEG or WebP."));
    if (QFileInfo(source).size() > maxResource) return fail(QStringLiteral("Source image exceeds 32 MB."));
    QImageReader reader(source); reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (!size.isValid() || qint64(size.width()) * size.height() > 16000000) return fail(QStringLiteral("Image header is invalid or exceeds 16 megapixels."));
    const QImage image = reader.read(); if (image.isNull()) return fail(reader.errorString());
    if (image.hasAlphaChannel() && (format == QLatin1String("jpg") || format == QLatin1String("jpeg"))) return fail(QStringLiteral("JPEG would discard transparency. Choose PNG/WebP or flatten the image explicitly in an editor."));
    // Crop BEFORE scaling: expanding a very thin source could otherwise
    // allocate an enormous intermediate even for a small requested output.
    QImage cropped = image;
    if (crop) {
        int w = image.width(), h = image.height();
        if (qint64(w) * height > qint64(h) * width) w = qMax(1, int(qint64(h) * width / height));
        else h = qMax(1, int(qint64(w) * height / width));
        cropped = image.copy((image.width() - w) / 2, (image.height() - h) / 2, w, h);
    }
    const QImage output = cropped.scaled(width, height, crop ? Qt::IgnoreAspectRatio : Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (output.isNull()) return fail(QStringLiteral("Image scaling failed; no output was saved."));
    QByteArray encoded; QBuffer buffer(&encoded); buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, format.toLatin1()); writer.setQuality(quality);
    if (!writer.write(output) || !save(destination, encoded)) return fail(QStringLiteral("Prepared image could not be encoded/saved."));
    return {{"ok", true}, {"bytes", encoded.size()}, {"width", output.width()}, {"height", output.height()}, {"alpha", output.hasAlphaChannel()},
        {"preview", EmbeddedResources::previewDataUrl(encoded, format, 512)}, {"msg", QStringLiteral("Prepared %1×%2 image (%3 bytes). Original preserved; review and stage the output separately.").arg(output.width()).arg(output.height()).arg(encoded.size())}};
}
QVariantMap inspectVideo(const QString &source)
{
    // Header-only MP4 inspection. This intentionally does not claim playback
    // compatibility merely because an ftyp box exists or a codec is named.
    QFile f(source); if (!f.open(QIODevice::ReadOnly) || f.size() < 16) return fail(QStringLiteral("Cannot read the video."));
    QVariantMap out{{"ok", true}, {"bytes", f.size()}, {"width", 0}, {"height", 0}, {"durationSeconds", 0.0}};
    QStringList codecs; int boxes = 0; bool ftyp = false;
    std::function<bool(qint64,qint64,int)> walk = [&](qint64 start, qint64 end, int depth) {
        if (depth > 12) return false;
        for (qint64 at = start; at + 8 <= end;) {
            if (++boxes > 10000 || !f.seek(at)) return false;
            const QByteArray h = f.read(16); if (h.size() < 8) return false;
            quint64 n = ByteUtils::be32(h, 0); int header = 8;
            if (n == 1) { if (h.size() < 16) return false; n = ByteUtils::be64(h, 8); header = 16; }
            if (n == 0) n = quint64(end - at);
            if (n < quint64(header) || n > quint64(end - at)) return false;
            const QByteArray type = h.mid(4,4); const qint64 body = at + header, stop = at + qint64(n);
            if (type == "ftyp") ftyp = true;
            if (type == "moov" || type == "trak" || type == "mdia" || type == "minf" || type == "stbl") { if (!walk(body, stop, depth + 1)) return false; }
            else if (type == "tkhd" && n >= quint64(header + 8)) {
                f.seek(stop - 8); const auto dimensions = f.read(8);
                const quint32 w = ByteUtils::be32(dimensions,0) >> 16, height = ByteUtils::be32(dimensions,4) >> 16;
                if (w && height) { out["width"] = w; out["height"] = height; }
            } else if (type == "mvhd" && n >= quint64(header + 20)) {
                f.seek(body); const auto b = f.read(qMin<qint64>(32, stop - body)); const bool v1 = b.size() && b[0] == 1;
                if ((!v1 && b.size() >= 20) || (v1 && b.size() >= 32)) { const quint32 scale = ByteUtils::be32(b, v1 ? 20 : 12); const quint64 duration = v1 ? ByteUtils::be64(b,24) : ByteUtils::be32(b,16); if (scale) out["durationSeconds"] = double(duration) / scale; }
            } else if (type == "stsd" && n >= quint64(header + 16)) {
                f.seek(body); const auto b = f.read(16); const QString codec = QString::fromLatin1(b.mid(12,4)); if (!codecs.contains(codec)) codecs << codec;
            }
            at = stop;
        }
        return true;
    };
    if (!walk(0, f.size(), 0) || !ftyp) return fail(QStringLiteral("Not a structurally readable MP4 container. No video was changed."));
    out["codecs"] = codecs; out["decoderVerified"] = false;
    out["msg"] = QStringLiteral("MP4 inspected: %1×%2, %3 seconds; sample entries: %4. Decoder/playback must be verified in the target emulator. No conversion was performed.").arg(out.value("width").toInt()).arg(out.value("height").toInt()).arg(out.value("durationSeconds").toDouble(),0,'f',2).arg(codecs.join(QStringLiteral(", ")));
    return out;
}
QVariantMap rpcDiagnostics(const QString &exePath, const QVariantMap &settings)
{
    auto out = GifRpc::inspect(exePath); QString why;
    out["configurationValid"] = GifRpc::validate(settings, &why); out["validationMessage"] = why;
    bool reachable = false;
#ifdef Q_OS_WIN
    for (int i = 0; i < 10; ++i) { QLocalSocket socket; socket.connectToServer(QStringLiteral("discord-ipc-%1").arg(i)); if (socket.waitForConnected(100)) { reachable = true; socket.disconnectFromServer(); break; } }
#endif
    out["ipcReachable"] = reachable; out["proxyConnectionVerified"] = false;
    out["msg"] = QStringLiteral("Configuration: %1. Discord IPC endpoint: %2. Endpoint reachability does not prove the emulator loaded the proxy, accepted activity or displayed an animated GIF.").arg(why.isEmpty() ? QStringLiteral("valid") : why, reachable ? QStringLiteral("reachable") : QStringLiteral("not reached"));
    return out;
}
}
