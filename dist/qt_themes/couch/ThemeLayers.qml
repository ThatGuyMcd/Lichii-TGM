// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
Item {
    id:layers
    property var skin
    property string view
    property var game
    function text(value){return String(value||"").split("{game.name}").join(game.name||"").split("{game.id}").join(game.id||"").split("{view}").join(view)}
    readonly property var background:((skin.views||{})[view]||{}).background||({x:0,y:0,width:1920,height:1080})
    Image{x:layers.background.x;y:layers.background.y;width:layers.background.width;height:layers.background.height;source:layers.skin.assets["background."+layers.view]||layers.skin.assets.background||"";fillMode:Image.PreserveAspectCrop}
    Repeater {
        model:layers.skin.layers||[]
        Item {
            required property var modelData
            x:modelData.x;y:modelData.y;width:modelData.width;height:modelData.height
            visible:!modelData.view||modelData.view===layers.view
            opacity:modelData.opacity===undefined?1:Math.max(0,Math.min(1,modelData.opacity))
            Rectangle{anchors.fill:parent;visible:modelData.type==="rectangle";color:modelData.colour||layers.skin.colours.surface;radius:modelData.radius||0}
            Image{anchors.fill:parent;visible:modelData.type==="image";source:layers.skin.assets[modelData.asset]||"";fillMode:modelData.fill==="crop"?Image.PreserveAspectCrop:Image.PreserveAspectFit}
            Text{anchors.fill:parent;visible:modelData.type==="text";text:layers.text(modelData.text);font.family:layers.skin.fontFamily;font.pixelSize:Math.max(16,Math.min(200,modelData.fontSize||26));color:modelData.colour||layers.skin.colours.text;wrapMode:Text.WordWrap;elide:Text.ElideRight}
        }
    }
}
