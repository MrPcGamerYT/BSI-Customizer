var keyImages = [
            "MouseLButton",
            "MouseRButton",
            "MouseMButton",
            "Left",
            "Up",
            "Right",
            "Down",
            "GamepadDpadUp",
            "GamepadDpadDown",
            "GamepadDpadLeft",
            "GamepadDpadRight"
        ]

var keyTextMap = {
    "32": "Space",
    "16777217": "Tab",
    "16777220": "Enter",
    "16777223": "Delete",
    "16777234": "Left",
    "16777235": "Up",
    "16777236": "Right",
    "16777237": "Down",
    "16777252": "CapsLock",
    "16777264": "F1",
    "16777265": "F2",
    "16777266": "F3",
    "16777267": "F4",
    "16777268": "F5",
    "16777269": "F6",
    "16777270": "F7",
    "16777271": "F8",
    "16777272": "F9",
    "16777273": "F10",
    "16777274": "F11",
    "16777275": "F12",
    "34":"Oem7",
    "39": "Oem7",
    "43": "OemPlus",
    "44": "OemComma",
    "45": "OemMinus",
    "46": "OemPeriod",
    "47": "Oem2",
    "58": "Oem1",
    "59": "Oem1",
    "60": "OemComma",
    "61": "OemPlus",
    "62": "OemPeriod",
    "63": "Oem2",
    "91": "Oem4",
    "92": "Oem5",
    "93": "Oem6",
    "95": "OemMinus",
    "96": "Oem3",
    "123": "Oem4",
    "124": "Oem5",
    "125": "Oem6",
    "126": "Oem3",
    "48": "0",
    "49": "1",
    "50": "2",
    "51": "3",
    "52": "4",
    "53": "5",
    "54": "6",
    "55": "7",
    "56": "8",
    "57": "9",
    "65":"A",
    "66":"B",
    "67":"C",
    "68":"D",
    "69":"E",
    "70":"F",
    "71":"G",
    "72":"H",
    "73":"I",
    "74":"J",
    "75":"K",
    "76":"L",
    "77":"M",
    "78":"N",
    "79":"O",
    "80":"P",
    "81":"Q",
    "82":"R",
    "83":"S",
    "84":"T",
    "85":"U",
    "86":"V",
    "87":"W",
    "88":"X",
    "89":"Y",
    "90":"Z",
    "41": "0",
    "33": "1",
    "64": "2",
    "35": "3",
    "36": "4",
    "37": "5",
    "94": "6",
    "38": "7",
    "42": "8",
    "40": "9"
}

var keysTranslationMap = {
    "Space": qsTranslate("QObject", "Space"),
    "MouseXButton1": qsTranslate("QObject", "X button1"),
    "MouseXButton2": qsTranslate("QObject", "X button2"),
    "MouseWheelUp": qsTranslate("QObject", "MouseWheelUp"),
    "MouseWheelDown": qsTranslate("QObject", "MouseWheelDown")
}

function moveItem(control, x, y, width, height, startingMouse, currentMouse, type = undefined) {
    var newX = x + currentMouse.x - startingMouse.x
    var newY = y + currentMouse.y - startingMouse.y
    var centerX = newX + width / 2
    var centerY = newY + height / 2

    if (centerX > plrBackend.glWinRect[0] && centerX < (plrBackend.glWinRect[2] + plrBackend.glWinRect[0])) {
        if (type === "Swipe")
            control.x1 = (((centerX - plrBackend.glWinRect[0]) * 100) / plrBackend.glWinRect[2]).toFixed(2)
        else
            control.x = (((centerX - plrBackend.glWinRect[0]) * 100) / plrBackend.glWinRect[2]).toFixed(2)
    }
    if (centerY > plrBackend.glWinRect[1] && centerY < (plrBackend.glWinRect[3] + plrBackend.glWinRect[1])) {
        if (type === "Swipe")
            control.y1 = (((centerY - plrBackend.glWinRect[1]) * 100) / plrBackend.glWinRect[3]).toFixed(2)
        else
            control.y = (((centerY - plrBackend.glWinRect[1]) * 100) / plrBackend.glWinRect[3]).toFixed(2)
    }
}

function keyPressed(event, rootItem) {
    // If we cant identify what key came
    if (event.key === 0)
        return

    // Empty the text field
    if (event.key === Qt.Key_Backspace) {
        if (pComposedKey === "")
            rootItem.pComposedKey = ""
        return
    }

    // Dont do anything
    if(event.key === Qt.Key_Escape) {
        return
    }

    if (event.isAutoRepeat)
        return

    event.accepted = true
    rootItem.pComposedKey = kmmBackend.getCurrentCombo()
}


var itemComponent = null;
var draggedItem = null;
var startingMouse;
var posnInWindow;
//var i = 0;
function startDrag(mouse, type)
{
    posnInWindow = iControlRect.mapToItem(iCanvasRect, 0, 0);
    startingMouse = { x: mouse.x, y: mouse.y }
    loadComponent(type);
}

//Creation is split into two functions due to an asynchronous wait while
//possible external files are loaded.

function loadComponent(type) {
    // console.log("creating componenet" + i)
    //i = i+1
    if (itemComponent !== null) { // component has been previously loaded
        createItem(type);
        return;
    }

    itemComponent = Qt.createComponent(iControlRect.componentFile);
    if (itemComponent.status === Component.Loading)  //Depending on the content, it can be ready or error immediately
        component.statusChanged.connect(createItem);
    else
        createItem(type);
}

function createItem() {
    console.log(iControlRect.dragImage)
    if (itemComponent.status === Component.Ready && draggedItem === null) {
        draggedItem = itemComponent.createObject(iKmmEditorWin, {x: posnInWindow.x, y: posnInWindow.y});
        draggedItem.fCreateNewControl()
        if (type === "Swipe") {
            draggedItem.pKmmControl.x1 = posnInWindow.x
            draggedItem.pKmmControl.y1 = posnInWindow.y
        }
        else {
            draggedItem.pKmmControl.x = posnInWindow.x
            draggedItem.pKmmControl.y = posnInWindow.y
        }

        // make sure created item is above the ground layer
    } else if (itemComponent.status === Component.Error) {
        draggedItem = null;
        console.log("error creating component");
        console.log(itemComponent.errorString());
    }
}

function continueDrag(mouse, type)
{
    if (draggedItem === null)
        return;

    moveItem(draggedItem.pKmmControl, posnInWindow.x, posnInWindow.y, width, height, startingMouse, mouse, type)
}

function endDrag(mouse, type)
{
    if (draggedItem === null)
        return;

    if (draggedItem.x > iKmmEditorWin.width) { //Don't drop it in the toolbox
        if (type === "Swipe") {
            draggedItem.fDeleteSwipeControls()
        }
        else
            kmmBackend.kmmEditorDeleteControl(draggedItem.pKmmControl)
        draggedItem = null;
    } else {
        if (type === "Swipe")
            draggedItem.fSetControlsPosition()
        else if(type === "Zoom" || type === "MouseZoom")
            draggedItem.fAssignCoordinates()

        draggedItem.fInEditableMode()
        draggedItem = null;
    }
}

function getPercentValue(val)
{
    var valF = parseFloat(val)
    return valF <= 100 ? valF : 100
}

function deleteControl(visualItem, control)
{
    visualItem.visible = false
    kmmBackend.kmmEditorDeleteControl(control)
}
