#include "SettingsPanelPatcher.h"

#include "BsDetector.h"
#include "CustomizationWorkspace.h"
#include "EmbeddedResources.h"
#include "PatchFile.h"
#include "QmlText.h"
#include "QtResources.h"
#include "ThemeEngine.h"

#include <QCryptographicHash>
#include <QDir>
#include <QHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QSet>
#include <QMap>
#include <QUrl>

namespace SettingsPanelPatcher {
namespace {
const QString kWindowMarker = QStringLiteral("BSI CUSTOMIZER: redesigned Settings window v1");
const QString kWindowDesignMarker = QStringLiteral("BSI CUSTOMIZER: compact Settings refresh v2");
const QString kAboutMarker = QStringLiteral("BSI CUSTOMIZER: redesigned About page v1");
const QString kSchema = QStringLiteral("bsi.settings-panel.recovery");
const QString kWindowLeaf = QStringLiteral("UiSettingsWindow.qml");
const QString kAboutLeaf = QStringLiteral("PlrAboutSetting.qml");
const QStringList kWindowUnitIds {
    QStringLiteral("iSettingsWindowShadowContainer"), QStringLiteral("fChangeSelectedSetting")
};
const QStringList kAboutUnitIds {
    QStringLiteral("aboutSettings"), QStringLiteral("Important links"),
    QStringLiteral("About and other info"), QStringLiteral("iSupportEmailText")
};
const QStringList kAboutPatcherMarkers {
    QStringLiteral("BSI CUSTOMIZER: about hidden"),
    QStringLiteral("BSI CUSTOMIZER: about shown:"),
    QStringLiteral("BSI CUSTOMIZER: about text:"),
    QStringLiteral("BSI CUSTOMIZER: about handler:"),
    QStringLiteral("BSI CUSTOMIZER: about-config ")
};
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
    int pageCount = 0;
    int qmlClosureCount = 0;
    QString aboutLogoExtension;
    QString error;
    bool ok = false;
};

bool hasAboutPatcherEdits(const QString &qml)
{
    for (const QString &marker : kAboutPatcherMarkers)
        if (qml.contains(marker)) return true;
    return false;
}

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

