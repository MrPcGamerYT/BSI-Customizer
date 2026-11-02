#include "SettingsPanelPatcher.h"

#include "BsDetector.h"
#include "CustomizationWorkspace.h"
#include "EmbeddedResources.h"
#include "QmlText.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QMap>
#include <QUrl>

namespace SettingsPanelPatcher {
namespace {
const QString kWindowMarker = QStringLiteral("BSI CUSTOMIZER: redesigned Settings window v1");
const QString kAboutMarker = QStringLiteral("BSI CUSTOMIZER: redesigned About page v1");
const QString kSchema = QStringLiteral("bsi.settings-panel.recovery");
const QString kWindowLeaf = QStringLiteral("UiSettingsWindow.qml");
const QString kAboutLeaf = QStringLiteral("PlrAboutSetting.qml");
constexpr qint64 kMaxImage = 256LL * 1024 * 1024;
constexpr qint64 kMaxQml = 8LL * 1024 * 1024;

struct Source
{
    QString path;
    QByteArray bytes;
    QString text;
};

struct TargetSources
{
    Source window;
    Source about;
    QString error;
    bool ok = false;
};

QByteArray sha256(const QByteArray &bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

QString sidecarPath(const QString &exePath)
{
    const QString canonical = QFileInfo(exePath).canonicalFilePath();
    if (canonical.isEmpty()) return {};
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/settings-panel-recovery");
    const QString key = QString::fromLatin1(sha256(canonical.toUtf8()));
    return QDir(root).filePath(key + QStringLiteral(".json"));
}

bool readBounded(const QString &path, QByteArray *bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > kMaxImage)
        return false;
    *bytes = file.read(kMaxImage + 1);
    return !bytes->isEmpty() && bytes->size() <= kMaxImage
        && file.error() == QFileDevice::NoError;
}

bool saveAtomic(const QString &path, const QByteArray &bytes)
{
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath()) || info.isSymLink()) return false;
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size()
        && file.commit();
}

QVariantMap fail(const QString &message)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("msg"), message}};
}

bool sourceFor(const QByteArray &image, const QString &exePath, const QString &leaf,
               Source *source, QString *error)
{
    QList<EmbeddedResources::Entry> matches;
    for (const auto &entry : EmbeddedResources::list(image, true)) {
        if (entry.format.compare(QLatin1String("qml"), Qt::CaseInsensitive) == 0
            && entry.leaf.compare(leaf, Qt::CaseInsensitive) == 0)
            matches << entry;
    }
    if (matches.size() != 1) {
        *error = matches.isEmpty()
            ? QStringLiteral("The executable does not contain the editable %1 source resource.").arg(leaf)
            : QStringLiteral("The executable contains %1 possible %2 sources; the mapping is ambiguous.")
                  .arg(matches.size()).arg(leaf);
        return false;
    }
    const auto &entry = matches.first();
    if (!entry.text || entry.size <= 0 || entry.size > kMaxQml) {
        *error = QStringLiteral("The %1 resource is not bounded editable UTF-8 QML.").arg(leaf);
        return false;
    }
    const QByteArray raw = EmbeddedResources::readEntry(exePath, entry.entryOff, entry.flags,
                                                         kMaxQml);
    const QString text = EmbeddedResources::decodeText(raw);
    if (raw.isEmpty() || text.isEmpty()) {
        *error = QStringLiteral("The %1 resource could not be read as valid UTF-8 QML.").arg(leaf);
        return false;
    }
    source->path = entry.path;
    source->bytes = raw;
    source->text = text;
    return true;
}

TargetSources readSources(const QString &exePath, const QByteArray &image)
{
    TargetSources out;
    if (!sourceFor(image, exePath, kWindowLeaf, &out.window, &out.error)) return out;
    if (!sourceFor(image, exePath, kAboutLeaf, &out.about, &out.error)) return out;
    if (out.window.path == out.about.path) {
        out.error = QStringLiteral("The Settings window and About page resolve to the same resource; refusing an unsafe patch.");
        return out;
    }
    out.ok = true;
    return out;
}

QByteArray utf8PreservingBom(const QString &text, const QByteArray &original)
{
    QByteArray bytes = text.toUtf8();
    if (original.startsWith(QByteArray::fromHex("efbbbf")))
        bytes.prepend(QByteArray::fromHex("efbbbf"));
    return bytes;
}

