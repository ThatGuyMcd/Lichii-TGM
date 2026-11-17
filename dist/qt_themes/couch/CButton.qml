// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic

Button {
    id: control
    property var skin: Couch.theme.data
    property string visualStyle: "normal"
    property bool selected: false
    property string navZone: ""
    property int navIndex: 0
    signal focused(string zone, int itemIndex)
    implicitHeight: 56
    implicitWidth: Math.max(80, contentItem.implicitWidth + 32)
    padding: 12
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    font.family: skin.fontFamily
    font.pixelSize: skin.metrics.valueSize
    Accessible.name: text
    opacity: enabled ? 1 : 0.42
    onActiveFocusChanged: if (activeFocus) focused(navZone, navIndex)
    contentItem: Text {
        text: control.text
        font: control.font
        color: control.visualStyle === "primary" ? control.skin.colours.onPrimary : control.skin.colours.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        maximumLineCount: 1
    }
    background: Rectangle {
        color: control.visualStyle === "primary" ? control.skin.colours.text
               : control.selected || control.down || control.hovered ? control.skin.colours.surface : "transparent"
        radius: control.skin.metrics.radius
        border.color: control.activeFocus ? control.skin.colours.focus : control.skin.colours.line
        border.width: control.activeFocus ? control.skin.metrics.focusWidth : control.visualStyle === "plain" || control.visualStyle === "row" || control.visualStyle === "tab" ? 0 : 1
    }
}
