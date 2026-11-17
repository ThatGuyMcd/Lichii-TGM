// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic

Rectangle {
    id:root;color:skin.colours.background
    readonly property var skin:Native.theme.data
    readonly property var payload:Native.state
    property string zone:"rows"
    property int current:0
    property int buttonIndex:0
    property int toolIndex:0
    property string keyboardAction:"path"
    function textInput(action,text,title){keyboardAction=action;keyboard.open(text,title,false,32767)}
    function focusFooter(){zone="footer";if(footerButtons.count)footerButtons.itemAt(buttonIndex).forceActiveFocus()}
    function controller(action){
        if(sessionModal.visible){sessionModal.controller(action);return}
        if(choices.visible){choices.controller(action);return}
        if(treeBrowser.visible){treeBrowser.controller(action);return}
        if(editor.visible){editor.controller(action);return}
        if(keyboard.visible){keyboard.controller(action);return}
        if(action==="back"){Native.action("back");return}
        const delta=action==="up"||action==="left"?-1:1
        if(zone==="message") {
            if(action==="up"||action==="down"){message.contentY=Math.max(0,Math.min(message.contentHeight-message.height,message.contentY+delta*80));return}
            if(action==="left"||action==="right"){focusFooter();return}
        }
        if(zone==="tools"){
            const tools=[upButton,homeButton,pathButton,drivesButton,filterButton,nameButton,multiButton].filter(function(item){return item.visible})
            if(action==="left"||action==="right"){toolIndex=Math.max(0,Math.min(tools.length-1,toolIndex+delta));tools[toolIndex].forceActiveFocus()}
            else if(action==="down"){zone="rows";focusRow()}
            else if(action==="accept")tools[toolIndex].clicked()
            return
        }
        if(zone==="footer"){
            if(action==="up"){if(payload.kind==="message")zone="message";else{zone="rows";focusRow()}return}
            if(action==="left"||action==="right"){buttonIndex=Math.max(0,Math.min(footerButtons.count-1,buttonIndex+delta));footerButtons.itemAt(buttonIndex).forceActiveFocus()}
            else if(action==="accept")footerButtons.itemAt(buttonIndex).clicked()
            return
        }
        const list=payload.kind==="files"?files:rows
        if(action==="up"||action==="down"){
            if(action==="up"&&current===0&&payload.kind==="files"){zone="tools";toolIndex=0;upButton.forceActiveFocus();return}
            if(action==="down"&&current>=list.count-1){zone="footer";footerButtons.itemAt(0).forceActiveFocus();return}
            current=Math.max(0,Math.min(list.count-1,current+delta));focusRow()
        }else if(action==="accept"){if(list.currentItem)list.currentItem.clicked()}
        else if(action==="left"&&payload.kind==="files"){Native.action("up");current=0;focusRow()}
    }
    function focusRow(){const list=payload.kind==="files"?files:rows;list.currentIndex=current;list.positionViewAtIndex(current,ListView.Contain);Qt.callLater(function(){if(list.currentItem)list.currentItem.forceActiveFocus();else if(footerButtons.count){root.zone="footer";footerButtons.itemAt(0).forceActiveFocus()}})}
    Item {
        width:1920;height:1080;anchors.centerIn:parent;scale:Math.min(root.width/1920,root.height/1080);transformOrigin:Item.Center
        Rectangle{anchors.fill:parent;color:skin.colours.scrim}
        Rectangle {
            x:320;y:100;width:1280;height:880;radius:16;color:skin.colours.background
            Text{x:32;y:26;width:1216;text:payload.title||qsTr("Settings");color:skin.colours.text;font.pixelSize:32;font.weight:Font.Medium;font.family:skin.fontFamily;wrapMode:Text.WordWrap;maximumLineCount:2;elide:Text.ElideRight}
            Flickable{id:message;x:32;y:100;width:1216;height:600;visible:payload.kind==="message";contentWidth:width;contentHeight:messageText.height;clip:true;boundsBehavior:Flickable.StopAtBounds;ScrollBar.vertical:ScrollBar{} Text{id:messageText;width:message.width-16;text:(payload.message||"")+(payload.detail?"\n\n"+payload.detail:"");color:skin.colours.text;font.family:skin.fontFamily;font.pixelSize:26;wrapMode:Text.WordWrap}}
            Row {
                visible:payload.kind==="files";x:32;y:90;spacing:12
                CButton{id:upButton;width:100;height:56;text:qsTr("Up");skin:root.skin;onClicked:Native.action("up")}
                CButton{id:homeButton;width:120;height:56;text:qsTr("Home");skin:root.skin;onClicked:Native.action("home")}
                CButton{id:pathButton;width:700;height:56;text:payload.path||"";skin:root.skin;onClicked:root.textInput("path",payload.path||"",qsTr("Enter path"))}
                CButton{id:drivesButton;width:130;height:56;text:qsTr("Drives");skin:root.skin;onClicked:{choices.title=qsTr("Drives");choices.choices=(payload.drives||[]).map(function(path,index){return {label:path,value:index}});choices.action="drive";choices.open()}}
                CButton{id:filterButton;width:130;height:56;text:qsTr("Filter");skin:root.skin;onClicked:{choices.title=qsTr("File type");choices.choices=(payload.filters||[]).map(function(filter){return {label:filter,value:filter}});choices.action="filter";choices.open()}}
            }
            Row{x:32;y:154;spacing:12;visible:payload.kind==="files"
                CButton{id:nameButton;width:600;height:52;text:payload.save?qsTr("File name: %1").arg(payload.selectedFile||""):payload.directory?qsTr("Choose this folder"):qsTr("Enter file name");skin:root.skin;onClicked:payload.directory?Native.action("choose-directory"):root.textInput("path",payload.selectedFile||"",qsTr("File name"))}
                CButton{id:multiButton;visible:!!payload.multiple;width:480;height:52;text:qsTr("Open selected files (%1)").arg((payload.selected||[]).length);skin:root.skin;onClicked:Native.action("accept-files")}
            }
            ListView {
                id:files;x:32;y:222;width:1216;height:500;visible:payload.kind==="files";model:payload.files||[];clip:true;spacing:8;boundsBehavior:Flickable.StopAtBounds;ScrollBar.vertical:ScrollBar{}
                delegate:CButton{
                    required property int index;required property var modelData
                    width:files.width-12;height:64;text:(modelData.selected?"✓  ":modelData.directory?"▸  ":"")+modelData.label;skin:root.skin
                    onActiveFocusChanged:if(activeFocus){root.current=index;root.zone="rows"}
                    onClicked:{Native.action("file",modelData.id);root.current=0;root.focusRow()}
                }
            }
            ListView {
                id:rows;x:32;y:110;width:1216;height:620;visible:payload.kind==="settings";model:Native.settings.rows;clip:true;spacing:4;boundsBehavior:Flickable.StopAtBounds;ScrollBar.vertical:ScrollBar{}
                delegate:CButton{
                    id:row;required property int index;required property var modelData
                    width:rows.width-12;height:88;opacity:modelData.enabled?1:0.42;skin:root.skin;visualStyle:"row"
                    onActiveFocusChanged:if(activeFocus){root.current=index;root.zone="rows"}
                    onClicked:{if(!modelData.enabled)return;if(modelData.kind==="action"||modelData.kind==="binding")Native.settings.activate(modelData.id);else if(modelData.kind==="tree")treeBrowser.open(Native.settings,modelData.id,modelData.label);else editor.open(Native.settings,modelData)}
                    contentItem:Item{Text{x:0;y:6;width:790;text:row.modelData.label;color:skin.colours.text;font.pixelSize:26;font.family:skin.fontFamily;wrapMode:Text.WordWrap;maximumLineCount:2;elide:Text.ElideRight}Text{anchors.right:parent.right;anchors.verticalCenter:parent.verticalCenter;width:360;text:row.modelData.display;color:skin.colours.focus;font.pixelSize:24;font.family:skin.fontFamily;wrapMode:Text.WrapAnywhere;maximumLineCount:2;elide:Text.ElideRight;horizontalAlignment:Text.AlignRight}}
                    Rectangle{anchors.bottom:parent.bottom;width:parent.width;height:1;color:skin.colours.line}
                }
            }
            Row {
                x:32;y:770;spacing:12
                Repeater{
                    id:footerButtons;model:payload.buttons||[]
                    CButton{
                        required property int index;required property var modelData
                        width:Math.min(300,Math.max(140,implicitWidth));height:64;text:modelData.label;enabled:modelData.enabled;skin:root.skin;visualStyle:index===0?"primary":"normal"
                        onActiveFocusChanged:if(activeFocus){root.buttonIndex=index;root.zone="footer"}
                        onClicked:Native.action("button",modelData.id)
                    }
                }
            }
        }
        Rectangle{anchors.fill:parent;visible:!!Couch.state.mapping;color:skin.colours.scrim;z:99;MouseArea{anchors.fill:parent}}
        Rectangle{visible:!!Couch.state.mapping;anchors.centerIn:parent;width:940;height:260;radius:16;color:skin.colours.surface;z:100;Column{anchors.centerIn:parent;spacing:24;Text{width:840;horizontalAlignment:Text.AlignHCenter;text:qsTr("Press a button or move a stick");font.pixelSize:32;color:skin.colours.text;font.family:skin.fontFamily}Text{width:840;horizontalAlignment:Text.AlignHCenter;text:qsTr("Wait for the timeout to cancel, or choose Cancel.");font.pixelSize:24;color:skin.colours.muted;font.family:skin.fontFamily}CButton{text:qsTr("Cancel");anchors.horizontalCenter:parent.horizontalCenter;skin:root.skin;onClicked:Couch.action("cancel-mapping")}}}
        ShellModal{id:sessionModal;anchors.fill:parent;skin:root.skin;onClosed:root.focusRow()}
        TreeBrowser{id:treeBrowser;anchors.fill:parent;skin:root.skin;onClosed:root.focusRow()}
        ChoiceMenu{id:choices;anchors.fill:parent;skin:root.skin;property string action:"";onChosen:function(value){Native.action(action,value);root.current=0;root.focusRow()}onClosed:root.focusRow()}
        Editor{id:editor;skin:root.skin;onClosed:root.focusRow()}
        Rectangle{anchors.fill:parent;visible:keyboard.visible;z:80;color:skin.colours.scrim;Rectangle{anchors.centerIn:parent;width:1140;height:750;radius:16;color:skin.colours.background}}
        Keyboard{id:keyboard;anchors.centerIn:parent;skin:root.skin;z:81;onAccepted:function(value){Native.action(root.keyboardAction,value);visible=false;root.focusRow()} onCancelled:{visible=false;root.focusRow()}}
    }
    Connections{target:Native;function onChanged(){if(!treeBrowser.visible&&!choices.visible&&!editor.visible&&!keyboard.visible&&root.zone==="rows")root.focusRow()}}
    Component.onCompleted:Qt.callLater(function(){
        const buttons=payload.buttons||[];root.buttonIndex=Math.max(0,buttons.findIndex(function(item){return item.default}))
        if(payload.kind==="message"){root.focusFooter()}else root.focusRow()
    })
}
