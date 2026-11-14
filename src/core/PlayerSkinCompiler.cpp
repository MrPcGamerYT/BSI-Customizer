#include "PlayerSkinCompiler.h"
#include "SkinProfiles.h"
#include "QtResources.h"
#include "EmbeddedResources.h"
#include <QCryptographicHash>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QSet>

namespace PlayerSkinCompiler {
namespace {
QVariantMap fail(const QString &msg) { return {{"ok",false},{"msg",msg}}; }
const QMap<QString,QStringList> &identifiers() {
    static const QMap<QString,QStringList> ids{
        {"UiButton.qml",{"pSubText","pExtraWidthAvailable","gradientBorderComp"}},
        {"UiImageButton.qml",{"pImageAnimationRunning","pDisableClickAndHoveredState","pUseImgSourceSize"}},
        {"UiTextField.qml",{"pInputValidationState","pErrorTemplateContent","fGetCurrentScreen"}},
        {"UiComboBox.qml",{"pMaxDropDownHeight","pVisibilityRole","pContentRightPadding"}},
        {"UiSlider.qml",{"pRecommendedValueText","pRangeValueVisibility","contentHeader"}},
        {"UiToggleButton.qml",{"pUseShortcutTooltip","iIndicatorMouseArea"}},
        {"UiDialog.qml",{"pDialogHeaderImageSource","pDialogImageHeadingText","iDialogCheckBox"}},
        {"UiSettingsWindow.qml",{"iSettingsWindowShadowContainer","fChangeSelectedSetting"}},
        {"UiShadowContainer.qml",{"containerContentRect","shadowEffectContainer","shadowThickness"}},
        {"UiToolTip.qml",{"toolTipShadowContainer","toolTipRect","pDisableWrapping"}},
        {"UiToast.qml",{"iToastRow","pDefaultTime","iCloseAnimation"}},
        {"PlrSidebar.qml",{"fGetMacroBtnEnabled","fHandleInstallApkBtnClicked"}},
        {"PlrTopbar.qml",{"fOpenHamburgerPopup","fReloadTopbarElements"}}
    };
    return ids;
}
const QString begin="/* BSI_PLAYER_SKIN_V1_BEGIN */", end="/* BSI_PLAYER_SKIN_V1_END */";
QString settings(const QVariantMap &p) {
    return begin+QStringLiteral("\n    readonly property int bsiSkinRadius: %1\n    readonly property int bsiSkinDuration: %2\n    readonly property color bsiSkinAccent: '%3'\n    readonly property color bsiSkinSurface: '%4'\n    ")
        .arg(p.value("radius").toInt()).arg(p.value("motion").toString()==QLatin1String("none")?0:p.value("duration").toInt())
        .arg(p.value("accent").toString(),p.value("surface").toString())+end;
}
QString behavior(const QString &property,const QString &type="ColorAnimation") {
    return QStringLiteral("\n        Behavior on %1 { %2 { duration: bsiSkinDuration; easing.type: Easing.OutCubic } }").arg(property,type);
}
QString transitions() {
    // No scaling of content/hit targets. Popup owns its visibility/close policy.
    return "\n    enter: Transition { NumberAnimation { property: 'opacity'; from: 0; to: 1; duration: bsiSkinDuration } }"
           "\n    exit: Transition { NumberAnimation { property: 'opacity'; to: 0; duration: bsiSkinDuration } }\n";
}
bool precise(const QByteArray &image,const QtResources::File &file,QString *error) {
    QByteArray copy=image;
    const int count=QtResources::sourceUnits(copy,{file.name},identifiers().value(file.name));
    if(count>1 || (count==0 && image.contains(QByteArray("qv4cdata",8)))) {
        if(error)*error=QStringLiteral("%1: cannot identify exactly one compiled source unit (found %2). Source-only skin success is refused.").arg(file.name).arg(count);
        return false;
    }
    return true;
}
QVariantMap operation(const QString &kind,const QString &path,const QByteArray &data) {
    return {{"kind",kind},{"path",path},{"data",QString::fromLatin1(data.toBase64())},
            {"sha256",QString::fromLatin1(QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex())}};
}
QString glyph(QString leaf) {
    leaf=QFileInfo(leaf).completeBaseName();
    leaf.remove(QRegularExpression("_(hover|click|disabled)$"));
    static const QMap<QString,QString> map{{"SidebarHome","home"},{"SidebarBack","back"},{"SidebarRecents","apps"},
        {"SidebarSettings","settings"},{"SidebarFullScreen","fullscreen"},{"FullScreen","fullscreen"},
        {"SidebarScreenshot","capture"},{"Screenshot","capture"},{"Volume","volume"},
        {"SidebarVolume","volume"},{"Keyboard","controls"},{"SidebarGameControls","controls"}};
    return map.value(leaf);
}
}
QStringList components() { return identifiers().keys(); }
QVariantMap compile(const QString &leaf,const QByteArray &source,const QVariantMap &profile) {
    const auto validated=SkinProfiles::validate(profile);
    if(!validated.value("ok").toBool())return validated;
    if(!identifiers().contains(leaf)||source.isEmpty()||source.size()>2*1024*1024)return fail("Unsupported or oversized player component.");
    QString s=QString::fromUtf8(source);s.replace("\r\n","\n");
    if(s.toUtf8()!=QByteArray(source).replace("\r\n","\n"))return fail("Player source is not valid UTF-8.");
    if(s.contains(begin)) {
        if(s.count(begin)!=1||s.count(end)!=1||s.indexOf(end)<s.indexOf(begin))return fail("Ambiguous existing player-skin settings.");
        const int a=s.indexOf(begin),b=s.indexOf(end)+end.size();
        s.replace(a,b-a,settings(profile));
        return {{"ok",true},{"source",s.toUtf8()},{"msg","Existing component skin settings updated."}};
    }
    for(const auto &id:identifiers().value(leaf)) if(!s.contains(id))return fail("Player component contract differs: "+leaf+" / "+id);
    // Only exact known declarations are touched. If a future OEM implementation
    // differs, report unsupported instead of rewriting arbitrary QML by regex.
    bool ok=true;
    const auto replace=[&](const QString &from,const QString &to,int expected=1){
        if(s.count(from)!=expected){ok=false;return;}s.replace(from,to);
    };
    if(leaf=="UiButton.qml") {
        replace("radius: iControl.pSize.radius","radius: bsiSkinRadius",3);
        replace("color: iControl.pColorState.background","color: iControl.pColorState.background"+behavior("color"),2);
    } else if(leaf=="UiImageButton.qml") {
        replace("background: Rectangle {\n        color: \"transparent\"","background: Rectangle {\n        radius: bsiSkinRadius\n        color: iControl.hovered ? Qt.rgba(bsiSkinAccent.r,bsiSkinAccent.g,bsiSkinAccent.b,0.14) : 'transparent'"+behavior("color"));
    } else if(leaf=="UiTextField.qml"||leaf=="UiComboBox.qml") {
        replace("color: iControl.pColorState.background","radius: bsiSkinRadius\n        color: iControl.pColorState.background"+behavior("color")+behavior("border.color"));
        if(leaf=="UiComboBox.qml") {
            replace("popup: Popup {","popup: Popup {"+transitions());
            replace("color: iControl.pColor.normal.background","radius: bsiSkinRadius\n            color: iControl.pColor.normal.background");
        }
    } else if(leaf=="UiSlider.qml") {
        replace("border.color: iControl.pColor.handleBorder","border.color: iControl.pColor.handleBorder"+behavior("color")+behavior("border.color"));
        replace("implicitHeight: iControl.pSize.sliderHeight","implicitHeight: iControl.pSize.sliderHeight\n                radius: height / 2");
    } else if(leaf=="UiToggleButton.qml") {
        replace("indicator: UiImage {","indicator: Rectangle {");
        const QRegularExpression image("        pImageWidth: iControl\\.pSize\\.iconWidth\\n        pImageHeight: iControl\\.pSize\\.iconHeight\\n        asset: [^\\n]+\\n");
        if(s.count(image)!=1)ok=false;
        s.replace(image,"        implicitWidth: 42; implicitHeight: 24\n        width: implicitWidth; height: implicitHeight; radius: height / 2\n        color: iControl.checked ? bsiSkinAccent : '#38383f'\n        Rectangle { width: 18; height: 18; radius: 9; y: 3\n            x: iControl.checked ? parent.width - width - 3 : 3\n            color: '#f2f2f5'\n            Behavior on x { NumberAnimation { duration: bsiSkinDuration; easing.type: Easing.OutCubic } }\n        }\n"+behavior("color")+"\n");
    } else if(leaf=="UiDialog.qml") {
        if(s.contains("enter:")||s.contains("exit:"))ok=false;
        replace("background: Rectangle {","background: Rectangle {\n        radius: bsiSkinRadius");
        replace("id: iControl","id: iControl\n"+transitions());
    } else if(leaf=="UiSettingsWindow.qml") {
        replace("color: UiTheme.colors.primary80","radius: bsiSkinRadius\n            color: bsiSkinSurface");
    } else if(leaf=="UiShadowContainer.qml") {
        replace("cornerRadius: 10","cornerRadius: bsiSkinRadius");
        replace("id: containerContentRect","id: containerContentRect\n        radius: bsiSkinRadius");
    } else if(leaf=="UiToolTip.qml") {
        replace("cornerRadius: 4","cornerRadius: bsiSkinRadius");
        replace("id: toolTipRect","id: toolTipRect\n            radius: bsiSkinRadius");
    } else if(leaf=="UiToast.qml") {
        replace("readonly property real pFadeTime: 300","readonly property real pFadeTime: bsiSkinDuration");
        replace("id: iToastRow","id: iToastRow\n    radius: bsiSkinRadius");
    } else {
        // Chrome: leave child anchors, window handling, callbacks and game
        // rendering untouched. Shared control/icon changes style its contents.
        const QString id=leaf=="PlrSidebar.qml"?"iSidebar":"iRoot";
        if(QRegularExpression("\\n    radius\\s*:").match(s).hasMatch())ok=false;
        replace("id: "+id,"id: "+id+"\n    radius: bsiSkinRadius");
    }
    if(!ok)return fail("Unrecognized/modified component shape: "+leaf+". No replacement generated.");
    const QRegularExpression root("(?m)^(Button|TextField|ComboBox|Slider|Switch|Dialog|Window|Item|Rectangle|UiRowLayout)\\s*\\{");
    const auto m=root.match(s);if(!m.hasMatch())return fail("Unsupported component root: "+leaf);
    s.insert(m.capturedEnd(),"\n    "+settings(profile)+"\n");
    return {{"ok",true},{"source",s.toUtf8()},{"msg","Generated rounded player component with interaction motion: "+leaf}};
}
QVariantMap plan(const QByteArray &image,const QVariantMap &profile,const QStringList &manualPaths) {
    const auto valid=SkinProfiles::validate(profile);if(!valid.value("ok").toBool())return valid;
    QVariantList ops,rows;QSet<QString> found,glyphs;QSet<qint64> seen;
    const auto files=QtResources::registeredFiles(image);
    for(const auto &file:files) {
        const QString g=glyph(file.name);
        if(!identifiers().contains(file.name)&&g.isEmpty())continue;
        if(!manualPaths.contains(file.path))return fail("Manually Scan Resources for this exact EXE before building a player GUI pack.");
        if(seen.contains(file.entryOff))return fail("Aliased player-skin resources need separate build support: "+file.path);
        seen.insert(file.entryOff);
        if(identifiers().contains(file.name)) {
            if(found.contains(file.name))return fail("Duplicate player component: "+file.name);
            QString error;if(!precise(image,file,&error))return fail(error);
            const auto r=compile(file.name,QtResources::readFile(image,file,2*1024*1024),profile);
            if(!r.value("ok").toBool())return r;
            ops<<operation("player-qml",file.path,r.value("source").toByteArray());found.insert(file.name);
            rows<<QVariantMap{{"path",file.path},{"feature","corners / controls / motion"}};
        } else if(file.name.endsWith(".svg",Qt::CaseInsensitive)) {
            QString tint=profile.value("accent").toString();
            if(file.name.contains("_hover"))tint="#f2f2f5";
            if(file.name.contains("_disabled"))tint="#656570";
            const auto svg=SkinProfiles::icon(g,profile.value("iconStyle").toString(),tint);
            ops<<operation("resource",file.path,svg);glyphs.insert(g);
            rows<<QVariantMap{{"path",file.path},{"feature","icon / "+g}};
        }
    }
    const QStringList required{"UiButton.qml","UiImageButton.qml","UiTextField.qml","UiComboBox.qml","UiDialog.qml","PlrSidebar.qml","PlrTopbar.qml"};
    QStringList missing;for(const auto &name:required)if(!found.contains(name))missing<<name;
    for(const auto &g:QStringList{"home","back","settings","controls","volume"})if(!glyphs.contains(g))missing<<"icon:"+g;
    if(!missing.isEmpty())return {{"ok",false},{"coverage",rows},{"missing",missing},{"msg","Full GUI pack refused: required resources are not in this EXE: "+missing.join(", ")+". They may be compiled-only or stored in companion UI DLLs/RCCs. No palette-only success was staged."}};
    if(ops.size()>126)return fail("Player GUI pack exceeds the 128-operation transaction limit.");
    return {{"ok",true},{"operations",ops},{"coverage",rows},{"msg",QStringLiteral("%1 player QML components and %2 icon families prepared. Native/Android surfaces, raster artwork, external DLL/RCC sources and boot are not modified.").arg(found.size()).arg(glyphs.size())}};
}
bool replace(QByteArray &image,const QString &path,const QByteArray &source,QString *error) {
    const auto failWith=[&](const QString &msg){if(error)*error=msg;return false;};
    QtResources::File selected;int matches=0;
    for(const auto &file:QtResources::registeredFiles(image))if(file.path==path){selected=file;++matches;}
    if(matches!=1||!identifiers().contains(selected.name))return failWith("Missing or ambiguous player QML source.");
    if(!precise(image,selected,error))return false;
    auto link=QtResources::findLink(image,selected.entryOff,selected.name);
    if(!link.found||link.ambiguous||link.sourceNames.size()!=1)return failWith("Ambiguous/aliased player component mapping.");
    auto room=EmbeddedResources::budget(image,selected.entryOff,selected.name);
    if(!room.known)return failWith(room.detail);
    const auto size=EmbeddedResources::payloadSizeFor(source,selected.flags);
    qint64 destination=-1;
    if(size>room.capacity){destination=QtResources::findFreeSpace(image,link,size+4,error);if(destination<0)return false;}
    QByteArray changed=image;qint64 entry=selected.entryOff;
    if(!QtResources::replaceResource(changed,entry,link,source,error,&entry,false,destination,identifiers().value(selected.name)))return false;
    if(changed.size()!=image.size()||QtResources::readFile(changed,{selected.name,entry,selected.flags})!=source)return failWith("Player component read-back/size verification failed.");
    image=changed;return true;
}
}
