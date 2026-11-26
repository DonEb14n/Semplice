import QtQuick
import QtQuick.Controls.Basic

// Support panel "Queue" (UI doc p.19, Figma "Support panel"): the previous, current and next track; click a
// neighbour, drag the list up / down or turn the mouse wheel over it to change track.
Panel {
    id: panel

    required property var app
    readonly property var queue: app.queue

    title: qsTr("Queue")
    property bool wide: false      // the player is collapsed: five tracks (two before, two after) instead of three

    Item {
        id: lane
        anchors {
            fill: parent
            leftMargin: Theme.space3
            rightMargin: Theme.space3
            bottomMargin: Theme.space3
        }
        visible: panel.queue.hasCurrent
        clip: true
        property real dragOffset: 0

        Column {
            id: rows
            width: parent.width
            // the current track always sits in the middle of the panel height
            y: lane.height / 2 - (current.y + current.height / 2) + lane.dragOffset * 0.4
            spacing: Theme.space1

            QueueRow {
                width: parent.width
                opacity: panel.wide ? 1 : 0
                visible: opacity > 0 && panel.queue.hasPrevious2
                Behavior on opacity { NumberAnimation { duration: Theme.durationNormal } }
                cover: panel.queue.previous2Cover
                title: panel.queue.previous2Title
                artist: panel.queue.previous2Artist
                onClicked: panel.queue.playPrevious2()
            }
            QueueRow {
                width: parent.width
                visible: panel.queue.hasPrevious
                cover: panel.queue.previousCover
                title: panel.queue.previousTitle
                artist: panel.queue.previousArtist
                onClicked: panel.queue.playPrevious()
            }
            QueueRow {
                id: current
                width: parent.width
                current: true
                cover: panel.queue.currentCover
                title: panel.queue.currentTitle
                artist: panel.queue.currentArtist
            }
            QueueRow {
                width: parent.width
                visible: panel.queue.hasNext
                cover: panel.queue.nextCover
                title: panel.queue.nextTitle
                artist: panel.queue.nextArtist
                onClicked: panel.queue.playNext()
            }
            QueueRow {
                width: parent.width
                opacity: panel.wide ? 1 : 0
                visible: opacity > 0 && panel.queue.hasNext2
                Behavior on opacity { NumberAnimation { duration: Theme.durationNormal } }
                cover: panel.queue.next2Cover
                title: panel.queue.next2Title
                artist: panel.queue.next2Artist
                onClicked: panel.queue.playNext2()
            }
            Behavior on y {
                enabled: !drag.active
                NumberAnimation { duration: Theme.durationFast }
            }
        }

        // the wheel skips tracks: one notch down = the next track, one notch up = the previous one
        WheelHandler {
            id: wheel
            property real accumulated: 0
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: (event) => {
                accumulated += event.angleDelta.y
                if (Math.abs(accumulated) < 120)
                    return
                const down = accumulated < 0
                accumulated = 0
                if (down && panel.queue.hasNext)
                    panel.queue.playNext()
                else if (!down && panel.queue.hasPrevious)
                    panel.queue.playPrevious()
            }
        }

        // drag up = the next track comes up, drag down = the previous one (like the carousel in the middle window)
        DragHandler {
            id: drag
            target: null
            xAxis.enabled: false
            onTranslationChanged: lane.dragOffset = Math.max(-80, Math.min(80, translation.y))
            onActiveChanged: {
                if (!active) {
                    const moved = lane.dragOffset
                    lane.dragOffset = 0
                    if (moved <= -30 && panel.queue.hasNext)
                        panel.queue.playNext()
                    else if (moved >= 30 && panel.queue.hasPrevious)
                        panel.queue.playPrevious()
                }
            }
        }
    }
}
