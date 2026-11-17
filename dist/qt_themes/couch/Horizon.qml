// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic

Rectangle {
    id: root
    objectName: "HorizonCouchRoot"
    color: skin.colours.background
    readonly property var skin: Couch.theme.data
    readonly property var ui: Couch.state
    readonly property string tab: ui.tab || "home"
    readonly property string group: ui.group || "Application"
    readonly property string section: ui.section || "general"
    readonly property var game: ui.game || ({})
    readonly property var allSections: Couch.settings.sections
    readonly property var sections: allSections.filter(function(page){return page.group === root.group})
    readonly property var groups: ["Application","Emulation","Video","Devices","Services","Diagnostics","Game"].filter(function(name){return root.allSections.some(function(page){return page.group===name})})
    readonly property var tabs: ["home","library","collections","themes","settings"]
    readonly property var labels: [qsTr("Home"),qsTr("Library"),qsTr("Collections"),qsTr("Themes"),qsTr("Settings")]
    readonly property bool mapping: section === "bindings" && !(ui.query || "").length
    readonly property real textScale: ui.textScale || 1
    readonly property bool nintendo: ui.prompts === "nintendo"
    readonly property var prompt: ({left:nintendo?"L":ui.prompts==="playstation"?"L1":"LB",right:nintendo?"R":ui.prompts==="playstation"?"R1":"RB",lt:nintendo?"ZL":ui.prompts==="playstation"?"L2":"LT",rt:nintendo?"ZR":ui.prompts==="playstation"?"R2":"RT",accept:ui.prompts==="playstation"?"×":"A",back:ui.prompts==="playstation"?"○":"B",search:nintendo?"Y":ui.prompts==="playstation"?"□":"X"})
    property string zone: "games"
    property int navIndex: 0
    property int settingIndex: 0
    property int sectionIndex: 0
    property var beforeModal
    property var clock: new Date()
    property var savedFocus: ({})
    function slot(view,name,key,fallback){const views=skin.views||{};const rect=(views[view]||{})[name]||{};return rect[key]===undefined?fallback:Number(rect[key])}
    readonly property real sidebar:skin.metrics.sidebarWidth
    readonly property real contentStart:sidebar+skin.metrics.columnGap
    readonly property var filteredGames: Couch.games.filter(function(item){
        if(ui.libraryQuery && !item.name.toLocaleLowerCase().includes(ui.libraryQuery.toLocaleLowerCase()))return false
        if(!ui.collection)return true
        if(ui.collection==="favorites")return item.favorite
        const collection=Couch.collections.find(function(entry){return entry.id===ui.collection})
        return !collection || (collection.games || []).includes(item.id)
    })
    function remember(name,index){zone=name;navIndex=index; if(name==="settings")settingIndex=index;if(name==="sections")sectionIndex=index; savedFocus[tab]={zone:name,index:index}}
    function restore(){const previous=savedFocus[tab];focusZone(previous?previous.zone:tab==="settings"?"sections":tab==="home"||tab==="library"?"games":"content",previous?previous.index:0)}
    function focusZone(name,index){
        zone=name;navIndex=Math.max(0,index||0)
        if(name==="settings")settingIndex=navIndex
        if(name==="sections")sectionIndex=navIndex
        Qt.callLater(function(){
            let target=null
            if(name==="tabs")target=tabButtons.itemAt(Math.min(navIndex,4))
            else if(name==="groups")target=groupButtons.itemAt(Math.min(navIndex,groups.length-1))
            else if(name==="tools")target=navIndex===0?scopeButton:searchButton
            else if(name==="bottom")target=navIndex===0?helpButton:navIndex===1?reviewButton:saveButton
            else if(name==="subhead")target=navIndex===0?playerButton:testButton
            else if(name==="sections"){
                sectionIndex=Math.min(navIndex,Math.max(0,sectionList.count-1));sectionList.currentIndex=sectionIndex;sectionList.positionViewAtIndex(sectionIndex,ListView.Contain)
                Qt.callLater(function(){if(sectionList.currentItem)sectionList.currentItem.forceActiveFocus()});return
            } else if(name==="settings"){
                settingIndex=Math.min(navIndex,Math.max(0,settingList.count-1));settingList.currentIndex=settingIndex;settingList.positionViewAtIndex(settingIndex,ListView.Contain)
                Qt.callLater(function(){if(settingList.currentItem)settingList.currentItem.forceActiveFocus();else root.focusZone("tools",1)});return
            } else if(name==="games"){
                const list=tab==="home"?homeGames:libraryGames;list.currentIndex=Math.min(navIndex,Math.max(0,list.count-1));list.positionViewAtIndex(list.currentIndex,ListView.Contain)
                Qt.callLater(function(){if(list.currentItem)list.currentItem.forceActiveFocus();else addFolder.forceActiveFocus()});return
            } else if(name==="dock")target=dockButtons.itemAt(Math.min(navIndex,5))
            else if(name==="hero")target=navIndex===0?startButton:gameSettingsButton
            else if(name==="content"){
                const list=tab==="collections"?collectionList:themeList;list.currentIndex=Math.min(navIndex,Math.max(0,list.count-1));list.positionViewAtIndex(list.currentIndex,ListView.Contain)
                Qt.callLater(function(){if(list.currentItem)list.currentItem.forceActiveFocus()});return
            } else if(name==="page-tools")target=tab==="themes"?importTheme:tab==="collections"?newCollection:librarySearch
            else if(name==="profile")target=profileButton
            if(target && target.visible && target.enabled)target.forceActiveFocus(Qt.TabFocusReason)
        })
    }
    function activateRow(row){
        if(!row || !row.enabled)return
        if(row.kind==="action" || row.kind==="binding")Couch.edit(row.id)
        else if(row.kind==="tree")treeBrowser.open(Couch.settings,row.id,row.label)
        else editor.open(Couch.settings,row)
    }
    function stepTab(delta){const index=tabs.indexOf(tab);Couch.action("tab",tabs[(index+delta+tabs.length)%tabs.length])}
    function stepGroup(delta){if(tab!=="settings"||!groups.length)return;const index=groups.indexOf(group);Couch.action("group",groups[(index+delta+groups.length)%groups.length]);focusZone("groups",(index+delta+groups.length)%groups.length)}
    function controller(action){
        const menus=[collectionMenu,systemMenu,scopeMenu,playerMenu,gameMenu,membership]
        for(let i=0;i<menus.length;++i)if(menus[i].visible){menus[i].controller(action);return}
        if(modal.visible){modal.controller(action);return}
        if(treeBrowser.visible){treeBrowser.controller(action);return}
        if(editor.visible){editor.controller(action);return}
        if(ui.mapping){if(action==="back")Couch.action("cancel-mapping");return}
        if(action==="tab-prev"||action==="tab-next"){stepTab(action==="tab-next"?1:-1);return}
        if(action==="group-prev"||action==="group-next"){stepGroup(action==="group-next"?1:-1);return}
        if(action==="more"&&tab==="collections"&&zone==="content"){collectionMenu.collection=(Couch.collections[navIndex]||{});collectionMenu.open();return}
        if(action==="more"&&tab==="settings"&&zone==="settings"){const row=Couch.settings.rows[settingIndex];if(row&&row.context)Couch.settings.context(row.id);return}
        if(action==="menu"){systemMenu.open();return}
        if(action==="search"){Couch.action(tab==="settings"?"search":"library-search");return}
        if(action==="back"){
            if(tab==="settings" && (zone==="settings"||zone==="bottom"||zone==="subhead"))focusZone("sections",sectionIndex)
            else if(tab==="settings"&&zone==="sections")focusZone("groups",groups.indexOf(group))
            else if(tab!=="home")Couch.action("tab","home")
            return
        }
        if(action==="accept"){
            const item=canvas.Window.window?canvas.Window.window.activeFocusItem:null
            if(item && typeof item.clicked==="function")item.clicked()
            return
        }
        const delta=action==="left"||action==="up"?-1:1
        if(zone==="tabs"){
            if(action==="down")focusZone(tab==="settings"?"groups":tab==="home"||tab==="library"?"games":"content",0)
            else if(action==="left"||action==="right")focusZone("tabs",Math.max(0,Math.min(4,navIndex+delta)))
        } else if(zone==="groups"){
            if(action==="up")focusZone("tabs",tabs.indexOf(tab))
            else if(action==="down")focusZone("sections",sections.findIndex(function(page){return page.id===section}))
            else focusZone("groups",Math.max(0,Math.min(groups.length-1,navIndex+delta)))
        } else if(zone==="sections"){
            if(action==="right")focusZone("settings",settingIndex)
            else if(action==="up"&&sectionIndex===0)focusZone("tools",0)
            else if(action==="up"||action==="down"){
                const next=Math.max(0,Math.min(sections.length-1,sectionIndex+delta));Couch.action("section",sections[next].id);focusZone("sections",next)
            }
        } else if(zone==="settings"){
            if(action==="left")focusZone("sections",sectionIndex)
            else if(action==="up"&&settingIndex===0)focusZone(mapping?"subhead":"tools",0)
            else if(action==="down"&&settingIndex>=settingList.count-1)focusZone("bottom",0)
            else if(action==="up"||action==="down")focusZone("settings",settingIndex+delta)
        } else if(zone==="bottom"){
            if(action==="up")focusZone("settings",settingIndex)
            else if(action==="left"||action==="right")focusZone("bottom",Math.max(0,Math.min(2,navIndex+delta)))
        } else if(zone==="tools"||zone==="subhead"){
            if(action==="up")focusZone(zone==="tools"?"groups":"tools",zone==="tools"?groups.indexOf(group):0)
            else if(action==="down")focusZone("settings",settingIndex)
            else focusZone(zone,Math.max(0,Math.min(1,navIndex+delta)))
        } else if(zone==="hero"){
            if(action==="up")focusZone("tabs",0)
            else if(action==="down")focusZone("games",0)
            else focusZone("hero",Math.max(0,Math.min(1,navIndex+delta)))
        } else if(zone==="games"){
            if(action==="up")focusZone(tab==="home"?"hero":"page-tools",0)
            else if(action==="down")focusZone("dock",0)
            else focusZone("games",Math.max(0,Math.min((tab==="home"?Couch.games:filteredGames).length-1,navIndex+delta)))
        } else if(zone==="dock"){
            if(action==="up")restore()
            else if(action==="left"||action==="right")focusZone("dock",Math.max(0,Math.min(5,navIndex+delta)))
        } else if(zone==="content"){
            const count=tab==="collections"?Couch.collections.length:Couch.theme.installed.length
            if(action==="up"&&navIndex===0)focusZone("page-tools",0)
            else if(action==="down"&&navIndex===count-1)focusZone("dock",0)
            else focusZone("content",Math.max(0,Math.min(count-1,navIndex+delta)))
        } else if(zone==="page-tools"){
            if(action==="up")focusZone("tabs",tabs.indexOf(tab))
            else if(action==="down")focusZone(tab==="library"?"games":"content",0)
        } else focusZone("tabs",tabs.indexOf(tab))
    }
    Connections {target:Couch;function onInput(action){root.controller(action)}}
    Connections {target:Couch.settings;function onChanged(){if(root.tab==="settings"&&!treeBrowser.visible&&!editor.visible&&!modal.visible&&root.zone==="settings")root.focusZone("settings",root.settingIndex)}}
    onTabChanged: Qt.callLater(restore)
    Timer {interval:1000;running:true;repeat:true;onTriggered:root.clock=new Date()}
    Item {
        id:canvas;objectName:"CouchDesignCanvas";width:1920;height:1080
        scale:Math.min(root.width/1920,root.height/1080);transformOrigin:Item.Center
        anchors.centerIn:parent
        Rectangle {anchors.fill:parent;color:skin.colours.background}
        ThemeLayers {anchors.fill:parent;skin:root.skin;view:root.tab;game:root.game}
        CButton {
            id:profileButton;x:80;y:64;width:270;height:64;visualStyle:"plain";navZone:"profile";skin:root.skin
            text:ui.profileName||qsTr("Profile");onFocused:function(zone,index){root.remember(zone,index)}
            contentItem:Row{spacing:14;Rectangle{width:60;height:60;radius:30;color:skin.colours.accent;CIcon{anchors.centerIn:parent;visible:!ui.profileImage;name:"profile";ink:"white"}Image{anchors.fill:parent;source:ui.profileImage||"";fillMode:Image.PreserveAspectCrop;cache:false}}Text{width:Math.max(0,profileButton.availableWidth-74);elide:Text.ElideRight;text:ui.profileName||qsTr("Profile");anchors.verticalCenter:parent.verticalCenter;color:skin.colours.text;font.family:skin.fontFamily;font.pixelSize:26}}
            onClicked:Couch.action("profiles")
        }
        Row {
            id:topTabs;x:560;y:66;height:64;spacing:20
            CButton {width:48;padding:4;height:38;anchors.verticalCenter:parent.verticalCenter;text:root.prompt.left;font.pixelSize:20;skin:root.skin;onClicked:root.stepTab(-1)}
            Repeater {
                id:tabButtons;model:root.labels
                CButton {
                    required property int index;required property string modelData
                    width:implicitWidth;height:62;text:modelData;visualStyle:"tab";font.pixelSize:30;navZone:"tabs";navIndex:index;skin:root.skin
                    onFocused:function(zone,itemIndex){root.remember(zone,itemIndex)}
                    onClicked:Couch.action("tab",root.tabs[index])
                    Rectangle {anchors.bottom:parent.bottom;anchors.left:parent.left;anchors.right:parent.right;height:3;color:skin.colours.accent;visible:root.tab===root.tabs[index]}
                }
            }
            CButton {width:48;padding:4;height:38;anchors.verticalCenter:parent.verticalCenter;objectName:"CouchTopNext";text:root.prompt.right;font.pixelSize:20;skin:root.skin;onClicked:root.stepTab(1)}
        }
        Column {
            x:1640;y:66;width:200;spacing:4
            Text{width:parent.width;horizontalAlignment:Text.AlignRight;text:Qt.formatTime(root.clock,"HH:mm");font.pixelSize:28;font.weight:Font.Medium;color:skin.colours.text;font.family:skin.fontFamily}
            Text{width:parent.width;horizontalAlignment:Text.AlignRight;text:Qt.formatDate(root.clock,"dd MMM yyyy");font.pixelSize:18;color:skin.colours.muted;font.family:skin.fontFamily}
        }
        Item {
            id:home;visible:root.tab==="home";x:80;y:164;width:1760;height:660
            Item {
                id:hero;x:root.slot("home","hero","x",80)-80;y:root.slot("home","hero","y",164)-164;width:root.slot("home","hero","width",1760);height:root.slot("home","hero","height",300)
                Text{x:0;y:24;width:990;text:root.game.name||qsTr("Welcome to Lichii-tgm");font.family:skin.fontFamily;font.pixelSize:56*root.textScale;font.weight:Font.Medium;color:skin.colours.text;wrapMode:Text.WordWrap;maximumLineCount:3;elide:Text.ElideRight}
                Row{x:0;y:226;spacing:16
                    CButton{id:startButton;text:root.prompt.accept+"  "+(ui.gameRunning?qsTr("Resume"):qsTr("Start"));width:180;height:60;visualStyle:"primary";enabled:!!root.game.path||!!ui.gameRunning;navZone:"hero";navIndex:0;skin:root.skin;onFocused:function(zone,index){root.remember(zone,index)} onClicked:Couch.action(ui.gameRunning?"resume":"play")}
                    CButton{id:gameSettingsButton;text:qsTr("Game settings");width:240;height:60;enabled:!!root.game.path;navZone:"hero";navIndex:1;skin:root.skin;onFocused:function(zone,index){root.remember(zone,index)} onClicked:Couch.action("game-settings")}
                }
                Image{x:1080;y:0;width:680;height:300;source:root.game.image||"";fillMode:Image.PreserveAspectCrop;asynchronous:true;cache:true;visible:source.toString().length>0}
            }
            Text{x:0;y:324;text:qsTr("Your games");color:skin.colours.text;font.family:skin.fontFamily;font.pixelSize:28;font.weight:Font.Medium}
            CButton{x:1540;y:314;width:220;height:52;text:qsTr("All games →");visualStyle:"plain";skin:root.skin;onClicked:Couch.action("tab","library")}
            ListView {
                id:homeGames;x:root.slot("home","games","x",80)-80;y:root.slot("home","games","y",540)-164;width:root.slot("home","games","width",1760);height:root.slot("home","games","height",284);orientation:ListView.Horizontal;spacing:20;clip:true
                model:Couch.games;boundsBehavior:Flickable.StopAtBounds;delegate:gameCard
                ScrollBar.horizontal:ScrollBar{}
            }
            CButton{id:addFolder;x:0;y:400;width:300;height:64;visible:!Couch.games.length;text:qsTr("Add a game folder");visualStyle:"primary";skin:root.skin;onClicked:Couch.action("add-directory")}
        }
        Item {
            visible:root.tab==="library";x:80;y:164;width:1760;height:660
            Text{x:0;y:14;width:1300;text:ui.libraryQuery?qsTr("Results for “%1”").arg(ui.libraryQuery):ui.collection?(Couch.collections.find(function(item){return item.id===ui.collection})||{}).name||qsTr("All games"):qsTr("All games");color:skin.colours.text;font.pixelSize:40;font.family:skin.fontFamily}
            CButton{id:librarySearch;x:1450;y:0;width:310;height:60;text:qsTr("Search games");skin:root.skin;navZone:"page-tools";onFocused:function(zone,index){root.remember(zone,index)} onClicked:Couch.action("library-search")}
            ListView{id:libraryGames;x:root.slot("library","games","x",80)-80;y:root.slot("library","games","y",264)-164;width:root.slot("library","games","width",1760);height:root.slot("library","games","height",530);orientation:ListView.Horizontal;spacing:24;model:root.filteredGames;clip:true;delegate:gameCard;boundsBehavior:Flickable.StopAtBounds;ScrollBar.horizontal:ScrollBar{}}
            Text{x:0;y:220;visible:!root.filteredGames.length;text:qsTr("No games match. Search again or choose another collection.");color:skin.colours.muted;font.pixelSize:26;font.family:skin.fontFamily}
        }
        Component {
            id:gameCard
            CButton {
                id:card;required property int index;required property var modelData
                width:root.tab==="library"?340:276;height:root.tab==="library"?480:276;padding:4;visualStyle:"plain";selected:modelData.id===root.ui.selected;skin:root.skin;navZone:"games";navIndex:index
                onFocused:function(zone,itemIndex){root.remember(zone,itemIndex);Couch.action("selected",modelData.id)}
                onClicked:{Couch.action("selected",modelData.id);gameMenu.open()}
                contentItem:Item{
                    Rectangle{anchors.top:parent.top;anchors.left:parent.left;anchors.right:parent.right;height:Math.min(parent.width,parent.height);color:skin.colours.surface;visible:!card.modelData.hasArt;Text{anchors.fill:parent;anchors.margins:20;text:card.modelData.name;color:skin.colours.text;font.pixelSize:26;font.family:skin.fontFamily;wrapMode:Text.WordWrap;maximumLineCount:6;elide:Text.ElideRight;verticalAlignment:Text.AlignVCenter;horizontalAlignment:Text.AlignHCenter}}
                    Image{anchors.top:parent.top;anchors.left:parent.left;anchors.right:parent.right;height:Math.min(parent.width,parent.height);source:card.modelData.image||"";fillMode:Image.PreserveAspectFit;asynchronous:true}
                    Text{anchors.left:parent.left;anchors.right:parent.right;anchors.bottom:parent.bottom;height:56;visible:root.tab==="library";text:card.modelData.name;color:skin.colours.text;font.family:skin.fontFamily;font.pixelSize:24;wrapMode:Text.WordWrap;maximumLineCount:2;elide:Text.ElideRight}
                }
                Accessible.name:modelData.name
            }
        }
        Item {
            visible:root.tab==="collections"||root.tab==="themes";x:80;y:164;width:1760;height:660
            Text{x:0;y:14;text:root.tab==="themes"?qsTr("Appearance"):qsTr("Collections");color:skin.colours.text;font.family:skin.fontFamily;font.pixelSize:40}
            CButton{id:newCollection;visible:root.tab==="collections";x:1450;y:0;width:310;height:60;text:qsTr("New collection");skin:root.skin;navZone:"page-tools";onFocused:function(zone,index){root.remember(zone,index)} onClicked:Couch.action("new-collection")}
            CButton{id:importTheme;visible:root.tab==="themes";x:1450;y:0;width:310;height:60;text:qsTr("Import theme");skin:root.skin;navZone:"page-tools";onFocused:function(zone,index){root.remember(zone,index)} onClicked:Couch.action("import-theme")}
            ListView {
                id:collectionList;visible:root.tab==="collections";x:0;y:100;width:1760;height:530;clip:true;spacing:16;model:Couch.collections;boundsBehavior:Flickable.StopAtBounds;ScrollBar.vertical:ScrollBar{}
                delegate:CButton{
                    required property int index;required property var modelData
                    width:collectionList.width-16;height:112;text:modelData.name;skin:root.skin;navZone:"content";navIndex:index
                    onFocused:function(zone,itemIndex){root.remember(zone,itemIndex)} onClicked:Couch.action("collection",modelData.id)
                }
            }
            ListView {
                id:themeList;visible:root.tab==="themes";x:0;y:100;width:1760;height:530;clip:true;spacing:16;model:Couch.theme.installed;boundsBehavior:Flickable.StopAtBounds;ScrollBar.vertical:ScrollBar{}
                delegate:CButton{
                    required property int index;required property var modelData
                    width:themeList.width-16;height:150;skin:root.skin;selected:root.skin.id===modelData.id;navZone:"content";navIndex:index
                    onFocused:function(zone,itemIndex){root.remember(zone,itemIndex)} onClicked:Couch.chooseTheme(modelData.id)
                    contentItem:Row{
                        spacing:28
                        Rectangle{width:220;height:122;radius:8;color:modelData.colours.background;border.width:1;border.color:modelData.colours.line;Rectangle{x:20;y:20;width:80;height:80;color:modelData.colours.surface;radius:6}Rectangle{x:120;y:20;width:76;height:12;color:modelData.colours.focus}Rectangle{x:120;y:48;width:76;height:12;color:modelData.colours.text}}
                        Column{anchors.verticalCenter:parent.verticalCenter;spacing:10;Text{text:modelData.name+(root.skin.id===modelData.id?qsTr(" · Selected"):"");color:skin.colours.text;font.pixelSize:30;font.family:skin.fontFamily}Text{text:modelData.author||"";color:skin.colours.muted;font.pixelSize:22;font.family:skin.fontFamily}}
                    }
                }
            }
        }
        Item {
            id:settingsPage;visible:root.tab==="settings";x:80;y:164;width:1760;height:784
            Row {
                x:0;y:0;width:1760;height:56;spacing:22
                CButton{width:48;padding:4;height:38;anchors.verticalCenter:parent.verticalCenter;text:root.prompt.lt;font.pixelSize:20;skin:root.skin;onClicked:root.stepGroup(-1)}
                Repeater{
                    id:groupButtons;model:root.groups
                    CButton{
                        required property int index;required property string modelData
                        width:(1620-(root.groups.length-1)*22)/Math.max(1,root.groups.length);height:52;text:qsTr(modelData);visualStyle:"tab";font.pixelSize:22;skin:root.skin;navZone:"groups";navIndex:index
                        onFocused:function(zone,itemIndex){root.remember(zone,itemIndex)} onClicked:Couch.action("group",modelData)
                        Rectangle{anchors.bottom:parent.bottom;anchors.left:parent.left;anchors.right:parent.right;height:3;color:skin.colours.focus;visible:root.group===modelData}
                    }
                }
                CButton{width:48;padding:4;height:38;anchors.verticalCenter:parent.verticalCenter;objectName:"CouchGroupNext";text:root.prompt.rt;font.pixelSize:20;skin:root.skin;onClicked:root.stepGroup(1)}
            }
            Rectangle{x:0;y:55;width:1760;height:1;color:skin.colours.line}
            Text{x:0;y:78;text:qsTr("Settings");color:skin.colours.text;font.family:skin.fontFamily;font.pixelSize:skin.metrics.titleSize;font.weight:Font.Medium}
            CButton{id:scopeButton;x:420;y:74;width:240;height:56;text:ui.scope==="global"?qsTr("Global settings ▾"):(ui.scopeName||qsTr("This game"))+" ▾";skin:root.skin;navZone:"tools";navIndex:0;onFocused:function(zone,index){root.remember(zone,index)} onClicked:scopeMenu.open()}
            CButton {
                id:searchButton;objectName:"CouchSettingsSearch";x:676;y:74;width:1084;height:56;text:ui.query||qsTr("Search all settings");skin:root.skin;navZone:"tools";navIndex:1
                onFocused:function(zone,index){root.remember(zone,index)} onClicked:Couch.action("search")
                contentItem:Row{spacing:16;CIcon{name:"search";ink:skin.colours.muted;width:28;height:28}Text{width:searchButton.width-140;text:searchButton.text;color:ui.query?skin.colours.text:skin.colours.muted;elide:Text.ElideRight;font.family:skin.fontFamily;font.pixelSize:22;maximumLineCount:1}Text{text:root.prompt.search;color:skin.colours.muted;font.family:skin.fontFamily;font.pixelSize:20}}
            }
            Rectangle{x:root.sidebar-1;y:148;width:1;height:636;color:skin.colours.line}
            Text{x:12;y:158;text:qsTr(root.group);color:skin.colours.muted;font.family:skin.fontFamily;font.pixelSize:20}
            ListView {
                id:sectionList;objectName:"CouchSettingsSections";x:4;y:198;width:root.sidebar-20;height:544;clip:true;spacing:4;model:root.sections;boundsBehavior:Flickable.StopAtBounds;ScrollBar.vertical:ScrollBar{}
                delegate:CButton{
                    required property int index;required property var modelData
                    width:sectionList.width-12;height:skin.metrics.sectionHeight;text:modelData.label;skin:root.skin;visualStyle:"row";selected:root.section===modelData.id && !ui.query;navZone:"sections";navIndex:index
                    contentItem:Text{text:parent.text;color:skin.colours.text;font.family:skin.fontFamily;font.pixelSize:24*root.textScale;verticalAlignment:Text.AlignVCenter;wrapMode:Text.WordWrap;maximumLineCount:2;elide:Text.ElideRight}
                    Rectangle{x:0;y:0;width:3;height:parent.height;color:skin.colours.focus;visible:parent.selected}
                    onFocused:function(zone,itemIndex){root.remember(zone,itemIndex)} onClicked:{Couch.action("section",modelData.id);root.focusZone("sections",index)}
                }
            }
            Text{x:12;y:762;text:qsTr("%1 sections · ↑ ↓").arg(root.sections.length);color:skin.colours.muted;font.pixelSize:skin.metrics.smallSize;font.family:skin.fontFamily}
            Text{x:root.contentStart;y:156;width:900;text:ui.query?qsTr("Search all settings"):(root.sections.find(function(page){return page.id===root.section})||{}).label||qsTr("Settings");color:skin.colours.text;font.family:skin.fontFamily;font.pixelSize:30;font.weight:Font.Medium;elide:Text.ElideRight}
            CButton{id:playerButton;x:1520;y:146;width:116;height:56;visible:["bindings","controls","controls-advanced","input-profiles","vibration"].includes(root.section);text:qsTr("Player %1").arg((ui.player||0)+1);skin:root.skin;navZone:"subhead";navIndex:0;onFocused:function(zone,index){root.remember(zone,index)} onClicked:playerMenu.open()}
            CButton{id:testButton;x:1648;y:146;width:112;height:56;padding:4;visible:root.mapping;text:qsTr("Test inputs");font.pixelSize:18;skin:root.skin;navZone:"subhead";navIndex:1;onFocused:function(zone,index){root.remember(zone,index)} onClicked:Couch.action("input-test")}
            Item {
                visible:root.mapping;x:root.contentStart;y:220;width:620;height:444
                Text{x:12;y:8;text:qsTr("Controller");color:skin.colours.text;font.family:skin.fontFamily;font.pixelSize:22;font.weight:Font.Medium}
                Text{x:400;y:8;width:210;horizontalAlignment:Text.AlignRight;text:qsTr("Player %1").arg((ui.player||0)+1);color:skin.colours.muted;font.family:skin.fontFamily;font.pixelSize:22}
                Image{x:0;y:40;width:620;height:365;source:ui.controllerImage||"";fillMode:Image.PreserveAspectFit;cache:false;Accessible.name:qsTr("Live controller input")}
                Text{x:12;y:414;width:600;text:ui.controllerName||qsTr("No controller detected");elide:Text.ElideRight;color:skin.colours.text;font.family:skin.fontFamily;font.pixelSize:20}
            }
            ListView {
                id:settingList;objectName:"CouchSettingsRows";x:root.mapping?root.contentStart+648:root.contentStart;y:220;width:root.mapping?1760-root.contentStart-648:1760-root.contentStart;height:460;clip:true;model:Couch.settings.rows;spacing:0;boundsBehavior:Flickable.StopAtBounds;ScrollBar.vertical:ScrollBar{}
                delegate:CButton {
                    id:setting;required property int index;required property var modelData
                    width:settingList.width-12;height:root.mapping?skin.metrics.mappingRowHeight:Math.max(skin.metrics.rowHeight,84*root.textScale);visualStyle:"row";opacity:modelData.enabled?1:0.42;skin:root.skin;navZone:"settings";navIndex:index
                    onFocused:function(zone,itemIndex){root.remember(zone,itemIndex)} onClicked:root.activateRow(modelData)
                    contentItem:Item{
                        Text{x:0;y:8;width:parent.width-(root.mapping?220:370);text:setting.modelData.label+(setting.modelData.dirty?" •":"");font.pixelSize:(root.mapping?skin.metrics.valueSize:skin.metrics.fontSize)*root.textScale;font.family:skin.fontFamily;color:skin.colours.text;wrapMode:Text.WordWrap;maximumLineCount:2;elide:Text.ElideRight}
                        Text{x:0;anchors.bottom:parent.bottom;anchors.bottomMargin:8;width:parent.width-(root.mapping?220:370);text:ui.scope!=="global"?(setting.modelData.inherited?qsTr("Using global setting"):qsTr("Override for this game")):"";font.pixelSize:18;font.family:skin.fontFamily;color:skin.colours.muted;elide:Text.ElideRight}
                        Text{anchors.right:parent.right;anchors.verticalCenter:parent.verticalCenter;width:root.mapping?205:350;text:setting.modelData.display;color:skin.colours.focus;font.family:skin.fontFamily;font.pixelSize:(root.mapping?skin.metrics.valueSize-2:skin.metrics.valueSize+2)*root.textScale;horizontalAlignment:Text.AlignRight;wrapMode:Text.WrapAnywhere;maximumLineCount:2;elide:Text.ElideRight}
                    }
                    Rectangle{anchors.bottom:parent.bottom;width:parent.width;height:1;color:skin.colours.line}
                    Accessible.name:modelData.label+": "+modelData.display
                }
            }
            Text{x:420;y:692;text:qsTr("%1 settings").arg(settingList.count);color:skin.colours.muted;font.pixelSize:18;font.family:skin.fontFamily}
            Text{x:1180;y:692;width:580;horizontalAlignment:Text.AlignRight;text:qsTr("↑ ↓ Scroll")+(root.mapping?qsTr(" · Select a binding to remap"):"");color:skin.colours.muted;font.pixelSize:18;font.family:skin.fontFamily}
            Rectangle {
                x:420;y:720;width:1340;height:64;color:skin.colours.surface
                readonly property var currentRow:settingList.model && settingList.model.length?settingList.model[Math.max(0,Math.min(settingList.model.length-1,root.settingIndex))]:({})
                Text{x:12;y:8;width:1006;height:48;text:parent.currentRow.help||qsTr("Choose a setting to see its help.");color:skin.colours.text;font.family:skin.fontFamily;font.pixelSize:20;wrapMode:Text.WordWrap;maximumLineCount:2;elide:Text.ElideRight;verticalAlignment:Text.AlignVCenter}
                CButton{id:helpButton;x:1030;y:8;width:80;height:48;text:qsTr("Help");font.pixelSize:20;skin:root.skin;navZone:"bottom";navIndex:0;enabled:parent.currentRow.id!==undefined;onFocused:function(zone,index){root.remember(zone,index)} onClicked:Couch.action("help",parent.currentRow.id)}
                CButton{id:reviewButton;objectName:"CouchReview";x:1122;y:8;width:96;height:48;text:qsTr("Review");font.pixelSize:20;skin:root.skin;navZone:"bottom";navIndex:1;onFocused:function(zone,index){root.remember(zone,index)} onClicked:Couch.action("review")}
                CButton{id:saveButton;objectName:"CouchSave";x:1230;y:8;width:98;height:48;text:qsTr("Save")+(ui.changeCount?" · "+ui.changeCount:"");font.pixelSize:20;visualStyle:"primary";skin:root.skin;navZone:"bottom";navIndex:2;onFocused:function(zone,index){root.remember(zone,index)} onClicked:Couch.action("save")}
            }
        }
        Rectangle {
            id:dock;visible:root.tab!=="settings";x:root.slot(root.tab,"dock","x",526);y:root.slot(root.tab,"dock","y",840);width:root.slot(root.tab,"dock","width",868);height:root.slot(root.tab,"dock","height",skin.metrics.dockHeight-8);radius:54;color:skin.colours.surface
            Row{anchors.centerIn:parent;spacing:12
                Repeater{
                    id:dockButtons;model:[{label:qsTr("Find setting"),action:"search",icon:"search"},{label:qsTr("Collections"),action:"tab",value:"collections",icon:"collections"},{label:qsTr("Controllers"),action:"controllers",icon:"controllers"},{label:qsTr("Themes"),action:"tab",value:"themes",icon:"themes"},{label:qsTr("Settings"),action:"tab",value:"settings",icon:"settings"},{label:qsTr("Desktop"),action:"desktop",icon:"desktop"}]
                    CButton{
                        required property int index;required property var modelData
                        width:128;height:92;skin:root.skin;visualStyle:"plain";navZone:"dock";navIndex:index
                        onFocused:function(zone,itemIndex){root.zone=zone;root.navIndex=itemIndex} onClicked:Couch.action(modelData.action,modelData.value||"")
                        contentItem:Column{spacing:5;CIcon{anchors.horizontalCenter:parent.horizontalCenter;name:modelData.icon;ink:skin.colours.focus;width:36;height:36}Text{width:parent.width;horizontalAlignment:Text.AlignHCenter;text:modelData.label;color:skin.colours.text;font.family:skin.fontFamily;font.pixelSize:18}}
                    }
                }
            }
        }
        Rectangle{x:80;y:968;width:1760;height:1;color:skin.colours.line}
        Text{x:80;y:992;width:960;text:qsTr("Lichii-tgm NX2 Emulator · %1").arg(ui.version||"");font.family:skin.fontFamily;font.pixelSize:22;color:skin.colours.text;elide:Text.ElideRight}
        Text{x:1110;y:992;width:730;horizontalAlignment:Text.AlignRight;text:(root.tab==="settings"?root.prompt.lt+" / "+root.prompt.rt+"  "+qsTr("Groups")+"    ":"")+root.prompt.search+"  "+qsTr("Search")+"    "+root.prompt.back+"  "+qsTr("Back")+"    "+root.prompt.accept+"  "+qsTr("Select");font.family:skin.fontFamily;font.pixelSize:22;color:skin.colours.text}
        ChoiceMenu{id:collectionMenu;anchors.fill:parent;skin:root.skin;property var collection:({});title:collection.name||"";choices:[{label:qsTr("Open collection"),value:"open"},{label:qsTr("Rename collection"),value:"rename",enabled:collection.id!=="favorites"},{label:qsTr("Delete collection"),value:"delete",enabled:collection.id!=="favorites"}];onChosen:function(value){Couch.action(value==="open"?"collection":value+"-collection",collection.id)}onClosed:root.restore()}
        ChoiceMenu{id:systemMenu;anchors.fill:parent;skin:root.skin;title:qsTr("Lichii-tgm");choices:[{label:qsTr("Resume game"),value:"resume",enabled:!!ui.gameRunning},{label:qsTr("Stop game"),value:"stop",enabled:!!ui.gameRunning},{label:qsTr("Desktop Mode"),value:"desktop"},{label:qsTr("Add game folder"),value:"add-directory"},{label:qsTr("Exit Lichii-tgm"),value:"exit"}];onChosen:function(value){Couch.action(value)}onClosed:root.restore()}
        TreeBrowser{id:treeBrowser;anchors.fill:parent;skin:root.skin;onClosed:root.restore()}
        Editor{id:editor;skin:root.skin;onClosed:root.restore()}
        ShellModal{id:modal;anchors.fill:parent;skin:root.skin;onClosed:root.restore()}
        ChoiceMenu{id:scopeMenu;anchors.fill:parent;skin:root.skin;title:qsTr("Settings scope");choices:[{label:qsTr("Global settings"),value:"global"},{label:root.game.name||qsTr("Selected game"),value:"game:"+(root.game.id||""),enabled:!!root.game.path}];onChosen:function(value){Couch.action("scope",value)} onClosed:root.restore()}
        ChoiceMenu{id:playerMenu;anchors.fill:parent;skin:root.skin;title:qsTr("Choose player");choices:Array.from({length:8},function(_,i){return {label:qsTr("Player %1").arg(i+1),value:i}});onChosen:function(value){Couch.action("player",value)} onClosed:root.restore()}
        ChoiceMenu{id:gameMenu;anchors.fill:parent;skin:root.skin;title:root.game.name||"";choices:[{label:qsTr("Start"),value:"play"},{label:qsTr("Game settings"),value:"game-settings"},{label:root.game.favorite?qsTr("Remove favorite"):qsTr("Add favorite"),value:"favorite"},{label:qsTr("Add to collection"),value:"collection-membership"}];onChosen:function(value){if(value==="collection-membership")membership.open();else Couch.action(value)} onClosed:root.restore()}
        ChoiceMenu{id:membership;anchors.fill:parent;skin:root.skin;title:qsTr("Add or remove from a collection");choices:Couch.collections.filter(function(item){return item.id!=="favorites"}).map(function(item){return {label:item.name,value:item.id}});onChosen:function(value){Couch.action("collection-toggle",value)} onClosed:root.restore()}
        Rectangle{visible:!!ui.toast;anchors.horizontalCenter:parent.horizontalCenter;y:864;width:Math.min(1400,toastText.implicitWidth+48);height:64;color:skin.colours.text;radius:8;z:100;Text{id:toastText;anchors.centerIn:parent;text:ui.toast||"";font.family:skin.fontFamily;font.pixelSize:24;color:skin.colours.onPrimary}}
        Rectangle{anchors.fill:parent;visible:!!ui.mapping;color:skin.colours.scrim;z:59;MouseArea{anchors.fill:parent}}
        Rectangle{visible:!!ui.mapping;anchors.centerIn:parent;width:940;height:260;radius:16;color:skin.colours.surface;z:60;Column{anchors.centerIn:parent;spacing:24;Text{width:840;horizontalAlignment:Text.AlignHCenter;text:qsTr("Press a button or move a stick");font.pixelSize:32;color:skin.colours.text;font.family:skin.fontFamily}Text{width:840;horizontalAlignment:Text.AlignHCenter;text:qsTr("The input will be assigned to the selected binding.");font.pixelSize:24;color:skin.colours.muted;font.family:skin.fontFamily}CButton{text:qsTr("Cancel");anchors.horizontalCenter:parent.horizontalCenter;skin:root.skin;onClicked:Couch.action("cancel-mapping")}}}
    }
    Component.onCompleted:Qt.callLater(restore)
}
