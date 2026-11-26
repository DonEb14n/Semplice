import QtQuick
import QtQuick.Controls.Basic

// Album card (UI doc p.44, Figma "Albums"): cover, type, title, statistics, artists, play and Edit, the table of
// tracks, then tags and the full release date.
ListView {
    id: page

    required property var app
    readonly property var card: app.albumCard
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    model: card.tracks
    ScrollBar.vertical: ScrollBar {}

    TrackMenuHost {
        id: menus
        app: page.app
        container: "album"
        containerId: page.card.albumId
    }

    header: Item {
        width: page.width
        height: head.implicitHeight + Theme.space4

        Column {
            id: head
            x: Theme.space4
            y: Theme.space2
            width: parent.width - Theme.space4 * 2
            spacing: Theme.space2

            Row {
                spacing: Theme.space3
                Avatar {
                    id: cover
                    width: Theme.px(72)
                    height: Theme.px(72)
                    round: false
                    source: page.card.cover
                }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    width: head.width - cover.width - Theme.space3
                    ElideText { width: parent.width; text: page.card.typeText; font.pixelSize: Theme.fontCaption; font.weight: Font.Bold }
                    ElideText { width: parent.width; text: page.card.title; font.pixelSize: Theme.fontDisplay; font.weight: Font.Black }
                    ElideText { width: parent.width; text: page.card.stats; font.pixelSize: Theme.fontCaption; font.weight: Font.Bold }
                }
            }
            // the artists of the album; a click opens the artist
            Flow {
                width: parent.width
                spacing: Theme.space2
                Repeater {
                    model: page.card.artists
                    delegate: Row {
                        id: who
                        required property var model
                        spacing: Theme.space1
                        Avatar { width: 20; height: 20; letter: who.model.letter; source: who.model.avatar }
                        ElideText {
                            anchors.verticalCenter: parent.verticalCenter
                            text: who.model.name
                            font.pixelSize: Theme.fontCaption
                            font.weight: Font.Bold
                            underlineOnHover: true
                            onClicked: page.app.open(Ops.pageArtist, who.model.artistId)
                        }
                    }
                }
            }
            Item {
                width: parent.width
                height: Theme.px(36)
                IconButton {
                    icon: "play"
                    outlined: true
                    tooltip: qsTr("Play")
                    iconSize: Theme.iconSmall
                    padding: Theme.space2
                    onClicked: page.card.hiddenOwner > 0 ? page.app.play(Ops.playHiddenArtist, page.card.hiddenOwner) : page.app.play(Ops.playAlbum, page.card.albumId)
                }
                PillButton {
                    visible: page.card.editable
                    anchors { right: editButton.left; rightMargin: Theme.space2; verticalCenter: parent.verticalCenter }
                    icon: "share"
                    text: qsTr("Share")
                    onClicked: page.app.open(Ops.pageShare, page.card.albumId, "album")
                }
                PillButton {
                    id: editButton
                    visible: page.card.editable
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                    icon: "edit"
                    text: qsTr("Edit")
                    onClicked: page.app.open(Ops.pageEditAlbum, page.card.albumId)
                }
            }
            // table header
            TrackTableHeader { width: parent.width }
        }
    }

    delegate: TrackRowItem {
        id: rowItem
        required property var model
        required property int index
        app: page.app
        editor: page.app.trackEditor
        width: page.width
        row: rowItem.model
        onPlayRequested: page.card.hiddenOwner > 0 ? page.app.playFrom(Ops.playHiddenArtist, page.card.hiddenOwner, index) : page.app.playFrom(Ops.playAlbum, page.card.albumId, index)
        onMenuRequested: (anchorItem) => menus.openFor(anchorItem, rowItem.model)
        onArtistRequested: (id) => { if (id > 0) page.app.open(Ops.pageArtist, id) }
    }

    footer: Item {
        width: page.width
        height: foot.implicitHeight + Theme.space5

        Column {
            id: foot
            x: Theme.space4
            y: Theme.space3
            width: parent.width - Theme.space4 * 2
            spacing: Theme.space2
            Flow {
                width: parent.width
                spacing: Theme.space1
                Repeater {
                    model: page.card.tags
                    delegate: Chip { required property string modelData; text: modelData }
                }
            }
            ElideText {
                width: parent.width
                visible: text.length > 0
                text: page.card.dateText
                font.pixelSize: Theme.fontCaption
                font.weight: Font.Bold
            }
        }
    }
}