    // The in-emulator Settings screen is a resource graph, not one document:
    // plr.qml declares the native tab model, UiSettingsWindow.qml loads each
    // pQmlSource dynamically, and those pages in turn use shared QML controls.
    // Validate that graph against the actual registered pool before offering a
    // rebuild, so a matching leaf from a detached/ambiguous resource is not
    // mistaken for the component the player actually loads.
    QHash<QString, QList<QtResources::File>> qmlByLeaf;
    for (const auto &file : QtResources::registeredFiles(image))
        if (file.name.endsWith(QLatin1String(".qml"), Qt::CaseInsensitive))
            qmlByLeaf[file.name.toCaseFolded()] << file;
    const auto uniqueQml = [&qmlByLeaf](const QString &leaf) -> const QtResources::File * {
        const auto it = qmlByLeaf.constFind(leaf.toCaseFolded());
        return it != qmlByLeaf.cend() && it.value().size() == 1 ? &it.value().first() : nullptr;
    };
    const QtResources::File *playerRoot = uniqueQml(QStringLiteral("plr.qml"));
    if (!playerRoot) {
        out.error = QStringLiteral("The registered player root plr.qml is missing or ambiguous; Settings tabs cannot be resolved safely.");
        return out;
    }
    const QByteArray playerBytes = QtResources::readFile(image, *playerRoot, kMaxQml);
    const QString playerText = EmbeddedResources::decodeText(playerBytes);
    const QRegularExpression inlineSettingsModelPattern(
        QStringLiteral("\\bproperty\\s+(?:[A-Za-z_][A-Za-z0-9_]*\\s+)*SettingElementModelList\\s+pSettingsItems\\b"));
    const bool inlineSettingsModel = inlineSettingsModelPattern.match(playerText).hasMatch();
    // Newer MSI/BlueStacks builds keep the same native SettingElementModelList
    // inside a Component in plr.qml, then pass its created instance into the
    // dynamically created UiSettingsWindow. Validate that live connection, not
    // just a similarly named orphan model.
    const QRegularExpression componentSettingsModelPattern(
        QStringLiteral("\\bComponent\\s*\\{\\s*id\\s*:\\s*iSettingsItems\\s+SettingElementModelList\\s*\\{"));
    const QRegularExpression componentSettingsBindingPattern(
        QStringLiteral("\\bpSettingsWindow\\s*=\\s*component\\.createObject\\s*\\([^;]*[\\\"']pSettingsItems[\\\"']\\s*:\\s*iSettingsItems\\.createObject\\s*\\("));
    const bool componentSettingsModel = componentSettingsModelPattern.match(playerText).hasMatch()
        && componentSettingsBindingPattern.match(playerText).hasMatch();
    if (playerText.isEmpty() || (!inlineSettingsModel && !componentSettingsModel)) {
        out.error = QStringLiteral("The registered plr.qml does not expose the expected native Settings tab model; this player layout is unsupported.");
        return out;
    }
    QSet<QString> pageNames;
    const QRegularExpression pageSource(QStringLiteral("\\bpQmlSource\\s*:\\s*['\\\"]([^'\\\"]+\\.qml)['\\\"]"));
    auto pageMatch = pageSource.globalMatch(playerText);
    while (pageMatch.hasNext()) {
        const QString page = pageMatch.next().captured(1);
        const QString leaf = QFileInfo(page).fileName();
        if (leaf.isEmpty() || pageNames.contains(leaf.toCaseFolded())) {
            out.error = QStringLiteral("The Settings tab model contains an empty or duplicate QML page path; refusing to rebuild an ambiguous panel.");
            return out;
        }
        if (!uniqueQml(leaf)) {
            out.error = QStringLiteral("The Settings tab QML page %1 is missing or ambiguous in the registered player resources.").arg(leaf);
            return out;
        }
        pageNames.insert(leaf.toCaseFolded());
    }
    if (pageNames.size() < 8 || !pageNames.contains(kAboutLeaf.toCaseFolded())) {
        out.error = QStringLiteral("The player root exposes only %1 Settings page(s), or no About page; the native tab graph is incomplete.").arg(pageNames.size());
        return out;
    }
    out.pageCount = pageNames.size();
    if (!out.window.text.contains(QLatin1String("pSettingsItems.pSettingElements"))
        || !out.window.text.contains(QLatin1String("setSource(model.modelData.pQmlSource"))
        || !out.window.text.contains(QLatin1String("model.modelData.pComponentObj = item"))
        || !out.window.text.contains(QLatin1String("fChangeSelectedSetting"))) {
        out.error = QStringLiteral("UiSettingsWindow.qml no longer loads and retains the player’s native Settings pages; this layout cannot be safely replaced.");
        return out;
    }

    // Resolve the QML-backed transitive component closure for the shell and
    // all tab roots, rather than only checking UiSettingsWindow/About leaves.
    // The generated panel uses the same native controls and these untouched
    // pages rely on them remaining registered in this pool.
    QSet<QString> pending { kWindowLeaf.toCaseFolded() };
    for (const QString &page : pageNames) pending.insert(page);
    QSet<QString> visited;
    const QRegularExpression componentType(QStringLiteral("\\b([A-Z][A-Za-z0-9_]*)\\s*\\{"));
    const QRegularExpression qmlPath(QStringLiteral("['\\\"](?:qrc:/)?([^'\\\"]+\\.qml)['\\\"]"), QRegularExpression::CaseInsensitiveOption);
    while (!pending.isEmpty()) {
        const QString key = *pending.cbegin();
        pending.remove(key);
        if (visited.contains(key)) continue;
        const auto it = qmlByLeaf.constFind(key);
        if (it == qmlByLeaf.cend() || it.value().size() != 1) continue;
        visited.insert(key);
        const QString text = EmbeddedResources::decodeText(QtResources::readFile(image, it.value().first(), kMaxQml));
        if (text.isEmpty()) {
            out.error = QStringLiteral("The registered Settings dependency %1 could not be decoded as QML.").arg(it.value().first().name);
            return out;
        }
        auto types = componentType.globalMatch(text);
        while (types.hasNext()) {
            const QString component = types.next().captured(1).toCaseFolded() + QStringLiteral(".qml");
            const auto found = qmlByLeaf.constFind(component);
            if (found != qmlByLeaf.cend()) {
                if (found.value().size() != 1) {
                    out.error = QStringLiteral("The Settings dependency %1 is ambiguous in the registered player resources.").arg(component);
                    return out;
                }
                pending.insert(component);
            }
        }
        auto paths = qmlPath.globalMatch(text);
        while (paths.hasNext()) {
            const QString path = QFileInfo(paths.next().captured(1)).fileName().toCaseFolded();
            if (qmlByLeaf.contains(path)) {
                if (qmlByLeaf.value(path).size() != 1) {
                    out.error = QStringLiteral("The Settings QML path %1 is ambiguous in the registered player resources.").arg(path);
                    return out;
                }
                pending.insert(path);
            }
        }
    }
    out.qmlClosureCount = visited.size();

