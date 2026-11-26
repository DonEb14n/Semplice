import QtQuick

// A rounded button with an icon and a text ("Edit", "Add to Discography", "Select folder", ...). The width follows
// the text, so translations never overflow.
Rectangle {
    id: root

    property string icon: ""
    property string text: ""
    property bool active: true
    property color tint: Theme.text
    signal clicked

    implicitHeight: Theme.controlHeight
    implicitWidth: row.implicitWidth + Theme.space4

    activeFocusOnTab: root.active
    Keys.onSpacePressed: if (root.active) root.clicked()
    Keys.onReturnPressed: if (root.active) root.clicked()
    radius: Theme.radiusPill
    color: "transparent"
    border.width: root.activeFocus ? Theme.focusRingWidth : Theme.strokeWidth
    scale: root.active && hover.hovered ? Theme.hoverScale : 1
    Behavior on scale { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutQuad } }
    border.color: root.activeFocus ? Theme.focusRing : (root.active ? root.tint : Theme.disabled)

    Row {
        id: row
        anchors.centerIn: parent
        spacing: Theme.space1
        Icon {
            visible: root.icon.length > 0
            anchors.verticalCenter: parent.verticalCenter
            name: root.icon
            size: Theme.iconSmall
            color: root.active ? root.tint : Theme.disabled
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            color: root.active ? root.tint : Theme.disabled
            font.pixelSize: Theme.fontBody
            font.weight: Font.Medium
        }
    }
    HoverHandler {
        id: hover
        cursorShape: root.active ? Qt.PointingHandCursor : Qt.ArrowCursor
    }
    TapHandler {
        enabled: root.active
        onTapped: if (Theme.tapAllowed(root)) root.clicked()
    }
}
