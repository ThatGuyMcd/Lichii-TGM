// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic

FocusScope {
    id: keyboard
    objectName:"CouchKeyboard"
    property var skin: Couch.theme.data
    property string title: qsTr("Enter text")
    property bool secret: false
    property int maximumLength: 32767
    property string mode: "abc"
    property bool shifted: false
    property int navRow: 1
    property int navColumn: 0
    property alias text: input.text
    signal accepted(string value)
    signal cancelled()
    implicitWidth: 1040
    implicitHeight: 650
    visible: false
    readonly property var keyRows: mode === "123" ? ["1234567890", "-/:;()$&@\"", ".,?!'_%+="]
                               : mode === "symbols" ? ["[]{}#%^*+=", "_\\|~<>€£¥¢", ".,!?;:@/()-`"]
                               : ["qwertyuiop", "asdfghjkl", "zxcvbnm"]
    property var controls: ({})
    function registerButton(button, row, col) { controls[row + ":" + col] = button }
    function rowLength(row) { return row === 0 ? 4 : row <= 3 ? Array.from(keyRows[row - 1]).length : row === 4 ? 6 : 2 }
    function focusKey(row, column) {
        navRow = Math.max(0, Math.min(5, row)); navColumn = Math.max(0, Math.min(rowLength(navRow) - 1, column))
        Qt.callLater(function() { const button = controls[navRow + ":" + navColumn]; if (button) button.forceActiveFocus(Qt.TabFocusReason) })
    }
    function open(value, heading, password, maxLength) {
        input.text = value || ""; title = heading || qsTr("Enter text"); secret = !!password
        maximumLength = maxLength || 32767; mode = "abc"; shifted = false; visible = true
        input.cursorPosition = input.text.length; focusKey(1, 0)
    }
    function insert(value) {
        const pos = input.cursorPosition; input.insert(pos, value); input.cursorPosition = Math.min(pos + value.length, input.length)
    }
    function edit(operation) {
        const result = Couch.editText(input.text, input.cursorPosition, operation, "")
        input.text = result.text; input.cursorPosition = result.caret
    }
    function controller(action) {
        if (action === "back") { cancelled(); return }
        if (action === "search") { edit("backspace"); return }
        if (action === "up") focusKey(navRow - 1, navColumn)
        else if (action === "down") focusKey(navRow + 1, navColumn)
        else if (action === "left") focusKey(navRow, navColumn - 1)
        else if (action === "right") focusKey(navRow, navColumn + 1)
        else if (action === "accept") { const button = controls[navRow + ":" + navColumn]; if (button) button.clicked() }
    }
    Text { x: 0; y: 0; width: parent.width; height: 48; text: keyboard.title; color: skin.colours.text; font.family: skin.fontFamily; font.pixelSize: 32; font.weight: Font.Medium; elide: Text.ElideRight }
    TextField {
        id: input; objectName: "CouchKeyboardText"; x: 0; y: 62; width: parent.width; height: 66
        maximumLength: keyboard.maximumLength; echoMode: keyboard.secret ? TextInput.Password : TextInput.Normal
        font.family: skin.fontFamily; font.pixelSize: 28; color: skin.colours.text
        selectByMouse: true
        background: Rectangle { color: skin.colours.surface; border.color: skin.colours.focus; border.width: 2; radius: skin.metrics.radius }
        onAccepted: keyboard.accepted(text)
    }
    Row {
        x: 0; y: 146; spacing: 12
        Repeater {
            model: ["ABC", "123", "#+=", "⇧"]
            CButton {
                required property int index; required property string modelData
                width: 112; height: 54; text: modelData; skin: keyboard.skin
                selected: index === 0 && keyboard.mode === "abc" || index === 1 && keyboard.mode === "123" || index === 2 && keyboard.mode === "symbols" || index === 3 && keyboard.shifted
                Component.onCompleted: keyboard.registerButton(this, 0, index)
                onActiveFocusChanged: if (activeFocus) { keyboard.navRow = 0; keyboard.navColumn = index }
                onClicked: { if (index === 3) keyboard.shifted = !keyboard.shifted; else keyboard.mode = ["abc", "123", "symbols"][index]; keyboard.focusKey(0, index) }
            }
        }
    }
    Column {
        x: 0; y: 220; width: parent.width; spacing: 12
        Repeater {
            model: keyboard.keyRows
            Row {
                required property int index; required property string modelData
                property int rowNumber: index + 1
                spacing: 12; anchors.horizontalCenter: parent.horizontalCenter
                Repeater {
                    model: Array.from(modelData)
                    CButton {
                        required property int index; required property string modelData
                        width:Math.min(88,(keyboard.width-(Array.from(parent.modelData).length-1)*12)/Array.from(parent.modelData).length); height: 68; font.pixelSize: 28; skin: keyboard.skin
                        text: keyboard.shifted ? modelData.toUpperCase() : modelData
                        Component.onCompleted: keyboard.registerButton(this, parent.rowNumber, index)
                        onActiveFocusChanged: if (activeFocus) { keyboard.navRow = parent.rowNumber; keyboard.navColumn = index }
                        onClicked: keyboard.insert(text)
                    }
                }
            }
        }
    }
    Row {
        x: 0; y: 478; spacing: 12
        Repeater {
            model: [qsTr("← Cursor"), qsTr("Space"), qsTr("Delete"), qsTr("Paste"), qsTr("Clear"), qsTr("Cursor →")]
            CButton {
                required property int index; required property string modelData
                text: modelData; width: index === 1 ? 220 : 148; height: 60; skin: keyboard.skin
                Component.onCompleted: keyboard.registerButton(this, 4, index)
                onActiveFocusChanged: if (activeFocus) { keyboard.navRow = 4; keyboard.navColumn = index }
                onClicked: {
                    if (index === 0) keyboard.edit("left")
                    else if (index === 1) keyboard.insert(" ")
                    else if (index === 2) keyboard.edit("backspace")
                    else if (index === 3) keyboard.insert(Couch.clipboardText())
                    else if (index === 4) {input.text="";input.cursorPosition=0}
                    else keyboard.edit("right")
                }
            }
        }
    }
    Row {
        x: 0; y: 560; spacing: 16
        CButton { text: qsTr("OK"); width: 180; height: 60; visualStyle: "primary"; skin: keyboard.skin; Component.onCompleted: keyboard.registerButton(this,5,0); onActiveFocusChanged: if (activeFocus) { keyboard.navRow=5;keyboard.navColumn=0 } onClicked: keyboard.accepted(input.text) }
        CButton { text: qsTr("Cancel"); width: 180; height: 60; skin: keyboard.skin; Component.onCompleted: keyboard.registerButton(this,5,1); onActiveFocusChanged: if (activeFocus) { keyboard.navRow=5;keyboard.navColumn=1 } onClicked: keyboard.cancelled() }
    }
    Keys.onEscapePressed: cancelled()
}
