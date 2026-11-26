import QtQuick

// The "..." menu of a track row with everything behind it: add to playlist / album, edit, remove, delete,
// "Add to Discography" (UI doc p.38, p.46). Used by the album and playlist pages.
Item {
    id: host

    required property var app
    property string container: "album"     // "album" or "playlist": where the table is
    property real containerId: 0
    property var row: null                 // the model row the menu was opened for

    function openFor(anchorItem, modelRow) {
        // a second click on the same "..." closes the menu (the press already started closing it: do not reopen)
        if (menu.visible) { menu.close(); return }
        if (Date.now() - menu.closedAt < 250) return
        host.row = modelRow
        menu.parent = anchorItem
        menu.entries = [
            { "label": qsTr("Add to playlist"), "id": "playlist" },
            { "label": qsTr("Add to album"), "id": "album" },
            { "label": qsTr("Edit"), "id": "edit" },
            { "label": qsTr("Remove"), "id": "remove", "enabled": modelRow.canRemove },
            { "label": qsTr("Delete"), "id": "delete", "danger": true }
        ]
        menu.open()
    }

    PopoverMenu {
        id: menu
        y: parent ? parent.height : 0
        x: parent ? parent.width - width : 0
        onChosen: (id) => {
            if (id === "playlist") playlistPicker.openAt(menu.parent)
            else if (id === "album") albumPicker.openAt(menu.parent)
            else if (id === "discography") artistPicker.openAt(menu.parent)
            else if (id === "edit") host.app.editTrack(host.row.trackId)
            else if (id === "remove")
                host.app.operate(host.container === "album" ? Ops.removeTrackFromAlbum : Ops.removeTrackFromPlaylist, host.row.trackId, host.containerId)
            else if (id === "delete") host.app.requestDelete(Ops.deleteTrack, host.row.trackId)
        }
    }

    PickerPopup {
        id: playlistPicker
        app: host.app
        kind: 1
        addLabel: qsTr("Add as new playlist")
        function openAt(anchor) { parent = anchor; y = anchor.height; x = anchor.width - width; open() }
        onPicked: (id) => host.app.operate(Ops.addTrackToPlaylist, host.row.trackId, id)
        onAddNewRequested: (text) => host.app.startPlaylistWithTrack(host.row.trackId, text)
    }
    PickerPopup {
        id: albumPicker
        app: host.app
        kind: 2
        allowAddNew: false
        function openAt(anchor) { parent = anchor; y = anchor.height; x = anchor.width - width; open() }
        onPicked: (id) => host.app.operate(Ops.addTrackToAlbum, host.row.trackId, id)
    }
    PickerPopup {
        id: artistPicker
        app: host.app
        kind: 0
        allowAddNew: false
        function openAt(anchor) { parent = anchor; y = anchor.height; x = anchor.width - width; open() }
        onPicked: (id) => host.app.operate(Ops.addToDiscography, host.row.trackId, id)
    }
}
