// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic

FocusScope {
    id: modal
    property var skin: Couch.theme.data
    readonly property var payload: Couch.state.modal || ({})
    readonly property string kind: payload.kind || ""
    readonly property var changes: Couch.settings.changes.concat(Couch.state.externalChanges?[{label:qsTr("Native dialog changes"),display:qsTr("Includes changes made in a native settings editor."),inherited:false}]:[])
    property int index: 0
    property string zone: "body"
    property int treeColumn: 0
    signal closed()
    visible: kind.length>0;z:60
    function controller(action){
        if(kind==="choices"){choiceMenu.controller(action);return}
        if(kind==="keyboard"){keyboard.controller(action);return}
        if(action==="back"){Couch.closeModal();return}
        if(kind==="tree"&&(action==="left"||action==="right")){treeColumn=Math.max(0,Math.min(((payload.rows||[])[tree.currentIndex]||{}).columns?.length-1||0,treeColumn+(action==="right"?1:-1)));return}
        if(kind==="review" && (action==="up"||action==="down") && zone==="body"){review.contentY=Math.max(0,Math.min(review.contentHeight-review.height,review.contentY+(action==="down"?100:-100)));if(action==="down"&&review.contentY>=review.contentHeight-review.height){zone="footer";save.forceActiveFocus()}return}
        if(kind==="review" && action==="up"&&zone==="footer"){zone="body";return}
        const controls=kind==="unsaved"?[leaveSave,leaveDiscard,leaveStay]:kind==="review"?[save,discard,share,back]:[back]
        if((kind==="help"||kind==="tree")&&(action==="up"||action==="down")&&zone==="body"){
            if(kind==="help"){helpText.contentY=Math.max(0,Math.min(helpText.contentHeight-helpText.height,helpText.contentY+(action==="down"?80:-80)));if(action==="down"&&helpText.contentY>=helpText.contentHeight-helpText.height-1){zone="footer";back.forceActiveFocus()}}
            else {tree.currentIndex=Math.max(0,Math.min(tree.count-1,tree.currentIndex+(action==="down"?1:-1)));tree.positionViewAtIndex(tree.currentIndex,ListView.Contain);Qt.callLater(function(){if(tree.currentItem)tree.currentItem.forceActiveFocus()})}
            return
        }
        if(action==="up"||action==="left")index=Math.max(0,index-1)
        else if(action==="down"||action==="right")index=Math.min(controls.length-1,index+1)
        else if(action==="accept"){const item=modal.Window.window?modal.Window.window.activeFocusItem:null;if(item&&typeof item.clicked==="function")item.clicked();return}
        controls[index].forceActiveFocus()
    }
    onKindChanged:{
        if(!visible){closed();return}index=0;zone="body"
        Qt.callLater(function(){
            if(kind==="keyboard")keyboard.open(payload.text||"",payload.title||"",!!payload.secret,payload.maximumLength||32767)
            else if(kind==="unsaved")leaveSave.forceActiveFocus()
            else if(kind==="review")save.forceActiveFocus()
            else if(kind==="choices")choiceMenu.open()
            else if(kind==="tree") {zone="body";tree.currentIndex=0;tree.positionViewAtBeginning();Qt.callLater(function(){if(tree.currentItem)tree.currentItem.forceActiveFocus()})}
            else if(kind==="review") {zone="body"}
            else back.forceActiveFocus()
        })
    }
    Rectangle{anchors.fill:parent;color:skin.colours.scrim}
    Rectangle {
        anchors.centerIn:parent;width:1140;height:kind==="keyboard"?750:kind==="input-test"?850:kind==="unsaved"?390:760
        color:skin.colours.background;radius:16
        Text{x:32;y:30;width:1076;visible:kind!=="keyboard";text:kind==="help"?(payload.row||{}).label||qsTr("Help"):payload.title||"";color:skin.colours.text;font.family:skin.fontFamily;font.pixelSize:32;font.weight:Font.Medium;wrapMode:Text.WordWrap;maximumLineCount:2;elide:Text.ElideRight}
        Column {
            x:32;y:100;width:1076;spacing:12;visible:kind==="unsaved"
            Text{width:parent.width;text:qsTr("You have %1 unsaved changes. Save or discard them before leaving.").arg(Couch.state.changeCount);color:skin.colours.text;font.family:skin.fontFamily;font.pixelSize:24;wrapMode:Text.WordWrap}
            CButton{id:leaveSave;width:1076;height:60;text:qsTr("Save and leave");visualStyle:"primary";skin:modal.skin;onActiveFocusChanged:if(activeFocus)modal.index=0;onClicked:Couch.action("leave-save")}
            CButton{id:leaveDiscard;width:1076;height:60;text:qsTr("Discard and leave");skin:modal.skin;onActiveFocusChanged:if(activeFocus)modal.index=1;onClicked:Couch.action("leave-discard")}
            CButton{id:leaveStay;width:1076;height:60;text:qsTr("Keep editing");skin:modal.skin;onActiveFocusChanged:if(activeFocus)modal.index=2;onClicked:Couch.closeModal()}
        }
        ListView {
            id:review;x:32;y:100;width:1076;height:490;visible:kind==="review";model:modal.changes;clip:true;spacing:12;ScrollBar.vertical:ScrollBar{}
            delegate:Rectangle{
                required property var modelData
                width:review.width-12;height:90;color:skin.colours.surface
                Text{x:16;y:10;width:parent.width-32;text:modelData.label;color:skin.colours.text;font.family:skin.fontFamily;font.pixelSize:24;elide:Text.ElideRight}
                Text{x:16;y:46;width:parent.width-32;text:modelData.inherited?qsTr("Use global"):modelData.display;color:skin.colours.focus;font.family:skin.fontFamily;font.pixelSize:22;elide:Text.ElideRight}
            }
        }
        Text{x:32;y:130;visible:kind==="review"&&!changes.length;text:qsTr("No unsaved changes.");color:skin.colours.text;font.pixelSize:24;font.family:skin.fontFamily}
        Flickable {
            id:helpText;x:32;y:110;width:1076;height:500;visible:kind==="help";contentWidth:width;contentHeight:helpContent.height;clip:true;boundsBehavior:Flickable.StopAtBounds;ScrollBar.vertical:ScrollBar{}
            Text{id:helpContent;width:helpText.width-16;text:(payload.row||{}).help||qsTr("Select a value and save when you are ready. Changes remain in the review until saved.");color:skin.colours.text;font.family:skin.fontFamily;font.pixelSize:24;wrapMode:Text.WordWrap}
        }
        ListView {
            id:tree;x:32;y:110;width:1076;height:500;visible:kind==="tree";model:payload.rows||[];clip:true;spacing:10;ScrollBar.vertical:ScrollBar{}
            delegate:CButton{
                required property int index;required property var modelData
                width:tree.width-12;height:72;text:(modelData.checked===2?"✓ ":"")+modelData.columns.map(function(value,col){return col===modal.treeColumn?"[ "+value+" ]":value}).join(" · ")+(modelData.children?" →":"");skin:modal.skin
                onActiveFocusChanged:if(activeFocus)modal.zone="body"
                onClicked:Couch.treeAction(modelData.index,modal.treeColumn,!modelData.children&&!modelData.checkable)
            }
        }
        Column {
            x:32;y:100;width:1076;spacing:16;visible:kind==="input-test"
            Image{width:1076;height:560;source:Couch.state.controllerImage||"";fillMode:Image.PreserveAspectFit;cache:false}
            Text{width:parent.width;text:Couch.state.controllerName||"";color:skin.colours.text;font.pixelSize:24;font.family:skin.fontFamily;horizontalAlignment:Text.AlignHCenter}
            Text{width:parent.width;text:qsTr("Navigation is paused. Hold Back for one second to finish.");color:skin.colours.muted;font.pixelSize:22;font.family:skin.fontFamily;horizontalAlignment:Text.AlignHCenter}
        }
        Row {
            x:32;anchors.bottom:parent.bottom;anchors.bottomMargin:30;spacing:12;visible:kind!=="keyboard"&&kind!=="unsaved"
            CButton{id:save;visible:kind==="review";width:240;height:60;text:qsTr("Save changes");visualStyle:"primary";skin:modal.skin;onActiveFocusChanged:if(activeFocus){modal.zone="footer";modal.index=0} onClicked:Couch.action("review-save")}
            CButton{id:discard;visible:kind==="review";width:200;height:60;text:qsTr("Discard changes");skin:modal.skin;onActiveFocusChanged:if(activeFocus){modal.zone="footer";modal.index=1} onClicked:Couch.action("discard")}
            CButton{id:share;visible:kind==="review";width:200;height:60;text:qsTr("Share settings");skin:modal.skin;onActiveFocusChanged:if(activeFocus){modal.zone="footer";modal.index=2} onClicked:Couch.action("share")}
            CButton{id:back;width:140;height:60;text:kind==="input-test"?qsTr("Finish"):qsTr("Back");skin:modal.skin;onActiveFocusChanged:if(activeFocus){modal.zone="footer";modal.index=kind==="review"?3:0} onClicked:Couch.closeModal()}
        }
        ChoiceMenu{id:choiceMenu;anchors.fill:parent;skin:modal.skin;title:payload.title||"";choices:payload.choices||[];onChosen:function(value){Couch.action("modal-choice",value)}onClosed:if(modal.kind==="choices"&&!choiceMenu.selecting)Couch.closeModal()}
        Keyboard{id:keyboard;anchors.centerIn:parent;skin:modal.skin;visible:kind==="keyboard";onAccepted:function(text){Couch.completeText(modal.payload.target||"",text)} onCancelled:Couch.closeModal()}
    }
}
