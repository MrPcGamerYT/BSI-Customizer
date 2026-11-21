#include "ShaderSettingsImage.h"
#include "ShaderSettingsSource.h"
#include "PeUtils.h"
#include "QtResources.h"
#include "ZlibUtils.h"
#include <QFileInfo>
#include <QHash>
#include <QSet>

namespace ShaderSettingsImage {
namespace {
constexpr qsizetype maxImage = 256LL * 1024 * 1024;
constexpr qsizetype maxSource = 8LL * 1024 * 1024;
const QString playerLeaf = QStringLiteral("plr.qml");
const QString windowLeaf = QStringLiteral("UiSettingsWindow.qml");
bool refuse(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}
QList<QtResources::File> files(const QByteArray &image)
{
    QList<QtResources::File> output;
    QSet<QPair<qint64, QPair<quint16, QString>>> seen;
    for (const auto &file : QtResources::registeredFiles(image)) {
        const auto key = qMakePair(file.entryOff, qMakePair(file.flags, file.path.isEmpty() ? file.name : file.path));
        if (!seen.contains(key)) { seen.insert(key); output << file; }
    }
    return output;
}
bool unique(const QList<QtResources::File> &entries, const QString &leaf,
            QtResources::File *file, QString *error)
{
    QList<QtResources::File> matches;
    for (const auto &entry : entries)
        if (entry.name.compare(leaf, Qt::CaseInsensitive) == 0) matches << entry;
    if (matches.size() != 1)
        return refuse(error, QStringLiteral("The registered shader integration resource %1 is missing or ambiguous; no file was changed.").arg(leaf));
    *file = matches.first();
    return true;
}
bool decode(const QByteArray &bytes, QString *text, QString *error)
{
    const auto utf8 = bytes.startsWith(QByteArray::fromHex("efbbbf")) ? bytes.mid(3) : bytes;
    const auto decoded = QString::fromUtf8(utf8);
    if (utf8.isEmpty() || utf8.size() > maxSource || decoded.toUtf8() != utf8 || decoded.contains(QChar(0)))
        return refuse(error, QStringLiteral("Native shader integration requires bounded, unambiguous UTF-8 QML sources."));
    *text = decoded;
    return true;
}
QByteArray encode(const QString &text, const QByteArray &before)
{
    const auto bom = QByteArray::fromHex("efbbbf");
    return (before.startsWith(bom) ? bom : QByteArray{}) + text.toUtf8();
}
QStringList ids(const QString &leaf)
{
    return leaf.compare(playerLeaf, Qt::CaseInsensitive) == 0
        ? QStringList{QStringLiteral("iAppwin"), QStringLiteral("fOpenSettingsWindow")}
        : QStringList{QStringLiteral("fChangeSelectedSetting"), QStringLiteral("fSwitchSettingPanel")};
}
}

bool replaceSource(QByteArray &image, const QString &leaf, const QByteArray &source, QString *error)
{
    if (image.isEmpty() || image.size() > maxImage || source.isEmpty() || source.size() > maxSource)
        return refuse(error, QStringLiteral("The native shader source/image exceeds its safe size limit."));
    QtResources::File file;
    if (!unique(files(image), leaf, &file, error)) return false;
    const auto link = QtResources::findLink(image, file.entryOff, file.name);
    if (!link.found || link.ambiguous || link.nodes.isEmpty() || !link.registered
        || (file.flags != 0 && file.flags != 1))
        return refuse(error, QStringLiteral("The native source %1 has no unique registered resource mapping.").arg(leaf));
    if (QtResources::readFile(image, file, maxSource) == source
        && QtResources::sourceUnits(image, {leaf}, ids(leaf), false, true) == 0) return true;
    const auto capacity = QtResources::inPlaceCapacity(image, file.entryOff, file.name, error);
    if (capacity < 0) return false;
    const QByteArray payload = file.flags == 1 ? ZlibUtils::compressForCapacity(source, capacity) : source;
    qint64 destination = -1;
    if (payload.size() > capacity) {
        destination = QtResources::findFreeSpace(image, link, payload.size() + 4, error);
        if (destination < 0)
            return refuse(error, QStringLiteral("The shader-integrated %1 needs %2 bytes (slot %3), and no verified mapped free space exists. Growing the EXE is forbidden; no file was changed.")
                                    .arg(leaf).arg(payload.size()).arg(capacity));
    }
    QByteArray changed = image;
    if (!QtResources::replaceResource(changed, file.entryOff, link, source, error, nullptr, false, destination, ids(leaf))) return false;
    if (changed.size() != image.size())
        return refuse(error, QStringLiteral("Native shader preparation unexpectedly grew the EXE; the entire plan was refused."));
    image = changed;
    return true;
}

Plan prepare(const QByteArray &image, bool enabled)
{
    Plan plan;
    if (image.isEmpty() || image.size() > maxImage || !PeUtils::isPe32Plus(image) || PeUtils::machine(image) != 0x8664u) {
        plan.error = QStringLiteral("The experimental regular shader plugin requires a bounded Windows x64 player EXE.");
        return plan;
    }
    const auto imports = PeUtils::importedModules(image);
    QSet<int> majors;
    if (imports.ok) {
        for (int major : {5, 6}) {
            const QString core = QStringLiteral("Qt%1Core.dll").arg(major);
            for (const auto &name : imports.modules)
                if (name.compare(core, Qt::CaseInsensitive) == 0) majors.insert(major);
        }
    }
    if (majors.size() != 1) {
        plan.error = QStringLiteral("The player's actual Qt Core import cannot be identified uniquely. No shader module ABI was guessed.");
        return plan;
    }
    plan.qtMajor = *majors.cbegin();
    const auto entries = files(image);
    QtResources::File playerFile, windowFile;
    if (!unique(entries, playerLeaf, &playerFile, &plan.error) || !unique(entries, windowLeaf, &windowFile, &plan.error)) return plan;
    const auto playerBytes = QtResources::readFile(image, playerFile, maxSource);
    const auto windowBytes = QtResources::readFile(image, windowFile, maxSource);
    QString player, window;
    if (!decode(playerBytes, &player, &plan.error) || !decode(windowBytes, &window, &plan.error)) return plan;
    QString newPlayer = player, newWindow = window;
    const bool hasPlayer = ShaderSettingsSource::hasPlayer(player), hasWindow = ShaderSettingsSource::hasWindow(window);
    if (!enabled && !hasPlayer && !hasWindow) {
        if (ShaderSettingsSource::hasFragments(player) || ShaderSettingsSource::hasFragments(window)) {
            plan.error = QStringLiteral("Partial or unknown shader fragments remain in native QML; removal is blocked instead of claiming success.");
            return plan;
        }
        // Removing an absent feature must not refresh native resource
        // timestamps or invalidate compiled player/Settings units it never
        // owned. In particular, do not route a no-op through replaceSource.
        plan.output = image;
        plan.ok = true;
        return plan;
    }
    if (enabled) {
        // A module-only update must not leave the old About traversal free to
        // restyle native button layers/slider handles. v21 keeps the complete
        // shell/page/footer flat and opaque, with selected-tab-only glass and a
        // hard Loader boundary. Older overlays must remain removable below.
        if (!window.contains(QLatin1String("//BSI v21"))) {
            plan.error = QStringLiteral("Update the rebuilt Settings panel before installing or updating Shaders. The current design uses the opaque native theme, selected-tab-only glass and isolated stock page/control materials. No file was changed.");
            return plan;
        }
        // The module deliberately does not carry OEM control implementations.
        // Validate the exact registered qrc:/ namespace it imports, not a
        // similarly named file elsewhere in the EXE or an unregistered string.
        const QStringList nativeControls{
            QStringLiteral("UiThemeObject.qml"), QStringLiteral("UiButton.qml"),
            QStringLiteral("UiColumnLayout.qml"), QStringLiteral("UiRowLayout.qml"),
            QStringLiteral("UiCheckBox.qml"), QStringLiteral("UiComboBox.qml"),
            QStringLiteral("UiSlider.qml"), QStringLiteral("UiScrollView.qml"),
            QStringLiteral("UiScrollBar.qml"), QStringLiteral("UiImage.qml"),
            QStringLiteral("UiTextField.qml"), QStringLiteral("UiMessageDialog.qml"),
            QStringLiteral("UiDialog.qml"), QStringLiteral("UiShadowContainer.qml"),
            QStringLiteral("UiDropShadowEffect.qml"), QStringLiteral("UiImageButton.qml")
        };
        for (const auto &name : nativeControls) {
            QList<QtResources::File> matches;
            for (const auto &entry : entries)
                if (entry.path == name) matches << entry;
            QString nativeSource, error;
            if (matches.size() != 1
                || !decode(QtResources::readFile(image, matches.first(), maxSource), &nativeSource, &error)) {
                plan.error = QStringLiteral("The stock Settings control qrc:/%1 is missing, ambiguous or unreadable. Shaders requires the player's real controls and live theme; no imitation UI or partial installation was substituted. No file was changed.").arg(name);
                return plan;
            }
        }
        QHash<QString, int> counts;
        for (const auto &file : entries)
            if (file.name.endsWith(QLatin1String(".qml"), Qt::CaseInsensitive)) ++counts[file.name.toCaseFolded()];
        const auto sourcePlan = ShaderSettingsSource::prepare(player, window, counts);
        if (!sourcePlan.ok) { plan.error = sourcePlan.error; return plan; }
        newPlayer = sourcePlan.player;
        newWindow = sourcePlan.window;
        plan.tabIndex = sourcePlan.tabIndex;
    } else if (hasPlayer || hasWindow) {
        if (!hasPlayer || !hasWindow || !ShaderSettingsSource::removePlayer(player, &newPlayer, &plan.error)
            || !ShaderSettingsSource::removeWindow(window, &newWindow, &plan.error)) {
            if (plan.error.isEmpty()) plan.error = QStringLiteral("Only part of the native shader integration is present; removal was refused.");
            return plan;
        }
    }
    plan.sources = {{playerFile.path, playerLeaf, playerBytes, encode(newPlayer, playerBytes)},
                    {windowFile.path, windowLeaf, windowBytes, encode(newWindow, windowBytes)}};
    QByteArray working = image;
    for (const auto &source : plan.sources)
        if (!replaceSource(working, source.leaf, source.after, &plan.error)) return plan;
    plan.output = working;
    plan.changed = working != image;
    plan.installed = enabled;
    plan.ok = true;
    return plan;
}
} // namespace ShaderSettingsImage
