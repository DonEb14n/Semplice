import QtQuick
import QtQuick.Controls.Basic

// Round/square icon-only button. M0: visual placeholder, emits `clicked` for later milestones.
Item {
    id: root

    property string icon: ""
    property string tooltip: ""
    property int iconSize: Theme.iconMedium
    property bool active: true
    property bool outlined: false
    property bool dimmed: false          // looks switched off but still reacts (shuffle, repeat)
    property int padding: Theme.space3
    property color tint: Theme.text      // Theme.danger for destructive buttons
    signal clicked

    implicitWidth: iconSize + padding * 2
    implicitHeight: iconSize + padding * 2

    scale: root.active && mouse.containsMouse ? Theme.hoverScale : 1
    Behavior on scale { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutQuad } }

    // keyboard: Tab reaches it, Space or Enter presses it
    activeFocusOnTab: root.active
    Keys.onSpacePressed: if (root.active) root.clicked()
    Keys.onReturnPressed: if (root.active) root.clicked()
    Rectangle {
        anchors.fill: parent
        anchors.margins: -Theme.space1
        radius: Theme.radiusPill
        color: "transparent"
        border.width: Theme.focusRingWidth
        border.color: Theme.focusRing
        visible: root.activeFocus
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusPill
        color: "transparent"
        border.width: root.outlined ? Theme.strokeWidth : 0
        border.color: (root.active && !root.dimmed) ? root.tint : Theme.disabled
    }

    Icon {
        anchors.centerIn: parent
        name: root.icon
        size: root.iconSize
        color: (root.active && !root.dimmed) ? root.tint : Theme.disabled
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        enabled: root.active
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }

    // the tooltip lives in the window overlay, so no other element can cover it
    ToolTip {
        visible: root.tooltip.length > 0 && mouse.containsMouse
        delay: 400
        text: root.tooltip
        background: Rectangle {
            color: Theme.text
            radius: Theme.radiusControl
        }
        contentItem: Text {
            text: root.tooltip
            color: Theme.surface
            font.pixelSize: Theme.fontCaption
        }
    }
}
