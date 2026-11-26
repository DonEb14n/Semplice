import QtQuick
import QtQuick.Controls.Basic

// The whole current queue, top to bottom (UI doc p.18, Figma "Hotbat all conditions"); click a row to play it.
Rectangle {
    id: popup

    required property var app
    readonly property var queue: app.queue

    color: Theme.surface
    radius: Theme.radiusPanel
    border.color: Theme.border

    // a click on the popup belongs to the popup: it must not reach the player panel behind it (a row near the top of the list
    // sits on top of the album / artist links of the player, and both reacted to the same click)
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: (wheel) => wheel.accepted = true   // the end of a list must not scroll the queue panel behind the popup
    }

    ElideText {
        id: heading
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
            margins: Theme.space3
        }
        text: qsTr("Queue")
        font.weight: Font.Bold
    }

    ElideText {
        anchors.centerIn: parent
        visible: popup.queue.count === 0
        color: Theme.textSecondary
        text: qsTr("The queue is empty")
    }

    ListView {
        id: list
        anchors {
            top: heading.bottom
            topMargin: Theme.space1
            left: parent.left
            right: parent.right
            bottom: parent.bottom
            leftMargin: Theme.space3
            rightMargin: Theme.space3
            bottomMargin: Theme.space3
        }
        clip: true
        model: popup.queue
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        onVisibleChanged: if (visible && popup.queue.currentRow >= 0) positionViewAtIndex(popup.queue.currentRow, ListView.Center)

        delegate: QueueRow {
            id: row
            required property int index
            required property var model
            width: list.width
            title: model.title
            artist: model.artist
            cover: model.cover
            current: model.isCurrent
            onClicked: popup.queue.playRow(row.index)
        }
    }
}
