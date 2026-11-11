#include "PlayerAppearancePatcher.h"

#include "EmbeddedResources.h"
#include "PlayerAppearanceScan.h"
#include "QtResources.h"
#include "ThemeEngine.h"

#include <QMap>
#include <QMetaType>
#include <QSet>
#include <QVariantList>

#include <algorithm>
#include <cmath>

namespace PlayerAppearancePatcher {
namespace {

constexpr qint64 kMaxQml = 8LL * 1024 * 1024;

struct Document
{
    QString resource;
    QString leaf;
    qint64 entry = -1;
    QtResources::Link link;
    QByteArray bytes;
    bool bom = false;
    std::string qml;
    QStringList identifiers;
};

// One *unique* structural identifier per catalog resource.
//
// Measured on the Qt 5 reference players (MSI 5.9 / BlueStacks 5.10): every QV4
// unit has an empty sourceFileIndex and neither "UiThemeObject" nor "plr.qml"
// appears in any string table, so filename-based invalidation matches nothing.
// These two property names are declared by exactly one unit each. Broadly
// shared names (iAppwin, iTopbar, buttonMedium, titleLargest, bodyExtraSmall)
// match 2-17 units and are deliberately NOT used: disabling a compiled unit
// whose source is not in the tree would stop that component from loading.
QStringList identifiersFor(const QString &leaf)
{
    if (leaf.compare(QLatin1String("plr.qml"), Qt::CaseInsensitive) == 0)
        return {QStringLiteral("pSidebarVisibleBeforeFullscreen")};
    if (leaf.compare(QLatin1String("UiThemeObject.qml"), Qt::CaseInsensitive) == 0)
        return {QStringLiteral("pFontFamily")};
    return {};
}

// The generic resource writer invalidates by filename/stem plus its own
// built-in per-file identifier table. When that set is wider than the one unit
// this feature can positively identify, the write would disable unrelated
// compiled units, so the edit is refused instead of risking a player that can
// no longer compile a component whose source it does not carry.
bool compiledShadowIsPrecise(const QByteArray &image, const Document &doc, int *broad, int *precise)
{
    // Empty sourceNames => unitNeedles() uses only the explicit identifiers.
    *precise = QtResources::matchingUnits(image, {}, doc.identifiers);
    *broad = QtResources::matchingUnits(image, doc.link.sourceNames, doc.identifiers);
    return *broad == *precise;
}

QList<QtResources::File> candidateFiles(const QByteArray &image)
{
    auto files = QtResources::registeredFiles(image);
    const auto theme = ThemeEngine::findThemeZlib(image);
    if (theme.hasQrcNode) {
        for (const auto &file : QtResources::filesInPool(image, theme.resource)) {
            bool present = false;
            for (const auto &old : files)
                if (old.entryOff == file.entryOff && old.name == file.name) present = true;
            if (!present) files << file;
        }
    }
    return files;
}

QList<Document> documents(const QByteArray &image, QStringList *ambiguous, QStringList *errors)
{
    QList<Document> out;
    QSet<qint64> seenEntries;
    QSet<QString> seenLeaves;
    QStringList duplicateLeaves;
    for (const auto &file : candidateFiles(image)) {
        const bool theme = file.name.compare(QLatin1String("UiThemeObject.qml"), Qt::CaseInsensitive) == 0;
        const bool chrome = file.name.compare(QLatin1String("plr.qml"), Qt::CaseInsensitive) == 0;
        if (!theme && !chrome) continue;
        if (seenEntries.contains(file.entryOff)) continue;
        seenEntries.insert(file.entryOff);
        if (seenLeaves.contains(file.name)) {
            if (!duplicateLeaves.contains(file.name)) duplicateLeaves << file.name;
            continue;
        }
        seenLeaves.insert(file.name);
        const auto bytes = QtResources::readFile(image, file, kMaxQml);
        if (bytes.isEmpty()) {
            if (errors) *errors << QStringLiteral("%1 could not be read as a bounded QML resource.").arg(file.name);
            continue;
        }
        Document doc;
        doc.leaf = file.name;
        doc.resource = file.path.isEmpty() ? file.name : file.path;
        doc.entry = file.entryOff;
        doc.bom = bytes.startsWith(QByteArray::fromHex("efbbbf"));
        const QByteArray body = doc.bom ? bytes.mid(3) : bytes;
        const QString text = QString::fromUtf8(body);
        if (text.toUtf8() != body) {
            if (errors) *errors << QStringLiteral("%1 is not UTF-8; no appearance token was offered.").arg(file.name);
            continue;
        }
        doc.link = QtResources::findLink(image, file.entryOff, file.name);
        if (!doc.link.found || doc.link.ambiguous) {
            if (errors) *errors << QStringLiteral("%1 has no unique validated resource mapping.").arg(file.name);
            continue;
        }
        doc.bytes = bytes;
        doc.qml = std::string(body.constData(), std::size_t(body.size()));
        doc.identifiers = identifiersFor(file.name);
        out << doc;
    }
    if (!duplicateLeaves.isEmpty()) {
        // Two distinct live records with the same catalog leaf are ambiguous even
        // if their logical paths differ. Refuse the leaf instead of choosing the
        // first record the tree happened to enumerate.
        out.erase(std::remove_if(out.begin(), out.end(), [&](const Document &doc) {
            return duplicateLeaves.contains(doc.leaf);
        }), out.end());
        if (ambiguous)
            for (const auto &leaf : duplicateLeaves)
                if (!ambiguous->contains(leaf)) *ambiguous << leaf;
    }
    return out;
}

QVariantMap tokenMap(const Token &token)
{
    return {{QStringLiteral("key"), token.key}, {QStringLiteral("resource"), token.resource},
            {QStringLiteral("path"), token.path}, {QStringLiteral("label"), token.label},
            {QStringLiteral("group"), token.group}, {QStringLiteral("property"), token.property},
            {QStringLiteral("value"), token.value}, {QStringLiteral("minimum"), token.minimum},
            {QStringLiteral("maximum"), token.maximum}, {QStringLiteral("step"), token.step}};
}

bool normalizedInteger(const QVariant &value, int *out)
{
    const int type = value.metaType().id();
    if (type == QMetaType::Bool || type == QMetaType::QString || type == QMetaType::QByteArray)
        return false;
    bool ok = false;
    const double number = value.toDouble(&ok);
    if (!ok || !std::isfinite(number) || std::floor(number) != number
        || number < -1000000.0 || number > 1000000.0)
        return false;
    *out = int(number);
    return true;
}

} // namespace

Analysis analyze(const QByteArray &image)
{
    Analysis out;
    QStringList errors;
    const auto docs = documents(image, &out.ambiguous, &errors);
    QSet<QString> keys;
    for (const auto &doc : docs) {
        const auto scanned = PlayerAppearanceScan::scan(doc.qml, doc.resource.toStdString());
        if (!scanned.ok) {
            if (!scanned.error.empty()) errors << QString::fromStdString(scanned.error);
            continue;
        }
        for (const auto &path : scanned.ambiguous) {
            const auto key = doc.resource + QLatin1Char('#') + QString::fromStdString(path);
            if (!out.ambiguous.contains(key)) out.ambiguous << key;
        }
        int broad = 0, precise = 0;
        if (!compiledShadowIsPrecise(image, doc, &broad, &precise)) {
            errors << QStringLiteral("%1 has %2 compiled QML unit(s) matching the writer's broad identifier table but only %3 that can be identified precisely; its tokens are refused on this build.")
                          .arg(doc.leaf).arg(broad).arg(precise);
            if (!out.ambiguous.contains(doc.resource)) out.ambiguous << doc.resource;
            continue;
        }
        if (!out.resources.contains(doc.resource)) out.resources << doc.resource;
        out.compiledUnits += precise;
        out.activeCompiledUnits += QtResources::activeUnits(image, {}, doc.identifiers);
        for (const auto &token : scanned.tokens) {
            Token item;
            item.path = QString::fromStdString(token.path);
            item.key = doc.resource + QLatin1Char('#') + item.path;
            if (keys.contains(item.key)) {
                if (!out.ambiguous.contains(item.key)) out.ambiguous << item.key;
                continue;
            }
            keys.insert(item.key);
            item.resource = doc.resource;
            item.label = QString::fromStdString(token.label);
            item.group = QString::fromStdString(token.group);
            item.property = QString::fromStdString(token.property);
            item.value = token.value;
            item.minimum = token.minimum;
            item.maximum = token.maximum;
            item.step = token.step;
            out.tokens << item;
        }
    }
    std::sort(out.tokens.begin(), out.tokens.end(), [](const Token &a, const Token &b) { return a.key < b.key; });
    out.ok = !out.tokens.isEmpty();
    if (out.ok) {
        out.msg = QStringLiteral("%1 literal appearance token(s) found in %2 verified QML resource(s). Edits are staged for a Workspace Dry Run; nothing is written by inspection.")
                      .arg(out.tokens.size()).arg(out.resources.size());
        if (!out.ambiguous.isEmpty())
            out.msg += QStringLiteral(" Refused as ambiguous or unsafe: %1.").arg(out.ambiguous.join(QStringLiteral(", ")));
        if (!errors.isEmpty())
            out.msg += QLatin1Char(' ') + errors.join(QStringLiteral(" "));
        if (out.activeCompiledUnits > 0)
            out.msg += QStringLiteral(" %1 matching compiled QML unit(s) are still active and must be invalidated by the verified writer before Apply.").arg(out.activeCompiledUnits);
    } else {
        QString detail = errors.isEmpty()
            ? QStringLiteral("This player may not expose the cataloged UiThemeObject.qml / plr.qml sources.")
            : errors.join(QStringLiteral(" "));
        if (!out.ambiguous.isEmpty())
            detail += QStringLiteral(" Refused as ambiguous or unsafe: %1.").arg(out.ambiguous.join(QStringLiteral(", ")));
        out.msg = QStringLiteral("No supported Player Appearance Studio tokens were found. %1").arg(detail);
    }
    return out;
}

QVariantMap toMap(const Analysis &analysis)
{
    QVariantList tokens;
    for (const auto &token : analysis.tokens) tokens << tokenMap(token);
    return {{QStringLiteral("ok"), analysis.ok}, {QStringLiteral("msg"), analysis.msg},
            {QStringLiteral("tokens"), tokens}, {QStringLiteral("resources"), analysis.resources},
            {QStringLiteral("ambiguous"), analysis.ambiguous},
            {QStringLiteral("compiledUnits"), analysis.compiledUnits},
            {QStringLiteral("activeCompiledUnits"), analysis.activeCompiledUnits}};
}

QVariantMap validateEdits(const QByteArray &image, const QVariantMap &edits)
{
    const auto fail = [](const QString &msg) { return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("msg"), msg}}; };
    if (edits.isEmpty()) return fail(QStringLiteral("Choose at least one appearance value to change."));
    if (edits.size() > 512) return fail(QStringLiteral("An appearance operation may contain at most 512 token edits."));
    const auto analysis = analyze(image);
    if (!analysis.ok) return fail(analysis.msg);
    QSet<QString> ambiguous;
    for (const auto &key : analysis.ambiguous) ambiguous.insert(key);
    QMap<QString, Token> byKey;
    for (const auto &token : analysis.tokens) byKey.insert(token.key, token);
    QVariantMap normalized;
    QStringList resources;
    for (auto it = edits.cbegin(); it != edits.cend(); ++it) {
        if (it.key().isEmpty() || it.key().size() > 1024 || ambiguous.contains(it.key()))
            return fail(QStringLiteral("Unknown or ambiguous appearance token: %1.").arg(it.key()));
        const auto found = byKey.constFind(it.key());
        if (found == byKey.constEnd()) {
            const auto hash = it.key().lastIndexOf(QLatin1Char('#'));
            const QString resource = hash > 0 ? it.key().left(hash) : QString();
            if (!resource.isEmpty() && analysis.ambiguous.contains(resource))
                return fail(QStringLiteral("%1 was refused on this player: its compiled QML shadow cannot be invalidated precisely. %2")
                                .arg(resource, analysis.msg));
            return fail(QStringLiteral("Unknown appearance token for this player: %1. Inspect again after changing the target.").arg(it.key()));
        }
        int value = 0;
        if (!normalizedInteger(it.value(), &value))
            return fail(QStringLiteral("Appearance values must be whole numbers: %1.").arg(it.key()));
        if (value < found->minimum || value > found->maximum)
            return fail(QStringLiteral("%1 must be between %2 and %3.")
                            .arg(it.key()).arg(found->minimum).arg(found->maximum));
        normalized.insert(it.key(), value);
        if (!resources.contains(found->resource)) resources << found->resource;
    }
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("msg"), QStringLiteral("%1 appearance edit(s) validated against the current player. Stage them, then Dry Run before Apply.").arg(normalized.size())},
            {QStringLiteral("edits"), normalized}, {QStringLiteral("resources"), resources}};
}

