// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
FocusScope {
    id:root
    property var skin:Couch.theme.data
    property var sourceModel:null
    property int controlId:-1
    property string title:""
    property var rows:[]
    property int current:0
    property int column:0
    readonly property var prompt:({accept:Couch.state.prompts==="playstation"?"×":"A",back:Couch.state.prompts==="playstation"?"○":"B",more:Couch.state.prompts==="playstation"?"△":Couch.state.prompts==="nintendo"?"X":"Y"})
    signal closed()
    visible:false;z:85
    function open(model,id,label){sourceModel=model;controlId=id;title=label;column=0;current=0;visible=true;refresh()}
    function refresh(){rows=sourceModel.treeRows(controlId);current=Math.min(current,Math.max(0,rows.length-1));list.currentIndex=current;list.positionViewAtIndex(current,ListView.Contain);Qt.callLater(function(){if(list.currentItem)list.currentItem.forceActiveFocus();else done.forceActiveFocus()})}
    function close(){visible=false;closed()}
    function activate(index){
        const row=rows[index];if(!row)return
        sourceModel.treeAction(controlId,row.index,row.children?0:column,!row.children&&!row.checkable)
        if(row.children)current=0
        refresh()
    }
    function controller(action){
        if(action==="more"){const row=rows[current];if(row)sourceModel.treeContext(controlId,row.index,column);return}
        if(action==="back"){if(rows.length&&rows[0].index===-1){sourceModel.treeAction(controlId,-1,0,false);current=0;refresh()}else close();return}
        if(action==="up"||action==="down"){current=Math.max(0,Math.min(rows.length-1,current+(action==="down"?1:-1)));refresh()}
        else if(action==="left"||action==="right"){column=Math.max(0,Math.min((rows[current]?.columns?.length||1)-1,column+(action==="right"?1:-1)))}
        else if(action==="accept")activate(current)
        else if(action==="menu")close()
    }
    Rectangle{anchors.fill:parent;color:skin.colours.scrim}
    Rectangle{
        anchors.centerIn:parent;width:1280;height:850;radius:16;color:skin.colours.background
        Text{x:32;y:26;width:1216;text:root.title;color:skin.colours.text;font.pixelSize:32;font.family:skin.fontFamily;elide:Text.ElideRight}
        Text{x:32;y:76;width:1216;text:qsTr("↑ ↓  Item    ← →  Column    %1  Select    %2  Parent / back    %3  More").arg(prompt.accept).arg(prompt.back).arg(prompt.more);color:skin.colours.muted;font.pixelSize:22;font.family:skin.fontFamily}
        ListView{
            id:list;x:32;y:126;width:1216;height:600;model:root.rows;clip:true;spacing:8;boundsBehavior:Flickable.StopAtBounds;ScrollBar.vertical:ScrollBar{}
            delegate:CButton{
                required property int index;required property var modelData
                width:list.width-12;height:76;skin:root.skin
                text:(modelData.checked===2?"✓  ":"")+modelData.columns.map(function(value,col){return col===root.column?"[ "+value+" ]":value}).join("  ·  ")+(modelData.children?"  →":"")
                onActiveFocusChanged:if(activeFocus)root.current=index
                onClicked:root.activate(index)
            }
        }
        CButton{id:done;x:32;y:758;width:200;height:60;text:qsTr("Done");skin:root.skin;onClicked:root.close()}
    }
    Connections{target:root.sourceModel;ignoreUnknownSignals:true;function onChanged(){if(root.visible)root.refresh()}}
}
