#include "EmulatorUiStudio.h"
#include "SkinProfiles.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QUrl>
#include <QStringList>
#include <QVector>

namespace EmulatorUiStudio {
namespace {
QString hash(const QByteArray &data) { return QString::fromLatin1(QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex()); }
QVariantMap fail(const QString &msg) { return {{"ok",false},{"msg",msg}}; }
QString quote(const QString &s) {
    const auto json=QJsonDocument(QJsonArray{s}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(json.mid(1,json.size()-2));
}
bool noSymlink(QString path) {
    while(!path.isEmpty()) {
        const QFileInfo info(path);
        if(info.isSymLink())return false;
        const QString parent=info.dir().absolutePath();
        if(parent==path)break;
        path=parent;
    }
    return true;
}
}
QMap<QString,QByteArray> files(const QVariantMap &p) {
    if(!SkinProfiles::validate(p).value("ok").toBool())return {};
    QMap<QString,QByteArray> out;
    const auto put=[&](const QString &name,const QString &text){out[name]=text.toUtf8();};
    put("qmldir","singleton Theme 1.0 Theme.qml\n");
    put("Theme.qml",QStringLiteral("pragma Singleton\nimport QtQuick 2.15\nQtObject {\n"
        "    readonly property color accent: %1\n    readonly property color surface: %2\n"
        "    readonly property int corner: %3\n    readonly property int duration: %4\n"
        "    readonly property string motion: %5\n}\n")
        .arg(quote(p.value("accent").toString()),quote(p.value("surface").toString()))
        .arg(p.value("radius").toInt()).arg(p.value("motion").toString()=="none"?0:p.value("duration").toInt())
        .arg(quote(p.value("motion").toString())));
    put("Icons.js",".pragma library\nvar style = "+quote(p.value("iconStyle").toString())+";\n");
    put("NativeControlStyle.qml",R"(import QtQuick 2.15
import "Icons.js" as Icons
Rectangle {
    id: control
    property bool hovered: false
    property bool pressed: false
    property bool selected: false
    radius: Icons.style === "bold" ? Math.min(Theme.corner, 6) : Theme.corner
    color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, pressed ? 0.22 : hovered ? 0.12 : selected ? 0.06 : 0)
    border.width: hovered || selected ? 1 : 0
    border.color: selected ? Theme.accent : Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.35)
    Behavior on color { ColorAnimation { duration: Theme.duration } }
    Behavior on border.color { ColorAnimation { duration: Theme.duration } }
}
)");
    put("BSISettingsOutline.qml",R"(import QtQuick 2.15
NativeControlStyle {
    Rectangle {
        width: 3; height: Math.max(0, parent.height - 14); radius: 1.5
        anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
        color: Theme.accent; visible: parent.selected
    }
}
)");
    put("SettingsShell.qml",R"(import QtQuick 2.15
Rectangle {
    id: shell
    color: Theme.surface; radius: Theme.corner
    border.width: 1; border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.3)
    property real reveal: Theme.motion === "none" ? 1 : 0
    opacity: reveal
    transform: [
        Translate { x: Theme.motion === "slide" ? (1-shell.reveal)*18 : 0; y: Theme.motion === "rise" ? (1-shell.reveal)*14 : 0 },
        Scale { origin.x: shell.width/2; origin.y: shell.height/2; xScale: Theme.motion === "scale" ? 0.97+shell.reveal*0.03 : 1; yScale: xScale }
    ]
    NumberAnimation on reveal { from: 0; to: 1; duration: Theme.duration; easing.type: Easing.OutCubic; running: Theme.motion !== "none" }
    Rectangle {
        x: parent.radius; y: 0; width: Math.max(0, parent.width-2*x); height: 1
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: "transparent" }
            GradientStop { position: 0.5; color: Theme.accent }
            GradientStop { position: 1; color: "transparent" }
        }
    }
}
)");
    put("SidebarVisual.qml",R"(import QtQuick 2.15
Rectangle {
    color: "transparent"; radius: Theme.corner
    border.width: 1; border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.2)
    Rectangle {
        x: 0; y: Math.min(12, parent.height/2); width: 2; height: Math.max(0, parent.height-2*y); radius: 1
        color: Theme.accent; opacity: 0.55
        SequentialAnimation on opacity {
            running: Theme.motion !== "none" && Theme.duration > 0 && visible
            loops: Animation.Infinite
            NumberAnimation { to: 0.18; duration: Math.max(800, Theme.duration*6) }
            NumberAnimation { to: 0.55; duration: Math.max(800, Theme.duration*6) }
        }
    }
}
)");
    put("BSISidebarVisual.qml","import QtQuick 2.15\nSidebarVisual {}\n");
    put("SidebarSkin.qml","import QtQuick 2.15\nBSISidebarVisual {}\n");
    put("BSITopbarVisual.qml",R"(import QtQuick 2.15
Rectangle {
    color: "transparent"; radius: Theme.corner
    border.width: 1; border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.2)
    Rectangle { x: parent.radius; y: parent.height-1; height: 1; width: Math.max(0, parent.width-2*x); color: Theme.accent; opacity: 0.45 }
}
)");
    put("TopbarSkin.qml","import QtQuick 2.15\nBSITopbarVisual {}\n");
    for(const auto &type:QStringList{"NativeControlStyle","BSISettingsOutline","SettingsShell","SidebarVisual","BSISidebarVisual","SidebarSkin","BSITopbarVisual","TopbarSkin"})
        out["qmldir"]+=type.toUtf8()+" 1.0 "+type.toUtf8()+".qml\n";
    out["profile.json"]=QJsonDocument(QJsonObject::fromVariantMap(p)).toJson();
    out["README.txt"]="BSI Emulator UI Studio\nNative Settings/sidebar/topbar and shared controls load this immutable bundle after Workspace Apply.\nDeveloped By NOVA XIS\nhttps://www.youtube.com/@nova_xis20\n\nDo not delete this folder while an applied player references it. Export or regenerate a new bundle to edit.\nWorkspace recovery restores the EXE; bundles remain so older recovery points still work.\nBoot, installer artwork, native navigation, optimizer actions and Android surfaces are not replaced.\n";
    return out;
}
bool verify(const QString &directory,const QVariantMap &hashes) {
    if(hashes.isEmpty()||hashes.size()>32||!noSymlink(QDir(directory).absolutePath()))return false;
    for(auto it=hashes.cbegin();it!=hashes.cend();++it) {
        if(it.key().contains('/')||it.key().contains('\\')||it.key()=="."||it.key()=="..")return false;
        QFile f(QDir(directory).filePath(it.key()));
        if(QFileInfo(f).isSymLink()||!f.open(QIODevice::ReadOnly)||f.size()>512*1024||hash(f.readAll())!=it.value().toString())return false;
    }
    return true;
}
QVariantMap publish(const QString &parent,const QVariantMap &p) {
    const auto valid=SkinProfiles::validate(p);if(!valid.value("ok").toBool())return valid;
    const auto bundle=files(p);QVariantMap hashes;QByteArray identity;
    for(auto it=bundle.cbegin();it!=bundle.cend();++it){hashes[it.key()]=hash(it.value());identity+=it.key().toUtf8()+'\0'+it.value()+'\0';}
    if(parent.trimmed().isEmpty())return fail("Choose an export folder first.");
    const QString root=QDir(parent).absolutePath(),name="studio-"+hash(identity),directory=QDir(root).filePath(name);
    if(!noSymlink(root)||!QDir().mkpath(root))return fail("Cannot create the Assets bundle folder. Check write permission; no EXE changed.");
    QLockFile lock(QDir(root).filePath(".bsi-studio.lock"));
    if(!lock.tryLock())return fail("Another Assets export is running.");
    if(QFileInfo::exists(directory)) {
        if(!verify(directory,hashes))return fail("An existing bundle was changed. Nothing was overwritten. Choose another export folder or restore the bundle.");
    } else {
        QTemporaryDir temp(QDir(root).filePath(".bsi-studio-XXXXXX"));
        if(!temp.isValid())return fail("Cannot stage the Assets bundle.");
        for(auto it=bundle.cbegin();it!=bundle.cend();++it) {
            QFile f(QDir(temp.path()).filePath(it.key()));
            if(!f.open(QIODevice::WriteOnly)||f.write(it.value())!=it.value().size())return fail("Assets export failed; the EXE was not changed.");
        }
        if(!verify(temp.path(),hashes)||!QDir().rename(temp.path(),directory))return fail("Cannot publish the verified Assets bundle.");
    }
    return {{"ok",true},{"directory",directory},{"hashes",hashes},{"msg","Generated a versioned Assets bundle. Native hooks must be staged and applied separately."}};
}
QVariantMap hook(const QString &leaf,const QByteArray &source,const QString &directory) {
    QString s=QString::fromUtf8(source);
    const QString marker="/* BSI_STUDIO_HOOK_BEGIN */",end="/* BSI_STUDIO_HOOK_END */";
    // Refuse stacking a new contract over already-connected source. The compiler
    // settings block is updated separately; immutable assets can be reapplied.
    const bool attached=s.contains(marker);
    if(attached) {
        if(s.count(marker)!=1||s.count(end)!=1||s.indexOf(end)<s.indexOf(marker))return fail("Ambiguous Studio import.");
        const int start=s.indexOf(marker),length=s.indexOf(end)+end.size()-start;
        s.replace(start,length,marker+"\nimport "+quote(QUrl::fromLocalFile(directory).toString(QUrl::FullyEncoded))+" as BSIStudio\n"+end);
    }
    if(!attached && s.contains("BSIStudio"))return fail("Unknown existing Studio contract.");
    const QRegularExpression root("(?m)^(Button|TextField|ComboBox|Slider|Switch|Dialog|Window|Item|Rectangle|UiRowLayout)\\s*\\{");
    const auto m=root.match(s);if(!m.hasMatch())return fail("Unsupported Studio root: "+leaf);
    if(!attached)s.insert(m.capturedStart(),marker+"\nimport "+quote(QUrl::fromLocalFile(directory).toString(QUrl::FullyEncoded))+" as BSIStudio\n"+end+"\n");
    const QRegularExpression props("readonly property (int|color) (bsiSkinRadius|bsiSkinDuration|bsiSkinAccent|bsiSkinSurface): [^\\n]+");
    auto matches=props.globalMatch(s);QVector<QRegularExpressionMatch> list;while(matches.hasNext())list<<matches.next();
    if(list.size()!=4)return fail("Missing compiled Studio settings: "+leaf);
    const QMap<QString,QString> values{{"bsiSkinRadius","corner"},{"bsiSkinDuration","duration"},{"bsiSkinAccent","accent"},{"bsiSkinSurface","surface"}};
    for(auto it=list.crbegin();it!=list.crend();++it)s.replace(it->capturedStart(),it->capturedLength(),"readonly property "+it->captured(1)+" "+it->captured(2)+": BSIStudio.Theme."+values.value(it->captured(2)));
    if(attached)return {{"ok",true},{"source",s.toUtf8()}};
    if(leaf=="UiSettingsWindow.qml") {
        const QString shell="id: iSettingsWindowShadowContainer\n        Rectangle {";
        if(s.count(shell)!=1||s.count("id: iSettingBtn")!=1)return fail("Settings shell differs; Studio refused.");
        s.replace(shell,"id: iSettingsWindowShadowContainer\n        BSIStudio.SettingsShell {");
        s.replace("id: iSettingBtn","id: iSettingBtn\n                                        BSIStudio.BSISettingsOutline { anchors.fill: parent; z: 5; selected: iSettingBtn.pSelected }");
    } else if(leaf=="UiImageButton.qml") {
        if(s.count("background: Rectangle {")!=1)return fail("Image-button background differs; Studio refused.");
        s.replace("background: Rectangle {","background: BSIStudio.NativeControlStyle {\n        hovered: iControl.hovered; pressed: iControl.down");
    } else if(leaf=="PlrSidebar.qml"||leaf=="PlrTopbar.qml") {
        const QString id=leaf=="PlrSidebar.qml"?"iSidebar":"iRoot";
        if(s.count("id: "+id+"\n")!=1)return fail("Chrome root differs; Studio refused.");
        s.replace("id: "+id+"\n","id: "+id+"\n    BSIStudio."+(leaf=="PlrSidebar.qml"?"SidebarSkin":"TopbarSkin")+" { anchors.fill: parent; z: 100 }\n");
    }
    return {{"ok",true},{"source",s.toUtf8()}};
}
}