    const auto assets = EmbeddedResources::list(image, true);
    const auto hasAsset = [&assets](const QString &path) {
        for (const auto &entry : assets)
            if (entry.path.compare(path, Qt::CaseInsensitive) == 0) return true;
        return false;
    };
    if (hasAsset(QStringLiteral("qrcAssets/BlueStacks5Beta.svg")))
        out.aboutLogoExtension = QStringLiteral(".svg");
    else if (hasAsset(QStringLiteral("qrcAssets/BlueStacks5Beta.png")))
        out.aboutLogoExtension = QStringLiteral(".png");
    else {
        out.error = QStringLiteral("The About profile logo asset is missing; the regenerated page would show a broken image.");
        return out;
    }
    if (!hasAsset(QStringLiteral("qrcAssets/Loader_bright.svg"))) {
        out.error = QStringLiteral("The About page’s loading-indicator SVG is missing from the registered player resources.");
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
        // BSI CUSTOMIZER: compact Settings refresh v2
        id: iSettingsWindowShadowContainer
        anchors.fill: parent
        Rectangle {
            anchors.fill: parent
            radius: 14
            color: UiTheme.colors.primary80
            border.width: 1
            border.color: UiTheme.colors.primary60
            clip: true

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 2
                gradient: Gradient {
                    GradientStop { position: 0.0; color: UiTheme.colors.primary60 }
                    GradientStop { position: 0.5; color: UiTheme.colors.accentNormal }
                    GradientStop { position: 1.0; color: UiTheme.colors.primary60 }
                }
            }

            Column {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10

                Item {
                    width: parent.width
                    height: 42
                    MouseArea {
                        objectName: "bsiSettingsDragHandle"
                        width: parent.width - 48
                        height: parent.height
                        onPressed: iSettingsWindow.startSystemMove()
                    }
                    Column {
                        anchors.left: parent.left
                        anchors.leftMargin: 2
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        Text {
                            text: qsTranslate("QObject", "Settings")
                            color: UiTheme.colors.primary10
                            font: UiTheme.fonts.bodyLarge
                        }
                        Text {
                            text: qsTranslate("QObject", "Choose a category")
                            color: UiTheme.colors.primary30
                            font: UiTheme.fonts.bodySmall
                        }
                    }
                    UiImageButton {
                        id: bsiSettingsClose
                        objectName: "bsiSettingsClose"
                        asset: "CloseIcon"
                        pImageWidth: 16
                        pImageHeight: 16
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: iSettingsWindow.close()
                    }
                }

                ListView {
                    id: bsiSettingsCategoryTabs
                    objectName: "bsiSettingsCategoryTabs"
                    width: parent.width
                    height: 76
                    clip: true
                    orientation: ListView.Horizontal
                    spacing: 7
                    currentIndex: pCurrentTabIndex
                    model: pSettingsItems.pSettingElements
                    ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
                    delegate: Rectangle {
                        objectName: "bsiSettingsCategoryTab" + model.index
                        width: 96
                        height: 68
                        radius: 10
                        color: model.index === pCurrentTabIndex
                               ? Qt.rgba(UiTheme.colors.accentNormal.r, UiTheme.colors.accentNormal.g,
                                         UiTheme.colors.accentNormal.b, 0.17)
                               : (bsiCategoryHover.containsMouse
                                  ? UiTheme.colors.primary60 : Qt.rgba(0, 0, 0, 0))
                        border.width: 1
                        border.color: model.index === pCurrentTabIndex
                                      ? UiTheme.colors.accentNormal : UiTheme.colors.primary60
                        Behavior on color { ColorAnimation { duration: 140 } }

                        Column {
                            anchors.centerIn: parent
                            width: parent.width - 10
                            spacing: 4
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: ["ϟ", "▦", "◈", "⌖", "♫", "♧", "⚙", "▯", "⌨", "◉", "⚒", "ⓘ"][model.index % 12]
                                color: model.index === pCurrentTabIndex
                                       ? UiTheme.colors.accentNormal : UiTheme.colors.primary10
                                font: UiTheme.fonts.bodyLarge
                            }
                            Text {
                                width: parent.width
                                text: model.modelData.pButtonText
                                color: model.index === pCurrentTabIndex
                                       ? UiTheme.colors.primary10 : UiTheme.colors.primary30
                                font: UiTheme.fonts.bodySmall
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                            }
                        }
                        Rectangle {
                            visible: model.modelData.pShowRedDot
                            width: 7
                            height: 7
                            radius: 4
                            color: UiTheme.colors.accentNormal
                            anchors.top: parent.top
                            anchors.right: parent.right
                            anchors.margins: 7
                        }
                        MouseArea {
                            id: bsiCategoryHover
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: fChangeSelectedSetting(model.index)
                        }
                    }
                }

                StackLayout {
                    id: iSelectedSettingControl
                    objectName: "bsiSettingsPageStack"
                    width: parent.width
                    height: Math.max(120, parent.height - 42 - 76 - 20)
                    currentIndex: pCurrentTabIndex
                    Repeater {
                        model: pSettingsItems.pSettingElements
                        delegate: Loader {
                            objectName: "bsiSettingsPageLoader" + model.index
                            Component.onCompleted: {
                                if (model.modelData.pComponentArgs !== null) setSource(model.modelData.pQmlSource, model.modelData.pComponentArgs)
                                else setSource(model.modelData.pQmlSource)
                            }
                            onLoaded: { model.modelData.pComponentObj = item; if (item.fOnQmlLoaded) item.fOnQmlLoaded() }
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
    onVisibleChanged: if (visible && transientParent) {
        width = transientParent.width * 0.72
        height = transientParent.height * 0.8
        x = transientParent.x + (transientParent.width - width) / 2
        y = transientParent.y + (transientParent.height - height) / 2
    }
)QML");
    const int rootClose = out.lastIndexOf(QLatin1String("\n}"));
    if (rootClose < 0) return {};
    out.insert(rootClose, helper);
    if (!out.contains(kWindowDesignMarker)) return {};
    return out;
}

QString buildAbout(const QString &developerName, const QVariantList &links, const QString &logoExtension)
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
                    extn: @@BSI_LOGO_EXTN@@
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
    generated.replace(QStringLiteral("@@BSI_LOGO_EXTN@@"), qmlString(logoExtension));
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
    QVariantMap applied = CustomizationWorkspace::apply(exePath, operations, inputHash,
        dryRun.value(QStringLiteral("outputSha256")).toString());
    if (!applied.value(QStringLiteral("ok")).toBool()) return applied;

    // The file-resource writer can invalidate a compiled QML unit by filename
    // or a known structural identifier, but MSI 5 units commonly have an empty
    // sourceFileIndex and omit the filename. Apply this feature's full page-ID
    // set as a second, verified pass. Never tell the user "applied" while
    // bytecode that still draws the original UI remains active.
    QByteArray written;
    if (!readBounded(exePath, &written))
        return fail(QStringLiteral("The patched player could not be read back to verify its compiled Settings QML."));
    const int activeBefore = QtResources::activeUnits(written,
        {kWindowLeaf, kAboutLeaf}, kWindowUnitIds + kAboutUnitIds);
    const int invalidated = QtResources::invalidateUnits(written,
        {kWindowLeaf, kAboutLeaf}, kWindowUnitIds + kAboutUnitIds);
    const int activeAfter = QtResources::activeUnits(written,
        {kWindowLeaf, kAboutLeaf}, kWindowUnitIds + kAboutUnitIds);
    if (activeAfter > 0) {
        QByteArray current;
        QString rollbackError;
        if (readBounded(exePath, &current) && current != image)
            PatchFile::writeVerified(exePath, current, image, &rollbackError);
        return fail(QStringLiteral("The Settings QML source was changed, but %1 active compiled unit(s) could not be disabled. The patch was rolled back where safe; restore the player backup if the rollback warning appears. %2")
            .arg(activeAfter).arg(rollbackError));
    }
    if (written != prepared) {
        QByteArray current;
        QString error;
        if (!readBounded(exePath, &current)
            || !PatchFile::writeVerified(exePath, current, written, &error)) {
            QByteArray afterFailure;
            QString rollbackError;
            if (readBounded(exePath, &afterFailure) && afterFailure != image)
                PatchFile::writeVerified(exePath, afterFailure, image, &rollbackError);
            return fail(QStringLiteral("The embedded compiled Settings units could not be committed safely. %1 %2")
                .arg(error, rollbackError));
        }
        QtResources::clearPlayerQmlCache(exePath);
    }
    if (activeBefore > 0) {
        QString message = applied.value(QStringLiteral("msg")).toString();
        message += QStringLiteral(" Disabled %1 stale compiled Settings/About unit(s); verified the player will compile the rebuilt QML on restart.").arg(invalidated);
        applied.insert(QStringLiteral("msg"), message);
    }
    return applied;
}

bool sourceHasMarker(const QByteArray &bytes, const QString &marker)
{
    return EmbeddedResources::decodeText(bytes).contains(marker);
}

bool hasWindowMarker(const QString &text)
{
    // Keep recognizing v1 so users can update or safely restore a panel built
    // by the previous Settings design.
    return text.contains(kWindowMarker) || text.contains(kWindowDesignMarker);
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
    state.settingsPageCount = sources.pageCount;
    state.settingsQmlResourceCount = sources.qmlClosureCount;
    if (!sources.about.text.contains(kAboutMarker) && hasAboutPatcherEdits(sources.about.text)) {
        state.supported = false;
        state.detail = QStringLiteral("The About page already contains separate About-editor changes. Restore the About page to stock before rebuilding the Settings panel, so those links and text are not overwritten.");
        return state;
    }
    const bool windowPatched = hasWindowMarker(sources.window.text);
    const bool aboutPatched = sources.about.text.contains(kAboutMarker);
    state.compiledUnitsDetected = QtResources::matchingUnits(image,
        {kWindowLeaf, kAboutLeaf}, kWindowUnitIds + kAboutUnitIds);
    state.compiledUnitsActive = QtResources::activeUnits(image,
        {kWindowLeaf, kAboutLeaf}, kWindowUnitIds + kAboutUnitIds);
    state.applied = windowPatched && aboutPatched && state.compiledUnitsActive == 0;
    const QString backup = sidecarPath(exePath);
    const QVariantMap recovery = backup.isEmpty() ? QVariantMap{} : readRecovery(backup, exePath);
    state.restorable = windowPatched && aboutPatched && !recovery.isEmpty();
    if (windowPatched != aboutPatched) {
        state.supported = false;
        state.detail = QStringLiteral("Only part of the custom Settings panel is present. Restore from the feature recovery snapshot before retrying.");
    } else if (windowPatched && aboutPatched && !state.restorable) {
        state.supported = false;
        state.detail = QStringLiteral("The custom Settings/About sources are present, but their feature recovery snapshot is missing or invalid. Do not overwrite this executable.");
    } else if (windowPatched && aboutPatched && state.compiledUnitsActive > 0) {
        state.detail = QStringLiteral("The Settings/About sources are patched and %1 native tab pages were resolved, but %2 matching embedded compiled unit(s) still take precedence. Reapply to invalidate them; the previous source write alone did not change the visible emulator UI.")
            .arg(state.settingsPageCount).arg(state.compiledUnitsActive);
    } else if (state.applied && state.compiledUnitsDetected > 0) {
        state.detail = QStringLiteral("The rebuilt Settings/About sources and all %1 native tab pages were resolved; all %2 matching compiled QML unit(s) are inactive. Restart the player to test the appearance; live rendering has not been verified.")
            .arg(state.settingsPageCount).arg(state.compiledUnitsDetected);
    } else if (state.applied) {
        state.detail = QStringLiteral("The rebuilt Settings/About sources and all %1 native tab pages were resolved across %2 QML resources. This executable has no recognizable embedded QV4 unit for the changed pages, so the visual result still requires a player restart/test.")
            .arg(state.settingsPageCount).arg(state.settingsQmlResourceCount);
    } else {
        state.detail = QStringLiteral("The Settings window has %1 native page roots and a verified %2-resource QML dependency closure. Source files are ready; no emulator UI change is claimed yet.")
            .arg(state.settingsPageCount).arg(state.settingsQmlResourceCount);
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
    if (!current.about.text.contains(kAboutMarker) && hasAboutPatcherEdits(current.about.text))
        return fail(QStringLiteral("The About page contains changes from the separate About editor. Restore that page to stock first; the Settings rebuild was not allowed to overwrite its links or text."));
    QVariantMap recovery = readRecovery(backup, exePath);
    const bool hadRecovery = !recovery.isEmpty();

    if (!enabled) {
        if (recovery.isEmpty()) return fail(QStringLiteral("No verified Settings-panel recovery snapshot was found; nothing was changed."));
        QString recoveryError;
        const QMap<QString, QByteArray> originals = originalsFromRecovery(recovery, &recoveryError);
        if (originals.isEmpty()) return fail(recoveryError);
        if (originals.size() != 2 || !originals.contains(current.window.path) || !originals.contains(current.about.path))
            return fail(QStringLiteral("The executable's resource paths no longer match the saved Settings-panel snapshot; restore is blocked."));
        if (!hasWindowMarker(current.window.text) || !current.about.text.contains(kAboutMarker))
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
    const bool hasPatchMarker = hasWindowMarker(current.window.text)
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
    const QString rebuiltAbout = buildAbout(developerName, links, current.aboutLogoExtension);
    if (rebuiltAbout.isEmpty())
        return fail(QStringLiteral("The About page could not be generated; no file was changed."));
    QMap<QString, QByteArray> replacements {
        {current.window.path, utf8PreservingBom(rebuiltWindow, originals.value(current.window.path))},
        {current.about.path, utf8PreservingBom(rebuiltAbout, originals.value(current.about.path))},
    };
    if (!hasWindowMarker(EmbeddedResources::decodeText(replacements.value(current.window.path)))
        || !sourceHasMarker(replacements.value(current.about.path), kAboutMarker))
        return fail(QStringLiteral("Generated QML failed its marker check; no file was changed."));
    if (replacements.value(current.window.path) == current.window.bytes
        && replacements.value(current.about.path) == current.about.bytes) {
        const int active = QtResources::activeUnits(image,
            {kWindowLeaf, kAboutLeaf}, kWindowUnitIds + kAboutUnitIds);
        if (active == 0)
            return {{QStringLiteral("ok"), true},
                    {QStringLiteral("msg"), QStringLiteral("The requested Settings-panel design is already applied and no stale compiled QML remains active.")}};
        QByteArray invalidated = image;
        QtResources::invalidateUnits(invalidated,
            {kWindowLeaf, kAboutLeaf}, kWindowUnitIds + kAboutUnitIds);
        if (QtResources::activeUnits(invalidated,
                {kWindowLeaf, kAboutLeaf}, kWindowUnitIds + kAboutUnitIds) > 0)
            return fail(QStringLiteral("The Settings/About sources already match, but their embedded compiled QML is still active and could not be disabled."));
        QLockFile lock(canonical + QStringLiteral(".bsi-settings-panel.lock"));
        if (!lock.tryLock(0))
            return fail(QStringLiteral("Another Settings-panel writer is using this executable; retry after it finishes."));
        QString error;
        if (!ThemeEngine::backup(exePath) || !PatchFile::writeVerified(exePath, image, invalidated, &error))
            return fail(QStringLiteral("Could not safely disable stale compiled Settings/About QML. %1").arg(error));
        QtResources::clearPlayerQmlCache(exePath);
        return {{QStringLiteral("ok"), true},
                {QStringLiteral("msg"), QStringLiteral("The Settings/About QML sources were already rebuilt; %1 stale compiled unit(s) were invalidated and verified. Restart BlueStacks to load the source.").arg(active)}};
    }

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
