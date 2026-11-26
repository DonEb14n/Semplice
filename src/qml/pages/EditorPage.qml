import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs

// One window for Add / Edit of an artist, album, playlist or track (UI doc p.31-47, Figma "Artists", "Albums",
// "Playlists"). It only shows the draft held by EditorVm; the rules (what can be saved) live there.
Item {
    id: page

    required property var app
    property var ed: app.editor                  // the EditorVm shown (the nested "Add artist" window passes artistEditor)
    property bool nested: false
    readonly property string entity: ed.entity
    readonly property bool isAlbum: entity === "album"
    readonly property bool isArtist: entity === "artist"
    readonly property bool isPlaylist: entity === "playlist"
    readonly property bool isTrack: entity === "track"
    readonly property var typeNames: [qsTr("Single"), qsTr("Album"), qsTr("EP"), qsTr("Live"), qsTr("Compilation"), qsTr("Other")]


    // The native file dialogs are created only when they are wanted: creating one loads the Windows shell dialog code, which
    // takes about a second during which the whole application stands still.
    Loader {
        id: pictureLoader
        active: false
        sourceComponent: FileDialog {
            title: page.isArtist ? qsTr("Choose a picture") : qsTr("Choose a cover")
            nameFilters: [qsTr("Pictures") + " (*.jpg *.jpeg *.png *.webp *.bmp)"]
            onAccepted: page.ed.choosePicture(selectedFile)
        }
        onLoaded: item.open()
    }
    Loader {
        id: musicLoader
        active: false
        sourceComponent: FileDialog {
            title: qsTr("Add music")
            fileMode: FileDialog.OpenFiles
            nameFilters: [qsTr("Music") + " (*.mp3 *.flac *.m4a *.aac *.ogg *.opus *.wav *.wma)"]
            onAccepted: page.ed.addFiles(selectedFiles)
        }
        onLoaded: item.open()
    }
    function openPictureDialog() {
        if (pictureLoader.item)
            pictureLoader.item.open()
        else
            pictureLoader.active = true
    }
    function openMusicDialog() {
        if (musicLoader.item)
            musicLoader.item.open()
        else
            musicLoader.active = true
    }

    TrackMenuHost {
        id: rowMenus
        app: page.app
        container: page.isAlbum ? "album" : "playlist"
        containerId: page.ed.id
    }

    // who to pick: artists for the album / track chips
    PickerPopup {
        id: artistPicker
        app: page.app
        kind: 0
        extraItems: page.isAlbum ? page.ed.pendingArtists : []
        addLabel: qsTr("Add new artist")
        onPicked: (id, title, cover) => page.ed.addAuthor(id, title, false, 0, cover)
        onAddNewRequested: (text) => page.app.open(7, 0, text)
    }
    PickerPopup {
        id: languagePicker
        app: page.app
        allowAddNew: false
        fixedItems: page.ed.languageChoices
        onPicked: (id, title) => page.ed.addTag(id)
    }
    PickerPopup {
        id: moodPicker
        app: page.app
        allowAddNew: false
        fixedItems: page.ed.moodChoices
        onPicked: (id, title) => page.ed.addTag(id)
    }
    PickerPopup {
        id: tagPicker
        app: page.app
        allowAddNew: false
        fixedItems: page.ed.tagChoices
        onPicked: (id, title) => page.ed.addTag(id)
    }

    Flickable {
        id: scroller
        anchors { left: parent.left; right: parent.right; top: parent.top; bottom: footer.top }
        contentWidth: width
        contentHeight: content.implicitHeight + Theme.space5
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

    Column {
        id: content
        x: Theme.space4
        y: Theme.space3
        width: scroller.width - Theme.space4 * 2
        spacing: Theme.space3

        // ---- picture + name + chips
        Row {
            width: parent.width
            spacing: Theme.space4

            Item {
                id: pictureBox
                width: Theme.px(140)
                height: Theme.px(140)
                Avatar {
                    anchors.fill: parent
                    round: page.isArtist
                    visible: !page.isTrack
                    letter: page.isArtist ? page.ed.title.trim().substring(0, 1).toUpperCase() : ""
                    source: page.ed.picture
                }
                Rectangle {
                    visible: !page.isTrack
                    anchors.fill: parent
                    radius: page.isArtist ? width / 2 : Theme.radiusControl
                    color: hover.hovered ? "#33000000" : "transparent"
                    Icon {
                        anchors.centerIn: parent
                        visible: hover.hovered || (!page.ed.hasPicture && !page.isArtist)
                        name: "edit"
                        size: Theme.iconMedium
                        color: "#FFFFFF"
                    }
                }
                HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                TapHandler { enabled: !page.isTrack; onTapped: page.openPictureDialog() }
                IconButton {
                    visible: page.ed.hasPicture && !page.isTrack
                    anchors { top: parent.top; right: parent.right; margins: Theme.space1 }
                    icon: "close"
                    tooltip: qsTr("Remove picture")
                    iconSize: 12
                    padding: Theme.space1
                    onClicked: page.ed.clearPicture()
                }
                visible: !page.isTrack
            }

            Column {
                width: parent.width - (pictureBox.visible ? pictureBox.width + Theme.space4 : 0)
                spacing: Theme.space2

                // the big editable name
                TextField {
                    id: nameField
                    width: parent.width
                    text: page.ed.title
                    placeholderText: page.isArtist ? qsTr("Artist name") : page.isPlaylist ? qsTr("Playlist name") : qsTr("Title")
                    placeholderTextColor: Theme.disabled
                    color: Theme.text
                    font.pixelSize: Theme.fontDisplay
                    font.weight: Font.Black
                    selectByMouse: true
                    padding: 0
                    maximumLength: 200
                    onTextEdited: page.ed.title = text
                    onTextChanged: if (!activeFocus) cursorPosition = 0  // a long name shows its beginning
                    background: Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 2
                        color: nameField.activeFocus ? Theme.text : Theme.hairline
                    }
                }

                // album type tabs
                Flow {
                    visible: page.isAlbum
                    width: parent.width
                    spacing: Theme.space1
                    Repeater {
                        model: page.typeNames
                        delegate: Rectangle {
                            id: tab
                            required property string modelData
                            required property int index
                            height: Theme.px(26)
                            width: tabText.implicitWidth + Theme.space4
                            radius: Theme.radiusPill
                            color: page.ed.albumType === index ? Theme.text : "transparent"
                            border.width: 1
                            border.color: Theme.text
                            Text {
                                id: tabText
                                anchors.centerIn: parent
                                text: tab.modelData
                                color: page.ed.albumType === tab.index ? Theme.surface : Theme.text
                                font.pixelSize: Theme.fontBody
                                font.weight: Font.Medium
                            }
                            TapHandler { onTapped: page.ed.albumType = tab.index }
                            HoverHandler { cursorShape: Qt.PointingHandCursor }
                        }
                    }
                }

                // artists (album and track)
                Flow {
                    visible: page.isAlbum || page.isTrack
                    width: parent.width
                    spacing: Theme.space1
                    Repeater {
                        model: page.ed.authors
                        delegate: Chip {
                            required property var model
                            required property int index
                            text: model.name
                            avatarLetter: model.letter
                            avatarSource: model.avatar
                            removable: true
                            reorderable: page.ed.authors.count > 1
                            chipIndex: index
                            onMoveRequested: (target) => page.ed.moveAuthor(index, target)
                            onRemoveRequested: page.ed.removeAuthor(index)
                        }
                    }
                    IconButton {
                        id: addArtistButton
                        icon: "plus"
                        outlined: true
                        tooltip: qsTr("Add artist")
                        iconSize: 12
                        padding: Theme.space1
                        onClicked: artistPicker.toggleAt(addArtistButton)
                    }
                    Text {
                        visible: page.ed.authors.count === 0
                        anchors.verticalCenter: undefined
                        text: qsTr("At least one artist is needed")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontCaption
                    }
                }

                // release date (album)
                Row {
                    visible: page.isAlbum
                    spacing: Theme.space2
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Release date")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontBody
                    }
                    DateWheel {
                        year: page.ed.year
                        month: page.ed.month
                        day: page.ed.day
                        valid: page.ed.dateValid
                        onEdited: (y, m, d) => page.ed.setDate(y, m, d)
                    }
                }

                // "Copy tags from artists" (album): adds the tags of the credited artists to the ones already there
                PillButton {
                    visible: page.isAlbum
                    text: qsTr("Copy tags from artists")
                    active: page.ed.authors.count > 0
                    onClicked: page.ed.copyTagsFromArtists()
                }

                // tags (artist, album): genres, the language of the lyrics, the mood
                Column {
                    visible: !page.isPlaylist && !page.isTrack
                    width: parent.width
                    spacing: Theme.space1
                    component TagSection: Column {
                        id: section
                        property string heading
                        property var names: []
                        property string addTooltip
                        property var picker
                        signal removeRequested(int index)
                        width: parent.width
                        spacing: Theme.space1
                        Text { text: section.heading; color: Theme.text; font.pixelSize: Theme.fontHeading; font.weight: Font.Bold }
                        Flow {
                            width: parent.width
                            spacing: Theme.space1
                            Repeater {
                                model: section.names
                                delegate: Chip {
                                    required property string modelData
                                    required property int index
                                    text: modelData
                                    removable: true
                                    onRemoveRequested: section.removeRequested(index)
                                }
                            }
                            IconButton {
                                id: addButton
                                icon: "plus"
                                outlined: true
                                tooltip: section.addTooltip
                                iconSize: 12
                                padding: Theme.space1
                                onClicked: section.picker.toggleAt(addButton)
                            }
                        }
                    }
                    TagSection {
                        heading: qsTr("Genres")
                        names: page.ed.tags
                        addTooltip: qsTr("Add tag")
                        picker: tagPicker
                        onRemoveRequested: (index) => page.ed.removeTag(index)
                    }
                    TagSection {
                        heading: qsTr("Lyric language")
                        names: page.ed.languageTags
                        addTooltip: qsTr("Add language")
                        picker: languagePicker
                        onRemoveRequested: (index) => page.ed.removeLanguage(index)
                    }
                    TagSection {
                        heading: qsTr("Mood")
                        names: page.ed.moodTags
                        addTooltip: qsTr("Add mood")
                        picker: moodPicker
                        onRemoveRequested: (index) => page.ed.removeMood(index)
                    }
                }
            }
        }

        // ---- whether the library lists this artist (hidden by hand: only the Hidden filter and the search find it)
        Row {
            visible: page.isArtist && page.ed.editing
            spacing: Theme.space2
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Status in Library")
                color: Theme.text
                font.pixelSize: Theme.fontHeading
                font.weight: Font.Bold
            }
            PillButton {
                text: qsTr("Hide")
                active: !page.ed.userHidden
                onClicked: page.ed.userHidden = true
            }
            PillButton {
                text: qsTr("Show")
                active: page.ed.userHidden
                onClicked: page.ed.userHidden = false
            }
        }

        // ---- the discography of the artist being edited
        Column {
            visible: page.isArtist && page.ed.editing && page.ed.albums.count > 0
            width: parent.width
            spacing: Theme.space1
            Text { text: qsTr("Discography"); color: Theme.text; font.pixelSize: Theme.fontHeading; font.weight: Font.Bold }
            // all the albums of the artist, wrapping onto as many rows as needed
            Flow {
                width: parent.width
                spacing: Theme.space2
                Repeater {
                    model: page.ed.albums
                    delegate: Item {
                    id: tile
                    required property var model
                    width: Theme.px(96)
                    height: tileColumn.implicitHeight
                    Column {
                        id: tileColumn
                        width: parent.width
                        spacing: 2
                        Avatar { width: Theme.px(96); height: Theme.px(96); round: false; source: tile.model.cover }
                        ElideText { width: parent.width; text: tile.model.title; font.pixelSize: Theme.fontCaption; font.weight: Font.Bold }
                        ElideText { width: parent.width; text: tile.model.subtitle; font.pixelSize: Theme.fontCaption; color: Theme.textSecondary }
                    }
                    }
                }
            }
        }

        // ---- tracks (album, playlist)
        Column {
            visible: page.isAlbum || page.isPlaylist
            width: parent.width
            spacing: 0

            Row {
                spacing: Theme.space2
                height: Theme.px(36)
                PillButton {
                    anchors.verticalCenter: parent.verticalCenter
                    icon: "plus-small"
                    text: qsTr("Add music")
                    active: !page.ed.busy
                    onClicked: page.openMusicDialog()
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: page.ed.skippedFiles > 0
                    text: qsTr("%n file(s) could not be read", "", page.ed.skippedFiles)
                    color: Theme.danger
                    font.pixelSize: Theme.fontCaption
                }
            }
            Item {
                width: parent.width
                height: Theme.px(24)
                visible: page.ed.rows.count > 0
                Text { x: Theme.space2; text: "#"; color: Theme.text; font.pixelSize: Theme.fontBody; font.weight: Font.Bold }
                Text { x: 40; text: qsTr("Title"); color: Theme.text; font.pixelSize: Theme.fontBody; font.weight: Font.Bold }
                Text { x: parent.width * 0.55; text: qsTr("Artist"); color: Theme.text; font.pixelSize: Theme.fontBody; font.weight: Font.Bold }
                Text { anchors.right: parent.right; anchors.rightMargin: 40; text: qsTr("Time"); color: Theme.text; font.pixelSize: Theme.fontBody; font.weight: Font.Bold }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.hairline }
            }
            Repeater {
                model: page.ed.rows
                delegate: Item {
                    id: row
                    required property var model
                    required property int index
                    width: content.width
                    readonly property bool editing: !row.model.isNew && page.app.trackEditor.entity === "track" && page.app.trackEditor.id === row.model.trackId
                    height: editing ? Theme.px(80) : Theme.listRowHeight
                    Loader {
                        anchors.fill: parent
                        anchors.topMargin: 2
                        anchors.bottomMargin: 2
                        active: row.editing
                        z: 5
                        sourceComponent: TrackRowEditor {
                            app: page.app
                            cover: row.model.cover
                            position: row.model.position
                        }
                    }
                    Text { x: Theme.space2; anchors.verticalCenter: parent.verticalCenter; text: row.model.position; color: Theme.textSecondary; font.pixelSize: Theme.fontBody }
                    ElideText {
                        visible: !row.model.isNew
                        x: 40
                        width: parent.width * 0.55 - 48
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.model.title
                        font.pixelSize: Theme.fontBody
                        font.weight: Font.Medium
                        scrollOnHover: true
                    }
                    TextField {
                        visible: row.model.isNew
                        x: 40
                        width: parent.width * 0.55 - 48
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.model.title
                        // the beginning of a long title stays visible (the field would show its end)
                        Component.onCompleted: cursorPosition = 0
                        onTextChanged: if (!activeFocus) cursorPosition = 0
                        padding: 0
                        color: row.model.duplicate ? Theme.danger : Theme.text
                        font.pixelSize: Theme.fontBody
                        font.weight: Font.Medium
                        selectByMouse: true
                        background: null
                        onTextEdited: page.ed.setRowTitle(row.index, text)
                    }
                    ElideText {
                        x: parent.width * 0.55
                        width: parent.width * 0.45 - 90
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.model.duplicate ? qsTr("Already in the library") : row.model.artist
                        color: row.model.duplicate ? Theme.danger : Theme.textSecondary
                        font.pixelSize: Theme.fontBody
                    }
                    Text {
                        anchors { right: parent.right; rightMargin: 40; verticalCenter: parent.verticalCenter }
                        text: row.model.duration
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontBody
                    }
                    IconButton {
                        id: rowMore
                        visible: !row.model.isNew && row.model.trackId > 0
                        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                        icon: "more"
                        tooltip: qsTr("More")
                        iconSize: Theme.iconSmall
                        padding: Theme.space1
                        onClicked: rowMenus.openFor(rowMore, row.model)
                    }
                    IconButton {
                        visible: row.model.isNew
                        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                        icon: "close"
                        tooltip: qsTr("Remove from the list")
                        iconSize: 12
                        padding: Theme.space1
                        onClicked: page.ed.removeRow(row.index)
                    }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.hairline; opacity: 0.5 }
                }
            }
        }

    }
    }

    // ---- progress, cancel / save: always visible
    Item {
        id: footer
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: footerColumn.implicitHeight + Theme.space3 * 2

        Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.hairline }
        Column {
            id: footerColumn
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: Theme.space4 }
            spacing: Theme.space2

            Row {
                visible: page.ed.busy
                width: parent.width
                spacing: Theme.space2
                Text { id: phase; anchors.verticalCenter: parent.verticalCenter; text: page.ed.progressText; color: Theme.text; font.pixelSize: Theme.fontBody }
                Rectangle {
                    width: parent.width - phase.width - cancelWork.width - Theme.space2 * 2
                    height: 8
                    anchors.verticalCenter: parent.verticalCenter
                    radius: 4
                    color: Theme.sliderTrack
                    Rectangle {
                        width: Math.max(8, parent.width * page.ed.progress)
                        height: parent.height
                        radius: 4
                        color: Theme.text
                    }
                }
                PillButton { id: cancelWork; icon: "close"; text: qsTr("Cancel"); onClicked: page.ed.cancelWork() }
            }
            Item {
                width: parent.width
                height: cancelSaveRow.height
                IconButton {
                    visible: page.ed.editing && !page.isTrack
                    anchors.left: parent.left
                    icon: "trash"
                    outlined: true
                    tint: Theme.danger
                    tooltip: qsTr("Delete")
                    iconSize: Theme.iconSmall
                    padding: Theme.space2
                    active: !page.ed.busy
                    onClicked: page.app.requestDelete(page.isArtist ? Ops.deleteArtist : page.isAlbum ? Ops.deleteAlbum : Ops.deletePlaylist, page.ed.id)
                }
            Row {
                id: cancelSaveRow
                anchors.right: parent.right
                spacing: Theme.space2
                IconButton {
                    icon: "close"
                    outlined: true
                    tooltip: qsTr("Cancel")
                    iconSize: Theme.iconSmall
                    padding: Theme.space2
                    active: !page.ed.busy
                    onClicked: page.app.nav.back()
                }
                IconButton {
                    icon: "save"
                    outlined: true
                    tooltip: qsTr("Save")
                    iconSize: Theme.iconSmall
                    padding: Theme.space2
                    active: page.ed.canSave
                    onClicked: page.ed.save()
                }
            }
            }
        }
    }
}
