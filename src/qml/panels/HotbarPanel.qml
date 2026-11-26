import QtQuick
import QtQuick.Controls.Basic

// Hotbar (UI doc p.17-18, Figma "Hotbat all conditions"). Its controls line up with the columns above:
// search under the library, Home and the history arrows at the left edge of the middle window, Settings at its
// right edge, volume at the left edge of the right column and the queue button at the far right.
Panel {
    id: panel

    required property var app
    // widths of the three columns above (set by Main.qml)
    property real libraryX: 0
    property real libraryWidth: 300
    property real middleX: 0
    property real middleWidth: 500
    property real rightX: 0
    property real rightWidth: 300
    property bool collapsed: false      // the player is collapsed: its controls and progress bar are here, between Home and Settings
    readonly property var nav: app.nav
    readonly property var player: app.player
    readonly property alias searchField: field

    function focusSearch() {
        field.forceActiveFocus()
        field.selectAll()
    }

    // ---- search (under the library column)
    Rectangle {
        x: panel.libraryX + Theme.space3 - 2
        width: panel.libraryWidth - Theme.space3 * 2 + 4
        height: Theme.px(32)
        anchors.verticalCenter: parent.verticalCenter
        radius: Theme.radiusPill
        color: "transparent"
        border.width: 1
        border.color: field.activeFocus ? Theme.text : Theme.hairline

        Icon {
            id: searchIcon
            anchors {
                left: parent.left
                leftMargin: Theme.space3
                verticalCenter: parent.verticalCenter
            }
            name: "search"
            size: Theme.iconSmall
            color: Theme.text
        }
        TextField {
            id: field
            anchors {
                left: searchIcon.right
                leftMargin: Theme.space2
                right: clearButton.left
                verticalCenter: parent.verticalCenter
            }
            background: null
            padding: 0
            color: Theme.text
            font.pixelSize: Theme.fontHeading
            selectByMouse: true
            clip: true
            text: panel.app.search.text
            // the results show only while there is text; focus alone (also when the window is activated again) opens nothing
            onTextEdited: {
                panel.app.search.text = text
                panel.app.setPopup(text.trim().length > 0 ? "search" : "")
            }
            onPressed: if (text.trim().length > 0) panel.app.setPopup("search")
            Keys.onEscapePressed: {
                panel.app.closePopups()
                focus = false
            }
        }
        IconButton {
            id: clearButton
            anchors {
                right: parent.right
                rightMargin: Theme.space1
                verticalCenter: parent.verticalCenter
            }
            visible: field.text.length > 0
            width: visible ? implicitWidth : 0
            icon: "close"
            iconSize: Theme.iconSmall
            padding: Theme.space1
            tooltip: qsTr("Clear")
            onClicked: {
                panel.app.search.text = ""
                panel.app.closePopups()
                field.forceActiveFocus()
            }
        }
    }

    // ---- navigation of the middle window
    Row {
        x: panel.middleX + Theme.space2
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.space1
        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            icon: "home"
            tooltip: qsTr("Home")
            outlined: true
            padding: Theme.space2
            onClicked: {
                panel.app.closePopups()
                panel.nav.home()
            }
        }
        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            icon: "back"
            tooltip: qsTr("Back")
            active: panel.nav.canBack
            iconSize: Theme.iconSmall
            padding: Theme.space2
            onClicked: {
                panel.app.closePopups()
                panel.nav.back()
            }
        }
        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            icon: "forward"
            tooltip: qsTr("Forward")
            active: panel.nav.canForward
            iconSize: Theme.iconSmall
            padding: Theme.space2
            onClicked: {
                panel.app.closePopups()
                panel.nav.forward()
            }
        }
    }
    IconButton {
        x: panel.middleX + panel.middleWidth - width - Theme.space2
        anchors.verticalCenter: parent.verticalCenter
        icon: "settings"
        tooltip: qsTr("Settings")
        outlined: true
        padding: Theme.space2
        onClicked: {
            panel.app.closePopups()
            // on the settings page the same button goes back to where the user came from; what was changed there is kept
            if (panel.nav.kind === 1) {
                if (panel.app.settingsVm.dirty)
                    panel.app.settingsVm.save()
                if (panel.nav.canBack)
                    panel.nav.back()
                else
                    panel.nav.home()
            } else {
                panel.nav.open(1, 0)
            }
        }
    }

    // ---- the controls of the collapsed player (the free space between the history arrows and Settings)
    Item {
        id: miniPlayer
        opacity: panel.collapsed ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: Theme.durationNormal * 1.4 } }
        x: panel.middleX + Theme.px(130)
        width: Math.max(0, panel.middleWidth - Theme.px(130) - Theme.px(56))
        height: parent.height
        Row {
            id: miniControls
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space1
            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                icon: "shuffle"
                tooltip: qsTr("Shuffle")
                iconSize: Theme.iconSmall
                padding: Theme.space1
                dimmed: !panel.player.shuffle
                active: panel.player.hasTrack
                onClicked: panel.player.toggleShuffle()
            }
            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                icon: "previous"
                tooltip: qsTr("Previous")
                iconSize: Theme.iconSmall
                padding: Theme.space1
                active: panel.player.canPrevious
                onClicked: panel.player.previous()
            }
            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                icon: panel.player.playing ? "pause" : "play"
                tooltip: panel.player.playing ? qsTr("Pause") : qsTr("Play")
                outlined: true
                iconSize: Theme.iconSmall
                padding: Theme.space2
                active: panel.player.hasTrack
                onClicked: panel.player.toggle()
            }
            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                icon: "next"
                tooltip: qsTr("Next")
                iconSize: Theme.iconSmall
                padding: Theme.space1
                active: panel.player.canNext
                onClicked: panel.player.next()
            }
            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                icon: panel.player.repeat === 2 ? "repeat-one" : "repeat"
                tooltip: panel.player.repeat === 0 ? qsTr("Repeat: off") : panel.player.repeat === 1 ? qsTr("Repeat: all") : qsTr("Repeat: current track")
                iconSize: Theme.iconSmall
                padding: Theme.space1
                dimmed: panel.player.repeat === 0
                active: panel.player.hasTrack
                onClicked: panel.player.cycleRepeat()
            }
        }
        TextMetrics {
            id: miniMetrics
            font.pixelSize: Theme.fontCaption
            text: panel.player.duration >= 3600000 ? "0:00:00" : "00:00"
        }
        Text {
            id: miniLeft
            anchors { left: miniControls.right; leftMargin: Theme.space2; verticalCenter: parent.verticalCenter }
            width: Math.ceil(miniMetrics.advanceWidth)
            text: Format.time(miniSlider.pressed ? miniSlider.dragValue * panel.player.duration : panel.player.position)
            color: Theme.text
            font.pixelSize: Theme.fontCaption
        }
        Text {
            id: miniRight
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            width: Math.ceil(miniMetrics.advanceWidth)
            text: Format.time(panel.player.duration)
            horizontalAlignment: Text.AlignRight
            color: Theme.text
            font.pixelSize: Theme.fontCaption
        }
        ThinSlider {
            id: miniSlider
            property real dragValue: 0
            anchors { left: miniLeft.right; right: miniRight.left; leftMargin: Theme.space2; rightMargin: Theme.space2; verticalCenter: parent.verticalCenter }
            enabled: panel.player.hasTrack && panel.player.duration > 0
            value: pressed ? dragValue : (panel.player.duration > 0 ? panel.player.position / panel.player.duration : 0)
            bubbleText: Format.time(Math.max(0, hoverValue) * panel.player.duration)
            onMoved: (v) => dragValue = v
            onCommitted: (v) => panel.player.seek(Math.round(v * panel.player.duration))
        }
    }

    // ---- volume (under the right column)
    Row {
        x: panel.rightX + Theme.space2
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.space1
        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            icon: (panel.player.muted || panel.player.volume <= 0) ? "volume-mute" : panel.player.volume <= 0.5 ? "volume-low" : "volume"
            tooltip: panel.player.muted ? qsTr("Unmute") : qsTr("Mute")
            iconSize: Theme.iconSmall
            padding: Theme.space2
            onClicked: panel.player.toggleMute()
        }
        ThinSlider {
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(40, (panel.rightWidth - Theme.space2 * 2 - 40 - 60 - Theme.space4 * 2) / 2)
            value: panel.player.muted ? 0 : panel.player.volume
            onMoved: (v) => panel.player.setVolume(v)
        }
    }

    // ---- queue (far right)
    IconButton {
        x: panel.rightX + panel.rightWidth - width - Theme.space2   // the right edge of the player column, wherever the column stands
        anchors.verticalCenter: parent.verticalCenter
        icon: "queue"
        tooltip: qsTr("Queue")
        active: panel.player.queueAvailable
        outlined: true
        padding: Theme.space2
        onClicked: panel.app.setPopup(panel.app.popup === "queue" ? "" : "queue")
    }
}