QString qmlString(const QString &value)
{
    QString out = QStringLiteral("\"");
    for (const QChar ch : value) {
        const ushort u = ch.unicode();
        if (ch == QLatin1Char('\\')) out += QStringLiteral("\\\\");
        else if (ch == QLatin1Char('"')) out += QStringLiteral("\\\"");
        else if (ch == QLatin1Char('\n')) out += QStringLiteral("\\n");
        else if (ch == QLatin1Char('\r')) out += QStringLiteral("\\r");
        else if (ch == QLatin1Char('\t')) out += QStringLiteral("\\t");
        else if (u < 0x20 || u == 0x2028 || u == 0x2029)
            out += QStringLiteral("\\u%1").arg(u, 4, 16, QLatin1Char('0'));
        else out += ch;
    }
    out += QLatin1Char('"');
    return out;
}

QString buildWindow(const QString &stock)
{
    const QString masked = QmlText::mask(stock);
    const auto objects = QmlText::objects(masked, QStringLiteral("UiShadowContainer"));
    QmlText::Object target;
    bool found = false;
    for (const auto &object : objects) {
        const QString body = masked.mid(object.open, object.end - object.open);
        if (body.contains(QLatin1String("id: iSettingsWindowShadowContainer"))) {
            target = object;
            found = true;
            break;
        }
    }
    if (!found) return {};

    // This is a visual-only replacement. The original root Window, its native
    // change/discard prompts, callbacks and the Loader model remain in place.
    const QString replacement = QStringLiteral(R"QML(
    UiShadowContainer {
        // BSI CUSTOMIZER: redesigned Settings window v1
        id: iSettingsWindowShadowContainer
        anchors.fill: parent

        Rectangle {
            anchors.fill: parent
            color: UiTheme.colors.primary80

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 12

                RowLayout {
                    id: iBsiSettingsHeader
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    spacing: 10

                    MouseArea {
                        id: iBsiSettingsDrag
                        anchors.fill: parent
                        z: 0
                        property real pPressX: 0
                        property real pPressY: 0
                        property real pWindowX: 0
                        property real pWindowY: 0
                        onPressed: function(mouse) {
                            pPressX = mouse.screenX
                            pPressY = mouse.screenY
                            pWindowX = iSettingsWindow.x
                            pWindowY = iSettingsWindow.y
                        }
                        onPositionChanged: function(mouse) {
                            if (!pressed || !iSettingsWindow.screen) return
                            var area = iSettingsWindow.screen.availableGeometry
                            iSettingsWindow.x = Math.max(area.x, Math.min(area.x + area.width - iSettingsWindow.width,
                                pWindowX + mouse.screenX - pPressX))
                            iSettingsWindow.y = Math.max(area.y, Math.min(area.y + area.height - iSettingsWindow.height,
                                pWindowY + mouse.screenY - pPressY))
                        }
                        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                    }
                    Text {
                        text: qsTranslate("QObject", "Settings")
                        font: UiTheme.fonts.bodyLarge
                        color: UiTheme.colors.primary10
                        Layout.fillWidth: true
                        z: 1
                    }
                    Text {
                        text: "×"
                        font.pixelSize: 24
                        color: UiTheme.colors.primary20
                        Layout.preferredWidth: 30
                        horizontalAlignment: Text.AlignHCenter
                        z: 2
                        MouseArea {
                            anchors.fill: parent
                            onClicked: iSettingsWindow.close()
                            cursorShape: Qt.PointingHandCursor
                        }
                    }
                }

                Flickable {
                    id: iBsiSettingsTabsViewport
                    Layout.fillWidth: true
                    Layout.preferredHeight: 70
                    contentWidth: iBsiSettingsTabs.implicitWidth
                    contentHeight: height
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    flickableDirection: Flickable.HorizontalFlick
                    ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

                    Row {
                        id: iBsiSettingsTabs
                        spacing: 7
                        height: iBsiSettingsTabsViewport.height

                        Repeater {
                            model: pSettingsItems.pSettingElements
                            delegate: Rectangle {
                                id: iBsiSettingTab
                                width: 88
                                height: 62
                                radius: 10
                                visible: model.modelData.pIsVisible === undefined || model.modelData.pIsVisible
                                color: model.index === pCurrentTabIndex
                                    ? Qt.rgba(UiTheme.colors.accentNormal.r, UiTheme.colors.accentNormal.g,
                                              UiTheme.colors.accentNormal.b, 0.19)
                                    : Qt.rgba(UiTheme.colors.primary60.r, UiTheme.colors.primary60.g,
                                              UiTheme.colors.primary60.b, 0.54)
                                border.width: model.index === pCurrentTabIndex ? 1 : 0
                                border.color: UiTheme.colors.accentNormal

                                Column {
                                    anchors.fill: parent
                                    anchors.margins: 4
                                    spacing: 2
                                    Text {
                                        text: fBsiSettingsIcon(model.index)
                                        color: model.index === pCurrentTabIndex
                                            ? UiTheme.colors.accentNormal : UiTheme.colors.primary20
                                        font.pixelSize: 19
                                        font.bold: true
                                        width: parent.width
                                        horizontalAlignment: Text.AlignHCenter
                                    }
                                    Text {
                                        text: model.modelData.pButtonText
                                        color: model.index === pCurrentTabIndex
                                            ? UiTheme.colors.primary10 : UiTheme.colors.primary20
                                        font: UiTheme.fonts.bodyExtraSmall
                                        width: parent.width
                                        horizontalAlignment: Text.AlignHCenter
                                        wrapMode: Text.Wrap
                                        maximumLineCount: 2
                                        elide: Text.ElideRight
                                    }
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: fChangeSelectedSetting(model.index)
                                    cursorShape: Qt.PointingHandCursor
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 260
                    radius: 12
                    color: Qt.rgba(UiTheme.colors.primary70.r, UiTheme.colors.primary70.g,
                                   UiTheme.colors.primary70.b, 0.56)

                    StackLayout {
                        id: iSelectedSettingControl
                        anchors.fill: parent
                        anchors.margins: 8
                        currentIndex: pCurrentTabIndex
                        Repeater {
                            model: pSettingsItems.pSettingElements
                            delegate: Loader {
                                Component.onCompleted: {
                                    if (model.modelData.pComponentArgs !== null)
                                        setSource(model.modelData.pQmlSource, model.modelData.pComponentArgs)
                                    else
                                        setSource(model.modelData.pQmlSource)
                                }
                                onLoaded: {
                                    model.modelData.pComponentObj = item
                                    if (item.fOnQmlLoaded)
                                        item.fOnQmlLoaded()
                                }
                            }
                        }
                    }
                }
            }
        }
    }
)QML");
    QString out = stock;
    out.replace(target.nameBegin, target.end - target.nameBegin, replacement);
    // The toolbar changes with BlueStacks translations, so use stable model
    // order to supply a distinct, legible icon for every stock category.
    const QString helper = QStringLiteral(R"QML(
    function fBsiSettingsIcon(index) {
        var icons = ["ϟ", "▣", "◈", "⌖", "◖", "♧", "⚙", "▯", "⌨", "◉", "⚒", "ⓘ"]
        return icons[index] || "•"
    }

    function fBsiFitSettingsWindow() {
        if (!transientParent || !screen) return
        var area = screen.availableGeometry
        var wantedWidth = Math.round(transientParent.width * 0.72)
        var wantedHeight = Math.round(transientParent.height * 0.80)
        width = Math.max(600, Math.min(wantedWidth, Math.round(area.width * 0.90)))
        height = Math.max(430, Math.min(wantedHeight, Math.round(area.height * 0.90)))
        x = Math.max(area.x, Math.min(area.x + area.width - width,
            transientParent.x + Math.round((transientParent.width - width) / 2)))
        y = Math.max(area.y, Math.min(area.y + area.height - height,
            transientParent.y + Math.round((transientParent.height - height) / 2)))
    }
    onVisibleChanged: if (visible) Qt.callLater(fBsiFitSettingsWindow)
)QML");
    const int rootClose = out.lastIndexOf(QLatin1String("\n}"));
    if (rootClose < 0) return {};
    out.insert(rootClose, helper);
    if (!out.contains(kWindowMarker)) return {};
    return out;
}

QString buildAbout(const QString &developerName, const QVariantList &links)
{
    QString rows;
    for (const QVariant &entry : links) {
        const QVariantMap link = entry.toMap();
        const QString label = link.value(QStringLiteral("title")).toString().trimmed();
        const QString url = link.value(QStringLiteral("url")).toString().trimmed();
        if (label.isEmpty() || url.isEmpty()) continue;
        rows += QStringLiteral("\n        { title: %1, url: %2 },")
            .arg(qmlString(label), qmlString(url));
    }
    const QString qDeveloper = qmlString(developerName.trimmed());
    QString generated = QStringLiteral(R"QML(import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    // BSI CUSTOMIZER: redesigned About page v1
    objectName: "aboutSettings"
    property bool pUpdateAvailable: false
    property string pUpdateVersion: ""
    property string pMoreInfoURL: ""
    property string pDownloadURL: ""
    property bool pIsInCompatibleUpdateVersion: false
    property string pParentWindow: ""
    property string pDeveloperName: @@BSI_DEVELOPER@@
    property var pDeveloperLinks: [@@BSI_LINKS@@
    ]

    UiColumnLayout {
        anchors.fill: parent
        spacing: 12
        UiScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: iBsiAboutContent.width
            contentHeight: iBsiAboutContent.implicitHeight + 20
            clip: true

            UiColumnLayout {
                id: iBsiAboutContent
                width: 548
                Layout.alignment: Qt.AlignHCenter | Qt.AlignTop
                spacing: 12

                Item { Layout.fillWidth: true; Layout.preferredHeight: 6 }
                UiImage {
                    asset: "BlueStacks5Beta"
                    pImageWidth: 144
                    pImageHeight: 44
                    Layout.alignment: Qt.AlignHCenter
                }
                Text {
                    text: qsTranslate("QObject", "%1 App Player").arg(uiBackend.productName)
                    font: UiTheme.fonts.bodyLarge
                    color: UiTheme.colors.primary10
                    Layout.alignment: Qt.AlignHCenter
                }
                Text {
                    id: iAndroidText
                    text: plrBackend.version + " " + qsTranslate("QObject", uiBackend.getImageTrait(plrBackend.getVmName()).shortDisplayName)
                    font: UiTheme.fonts.bodySmall
                    color: UiTheme.colors.primary30
                    Layout.alignment: Qt.AlignHCenter
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: UiTheme.colors.primary60
                }
                Text {
                    text: qsTranslate("QObject", "Developed by")
                    font: UiTheme.fonts.bodySmall
                    color: UiTheme.colors.primary30
                    Layout.alignment: Qt.AlignHCenter
                }
                Text {
                    text: pDeveloperName
                    font: UiTheme.fonts.bodyLarge
                    color: UiTheme.colors.primary10
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                Repeater {
                    model: pDeveloperLinks
                    delegate: Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 34
                        radius: 8
                        color: Qt.rgba(UiTheme.colors.primary60.r, UiTheme.colors.primary60.g,
                                       UiTheme.colors.primary60.b, 0.38)
                        Text {
                            anchors.fill: parent
                            anchors.margins: 8
                            text: modelData.title
                            color: UiTheme.colors.accentNormal
                            font: UiTheme.fonts.bodyMedium
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                var target = String(modelData.url || "")
                                if (/^https?:\/\//i.test(target)) Qt.openUrlExternally(target)
                            }
                        }
                    }
                }
                Text {
                    visible: pDeveloperLinks.length === 0
                    text: qsTranslate("QObject", "No links configured")
                    color: UiTheme.colors.primary30
                    font: UiTheme.fonts.bodySmall
                    Layout.alignment: Qt.AlignHCenter
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: UiTheme.colors.primary60
                }
                UiButton {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 168
                    pSize: UiTheme.buttons.sizes.small
                    pColor: UiTheme.buttons.colors.accent
                    text: pUpdateAvailable ? qsTranslate("QObject", "Download Update") : qsTranslate("QObject", "Check for updates")
                    visible: !iOnLatestVersion.visible && !pIsInCompatibleUpdateVersion
                    onClicked: {
                        if (pUpdateAvailable) Qt.openUrlExternally(pDownloadURL)
                        else { iLoader.visible = true; plrBackend.checkForUpdate() }
                    }
                }
                Text {
                    id: iOnLatestVersion
                    text: qsTranslate("QObject", "You are on the latest version")
                    font: UiTheme.fonts.bodySmall
                    color: UiTheme.colors.primary20
                    visible: false
                    Layout.alignment: Qt.AlignHCenter
                }
                UiTextUrl {
                    visible: pUpdateAvailable && pIsInCompatibleUpdateVersion
                    text: qsTranslate("QObject", "An update is available but may not be compatible. Open update information.")
                    font: UiTheme.fonts.bodySmall
                    color: UiTheme.colors.primary20
                    linkColor: UiTheme.colors.accentNormal
                    wrapMode: Text.Wrap
                    onLinkActivated: Qt.openUrlExternally(pMoreInfoURL)
                }
                UiImage {
                    id: iLoader
                    asset: "Loader_bright"
                    pImageWidth: 16
                    pImageHeight: 16
                    visible: false
                    cache: false
                    Layout.alignment: Qt.AlignHCenter
                    RotationAnimation on rotation {
                        loops: Animation.Infinite
                        duration: 1200
                        from: 0
                        to: 360
                        running: iLoader.visible
                    }
                }
                Text {
                    text: qsTranslate("QObject", "Checking for updates…")
                    color: UiTheme.colors.primary30
                    font: UiTheme.fonts.bodySmall
                    visible: iLoader.visible
                    Layout.alignment: Qt.AlignHCenter
                }
            }
        }
    }

    function fCheckForUpdateCallback(apiCallStatus, jsonData) {
        if (apiCallStatus === 0) {
            var jsonObj = JSON.parse(jsonData)
            pUpdateAvailable = String(jsonObj.update_available).toLowerCase() === "true"
            pIsInCompatibleUpdateVersion = false
            iLoader.visible = false
            if (pUpdateAvailable) {
                pUpdateVersion = jsonObj.update_details.client_version
                pMoreInfoURL = jsonObj.update_details.detailed_changelogs_url
                pDownloadURL = jsonObj.update_details.download_url
                pIsInCompatibleUpdateVersion = String(jsonObj.update_details.is_build_compatible).toLowerCase() !== "true"
            } else {
                iOnLatestVersion.visible = true
            }
        } else {
            pUpdateAvailable = false
            iLoader.visible = false
            pIsInCompatibleUpdateVersion = false
        }
    }
    function fUpdateShortDisplayName(displayName) {
        iAndroidText.text = plrBackend.version + " " + displayName
    }
}
)QML");
    generated.replace(QStringLiteral("@@BSI_DEVELOPER@@"), qDeveloper);
    generated.replace(QStringLiteral("@@BSI_LINKS@@"), rows);
    return generated;
}

QVariantMap readRecovery(const QString &path, const QString &exePath)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > 12 * 1024 * 1024)
        return {};
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) return {};
    const QVariantMap map = doc.toVariant().toMap();
    if (map.value(QStringLiteral("schema")).toString() != kSchema
        || map.value(QStringLiteral("version")).toInt() != 1
        || map.value(QStringLiteral("target")).toString() != QFileInfo(exePath).canonicalFilePath())
        return {};
    return map;
}

