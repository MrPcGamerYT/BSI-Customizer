import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Popup {
    id: panel
    objectName: "voidAppearancePanel"
    width: Math.min(700,parent ? parent.width-32 : 700)
    height: Math.min(650,parent ? parent.height-32 : 650)
    padding: 24
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    property var draft: ({})
    property string errorText: ""
    function loadDraft() {draft=JSON.parse(JSON.stringify(Theme.colors));errorText=""}
    onOpened: {Theme.sync();loadDraft()}
    background: Rectangle {color:Theme.background;radius:Theme.corner;border.width:1;border.color:Theme.border}
    enter: Transition {enabled:Theme.motionEnabled;ParallelAnimation {NumberAnimation {property:"opacity";from:0;to:1;duration:Theme.duration} NumberAnimation {property:"scale";from:0.97;to:1;duration:Theme.duration;easing.type:Easing.OutCubic}}}
    exit: Transition {enabled:Theme.motionEnabled;NumberAnimation {property:"opacity";from:1;to:0;duration:Math.min(120,Theme.duration)}}
    contentItem: ColumnLayout {
        spacing: 16
        RowLayout {
            Layout.fillWidth:true
            ColumnLayout {
                spacing:3
                Label {text:"Appearance";color:Theme.text;font.family:Theme.fontFamily;font.pixelSize:23;font.bold:true}
                Label {text:"Six dark worlds. Your colors. The whole native interface.";color:Theme.muted;font.pixelSize:12}
            }
            Item {Layout.fillWidth:true}
            ToolButton {id:closeAppearance;objectName:"closeAppearance";text:"×";width:32;height:32;hoverEnabled:true;palette.buttonText:Theme.text;onClicked:panel.close();background:Rectangle {radius:8;color:closeAppearance.hovered?Theme.raised:Theme.surface;border.width:1;border.color:Theme.border}}
        }
        ScrollView {
            Layout.fillWidth:true;Layout.fillHeight:true;clip:true
            contentWidth:availableWidth
            ColumnLayout {
                width:parent.width
                spacing:18
                Label {Layout.fillWidth:true;wrapMode:Text.Wrap;color:Theme.warning;visible:!Theme.persistenceAvailable;text:Theme.persistenceNotice}
                GridLayout {
                    columns:2;columnSpacing:10;rowSpacing:10;Layout.fillWidth:true
                    Repeater {
                        model:Theme.presets
                        delegate: AbstractButton {
                            id: card
                            objectName:"themePreset_"+modelData.id
                            Layout.fillWidth:true;implicitHeight:94
                            hoverEnabled:true;padding:13
                            onClicked:{Theme.choose(modelData.id);panel.loadDraft()}
                            scale:down?0.98:1
                            Behavior on scale {enabled:Theme.motionEnabled;NumberAnimation {duration:Theme.duration;easing.type:Easing.OutCubic}}
                            background: Rectangle {
                                color:modelData.palette.background;radius:12
                                border.width:Theme.selectedId===modelData.id?2:1
                                border.color:Theme.selectedId===modelData.id?modelData.palette.accent:card.hovered?Theme.muted:modelData.palette.border
                            }
                            contentItem: ColumnLayout {
                                anchors.margins:13;spacing:5
                                RowLayout {
                                    Label {text:modelData.name;color:modelData.palette.text;font.bold:true;font.pixelSize:14}
                                    Item {Layout.fillWidth:true}
                                    Rectangle {width:8;height:8;radius:4;color:modelData.palette.accent;visible:Theme.selectedId===modelData.id}
                                }
                                Label {Layout.fillWidth:true;elide:Text.ElideRight;text:modelData.description;color:modelData.palette.muted;font.pixelSize:11}
                                Row {
                                    spacing:4
                                    Repeater {model:[modelData.palette.surface,modelData.palette.raised,modelData.palette.border,modelData.palette.accent];Rectangle {width:26;height:5;radius:2;color:modelData}}
                                }
                            }
                        }
                    }
                }
                Label {text:"Make it yours";font.pixelSize:15;font.bold:true;color:Theme.text}
                GridLayout {
                    columns:2;Layout.fillWidth:true;columnSpacing:12;rowSpacing:10
                    Repeater {
                        model:["background","surface","raised","border","text","muted","accent"]
                        delegate: RowLayout {
                            Layout.fillWidth:true;spacing:8
                            Rectangle {width:12;height:26;radius:4;color:panel.draft[modelData] || Theme.surface;border.width:1;border.color:Theme.border}
                            ColumnLayout {
                            Layout.fillWidth:true;spacing:3
                            Label {text:modelData.charAt(0).toUpperCase()+modelData.slice(1);color:Theme.muted;font.pixelSize:10}
                            TextField {
                                id:hex
                                objectName:"customColor_"+modelData
                                Layout.fillWidth:true
                                placeholderText:modelData
                                text:panel.draft[modelData] || ""
                                color:Theme.text;font.family:Theme.fontFamily;font.pixelSize:12
                                selectByMouse:true
                                validator:RegularExpressionValidator {regularExpression:/#[0-9a-fA-F]{6}/}
                                onTextEdited:{var next=Object.assign({},panel.draft);next[modelData]=text;panel.draft=next}
                                background:Rectangle {radius:8;color:Theme.surface;border.width:1;border.color:hex.activeFocus?Theme.accent:Theme.border}
                                ToolTip.visible:hovered;ToolTip.text:modelData
                                Accessible.name:modelData+" color"
                            }
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth:true
                    Label {text:"Font";color:Theme.muted;Layout.preferredWidth:65}
                    TextField {objectName:"themeFont";Layout.fillWidth:true;text:Theme.fontFamily;color:Theme.text;onEditingFinished:if(!Theme.setFont(text))panel.errorText="Use an installed font name (letters, numbers, spaces or hyphens).";background:Rectangle {color:Theme.surface;radius:8;border.width:1;border.color:Theme.border}}
                }
                RowLayout {
                    Layout.fillWidth:true
                    Label {text:"Corners";color:Theme.muted;Layout.preferredWidth:65}
                    Slider {objectName:"themeCorners";Layout.fillWidth:true;from:0;to:24;stepSize:1;value:Theme.corner;onMoved:Theme.setCorner(value);palette.highlight:Theme.accent;palette.button:Theme.accent}
                    Label {text:Theme.corner+" px";color:Theme.text;Layout.preferredWidth:45}
                }
                RowLayout {
                    Layout.fillWidth:true
                    Label {text:"Motion";color:Theme.muted;Layout.preferredWidth:65}
                    Switch {objectName:"themeMotion";checked:Theme.motionEnabled;onToggled:Theme.setMotion(checked);palette.highlight:Theme.accent}
                    Slider {Layout.fillWidth:true;from:0;to:400;stepSize:10;value:Theme.duration;enabled:Theme.motionEnabled;onMoved:Theme.setDuration(value);palette.highlight:Theme.accent}
                    Label {text:Theme.duration+" ms";color:Theme.text;Layout.preferredWidth:55}
                }
                Label {Layout.fillWidth:true;wrapMode:Text.Wrap;color:Theme.warning;visible:Theme.textContrast<4.5;text:"Low text contrast: consider a brighter text color for readability."}
                Label {Layout.fillWidth:true;wrapMode:Text.Wrap;color:Theme.danger;visible:panel.errorText.length>0;text:panel.errorText}
            }
        }
        RowLayout {
            Layout.fillWidth:true;spacing:12
            Label {text:Theme.name;color:Theme.muted;font.pixelSize:12;Layout.fillWidth:true}
            Button {
                text:"Reset";onClicked:{Theme.reset();panel.loadDraft()}
                palette.buttonText:Theme.text
                background:Rectangle {radius:8;color:Theme.surface;border.width:1;border.color:Theme.border}
            }
            Button {
                objectName:"applyCustomTheme";text:"Apply my colors";font.bold:true
                onClicked:{if(Theme.applyCustom(panel.draft))panel.errorText="";else panel.errorText="Every color must be a six-digit hex value, such as #82dfb1."}
                palette.buttonText:Theme.background
                background:Rectangle {radius:8;color:Theme.accent}
            }
        }
    }
}
