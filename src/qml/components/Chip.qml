import QtQuick

// A small rounded tag: an artist of an album, a genre tag. With `removable` it has a little x.
Rectangle {
    id: root

    property string text: ""
    property bool removable: false
    property string avatarLetter: ""     // set: a small round letter in front (artist chips)
    property string avatarSource: ""
    property bool clickable: false
    property bool reorderable: false     // drag the chip to another place of its Flow
    property int chipIndex: -1           // the place in the Flow (the model row), needed with reorderable
    signal moveRequested(int target)
    signal removeRequested
    signal clicked

    implicitHeight: Math.round(Theme.controlHeight * 0.85)
    implicitWidth: row.implicitWidth + Theme.space3
    radius: Theme.radiusControl
    color: Theme.surface
    border.width: 1
    border.color: Theme.border

    Row {
        id: row
        anchors.centerIn: parent
        spacing: Theme.space1
        Avatar {
            visible: root.avatarLetter.length > 0
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            height: 16
            letter: root.avatarLetter
            source: root.avatarSource
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            color: Theme.text
            font.pixelSize: Theme.fontCaption
            elide: Text.ElideRight
            width: Math.min(implicitWidth, 160)
        }
        IconButton {
            visible: root.removable
            anchors.verticalCenter: parent.verticalCenter
            icon: "close"
            iconSize: 10
            padding: 2
            tooltip: qsTr("Remove")
            onClicked: root.removeRequested()
        }
    }
    z: reorder.active ? 10 : 0
    opacity: reorder.active ? 0.85 : 1
    DragHandler {
        id: reorder
        enabled: root.reorderable
        target: root
        cursorShape: Qt.SizeAllCursor
        onActiveChanged: {
            if (active || !root.parent)
                return
            // dropped on another chip: that is the new place; anywhere else the chip goes back
            const cx = root.x + root.width / 2
            const cy = root.y + root.height / 2
            let target = -1
            for (const c of root.parent.children) {
                if (c !== root && c.chipIndex !== undefined && c.chipIndex >= 0 && cx >= c.x && cx <= c.x + c.width && cy >= c.y && cy <= c.y + c.height)
                    target = c.chipIndex
            }
            if (target >= 0)
                root.moveRequested(target)
            else if (root.parent.forceLayout)
                root.parent.forceLayout()
        }
    }
    TapHandler {
        enabled: root.clickable
        onTapped: root.clicked()
    }
    HoverHandler { cursorShape: root.clickable ? Qt.PointingHandCursor : Qt.ArrowCursor }
}