QVariantList sourceRecords(const TargetSources &sources, const QMap<QString, QByteArray> &originals,
                           const QMap<QString, QByteArray> &patched = {})
{
    QVariantList records;
    for (const Source *source : {&sources.window, &sources.about}) {
        const QByteArray original = originals.value(source->path);
        const QByteArray replacement = patched.value(source->path);
        QVariantList acceptedHashes;
        const QString currentHash = QString::fromLatin1(sha256(source->bytes));
        const QString replacementHash = QString::fromLatin1(sha256(replacement));
        acceptedHashes << currentHash;
        if (replacementHash != currentHash) acceptedHashes << replacementHash;
        records << QVariantMap {
            {QStringLiteral("path"), source->path},
            {QStringLiteral("original"), QString::fromLatin1(original.toBase64())},
            {QStringLiteral("originalSha256"), QString::fromLatin1(sha256(original))},
            {QStringLiteral("acceptedSha256"), acceptedHashes},
        };
    }
    return records;
}

QMap<QString, QByteArray> originalsFromRecovery(const QVariantMap &recovery, QString *error)
{
    QMap<QString, QByteArray> originals;
    const QVariantList records = recovery.value(QStringLiteral("sources")).toList();
    if (records.size() != 2) {
        *error = QStringLiteral("The Settings-panel recovery snapshot is incomplete; restore is blocked.");
        return {};
    }
    for (const QVariant &value : records) {
        const QVariantMap record = value.toMap();
        const QString path = record.value(QStringLiteral("path")).toString();
        const QByteArray raw = QByteArray::fromBase64(record.value(QStringLiteral("original")).toByteArray());
        if (path.isEmpty() || raw.isEmpty() || raw.size() > kMaxQml
            || QString::fromLatin1(sha256(raw)) != record.value(QStringLiteral("originalSha256")).toString()
            || originals.contains(path)) {
            *error = QStringLiteral("The Settings-panel recovery snapshot failed its integrity check.");
            return {};
        }
        originals.insert(path, raw);
    }
    return originals;
}

