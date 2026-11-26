import QtQuick

// One row of an album / playlist table: number, cover with title and artist, (album), time and the "..." button.
Item {
    id: root

    required property var app
    required property var row          // the model row ({ position, title, artist, albumTitle, duration, cover })
    property bool showAlbum: false
    property var editor: null          // app.trackEditor: the row turns into an editor while it edits this track
    readonly property bool editing: editor !== null && editor.entity === "track" && editor.id === row.trackId
    property bool selected: false
    readonly property alias hovered: hover.hovered
    signal playRequested
    signal menuRequested(Item anchorItem)
    signal artistRequested(var id)
    signal albumRequested

    implicitHeight: editing ? Math.round(Theme.rowHeight * 1.8) : Theme.rowHeight

    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: Theme.space2
        anchors.rightMargin: Theme.space2
        radius: Theme.radiusControl
        color: root.hovered ? Theme.highlight : "transparent"
        border.width: root.hovered ? 1 : 0
        border.color: Theme.border
    }

    Text {
        id: number
        x: Theme.space4
        width: Theme.px(24)
        anchors.verticalCenter: parent.verticalCenter
        text: root.row.position
        color: Theme.text
        font.pixelSize: Theme.fontBody
    }
    Avatar {
        id: cover
        anchors { left: number.right; leftMargin: Theme.space2; verticalCenter: parent.verticalCenter }
        width: Theme.px(32)
        height: Theme.px(32)
        round: false
        source: root.row.cover
        HoverHandler { cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: root.playRequested() }
    }
    Column {
        anchors {
            left: cover.right
            leftMargin: Theme.space2
            right: root.showAlbum ? albumText.left : timeText.left
            rightMargin: Theme.space2
            verticalCenter: parent.verticalCenter
        }
        ElideText {
            width: parent.width
            text: root.row.title
            font.weight: Font.Bold
            scrollOnHover: true
            scrollBackground: Theme.highlight
            underlineOnHover: true
            onClicked: root.playRequested()
        }
        ArtistLinks {
            width: parent.width
            artists: root.row.artists
            pixelSize: Theme.fontCaption
            onArtistClicked: (id) => root.artistRequested(id)
        }
    }
    ElideText {
        id: albumText
        visible: root.showAlbum
        scrollOnHover: true
        scrollBackground: Theme.highlight
        width: visible ? Math.max(80, root.width * 0.22) : 0
        anchors { right: timeText.left; rightMargin: Theme.space3; verticalCenter: parent.verticalCenter }
        text: root.row.albumTitle
        underlineOnHover: root.row.albumId > 0
        onClicked: root.albumRequested()
    }
    Text {
        id: timeText
        anchors { right: more.left; rightMargin: Theme.space1; verticalCenter: parent.verticalCenter }
        text: root.row.duration
        color: Theme.text
        font.pixelSize: Theme.fontBody
        width: Theme.px(44)
    }
    IconButton {
        id: more
        anchors { right: parent.right; rightMargin: Theme.space3; verticalCenter: parent.verticalCenter }
        opacity: root.hovered ? 1 : 0
        icon: "more"
        iconSize: Theme.iconSmall
        padding: Theme.space1
        tooltip: qsTr("More")
        onClicked: root.menuRequested(more)
    }

    HoverHandler { id: hover }

    Loader {
        anchors.fill: parent
        anchors.leftMargin: Theme.space2
        anchors.rightMargin: Theme.space2
        anchors.topMargin: 2
        anchors.bottomMargin: 2
        active: root.editing
        z: 5
        sourceComponent: TrackRowEditor {
            app: root.app
            cover: root.row.cover
            position: root.row.position
        }
    }
}