bool apply(QByteArray &image, const QVariantMap &edits, QString *error)
{
    const auto refuse = [&](const QString &message) { if (error) *error = message; return false; };
    if (error) error->clear();
    const auto validation = validateEdits(image, edits);
    if (!validation.value(QStringLiteral("ok")).toBool())
        return refuse(validation.value(QStringLiteral("msg")).toString());
    const auto normalized = validation.value(QStringLiteral("edits")).toMap();
    QStringList errors;
    const auto docs = documents(image, nullptr, &errors);
    QMap<QString, Document> byResource;
    for (const auto &doc : docs) byResource.insert(doc.resource, doc);
    QMap<QString, std::vector<PlayerAppearanceScan::Edit>> perResource;
    for (auto it = normalized.cbegin(); it != normalized.cend(); ++it) {
        const auto hash = it.key().lastIndexOf(QLatin1Char('#'));
        if (hash <= 0 || hash + 1 >= it.key().size()) return refuse(QStringLiteral("Invalid appearance token key."));
        const QString resource = it.key().left(hash);
        const QString path = it.key().mid(hash + 1);
        if (!byResource.contains(resource)) return refuse(QStringLiteral("The appearance resource is no longer available: %1.").arg(resource));
        perResource[resource].push_back({path.toStdString(), it.value().toInt()});
    }
    QByteArray changed = image;
    for (auto it = perResource.cbegin(); it != perResource.cend(); ++it) {
        const auto &doc = byResource[it.key()];
        std::string source = doc.qml;
        std::string scanError;
        if (!PlayerAppearanceScan::apply(source, doc.resource.toStdString(), it.value(), &scanError))
            return refuse(QString::fromStdString(scanError));
        QByteArray replacement = QByteArray::fromStdString(source);
        if (doc.bom) replacement.prepend(QByteArray::fromHex("efbbbf"));
        if (replacement.isEmpty() || replacement.size() > kMaxQml)
            return refuse(QStringLiteral("The appearance rewrite produced an invalid QML size."));
        int broad = 0, precise = 0;
        if (!compiledShadowIsPrecise(changed, doc, &broad, &precise))
            return refuse(QStringLiteral("%1: %2 compiled QML units match the resource writer's identifier table but only %3 can be identified precisely. Writing could disable a component whose source this player does not carry, so nothing was written. Use a player build that loads this page from QML source.")
                              .arg(doc.leaf).arg(broad).arg(precise));
        const auto room = EmbeddedResources::budget(changed, doc.entry, doc.leaf);
        if (!room.known) return refuse(room.detail);
        const qint64 payload = EmbeddedResources::payloadSizeFor(replacement, doc.link.compression);
        qint64 destination = -1;
        if (payload > room.capacity) {
            destination = QtResources::findFreeSpace(changed, doc.link, 4 + payload, error);
            if (destination < 0)
                return refuse(QStringLiteral("%1 needs %2 payload bytes but only %3 fit in place and no verified free space remains. Nothing was written.")
                                  .arg(doc.leaf).arg(payload).arg(room.capacity));
        }
        // Invalidate the one positively identified unit before the generic
        // writer runs; Qt 5 units omit sourceFileIndex entirely. The precision
        // guard above proves the writer's own broader pass adds no extra units.
        QtResources::invalidateUnits(changed, {}, doc.identifiers);
        qint64 newEntry = doc.entry;
        if (!QtResources::replaceResource(changed, doc.entry, doc.link, replacement, error, &newEntry,
                                          /*allowGrowth=*/false, destination))
            return false;
        if (changed.size() != image.size())
            return refuse(QStringLiteral("The appearance rewrite would change the EXE size; nothing was written."));
        const auto check = QtResources::findLink(changed, newEntry, doc.leaf);
        if (!check.found || check.ambiguous
            || QtResources::readFile(changed, {doc.leaf, newEntry, doc.link.compression}, kMaxQml) != replacement)
            return refuse(QStringLiteral("The rewritten appearance resource did not read back correctly; nothing was written."));
        if (QtResources::activeUnits(changed, doc.link.sourceNames, doc.identifiers) > 0
            || QtResources::activeUnits(changed, {}, doc.identifiers) > 0)
            return refuse(QStringLiteral("Matching compiled QML units remain active after invalidation; the player could ignore this edit. Nothing was written."));
    }
    image.swap(changed);
    return true;
}

} // namespace PlayerAppearancePatcher