bool recoveryAcceptsCurrent(const QVariantMap &record, const QByteArray &current)
{
    const QString digest = QString::fromLatin1(sha256(current));
    const QVariantList accepted = record.value(QStringLiteral("acceptedSha256")).toList();
    if (!accepted.isEmpty()) {
        for (const QVariant &hash : accepted)
            if (hash.toString() == digest) return true;
        return false;
    }
    // Read the first, not-yet-deployed schema during an in-place upgrade.
    return record.value(QStringLiteral("patchedSha256")).toString() == digest;
}

QVariantList operationsFor(const QMap<QString, QByteArray> &replacement)
{
    QVariantList operations;
    for (auto it = replacement.cbegin(); it != replacement.cend(); ++it) {
        operations << QVariantMap {
            {QStringLiteral("kind"), QStringLiteral("resource")},
            {QStringLiteral("path"), it.key()},
            {QStringLiteral("data"), QString::fromLatin1(it.value().toBase64())},
            {QStringLiteral("sha256"), QString::fromLatin1(sha256(it.value()))},
        };
    }
    return operations;
}

bool validateProfile(const QString &developerName, const QVariantList &links, QString *error)
{
    if (developerName.trimmed().isEmpty() || developerName.trimmed().size() > 80
        || developerName.contains(QChar(0)) || links.size() > 4) {
        *error = QStringLiteral("Enter a developer name up to 80 characters and no more than four About links.");
        return false;
    }
    for (const QVariant &value : links) {
        const QVariantMap link = value.toMap();
        const QString title = link.value(QStringLiteral("title")).toString().trimmed();
        const QString urlText = link.value(QStringLiteral("url")).toString().trimmed();
        if (title.isEmpty() && urlText.isEmpty()) continue;
        if (title.isEmpty() || title.size() > 80 || urlText.size() > 512
            || QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f]")).match(urlText).hasMatch()) {
            *error = QStringLiteral("Every About link needs a label up to 80 characters and a valid HTTP(S) URL.");
            return false;
        }
        const QUrl url(urlText, QUrl::StrictMode);
        if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty()
            || (url.scheme().compare(QLatin1String("http"), Qt::CaseInsensitive) != 0
                && url.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) != 0)) {
            *error = QStringLiteral("About links must be valid HTTP or HTTPS URLs.");
            return false;
        }
    }
    return true;
}

