import QtQuick
import QtQuick.Effects

// The middle window: Home (M7), the category page (M7); every other page is still a placeholder (M8).
// The whole window is a drop zone for music (UI doc p.29).
Panel {
    id: panel

    required property var app
    required property var shell
    readonly property var nav: app.nav
    property bool droppingFiles: false
    readonly property bool dropping: droppingFiles || app.devDropping

    // NavigationVm.Kind: Home, Settings, Artist, Album, Playlist, Category, SearchResults, AddArtist, AddAlbum, AddPlaylist, Import
    readonly property var pageTitles: [
        app.home.greeting, qsTr("Settings"), app.artistCard.name, app.albumCard.title, app.playlistCard.name,
        qsTr("Recently Played"), qsTr("Search results"), qsTr("Add artist"), qsTr("Add album"), qsTr("Add playlist"), qsTr("Import"), qsTr("Edit artist"), qsTr("Edit album"), qsTr("Edit playlist"), qsTr("Edit track"), qsTr("Share")
    ]

    title: panel.dropping ? qsTr("Import") : (nav.kind === 5 ? app.category.title : (nav.kind === 8 && app.editor.entity === "playlist") ? qsTr("Add playlist") : pageTitles[nav.kind])

    // ---- the page
    Item {
        id: pageArea
        anchors.fill: parent

        Loader {
            id: pageLoader
            anchors.fill: parent
            active: panel.app.startup === 3
            onLoaded: scrollMemory.attach(pageLoader.item)
            sourceComponent: panel.nav.kind === 0 ? homePage : panel.nav.kind === 5 ? categoryPage : panel.nav.kind === 2 ? artistPage : panel.nav.kind === 3 ? albumPage : panel.nav.kind === 4 ? playlistPage : panel.nav.kind === 1 ? settingsPage : panel.nav.kind === 15 ? sharePage : (panel.nav.kind === 7 || panel.nav.kind >= 8 && panel.nav.kind !== 10) ? editorPage : placeholderPage
        }

        // going back to a page opens it where it was left: every scrolling page reports where it is, and gets it back when it is
        // built (the content of a page arrives a little after the page, so the position is set again until the content is long enough)
        QtObject {
            id: scrollMemory
            property var positions: ({})
            property var flick: null
            property string key: ""
            property real wanted: -1
            function currentKey() { return panel.nav.kind + ":" + panel.nav.pageId + ":" + panel.nav.pageText }
            function attach(item) {
                flick = item ? (item.scroller !== undefined ? item.scroller : (item.contentY !== undefined ? item : null)) : null
                key = currentKey()
                const saved = positions[key]
                wanted = (flick && saved !== undefined && saved > 0) ? saved : -1
                apply()
            }
            function apply() {
                if (!flick || wanted < 0)
                    return
                if (flick.contentHeight - flick.height >= wanted) {
                    flick.contentY = flick.originY + wanted
                    wanted = -1
                }
            }
        }
        Connections {
            target: scrollMemory.flick
            ignoreUnknownSignals: true
            function onContentYChanged() {
                if (scrollMemory.wanted < 0 && scrollMemory.key.length > 0)
                    scrollMemory.positions[scrollMemory.key] = scrollMemory.flick.contentY - scrollMemory.flick.originY
            }
            function onContentHeightChanged() { scrollMemory.apply() }
            function onMovementStarted() { scrollMemory.wanted = -1 }
        }

        // every page change fades the new page in and lets it settle a few pixels
        ParallelAnimation {
            id: pageFade
            NumberAnimation { target: pageLoader; property: "opacity"; from: 0.0; to: 1.0; duration: Theme.durationNormal; easing.type: Easing.OutCubic }
            NumberAnimation { target: pageLoader; property: "anchors.leftMargin"; from: Theme.px(36) * panel.nav.direction; to: 0; duration: Theme.durationNormal * 1.4; easing.type: Easing.OutCubic }
            NumberAnimation { target: pageLoader; property: "scale"; from: 0.985; to: 1.0; duration: Theme.durationNormal * 1.4; easing.type: Easing.OutCubic }
        }
        Connections {
            target: panel.nav
            function onPageChanged() {
                scrollMemory.key = ""      // the old page is left: what it does while it is replaced is not a scroll of the user
                scrollMemory.wanted = -1
                Qt.callLater(function () { scrollMemory.attach(pageLoader.item) })
                pageFade.restart()
            }
        }

        Component { id: homePage; HomePage { app: panel.app } }
        Component { id: categoryPage; CategoryPage { app: panel.app } }
        Component { id: artistPage; ArtistPage { app: panel.app } }
        Component { id: sharePage; SharePage { app: panel.app } }
        Component { id: albumPage; AlbumPage { app: panel.app } }
        Component { id: playlistPage; PlaylistPage { app: panel.app } }
        Component { id: settingsPage; SettingsPage { app: panel.app; onLicenseRequested: readOnlyLicense.open() } }
        Component { id: editorPage; EditorPage { app: panel.app; ed: panel.nav.kind === 7 ? panel.app.artistEditor : panel.app.editor } }
        Component {
            id: placeholderPage
            Item {
                ElideText {
                    anchors.centerIn: parent
                    width: parent.width - Theme.space6
                    horizontalAlignment: Text.AlignHCenter
                    color: Theme.textSecondary
                    wrapMode: Text.WordWrap
                    maximumLineCount: 4
                    text: qsTr("This page is not available yet (#%1).").arg(panel.nav.pageId)
                }
            }
        }
    }

    // ---- drop zone: the page is blurred and an import icon appears (UI doc p.29)
    Rectangle {  // the blurred copy has transparent edges: hide the sharp page behind it
        anchors.fill: pageArea
        color: Theme.surface
        visible: panel.dropping
    }
    ShaderEffectSource {
        id: snapshot
        sourceItem: pageArea
        live: panel.dropping
        visible: false
        anchors.fill: pageArea
    }
    MultiEffect {
        id: blurred
        anchors.fill: pageArea
        source: snapshot
        visible: panel.dropping
        blurEnabled: true
        blur: 1.0
        blurMax: 48
    }
    Rectangle {
        visible: panel.dropping
        anchors.centerIn: parent
        width: Theme.px(120)
        height: Theme.px(120)
        radius: 28
        color: Theme.dropZone
        border.color: Theme.border
        Icon {
            anchors.centerIn: parent
            name: "import"
            size: 64
            color: Theme.text
        }
    }
    DropArea {
        anchors.fill: parent
        z: 5
        onEntered: (drag) => {
            if (drag.hasUrls && panel.app.startup === 3) {
                snapshot.scheduleUpdate()
                panel.droppingFiles = true
            } else {
                drag.accepted = false
            }
        }
        onExited: panel.droppingFiles = false
        onDropped: (drop) => {
            panel.droppingFiles = false
            if (drop.hasUrls) {
                panel.app.dropFiles(drop.urls)
                drop.acceptProposedAction()
            }
        }
    }

    LicenseDialog {
        id: readOnlyLicense
        licenseText: panel.app.settingsVm.licenseText
    }
    // ---- delete confirmation: its sentence says what will really happen (computed by the engine first)
    ConfirmCard {
        id: deleteCard
        property int op: 0
        property double target: 0
        onAccepted: panel.app.operate(op, target)
    }
    Connections {
        target: panel.app
        function onDeleteConfirmation(op, id, title, text) {
            deleteCard.op = op
            deleteCard.target = id
            deleteCard.title = title
            deleteCard.text = text
            deleteCard.open()
        }
    }

    ConfirmCard {
        id: mergeCard
        onAccepted: panel.app.confirmMerge()
        onClosed: panel.app.cancelMerge()
    }
    Connections {
        target: panel.app
        function onMergeConfirmation(title, text) {
            mergeCard.title = title
            mergeCard.text = text
            mergeCard.open()
        }
    }

    BannerHost {
        banners: panel.app.banners
        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
            margins: Theme.space3
        }
        z: 10
    }
}
