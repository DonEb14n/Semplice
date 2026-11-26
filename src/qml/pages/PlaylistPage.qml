import QtQuick
import QtQuick.Controls.Basic

// Playlist card (UI doc p.48, Figma "Playlists"): cover, label, name, statistics, play and Edit, the table
// with the album column.
ListView {
    id: page

    required property var app
    readonly property var card: app.playlistCard
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    model: card.tracks
    ScrollBar.vertical: ScrollBar {}

    TrackMenuHost {
        id: menus
        app: page.app
        container: "playlist"
        containerId: page.card.playlistId
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
                Avatar { id: cover; width: Theme.px(72); height: Theme.px(72); round: false; source: page.card.cover }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    width: head.width - cover.width - Theme.space3
                    ElideText { width: parent.width; text: qsTr("Playlist"); font.pixelSize: Theme.fontCaption }
                    ElideText { width: parent.width; text: page.card.name; font.pixelSize: Theme.fontDisplay; font.weight: Font.Black }
                    ElideText { width: parent.width; text: page.card.stats; font.pixelSize: Theme.fontCaption; font.weight: Font.Bold }
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
                    onClicked: page.app.play(Ops.playPlaylist, page.card.playlistId)
                }
                PillButton {
                    anchors { right: editButton.left; rightMargin: Theme.space2; verticalCenter: parent.verticalCenter }
                    icon: "share"
                    text: qsTr("Share")
                    onClicked: page.app.open(Ops.pageShare, page.card.playlistId, "playlist")
                }
                PillButton {
                    id: editButton
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                    icon: "edit"
                    text: qsTr("Edit")
                    onClicked: page.app.open(Ops.pageEditPlaylist, page.card.playlistId)
                }
            }
            TrackTableHeader { width: parent.width; showAlbum: true }
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
        showAlbum: true
        onPlayRequested: page.app.playFrom(Ops.playPlaylist, page.card.playlistId, index)
        onMenuRequested: (anchorItem) => menus.openFor(anchorItem, rowItem.model)
        onArtistRequested: (id) => { if (id > 0) page.app.open(Ops.pageArtist, id) }
        onAlbumRequested: if (rowItem.model.albumId > 0) page.app.open(Ops.pageAlbum, rowItem.model.albumId)
    }
}
