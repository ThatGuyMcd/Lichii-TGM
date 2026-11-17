// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic

FocusScope {
    id: menu
    property var skin: Couch.theme.data
    property string title: ""
    property var choices: []
    property int selected: 0
    property bool selecting:false
    property var before
    signal chosen(var value)
    signal closed()
    visible: false; z: 70
    function open(){selected=Math.max(0,choices.findIndex(function(item){return item.enabled!==false}));visible=true;focusChoice()}
    function close(){visible=false;closed()}
    function choose(value){selecting=true;close();chosen(value);selecting=false}
    function focusChoice(){list.currentIndex=selected;list.positionViewAtIndex(selected,ListView.Contain);Qt.callLater(function(){if(list.currentItem)list.currentItem.forceActiveFocus()})}
    function move(delta){let next=selected+delta;while(next>=0&&next<choices.length&&choices[next].enabled===false)next+=delta;if(next>=0&&next<choices.length)selected=next;focusChoice()}
    function controller(action){
        if(action==="back"){close();return}
        if(action==="up"){move(-1)}
        else if(action==="down"){move(1)}
        else if(action==="accept"&&choices.length&&choices[selected].enabled!==false){const value=choices[selected].value;choose(value)}
    }
    Rectangle{anchors.fill:parent;color:skin.colours.scrim}
    Rectangle{
        anchors.centerIn:parent;width:1140;height:Math.max(240,Math.min(800,150+menu.choices.length*78));radius:16;color:skin.colours.background
        Text{x:32;y:26;width:1076;text:menu.title;color:skin.colours.text;font.family:skin.fontFamily;font.pixelSize:32;font.weight:Font.Medium;elide:Text.ElideRight}
        ListView{
            id:list;x:32;y:90;width:1076;height:parent.height-150;model:menu.choices;clip:true;spacing:10;boundsBehavior:Flickable.StopAtBounds;ScrollBar.vertical:ScrollBar{}
            delegate:CButton{
                required property int index;required property var modelData
                width:list.width-12;height:68;text:modelData.label;enabled:modelData.enabled!==false;skin:menu.skin
                onActiveFocusChanged:if(activeFocus)menu.selected=index
                onClicked:{const value=modelData.value;menu.choose(value)}
            }
        }
        CButton{x:32;anchors.bottom:parent.bottom;anchors.bottomMargin:12;width:140;height:42;text:qsTr("Back");font.pixelSize:20;skin:menu.skin;onClicked:menu.close()}
    }
}
