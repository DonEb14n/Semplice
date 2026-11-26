import QtQuick

// Horizontal slider (volume, progress), Figma "Buttons conditions": a thick rounded track with a dark fill;
// while the mouse is over it a knob and a speech bubble show the value under the pointer.
// `value` is 0..1; `moved` is emitted while the user drags, `committed` on release.
Item {
    id: root

    property real value: 0
    property real hoverValue: -1          // 0..1 under the mouse, -1 when outside
    property bool showKnob: true
    readonly property bool pressed: mouse.pressed
    readonly property bool hovered: mouse.containsMouse
    // the bubble above the pointer (progress bar: the time); empty = none
    property string bubbleText: ""
    signal moved(real value)
    signal committed(real value)

    implicitHeight: Theme.px(20)
    implicitWidth: Theme.px(100)

    Rectangle {
        id: track
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        height: 8
        radius: 4
        color: Theme.sliderTrack
        border.width: 1
        border.color: Theme.border

        Rectangle {
            width: Math.max(parent.height, parent.width * Math.max(0, Math.min(1, root.value)))
            height: parent.height
            radius: parent.radius
            visible: root.value > 0 || root.enabled
            color: root.enabled ? Theme.avatarBackground : Theme.disabled
        }
    }

    // knob at the current value while hovering or dragging
    Rectangle {
        visible: root.showKnob && root.enabled && (root.hovered || root.pressed)
        width: 12
        height: 12
        radius: 6
        color: Theme.avatarBackground
        x: Math.max(0, Math.min(root.width, root.width * root.value)) - width / 2
        anchors.verticalCenter: parent.verticalCenter
    }

    // bubble with a small tail (the "2:00" of the design)
    Item {
        visible: root.bubbleText.length > 0 && root.hoverValue >= 0 && root.enabled
        x: Math.max(0, Math.min(root.width - width, root.hoverValue * root.width - width / 2))
        y: -height - 2
        width: bubble.width
        height: bubble.height + 5
        Rectangle {
            id: bubble
            width: bubbleLabel.implicitWidth + Theme.space3
            height: bubbleLabel.implicitHeight + Theme.space1 * 2
            radius: Theme.radiusControl
            color: Theme.surface
            border.color: Theme.border
            Text {
                id: bubbleLabel
                anchors.centerIn: parent
                text: root.bubbleText
                color: Theme.text
                font.pixelSize: Theme.fontBody
                font.weight: Font.Bold
            }
        }
        Rectangle {  // the tail
            width: 8
            height: 8
            rotation: 45
            color: Theme.surface
            border.color: Theme.border
            x: Math.max(4, Math.min(parent.width - 12, root.hoverValue * root.width - parent.x - 4))
            y: bubble.height - 5
            z: -1
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        enabled: root.enabled
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        function at(x) { return Math.max(0, Math.min(1, x / width)) }
        onPositionChanged: (e) => {
            root.hoverValue = at(e.x)
            if (pressed)
                root.moved(at(e.x))
        }
        onPressed: (e) => root.moved(at(e.x))
        onReleased: (e) => root.committed(at(e.x))
        onExited: root.hoverValue = -1
    }
}
