// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
Canvas {
    property string name: "search"
    property color ink: "white"
    property var skin:Couch.theme.data
    readonly property string asset:skin.assets["icon."+name]||""
    onAssetChanged:requestPaint()
    Image{anchors.fill:parent;source:parent.asset;fillMode:Image.PreserveAspectFit}
    implicitWidth: 36; implicitHeight: 36
    onNameChanged: requestPaint()
    onInkChanged: requestPaint()
    onPaint: {
        const c=getContext("2d");c.reset();if(asset)return;c.scale(width/36,height/36);
        c.strokeStyle=ink;c.fillStyle=ink;c.lineWidth=2.2;c.lineCap="round";c.lineJoin="round";
        function line(x,y,a,b){c.moveTo(x,y);c.lineTo(a,b)}
        c.beginPath()
        if(name==="search"){c.arc(15,15,10,0,Math.PI*2);c.moveTo(23,23);c.lineTo(32,32)}
        else if(name==="profile"){c.arc(18,11,6,0,Math.PI*2);c.moveTo(6,32);c.bezierCurveTo(6,16,30,16,30,32)}
        else if(name==="desktop"){c.rect(3,4,30,22);line(18,26,18,32);line(10,32,26,32)}
        else if(name==="collections"){c.rect(3,3,12,12);c.rect(21,3,12,12);c.rect(3,21,12,12);c.rect(21,21,12,12)}
        else if(name==="themes"){c.moveTo(18,2);c.lineTo(34,18);c.lineTo(18,34);c.lineTo(2,18);c.closePath();line(18,2,18,34)}
        else if(name==="controllers"){c.moveTo(10,10);c.bezierCurveTo(3,10,2,27,6,28);c.bezierCurveTo(10,30,12,23,14,23);c.lineTo(22,23);c.bezierCurveTo(25,23,26,30,31,28);c.bezierCurveTo(35,26,33,10,26,10);c.closePath();line(8,17,16,17);line(12,13,12,21);c.moveTo(26,15);c.arc(25,15,1,0,Math.PI*2);c.moveTo(29,19);c.arc(28,19,1,0,Math.PI*2)}
        else if(name==="settings"){c.arc(18,18,10,0,Math.PI*2);c.moveTo(22,18);c.arc(18,18,4,0,Math.PI*2);for(let i=0;i<8;i++){const a=i*Math.PI/4;line(18+11*Math.cos(a),18+11*Math.sin(a),18+15*Math.cos(a),18+15*Math.sin(a))}}
        c.stroke()
    }
}
