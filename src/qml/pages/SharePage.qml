import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs

// Share (Figma "Middle window all conditions", the "Share" frames): everything that goes into a shared file - or everything that is in
// a file about to be imported - as cards: the artists with the status they get in the library, the albums or the playlist with
// their tags and tracks. Every card and every track has a check box; what is not ticked stays out. The button at the bottom right
// saves the file (export) or adds the content to the library (import).
// This page only shows what ShareVm holds; it never changes anything in the library.
Item {
    id: page

    required property var app
    readonly property var share: app.share
    readonly property bool importing: share.importing

    // The native file dialog is created only when it is wanted (creating one takes about a second).
    Loader {
        id: saveLoader
        active: false
        sourceComponent: FileDialog {
            title: qsTr("Save the shared file")
            fileMode: FileDialog.SaveFile
            defaultSuffix: "smpl"
            nameFilters: [qsTr("Semplice share files") + " (*.smpl)"]
            currentFile: "file:///" + page.share.suggestedName + ".smpl"
            onAccepted: page.share.exportTo(selectedFile)
        }
        onLoaded: item.open()
    }
    function openSaveDialog() {
        if (saveLoader.item)
            saveLoader.item.open()
        else
            saveLoader.active = true
    }

    // the round check box (a check mark inside the circle when the item is taken)
    component Tick: Item {
        id: tick
        property bool on: false
        property bool enabledTick: true
        signal toggled
        implicitWidth: Theme.px(22)
        implicitHeight: Theme.px(22)
        opacity: enabledTick ? 1 : 0.4
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: Theme.surface
            border.width: Theme.strokeWidth
            border.color: Theme.text
        }
        Icon {
            anchors.centerIn: parent
            visible: tick.on
            name: "check"
            size: Theme.iconSmall
            color: Theme.text
        }
        HoverHandler { cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: tick.toggled() }
    }

    // a small bold heading above a group of chips ("Genres", "Lyric language", "Mood")
    component ChipGroup: Column {
        id: group
        property string heading
        property var names: []
        visible: names.length > 0
        width: parent ? parent.width : 0
        spacing: Theme.space1
        Text { text: group.heading; color: Theme.text; font.pixelSize: Theme.fontHeading; font.weight: Font.Bold }
        Flow {
            width: parent.width
            spacing: Theme.space1
            Repeater {
                model: group.names
                delegate: Chip { required property string modelData; text: modelData }
            }
        }
    }

    component Card: Rectangle {
        radius: Theme.radiusControl
        color: Theme.surface
        border.width: 1
        border.color: Theme.border
    }

    ListView {
        id: list
        anchors { left: parent.left; right: parent.right; top: parent.top; bottom: footer.top }
        clip: true
        spacing: Theme.space2
        topMargin: Theme.space2
        bottomMargin: Theme.space2
        boundsBehavior: Flickable.StopAtBounds
        model: page.share
        ScrollBar.vertical: ScrollBar {}

        delegate: Item {
            id: row
            required property var model
            readonly property var node: model
            width: list.width
            height: loader.item ? loader.item.height : 0

            Loader {
                id: loader
                x: Theme.space4
                width: parent.width - Theme.space4 * 2
                sourceComponent: row.node.type === 3 ? headingComponent : row.node.type === 0 ? artistComponent : cardComponent
            }

            Component {
                id: headingComponent
                Item {
                    height: headingText.implicitHeight + Theme.space1
                    ElideText {
                        id: headingText
                        width: parent.width
                        text: row.node.title
                        font.pixelSize: Theme.fontTitle
                        font.weight: Font.Bold
                    }
                }
            }

            // ---- an artist
            Component {
                id: artistComponent
                Card {
                    height: artistColumn.implicitHeight + Theme.space3 * 2
                    Column {
                        id: artistColumn
                        x: Theme.space3
                        y: Theme.space3
                        width: parent.width - Theme.space3 * 2
                        spacing: Theme.space2
                        Item {
                            width: parent.width
                            height: Theme.px(64)
                            Avatar {
                                id: face
                                width: Theme.px(64)
                                height: width
                                round: true
                                letter: row.node.title.substring(0, 1).toUpperCase()
                                source: row.node.cover
                            }
                            Tick {
                                anchors { horizontalCenter: face.right; verticalCenter: face.top; horizontalCenterOffset: -Theme.px(8); verticalCenterOffset: Theme.px(8) }
                                on: row.node.checked
                                onToggled: page.share.toggle(row.node.nodeIndex)
                            }
                            Column {
                                anchors { left: face.right; leftMargin: Theme.space3; right: expand.left; rightMargin: Theme.space2; verticalCenter: parent.verticalCenter }
                                ElideText { width: parent.width; text: row.node.label; font.pixelSize: Theme.fontCaption; font.weight: Font.Bold }
                                ElideText {
                                    width: parent.width
                                    text: row.node.title
                                    font.pixelSize: Theme.fontDisplay
                                    font.weight: Font.Black
                                    color: row.node.hidden ? Theme.disabled : Theme.text
                                }
                            }
                            IconButton {
                                id: expand
                                anchors { right: parent.right; bottom: parent.bottom }
                                icon: row.node.expanded ? "chevron-up" : "chevron-down"
                                iconSize: Theme.iconSmall
                                padding: Theme.space1
                                tooltip: row.node.expanded ? qsTr("Show less") : qsTr("Show more")
                                onClicked: page.share.toggleExpanded(row.node.nodeIndex)
                            }
                        }
                        Row {
                            visible: row.node.expanded
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
                                active: !row.node.hidden && !(page.importing && row.node.existing)
                                onClicked: page.share.setHidden(row.node.nodeIndex, true)
                            }
                            PillButton {
                                text: qsTr("Show")
                                active: row.node.hidden && !(page.importing && row.node.existing)
                                onClicked: page.share.setHidden(row.node.nodeIndex, false)
                            }
                        }
                        ChipGroup { visible: row.node.expanded && row.node.genres.length > 0; heading: qsTr("Genres"); names: row.node.genres }
                        ChipGroup { visible: row.node.expanded && row.node.languages.length > 0; heading: qsTr("Lyric language"); names: row.node.languages }
                        ChipGroup { visible: row.node.expanded && row.node.moods.length > 0; heading: qsTr("Mood"); names: row.node.moods }
                    }
                }
            }

            // ---- an album or a playlist, with its tracks
            Component {
                id: cardComponent
                Card {
                    height: cardColumn.implicitHeight + Theme.space3 * 2
                    opacity: row.node.existing && row.node.type === 1 ? 0.55 : 1
                    Column {
                        id: cardColumn
                        x: Theme.space3
                        y: Theme.space3
                        width: parent.width - Theme.space3 * 2
                        spacing: Theme.space2
                        Item {
                            width: parent.width
                            height: Theme.px(96)
                            Avatar {
                                id: cover
                                width: Theme.px(96)
                                height: width
                                round: false
                                source: row.node.cover
                            }
                            Column {
                                anchors { left: cover.right; leftMargin: Theme.space3; right: tickBox.left; rightMargin: Theme.space2; verticalCenter: parent.verticalCenter }
                                spacing: Theme.space1
                                ElideText { width: parent.width; text: row.node.label; font.pixelSize: Theme.fontCaption; font.weight: Font.Bold }
                                ElideText { width: parent.width; text: row.node.title; font.pixelSize: Theme.fontDisplay; font.weight: Font.Black }
                                Flow {
                                    width: parent.width
                                    spacing: Theme.space1
                                    Repeater {
                                        model: row.node.authors
                                        delegate: Chip {
                                            required property string modelData
                                            text: modelData
                                            avatarLetter: modelData.substring(0, 1).toUpperCase()
                                        }
                                    }
                                }
                            }
                            Tick {
                                id: tickBox
                                anchors { right: parent.right; top: parent.top }
                                on: row.node.checked
                                onToggled: page.share.toggle(row.node.nodeIndex)
                            }
                        }
                        // what the library says about the item, when it is known (import)
                        Text {
                            visible: page.importing && row.node.existing
                            width: parent.width
                            text: row.node.type === 1 ? qsTr("Already in your library: it is skipped.") : qsTr("A playlist with this name exists: this one is added as a copy.")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontCaption
                            wrapMode: Text.WordWrap
                        }
                        Row {
                            visible: row.node.expanded && row.node.dateText.length > 0
                            spacing: Theme.space2
                            Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Release date"); color: Theme.text; font.pixelSize: Theme.fontHeading; font.weight: Font.Bold }
                            Chip { anchors.verticalCenter: parent.verticalCenter; text: row.node.dateText }
                        }
                        ChipGroup { visible: row.node.expanded && row.node.genres.length > 0; heading: qsTr("Genres"); names: row.node.genres }
                        ChipGroup { visible: row.node.expanded && row.node.languages.length > 0; heading: qsTr("Lyric language"); names: row.node.languages }
                        ChipGroup { visible: row.node.expanded && row.node.moods.length > 0; heading: qsTr("Mood"); names: row.node.moods }

                        // the table of tracks
                        Card {
                            visible: row.node.expanded && row.node.tracks.length > 0
                            width: parent.width
                            height: visible ? trackColumn.implicitHeight + Theme.space2 : 0
                            Column {
                                id: trackColumn
                                y: Theme.space1
                                width: parent.width
                                Item {
                                    width: parent.width
                                    height: Theme.px(28)
                                    Text { x: Theme.space3; anchors.verticalCenter: parent.verticalCenter; text: "#"; color: Theme.text; font.pixelSize: Theme.fontCaption; font.weight: Font.Bold }
                                    Text { x: Theme.space3 + Theme.px(28); anchors.verticalCenter: parent.verticalCenter; text: qsTr("Title"); color: Theme.text; font.pixelSize: Theme.fontCaption; font.weight: Font.Bold }
                                    Text { anchors.right: parent.right; anchors.rightMargin: Theme.px(44); anchors.verticalCenter: parent.verticalCenter; text: qsTr("Time"); color: Theme.text; font.pixelSize: Theme.fontCaption; font.weight: Font.Bold }
                                    Rectangle { anchors.bottom: parent.bottom; width: parent.width - Theme.space3 * 2; x: Theme.space3; height: 1; color: Theme.hairline }
                                }
                                Repeater {
                                    model: row.node.tracks
                                    delegate: Item {
                                        id: trackRow
                                        required property var modelData
                                        width: trackColumn.width
                                        height: Theme.px(44)
                                        opacity: modelData.existing ? 0.5 : 1
                                        Text { x: Theme.space3; width: Theme.px(24); anchors.verticalCenter: parent.verticalCenter; text: trackRow.modelData.position; color: Theme.text; font.pixelSize: Theme.fontCaption }
                                        Avatar {
                                            id: tcover
                                            x: Theme.space3 + Theme.px(28)
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: Theme.px(30)
                                            height: width
                                            round: false
                                            source: trackRow.modelData.cover
                                        }
                                        Column {
                                            anchors { left: tcover.right; leftMargin: Theme.space2; right: ttime.left; rightMargin: Theme.space2; verticalCenter: parent.verticalCenter }
                                            ElideText { width: parent.width; text: trackRow.modelData.title; font.weight: Font.Bold }
                                            ElideText { width: parent.width; text: trackRow.modelData.subtitle; font.pixelSize: Theme.fontCaption; color: Theme.textSecondary }
                                        }
                                        Text {
                                            id: ttime
                                            anchors { right: ttick.left; rightMargin: Theme.space2; verticalCenter: parent.verticalCenter }
                                            text: Format.time(trackRow.modelData.durationMs)
                                            color: Theme.text
                                            font.pixelSize: Theme.fontCaption
                                        }
                                        Tick {
                                            id: ttick
                                            anchors { right: parent.right; rightMargin: Theme.space3; verticalCenter: parent.verticalCenter }
                                            on: trackRow.modelData.checked
                                            enabledTick: !trackRow.modelData.existing
                                            onToggled: if (!trackRow.modelData.existing) page.share.toggle(trackRow.modelData.index)
                                        }
                                    }
                                }
                            }
                        }
                        Item {
                            width: parent.width
                            height: Theme.px(24)
                            IconButton {
                                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                                icon: row.node.expanded ? "chevron-up" : "chevron-down"
                                iconSize: Theme.iconSmall
                                padding: Theme.space1
                                tooltip: row.node.expanded ? qsTr("Show less") : qsTr("Show more")
                                onClicked: page.share.toggleExpanded(row.node.nodeIndex)
                            }
                        }
                    }
                }
            }
        }
    }

    ElideText {
        anchors.centerIn: list
        visible: !page.share.ready
        color: Theme.textSecondary
        text: qsTr("Loading...")
    }

    Item {
        id: footer
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: Theme.px(56)
        Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.hairline }
        ThinSlider {
            anchors { left: parent.left; leftMargin: Theme.space4; right: buttons.left; rightMargin: Theme.space4; verticalCenter: parent.verticalCenter }
            visible: page.share.busy
            enabled: false
            showKnob: false
            value: page.share.progress
        }
        Row {
            id: buttons
            anchors { right: parent.right; rightMargin: Theme.space4; verticalCenter: parent.verticalCenter }
            spacing: Theme.space2
            IconButton {
                icon: "close"
                outlined: true
                tooltip: qsTr("Cancel")
                iconSize: Theme.iconSmall
                padding: Theme.space2
                onClicked: page.app.nav.back()
            }
            IconButton {
                icon: page.importing ? "download" : "share"
                outlined: true
                tooltip: page.importing ? qsTr("Add to the library") : qsTr("Save the file")
                iconSize: Theme.iconSmall
                padding: Theme.space2
                active: page.share.canGo
                onClicked: page.importing ? page.share.importNow() : page.openSaveDialog()
            }
        }
    }
}