QVariantMap applyResources(const QString &exePath, const QVariantList &operations)
{
    QByteArray image;
    if (!readBounded(exePath, &image))
        return fail(QStringLiteral("The selected HD-Player.exe is unreadable or exceeds the safe 256 MB limit."));
    const QString inputHash = CustomizationWorkspace::digest(image);
    QByteArray prepared;
    const QVariantMap dryRun = CustomizationWorkspace::simulate(exePath, operations, inputHash, &prepared);
    if (!dryRun.value(QStringLiteral("ok")).toBool()) return dryRun;
    return CustomizationWorkspace::apply(exePath, operations, inputHash,
                                         dryRun.value(QStringLiteral("outputSha256")).toString());
}

bool sourceHasMarker(const QByteArray &bytes, const QString &marker)
{
    return EmbeddedResources::decodeText(bytes).contains(marker);
}

} // namespace

State analyze(const QString &exePath)
{
    State state;
    if (exePath.isEmpty() || !QFileInfo(exePath).isFile()) {
        state.detail = QStringLiteral("Select an HD-Player.exe before rebuilding its Settings panel.");
        return state;
    }
    QByteArray image;
    if (!readBounded(exePath, &image)) {
        state.detail = QStringLiteral("The selected executable is unreadable or exceeds the safe 256 MB limit.");
        return state;
    }
    const TargetSources sources = readSources(exePath, image);
    if (!sources.ok) {
        state.detail = sources.error;
        return state;
    }
    state.supported = true;
    const bool windowPatched = sources.window.text.contains(kWindowMarker);
    const bool aboutPatched = sources.about.text.contains(kAboutMarker);
    state.applied = windowPatched && aboutPatched;
    const QString backup = sidecarPath(exePath);
    const QVariantMap recovery = backup.isEmpty() ? QVariantMap{} : readRecovery(backup, exePath);
    state.restorable = state.applied && !recovery.isEmpty();
    if (windowPatched != aboutPatched) {
        state.supported = false;
        state.detail = QStringLiteral("Only part of the custom Settings panel is present. Restore from the feature recovery snapshot before retrying.");
    } else if (state.applied && !state.restorable) {
        state.supported = false;
        state.detail = QStringLiteral("The custom Settings panel is present, but its feature recovery snapshot is missing or invalid. Do not overwrite this executable.");
    } else if (state.applied) {
        state.detail = QStringLiteral("Custom Settings window and About page are installed. Original QML is backed up for a selective restore.");
    } else {
        state.detail = QStringLiteral("Editable Settings-window and About-page QML sources are present. The native settings tabs and save/discard logic can be preserved.");
    }
    return state;
}

