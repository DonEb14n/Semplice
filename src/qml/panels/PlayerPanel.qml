import QtQuick
import QtQuick.Controls.Basic

// Player panel (UI doc p.20-22, Figma "Player panel all conditions"): source header, cover, title and
// artists, progress bar, controls.
Panel {
    id: panel

    required property var app
    readonly property var player: app.player
    readonly property int pad: Theme.space3

    titleLine: false
    property bool collapsed: false      // the controls and the progress bar are in the hotbar, the queue gets the room
    signal toggleRequested
    // how much lower the panel is when collapsed: the controls, the progress bar and the gap between them
    readonly property real foldedSaving: controls.height + progress.height + Theme.space1
    // while a popup (queue, search) is open its rows may lie over the title and the artists: those links do not react then
    readonly property bool linksActive: app.popup === ""

    // When the panel is taller than "cover + title + controls" (full screen), the group is centred instead of leaving a gap
    // between the cover and the title: half of the spare height goes above the cover, half below the controls.
    readonly property real spare: panel.collapsed ? 0 : Math.max(0, panel.height - header.height - trackLine.height - progress.height - controls.height
                                           - (width - panel.pad * 2) - Theme.space3 * 2 - Theme.space2 - Theme.space1 * 2)

    // ---- header: where the music comes from; opens that view
    Column {
        id: header
        enabled: panel.linksActive
        anchors {
            top: parent.top
            topMargin: Theme.space3
            left: parent.left
            right: parent.right
            leftMargin: panel.pad
            rightMargin: panel.pad
        }
        ElideText {
            width: parent.width
            visible: panel.player.hasTrack
            text: panel.player.headerText
            font.pixelSize: Theme.fontTitle
            font.weight: Font.Medium
            underlineOnHover: panel.player.headerClickable
            onClicked: if (panel.player.headerClickable) panel.app.open(panel.player.headerKind(), panel.player.headerId(), panel.player.headerKey())
        }
        ElideText {
            width: parent.width
            visible: panel.player.hasTrack && panel.player.headerIsDiscography
            text: qsTr("Discography")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontCaption
        }
        ElideText {
            width: parent.width
            visible: !panel.player.hasTrack
            text: qsTr("Nothing is playing")
            font.pixelSize: Theme.fontTitle
            font.weight: Font.Medium
        }
    }

    // ---- bottom-up: controls, progress, title/artist; the cover takes what is left
    Row {
        id: controls
        opacity: panel.collapsed ? 0 : 1
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: Theme.durationNormal } }
        anchors {
            bottom: parent.bottom
            bottomMargin: Theme.space3 + panel.spare / 2
            horizontalCenter: parent.horizontalCenter
        }
        spacing: Theme.space2
        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            icon: "shuffle"
            tooltip: qsTr("Shuffle")
            iconSize: Theme.iconMedium
            padding: Theme.space2
            dimmed: !panel.player.shuffle
            active: panel.player.hasTrack
            onClicked: panel.player.toggleShuffle()
        }
        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            icon: "previous"
            tooltip: qsTr("Previous")
            outlined: true
            iconSize: Theme.iconSmall
            padding: Theme.space2
            active: panel.player.canPrevious
            onClicked: panel.player.previous()
        }
        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            icon: panel.player.playing ? "pause" : "play"
            tooltip: panel.player.playing ? qsTr("Pause") : qsTr("Play")
            outlined: true
            iconSize: Theme.iconMedium
            padding: Theme.space3
            active: panel.player.hasTrack
            onClicked: panel.player.toggle()
        }
        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            icon: "next"
            tooltip: qsTr("Next")
            outlined: true
            iconSize: Theme.iconSmall
            padding: Theme.space2
            active: panel.player.canNext
            onClicked: panel.player.next()
        }
        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            icon: panel.player.repeat === 2 ? "repeat-one" : "repeat"
            tooltip: panel.player.repeat === 0 ? qsTr("Repeat: off") : panel.player.repeat === 1 ? qsTr("Repeat: all") : qsTr("Repeat: current track")
            iconSize: Theme.iconMedium
            padding: Theme.space2
            dimmed: panel.player.repeat === 0
            active: panel.player.hasTrack
            onClicked: panel.player.cycleRepeat()
        }
    }

    // expanded: ^ at the bottom right (collapse); collapsed: v at the end of the title line (expand)
    IconButton {
        anchors {
            right: parent.right
            rightMargin: Theme.space1
            bottom: parent.bottom
            bottomMargin: panel.collapsed ? Theme.space3 : Theme.space1
        }
        icon: panel.collapsed ? "chevron-down" : "chevron-up"
        tooltip: panel.collapsed ? qsTr("Expand the player") : qsTr("Collapse the player")
        iconSize: Theme.iconSmall
        padding: Theme.space1
        onClicked: panel.toggleRequested()
    }

    // the two times always take the room of the widest time ("00:00"), so the bar between them keeps its length
    TextMetrics {
        id: timeMetrics
        font.pixelSize: Theme.fontBody
        text: panel.player.duration >= 3600000 ? "0:00:00" : "00:00"
    }

    Item {
        id: progress
        z: 5  // the bubble of the progress bar goes above the artist names, not below them
        opacity: panel.collapsed ? 0 : 1
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: Theme.durationNormal } }
        anchors {
            left: parent.left
            right: parent.right
            leftMargin: panel.pad
            rightMargin: panel.pad
            bottom: controls.top
            bottomMargin: Theme.space1
        }
        height: Theme.px(28)

        Text {
            id: timeLeft
            anchors.verticalCenter: parent.verticalCenter
            text: Format.time(slider.pressed ? slider.dragValue * panel.player.duration : panel.player.position)
            color: Theme.text
            font.pixelSize: Theme.fontBody
            width: Math.ceil(timeMetrics.advanceWidth)
        }
        Text {
            id: timeRight
            anchors {
                right: parent.right
                verticalCenter: parent.verticalCenter
            }
            text: Format.time(panel.player.duration)
            color: Theme.text
            font.pixelSize: Theme.fontBody
            horizontalAlignment: Text.AlignRight
            width: Math.ceil(timeMetrics.advanceWidth)
        }
        ThinSlider {
            id: slider
            property real dragValue: 0
            anchors {
                left: timeLeft.right
                right: timeRight.left
                leftMargin: Theme.space2
                rightMargin: Theme.space2
                verticalCenter: parent.verticalCenter
            }
            enabled: panel.player.hasTrack && panel.player.duration > 0
            value: pressed ? dragValue : (panel.player.duration > 0 ? panel.player.position / panel.player.duration : 0)
            bubbleText: Format.time(Math.max(0, hoverValue) * panel.player.duration)  // the time under the mouse
            onMoved: (v) => dragValue = v
            onCommitted: (v) => panel.player.seek(Math.round(v * panel.player.duration))
        }
    }

    Column {
        id: trackLine
        anchors {
            left: parent.left
            right: parent.right
            leftMargin: panel.pad + Theme.space1
            rightMargin: panel.pad
            bottom: panel.collapsed ? parent.bottom : progress.top
            bottomMargin: panel.collapsed ? Theme.space3 : 0
        }
        enabled: panel.linksActive
        ElideText {
            width: Math.min(implicitWidth, parent.width - (panel.collapsed ? Theme.px(28) : 0))   // the click area is the text itself
            text: panel.player.title
            font.pixelSize: Theme.fontBody  // the same size as the queue rows
            font.weight: Font.Bold
            underlineOnHover: panel.player.hasTrack
            onClicked: if (panel.player.hasTrack) panel.app.open(panel.player.titleKind(), panel.player.titleId())
        }
        ArtistLinks {
            width: parent.width
            visible: panel.player.artists.length > 0
            artists: panel.player.artists
            pixelSize: Theme.fontBody
            onArtistClicked: (id) => { if (id > 0) panel.app.open(2, id) }
        }
    }

    Item {
        id: coverArea
        anchors {
            top: header.bottom
            topMargin: Theme.space2 + panel.spare / 2
            bottom: trackLine.top
            bottomMargin: Theme.space1
            left: parent.left
            right: parent.right
            leftMargin: panel.pad
            rightMargin: panel.pad
        }
        Avatar {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.max(0, Math.min(parent.width, parent.height))
            height: width
            round: false
            letter: ""
            source: panel.player.coverUrl
        }
    }
}
