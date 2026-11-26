import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Library panel (UI doc p.5-17): tab carousel switcher, view modes, sort/filter menu, "Add" submenu.
Panel {
    id: panel

    required property var app
    readonly property var lib: app.library
    readonly property var tabNames: [qsTr("Playlists"), qsTr("Artists"), qsTr("Albums")]
    // PlayKind of the "play" click and NavigationVm.Kind of the "open" click, per tab
    readonly property var playKinds: [3, 0, 2]
    readonly property var openKinds: [4, 2, 3]

    title: qsTr("Library")

    IconButton {
        id: addButton
        parent: panel.titleItem.parent
        icon: "plus"
        tooltip: qsTr("Add")
        iconSize: Theme.iconSmall
        outlined: true
        padding: Theme.space1 + 2
        anchors {
            right: parent.right
            rightMargin: Theme.space3
            verticalCenter: panel.titleItem.verticalCenter
        }
        onClicked: addMenu.toggle()
    }

    // ---- "Add" submenu
    FadePopup {
        id: addMenu
        property double closedAt: 0
        onAboutToHide: closedAt = Date.now()
        function toggle() { if (visible) close(); else if (Date.now() - closedAt > 250) open() }
        parent: addButton
        x: addButton.width - width
        y: addButton.height + Theme.space1
        padding: Theme.space3
        background: PopupSurface {}
        contentItem: Column {
            spacing: Theme.space2
            Text {
                text: qsTr("Add")
                color: Theme.text
                font.pixelSize: Theme.fontBody
                font.weight: Font.Bold
            }
            Repeater {
                model: [{ "label": qsTr("Artist"), "page": 7 }, { "label": qsTr("Album"), "page": 8 }, { "label": qsTr("Playlist"), "page": 9 }]
                delegate: ElideText {
                    required property var modelData
                    text: modelData.label
                    underlineOnHover: true
                    onClicked: {
                        addMenu.close()
                        panel.app.open(modelData.page, 0)
                    }
                }
            }
        }
    }

    // ---- tab switcher: the active tab stays in the middle, a neighbour replaces it when clicked
    Item {
        id: tabs
        anchors {
            top: parent.top
            topMargin: Theme.space1
            left: parent.left
            right: parent.right
            leftMargin: Theme.space2
            rightMargin: Theme.space2
        }
        height: Theme.px(40)

        // The three tabs are always there; the active one sits in the pill in the middle, its neighbours at the sides. Another tab
        // slides into the middle (and the pill follows the width of its title) while the one that was there slides out to its side.
        TextMetrics {
            id: titleMetrics
            font.pixelSize: Theme.fontBody
            font.weight: Font.Medium
            text: panel.tabNames[panel.lib.tab]
        }
        // the active pill takes what its text needs (up to 55 %); the neighbours share the rest
        property real pillWidth: Math.min(tabs.width * 0.55, Math.ceil(titleMetrics.advanceWidth) + viewButton.width + Theme.space4)
        readonly property real sideWidth: (tabs.width - pillWidth) / 2
        Behavior on pillWidth { NumberAnimation { duration: Theme.durationNormal * 1.4; easing.type: Easing.OutCubic } }

        Rectangle {
            id: pill
            x: tabs.sideWidth
            width: tabs.pillWidth
            height: parent.height
            radius: Theme.radiusControl
            color: "transparent"
            border.width: 1
            border.color: Theme.text
            IconButton {
                id: viewButton
                anchors {
                    right: parent.right
                    rightMargin: Theme.space1
                    verticalCenter: parent.verticalCenter
                }
                icon: panel.lib.viewMode === 0 ? "view-list" : panel.lib.viewMode === 1 ? "view-xlarge" : "view-large"
                iconSize: Theme.iconSmall
                padding: Theme.space1
                tooltip: qsTr("View and sorting")
                onClicked: viewMenu.toggle()
            }
        }
        Repeater {
            model: 3
            delegate: ElideText {
                id: tab
                required property int index
                readonly property int rel: index - panel.lib.tab        // -1 left of the pill, 0 in it, 1 right of it
                readonly property bool active: rel === 0
                readonly property int duration: Theme.durationNormal * 1.4
                text: panel.tabNames[index]
                height: tabs.height
                font.weight: active ? Font.Medium : Font.Normal
                color: active ? Theme.text : Theme.textSecondary
                horizontalAlignment: active ? Text.AlignLeft : Text.AlignHCenter
                underlineOnHover: !active
                tooltipEnabled: false
                opacity: Math.abs(rel) <= 1 ? 1 : 0
                visible: opacity > 0.01
                // places: the pill (inside, left of the view button), the side slots, and out of sight further away
                x: rel === 0 ? tabs.sideWidth + Theme.space3
                   : rel === -1 ? 0
                   : rel === 1 ? tabs.sideWidth + tabs.pillWidth
                   : rel < 0 ? -tabs.sideWidth : tabs.width
                width: rel === 0 ? tabs.pillWidth - Theme.space3 - viewButton.width - Theme.space1 : tabs.sideWidth
                Behavior on x { NumberAnimation { duration: tab.duration; easing.type: Easing.OutCubic } }
                Behavior on width { NumberAnimation { duration: tab.duration; easing.type: Easing.OutCubic } }
                Behavior on opacity { NumberAnimation { duration: tab.duration; easing.type: Easing.OutCubic } }
                Behavior on color { ColorAnimation { duration: tab.duration } }
                onClicked: panel.lib.tab = index
            }
        }
    }

    // ---- view / sort / filter menu (UI doc p.6, p.10)
    FadePopup {
        id: viewMenu
        property double closedAt: 0
        onAboutToHide: closedAt = Date.now()
        function toggle() { if (visible) close(); else if (Date.now() - closedAt > 250) open() }
        parent: tabs
        x: (tabs.width - width) / 2
        y: tabs.height
        padding: Theme.space3
        background: PopupSurface {}
        contentItem: Column {
            spacing: 2
            width: Theme.px(200)

            // ---- View
            ElideText { width: parent.width; text: qsTr("View"); font.weight: Font.Bold }
            Row {
                spacing: Theme.space3
                Repeater {
                    model: [{ "icon": "view-list", "mode": 0, "tip": qsTr("List") },
                            { "icon": "view-xlarge", "mode": 1, "tip": qsTr("Extra large icons (2 per row)") },
                            { "icon": "view-large", "mode": 2, "tip": qsTr("Large icons (3 per row)") }]
                    delegate: IconButton {
                        required property var modelData
                        icon: modelData.icon
                        tooltip: modelData.tip
                        iconSize: Theme.iconMedium
                        padding: Theme.space1
                        dimmed: panel.lib.viewMode !== modelData.mode
                        onClicked: panel.lib.viewMode = modelData.mode
                    }
                }
            }

            // ---- Sort, with the two direction arrows (the active one is dark)
            Row {
                spacing: Theme.space1
                ElideText { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Sort"); font.weight: Font.Bold }
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    icon: "arrow-down"
                    iconSize: Theme.iconSmall
                    padding: Theme.space1
                    dimmed: !panel.lib.sortDown
                    tooltip: panel.lib.sortKey === 0 ? qsTr("A to Z") : panel.lib.sortKey === 1 ? qsTr("Newest first") : qsTr("Newest year first")
                    onClicked: panel.lib.setSortDown(true)
                }
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    icon: "arrow-up"
                    iconSize: Theme.iconSmall
                    padding: Theme.space1
                    dimmed: panel.lib.sortDown
                    tooltip: panel.lib.sortKey === 0 ? qsTr("Z to A") : panel.lib.sortKey === 1 ? qsTr("Oldest first") : qsTr("Oldest year first")
                    onClicked: panel.lib.setSortDown(false)
                }
            }
            Repeater {
                model: [{ "key": 0, "label": qsTr("Alphabetical") }, { "key": 1, "label": qsTr("Recent") }, { "key": 2, "label": qsTr("Year") }]
                delegate: ElideText {
                    required property var modelData
                    visible: panel.lib.keyAllowed(modelData.key)
                    width: parent.width
                    height: visible ? implicitHeight : 0
                    text: modelData.label
                    font.underline: panel.lib.sortKey === modelData.key  // the chosen one is underlined (Figma)
                    underlineOnHover: true
                    onClicked: panel.lib.chooseSort(modelData.key)
                }
            }

            // ---- Filter: the album types that are shown are underlined (all by default)
            ElideText { visible: panel.lib.tab === 2 || panel.lib.tab === 1; width: parent.width; height: visible ? implicitHeight : 0; text: qsTr("Filter"); font.weight: Font.Bold }
            ElideText {
                visible: panel.lib.tab === 1
                width: parent.width
                height: visible ? implicitHeight : 0
                text: qsTr("Hidden")
                font.underline: panel.lib.hiddenOnly   // on = only the artists hidden by hand are listed
                underlineOnHover: true
                onClicked: panel.lib.toggleHiddenOnly()
            }
            Repeater {
                model: panel.lib.tab === 2 ? [{ "type": 1, "label": qsTr("Album") }, { "type": 0, "label": qsTr("Single") },
                                              { "type": 2, "label": qsTr("EP") }, { "type": 3, "label": qsTr("Live") },
                                              { "type": 4, "label": qsTr("Compilation") }, { "type": 5, "label": qsTr("Other") }] : []
                delegate: ElideText {
                    required property var modelData
                    width: parent.width
                    text: modelData.label
                    font.underline: (panel.lib.typeMask & (1 << modelData.type)) !== 0
                    underlineOnHover: true
                    onClicked: panel.lib.toggleType(modelData.type)
                }
            }
        }
    }

    // ---- the list
    Item {
        id: listArea
        clip: true   // a list that slides in does not cross the edge of the window
        anchors {
            top: tabs.bottom
            topMargin: Theme.space2
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }

        Loader {
            id: listLoader
            anchors.fill: parent
            active: panel.lib.ready && panel.lib.count > 0
            sourceComponent: panel.lib.viewMode === 0 ? listView : gridView
        }
        // another tab: the new list slides in from the side the tab lies on (and fades in); another view mode only fades in
        property int shownTab: panel.lib.tab
        property real slideFrom: 0
        ParallelAnimation {
            id: listFade
            NumberAnimation { target: listLoader; property: "opacity"; from: 0.0; to: 1.0; duration: Theme.durationNormal * 1.4; easing.type: Easing.OutCubic }
            NumberAnimation { target: listLoader; property: "anchors.leftMargin"; from: listArea.slideFrom; to: 0; duration: Theme.durationNormal * 1.4; easing.type: Easing.OutCubic }
        }
        Connections {
            target: panel.lib
            function onTabChanged() {
                listArea.slideFrom = (panel.lib.tab > listArea.shownTab ? 1 : -1) * Theme.px(70)
                listArea.shownTab = panel.lib.tab
                listFade.restart()
            }
            function onViewChanged() { listArea.slideFrom = 0; listFade.restart() }
        }

        Component {
            id: listView
            ListView {
                id: view
                objectName: "libraryView"
                clip: true
                model: panel.lib.model
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                delegate: LibraryItem {
                    required property var model
                    title: model.title
                    subtitle: model.subtitle
                    cover: model.cover
                    letter: model.letter
                    dimmed: model.dimmed
                    width: view.width
                    cellWidth: view.width
                    tab: panel.lib.tab
                    mode: 0
                    onPlayRequested: panel.app.play(panel.playKinds[panel.lib.tab], model.itemId)
                    onOpenRequested: panel.app.open(panel.openKinds[panel.lib.tab], model.itemId)
                    onOpenOwnerRequested: panel.app.open(2, model.ownerId)
                }
            }
        }

        Component {
            id: gridView
            GridView {
                id: grid
                objectName: "libraryView"
                readonly property int columns: panel.lib.viewMode === 1 ? 2 : 3
                clip: true
                model: panel.lib.model
                boundsBehavior: Flickable.StopAtBounds
                cellWidth: width / columns
                cellHeight: panel.lib.viewMode === 2 ? cellWidth : cellWidth + (panel.lib.tab === 2 ? 52 : 34)
                ScrollBar.vertical: ScrollBar {}
                delegate: LibraryItem {
                    required property var model
                    title: model.title
                    subtitle: model.subtitle
                    cover: model.cover
                    letter: model.letter
                    dimmed: model.dimmed
                    width: grid.cellWidth
                    height: grid.cellHeight
                    cellWidth: grid.cellWidth
                    tab: panel.lib.tab
                    mode: panel.lib.viewMode
                    onPlayRequested: panel.app.play(panel.playKinds[panel.lib.tab], model.itemId)
                    onOpenRequested: panel.app.open(panel.openKinds[panel.lib.tab], model.itemId)
                    onOpenOwnerRequested: panel.app.open(2, model.ownerId)
                }
            }
        }

        // nothing in this tab yet
        ElideText {
            anchors {
                centerIn: parent
                horizontalCenterOffset: 0
            }
            width: parent.width - Theme.space6
            visible: panel.lib.ready && panel.lib.count === 0 && !panel.lib.loading
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            maximumLineCount: 4
            color: Theme.textSecondary
            text: panel.lib.tab === 0 ? qsTr("No playlists yet") : panel.lib.tab === 1 ? qsTr("No artists yet") : qsTr("No albums yet")
        }
    }
}