QVariantMap apply(const QString &exePath, const QString &developerName,
                  const QVariantList &links, bool enabled)
{
    if (BsDetector::isProcessRunning(QStringLiteral("HD-Player.exe")))
        return fail(QStringLiteral("Close every HD-Player window before rebuilding or restoring the Settings panel."));
    const QString canonical = QFileInfo(exePath).canonicalFilePath();
    if (canonical.isEmpty() || QFileInfo(exePath).suffix().compare(QLatin1String("exe"), Qt::CaseInsensitive) != 0)
        return fail(QStringLiteral("Select an existing HD-Player.exe file first."));
    const QString backup = sidecarPath(exePath);
    if (backup.isEmpty()) return fail(QStringLiteral("A safe local recovery path could not be prepared."));

    QByteArray image;
    if (!readBounded(exePath, &image))
        return fail(QStringLiteral("The selected executable is unreadable or exceeds the safe 256 MB limit."));
    const TargetSources current = readSources(exePath, image);
    if (!current.ok) return fail(current.error);
    QVariantMap recovery = readRecovery(backup, exePath);
    const bool hadRecovery = !recovery.isEmpty();

    if (!enabled) {
        if (recovery.isEmpty()) return fail(QStringLiteral("No verified Settings-panel recovery snapshot was found; nothing was changed."));
        QString recoveryError;
        const QMap<QString, QByteArray> originals = originalsFromRecovery(recovery, &recoveryError);
        if (originals.isEmpty()) return fail(recoveryError);
        if (originals.size() != 2 || !originals.contains(current.window.path) || !originals.contains(current.about.path))
            return fail(QStringLiteral("The executable's resource paths no longer match the saved Settings-panel snapshot; restore is blocked."));
        if (!current.window.text.contains(kWindowMarker) || !current.about.text.contains(kAboutMarker))
            return fail(QStringLiteral("The Settings-panel sources have changed since this patch. Automatic restore is blocked to protect later edits."));
        const QVariantList records = recovery.value(QStringLiteral("sources")).toList();
        for (const QVariant &value : records) {
            const QVariantMap record = value.toMap();
            const Source *source = record.value(QStringLiteral("path")).toString() == current.window.path
                ? &current.window : &current.about;
            if (!recoveryAcceptsCurrent(record, source->bytes))
                return fail(QStringLiteral("A Settings-panel resource was edited after patching; restore is blocked rather than overwriting that change."));
        }
        const auto restored = applyResources(exePath, operationsFor(originals));
        if (!restored.value(QStringLiteral("ok")).toBool()) return restored;
        QFile::remove(backup);
        return {{QStringLiteral("ok"), true},
                {QStringLiteral("msg"), QStringLiteral("The original Settings-window and About-page sources were restored without reverting other executable resources. Restart BlueStacks to see the change.")}};
    }

    QString profileError;
    if (!validateProfile(developerName, links, &profileError)) return fail(profileError);
    const bool hasPatchMarker = current.window.text.contains(kWindowMarker)
        || current.about.text.contains(kAboutMarker);
    QMap<QString, QByteArray> originals;
    if (!recovery.isEmpty()) {
        QString recoveryError;
        originals = originalsFromRecovery(recovery, &recoveryError);
        if (originals.isEmpty()) return fail(recoveryError);
        if (originals.size() != 2 || !originals.contains(current.window.path) || !originals.contains(current.about.path))
            return fail(QStringLiteral("The executable's resource paths no longer match the saved Settings-panel snapshot; patch is blocked."));
        const QVariantList records = recovery.value(QStringLiteral("sources")).toList();
        for (const QVariant &value : records) {
            const QVariantMap record = value.toMap();
            const Source *source = record.value(QStringLiteral("path")).toString() == current.window.path
                ? &current.window : &current.about;
            if (!recoveryAcceptsCurrent(record, source->bytes))
                return fail(QStringLiteral("A saved Settings-panel resource was changed by another patch. Restore or inspect it manually before rebuilding."));
        }
    } else {
        if (hasPatchMarker)
            return fail(QStringLiteral("A Settings-panel marker exists without its recovery snapshot. The executable was not overwritten."));
        originals.insert(current.window.path, current.window.bytes);
        originals.insert(current.about.path, current.about.bytes);
        recovery = {{QStringLiteral("schema"), kSchema},
                    {QStringLiteral("version"), 1},
                    {QStringLiteral("target"), canonical}};
    }

    const QString originalWindowText = EmbeddedResources::decodeText(originals.value(current.window.path));
    const QString originalAboutText = EmbeddedResources::decodeText(originals.value(current.about.path));
    if (originalWindowText.isEmpty() || originalAboutText.isEmpty())
        return fail(QStringLiteral("A saved original QML source could not be decoded; the executable was not changed."));
    const QString rebuiltWindow = buildWindow(originalWindowText);
    if (rebuiltWindow.isEmpty())
        return fail(QStringLiteral("This BlueStacks build's Settings-window structure is not recognized. No file was changed."));
    const QString rebuiltAbout = buildAbout(developerName, links);
    if (rebuiltAbout.isEmpty())
        return fail(QStringLiteral("The About page could not be generated; no file was changed."));
    QMap<QString, QByteArray> replacements {
        {current.window.path, utf8PreservingBom(rebuiltWindow, originals.value(current.window.path))},
        {current.about.path, utf8PreservingBom(rebuiltAbout, originals.value(current.about.path))},
    };
    if (!sourceHasMarker(replacements.value(current.window.path), kWindowMarker)
        || !sourceHasMarker(replacements.value(current.about.path), kAboutMarker))
        return fail(QStringLiteral("Generated QML failed its marker check; no file was changed."));
    if (replacements.value(current.window.path) == current.window.bytes
        && replacements.value(current.about.path) == current.about.bytes)
        return {{QStringLiteral("ok"), true},
                {QStringLiteral("msg"), QStringLiteral("The requested Settings-panel design is already applied.")}};

    recovery.insert(QStringLiteral("sources"), sourceRecords(current, originals, replacements));
    recovery.insert(QStringLiteral("developerName"), developerName.trimmed());
    if (!saveAtomic(backup, QJsonDocument::fromVariant(recovery).toJson(QJsonDocument::Compact)))
        return fail(QStringLiteral("Could not save the feature-specific recovery snapshot. No executable resources were changed."));

    const QVariantMap result = applyResources(exePath, operationsFor(replacements));
    if (!result.value(QStringLiteral("ok")).toBool()) {
        // Preserve an existing valid recovery file; remove only the one created
        // by this attempt, so a failed first apply cannot masquerade as active.
        if (!hadRecovery) QFile::remove(backup);
        return result;
    }
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("msg"), QStringLiteral("The compact, draggable Settings window, icon tabs and customizable About page were applied. A selective recovery snapshot was saved; restart BlueStacks to see it.")},
            {QStringLiteral("recovery"), result.value(QStringLiteral("id"))}};
}

} // namespace SettingsPanelPatcher
