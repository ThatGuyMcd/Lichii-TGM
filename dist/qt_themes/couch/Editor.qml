// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic

FocusScope {
    id: editor
    objectName:"CouchSettingEditor"
    property var skin: Couch.theme.data
    property var sourceModel
    property var row: ({})
    property var temporary
    property string error: ""
    property int choiceIndex: 0
    property int footerIndex: 0
    property int dateColumn: 0
    readonly property var dateFields:row.dateMode==="date"?[0,1,2]:row.dateMode==="time"?(row.seconds?[3,4,5]:[3,4]):row.seconds?[0,1,2,3,4,5]:[0,1,2,3,4]
    property string zone: "choices"
    property var focusBefore
    property string keyboardTarget: "text"
    signal closed()
    visible: false
    anchors.fill: parent
    z: 50
    function open(model, data) {
        sourceModel = model; row = data; temporary = data.value; error = ""; zone = "choices"; footerIndex = 0
        focusBefore = editor.Window.window ? editor.Window.window.activeFocusItem : null
        choiceIndex = data.kind === "toggle" ? (data.value ? 0 : 1) : Math.max(0, Number(data.value) || 0)
        visible = true
        Qt.callLater(function() {
            if (row.kind === "select" || row.kind === "toggle") { choices.currentIndex = choiceIndex; choices.positionViewAtIndex(choiceIndex, ListView.Contain); if (choices.currentItem) choices.currentItem.forceActiveFocus() }
            else if (row.kind==="date") { zone="date"; dateColumn=0; dateWheels.itemAt(0).forceActiveFocus() }
            else if (row.kind==="number" || row.kind==="slider") { zone="number"; numberWheel.forceActiveFocus() }
            else ok.forceActiveFocus()
        })
    }
    function close() { keyboard.visible = false; visible = false; if (focusBefore) focusBefore.forceActiveFocus(); closed() }
    function commit() {
        if (sourceModel.stage(row.id, temporary)) close()
        else error = qsTr("Enter a value within this setting’s allowed range and format.")
    }
    function numberText(){const value=Number(temporary);return isFinite(value)?String(Number(value.toFixed(Math.min(12,Math.max(6,row.decimals||0))))):String(temporary)}
    function adjust(delta) { temporary = Math.min(row.maximum, Math.max(row.minimum, Number((Number(temporary) + delta * (row.step || 1)).toFixed(Math.min(12,Math.max(8,row.decimals||0)))))) }
    function dateParts() {
        const date=new Date(String(temporary))
        return [date.getFullYear(),date.getMonth()+1,date.getDate(),date.getHours(),date.getMinutes(),date.getSeconds()]
    }
    function shiftDate(delta) {
        const part=dateParts();const field=dateFields[dateColumn];part[field]+=delta
        const days=new Date(part[0],part[1],0).getDate()
        if(field===0 || field===1)part[2]=Math.min(part[2],days)
        const date=new Date(part[0],part[1]-1,part[2],part[3],part[4],part[5],0)
        const minimum=new Date(row.minimumDate), maximum=new Date(row.maximumDate)
        const clamped=new Date(Math.max(minimum.getTime(),Math.min(maximum.getTime(),date.getTime())))
        function two(n){return String(n).padStart(2,"0")}
        temporary=String(clamped.getFullYear()).padStart(4,"0")+"-"+two(clamped.getMonth()+1)+"-"+two(clamped.getDate())+"T"+two(clamped.getHours())+":"+two(clamped.getMinutes())+":"+two(clamped.getSeconds())+".000"
    }
    function textEntry(target) {
        keyboardTarget = target
        keyboard.open(String(temporary === undefined ? "" : temporary), target === "exact" ? qsTr("Exact value") : row.label, !!row.secret, row.maximumLength || 32767)
    }
    function controller(action) {
        if (keyboard.visible) { keyboard.controller(action); return }
        if (action === "back") { close(); return }
        if (action === "tab-prev" || action === "tab-next" || action === "group-prev" || action === "group-next") return
        if (row.kind === "select" || row.kind === "toggle") {
            if (action === "up" && zone === "choices") choiceIndex = Math.max(0, choiceIndex - 1)
            else if (action === "down" && zone === "choices" && choiceIndex < choices.count - 1) choiceIndex++
            else if (action === "down") { zone = "footer"; ok.forceActiveFocus(); return }
            else if (action === "up" && zone === "footer") zone = "choices"
            else if (action === "accept" && zone === "choices") { const value = row.kind === "toggle" ? choiceIndex === 0 : row.choices[choiceIndex].value; temporary = value; commit(); return }
            if (zone === "choices") { choices.currentIndex = choiceIndex; choices.positionViewAtIndex(choiceIndex,ListView.Contain); Qt.callLater(function(){if(choices.currentItem)choices.currentItem.forceActiveFocus()}); return }
        }
        if(row.kind==="date") {
            if(zone==="date") {
                if(action==="left"||action==="right"){dateColumn=Math.max(0,Math.min(dateFields.length-1,dateColumn+(action==="right"?1:-1)));dateWheels.itemAt(dateColumn).forceActiveFocus();return}
                if(action==="up"||action==="down"){shiftDate(action==="up"?1:-1);return}
                if(action==="accept"){zone="footer";ok.forceActiveFocus();return}
            } else if(action==="up"){zone="date";dateWheels.itemAt(dateColumn).forceActiveFocus();return}
        }
        if ((row.kind==="number" || row.kind==="slider") && zone==="footer" && action==="up") {zone="number";numberWheel.forceActiveFocus();return}
        if ((row.kind === "number" || row.kind === "slider") && zone !== "footer") {
            if(action==="accept"){zone="footer";ok.forceActiveFocus();return}
            if(action==="search"){textEntry("exact");return}
            if (action === "left" || action === "down") { adjust(-1); return }
            if (action === "right" || action === "up") { adjust(1); return }
        }
        const controls = [ok,cancel,resetButton,inheritButton,exactButton,textButton]
        const available = controls.filter(function(item){ return item.visible && item.enabled })
        let focused = available.findIndex(function(item){return item.activeFocus})
        if (action === "left" || action === "up") focused = Math.max(0,focused - 1)
        if (action === "right" || action === "down") focused = Math.min(available.length - 1,focused + 1)
        if (action === "accept" && focused >= 0) available[focused].clicked()
        else if (focused >= 0) {zone="footer";available[focused].forceActiveFocus()}
    }
    Rectangle { anchors.fill: parent; color: skin.colours.scrim }
    Rectangle {
        id: panel; width: keyboard.visible ? 1140 : 1140; height: keyboard.visible ? 750 : Math.min(900, Math.max(400, content.height + 64))
        anchors.centerIn: parent; color: skin.colours.background; radius: 16
        Column {
            id: content; visible: !keyboard.visible; x: 32; y: 32; width: parent.width - 64; spacing: 20
            Text { width: parent.width; text: editor.row.label || ""; font.family: skin.fontFamily; font.pixelSize: 32; font.weight: Font.Medium; wrapMode: Text.WordWrap; color: skin.colours.text }
            Text { width: parent.width; text: editor.row.help || ""; visible: text.length > 0; maximumLineCount: 4; elide: Text.ElideRight; wrapMode: Text.WordWrap; font.family: skin.fontFamily; font.pixelSize: 22; color: skin.colours.muted }
            ListView {
                id: choices; width: parent.width; height: Math.min(count * 72, 430); clip: true
                visible: editor.row.kind === "select" || editor.row.kind === "toggle"
                model: editor.row.kind === "toggle" ? [{label:qsTr("On"),value:true},{label:qsTr("Off"),value:false}] : editor.row.choices || []
                boundsBehavior: Flickable.StopAtBounds; spacing: 8; ScrollBar.vertical: ScrollBar {}
                delegate: CButton {
                    required property int index; required property var modelData
                    width: choices.width - 12; height: 64; text: modelData.label; skin: editor.skin
                    enabled: modelData.enabled === undefined || modelData.enabled
                    selected: String(modelData.value) === String(editor.temporary)
                    onActiveFocusChanged: if(activeFocus){editor.choiceIndex=index;editor.zone="choices"}
                    onClicked: {editor.temporary=modelData.value;editor.commit()}
                }
            }
            Column {
                width: parent.width; spacing: 14; visible: editor.row.kind === "slider" || editor.row.kind === "number"
                CButton {id:numberWheel;objectName:"CouchNumberWheel";width:parent.width;height:86;text:editor.numberText()+(editor.row.suffix||"");font.pixelSize:56;skin:editor.skin;onClicked:editor.textEntry("exact");onActiveFocusChanged:if(activeFocus)editor.zone="number"}
                Text { visible:false;width: parent.width; horizontalAlignment: Text.AlignHCenter; text: String(editor.temporary) + (editor.row.suffix || ""); color: skin.colours.focus; font.pixelSize: 56; font.family: skin.fontFamily; elide: Text.ElideRight }
                Slider { id:numberSlider; width: parent.width; height: 60; visible: editor.row.kind === "slider"; from: editor.row.minimum || 0; to: editor.row.maximum===undefined?100:editor.row.maximum; stepSize: editor.row.step || 1; value: Number(editor.temporary); onMoved: editor.temporary=value; onActiveFocusChanged: if(activeFocus)editor.zone="number"
                    background:Rectangle{x:numberSlider.leftPadding;y:numberSlider.topPadding+numberSlider.availableHeight/2-3;width:numberSlider.availableWidth;height:6;radius:3;color:skin.colours.line;Rectangle{width:numberSlider.visualPosition*parent.width;height:parent.height;radius:3;color:skin.colours.focus}}
                    handle:Rectangle{x:numberSlider.leftPadding+numberSlider.visualPosition*(numberSlider.availableWidth-width);y:numberSlider.topPadding+numberSlider.availableHeight/2-height/2;width:28;height:28;radius:14;color:skin.colours.focus;border.width:2;border.color:skin.colours.text}
                }
                Row {
                    spacing: 12; anchors.horizontalCenter: parent.horizontalCenter
                    CButton {text:"−10";width:110;skin:editor.skin;onClicked:editor.adjust(-10);onActiveFocusChanged:if(activeFocus)editor.zone="number"}
                    CButton {text:"−";width:90;skin:editor.skin;onClicked:editor.adjust(-1);onActiveFocusChanged:if(activeFocus)editor.zone="number"}
                    CButton {id:exactButton;text:qsTr("Exact value");width:200;skin:editor.skin;onClicked:editor.textEntry("exact")}
                    CButton {text:"＋";width:90;skin:editor.skin;onClicked:editor.adjust(1);onActiveFocusChanged:if(activeFocus)editor.zone="number"}
                    CButton {text:"＋10";width:110;skin:editor.skin;onClicked:editor.adjust(10);onActiveFocusChanged:if(activeFocus)editor.zone="number"}
                }
                Text {width:parent.width;horizontalAlignment:Text.AlignHCenter;text:String(editor.row.minimum)+" — "+String(editor.row.maximum);color:skin.colours.muted;font.pixelSize:20;font.family:skin.fontFamily}
            }
            Row {
                width:parent.width;height:230;spacing:12;visible:editor.row.kind==="date"
                Repeater {
                    id:dateWheels;model:editor.dateFields.map(function(field){return [qsTr("Year"),qsTr("Month"),qsTr("Day"),qsTr("Hour"),qsTr("Minute"),qsTr("Second")][field]})
                    CButton {
                        required property int index;required property string modelData
                        width:(1076-(dateWheels.count-1)*12)/Math.max(1,dateWheels.count);height:220;skin:editor.skin
                        onActiveFocusChanged:if(activeFocus){editor.zone="date";editor.dateColumn=index}
                        onClicked:{editor.dateColumn=index;editor.shiftDate(1)}
                        contentItem:Column {
                            spacing:12
                            Text{width:parent.width;text:modelData;color:skin.colours.muted;font.family:skin.fontFamily;font.pixelSize:22;horizontalAlignment:Text.AlignHCenter}
                            Text{width:parent.width;text:"▲";color:skin.colours.muted;font.pixelSize:22;horizontalAlignment:Text.AlignHCenter}
                            Text{width:parent.width;text:editor.row.kind==="date"?String(editor.dateParts()[editor.dateFields[index]]).padStart(editor.dateFields[index]===0?4:2,"0"):"";color:skin.colours.focus;font.family:skin.fontFamily;font.pixelSize:48;horizontalAlignment:Text.AlignHCenter}
                            Text{width:parent.width;text:"▼";color:skin.colours.muted;font.pixelSize:22;horizontalAlignment:Text.AlignHCenter}
                        }
                        WheelHandler{onWheel:function(event){editor.dateColumn=parent.index;editor.shiftDate(event.angleDelta.y>0?1:-1)}}
                    }
                }
            }
            Column {
                width: parent.width; spacing: 16
                visible: !["number","slider","select","toggle","date"].includes(editor.row.kind)
                Text {width:parent.width;text:editor.row.secret && String(editor.temporary).length ? "••••••••" : String(editor.temporary === undefined ? "" : editor.temporary);color:skin.colours.text;font.pixelSize:28;wrapMode:Text.WrapAnywhere;maximumLineCount:5;elide:Text.ElideRight;font.family:skin.fontFamily}
                CButton {id:textButton;text:editor.row.kind === "date" ? qsTr("Edit date and time") : qsTr("Edit text");visible:editor.row.kind !== "info";skin:editor.skin;onClicked:editor.textEntry("text")}
            }
            Text { width:parent.width;visible:text.length>0;text:editor.error;color:skin.colours.accent;font.pixelSize:22;wrapMode:Text.WordWrap;Accessible.role:Accessible.AlertMessage }
            Rectangle {width:parent.width;height:1;color:skin.colours.line}
            Row {
                spacing: 12
                CButton {id:ok;objectName:"CouchEditorOK";text:qsTr("OK");width:140;visualStyle:"primary";skin:editor.skin;onClicked:editor.row.kind==="info"?editor.close():editor.commit();onActiveFocusChanged:if(activeFocus)editor.zone="footer"}
                CButton {id:cancel;text:qsTr("Cancel");width:140;skin:editor.skin;onClicked:editor.close();onActiveFocusChanged:if(activeFocus)editor.zone="footer"}
                CButton {id:resetButton;text:qsTr("Reset");width:140;visible:editor.row.kind!=="info"&&!!editor.row.canReset;skin:editor.skin;onClicked:{if(editor.sourceModel.reset(editor.row.id,false)){editor.row=editor.sourceModel.describe(editor.row.id);editor.temporary=editor.row.value}} onActiveFocusChanged:if(activeFocus)editor.zone="footer"}
                CButton {id:inheritButton;text:qsTr("Use global");width:190;visible:!!editor.row.canInherit;skin:editor.skin;onClicked:{editor.sourceModel.reset(editor.row.id,true);editor.close()} onActiveFocusChanged:if(activeFocus)editor.zone="footer"}
            }
        }
        Keyboard {
            id:keyboard;anchors.centerIn:parent;skin:editor.skin
            onAccepted:function(value){
                if(editor.keyboardTarget==="exact") {const number=Number(value);if(value.trim().length===0 || !isFinite(number) || number<editor.row.minimum || number>editor.row.maximum){editor.error=qsTr("The exact value is outside the allowed range.");return}editor.temporary=number}
                else editor.temporary=value
                visible=false;editor.error="";ok.forceActiveFocus()
            }
            onCancelled:{visible=false;ok.forceActiveFocus()}
        }
    }
}
