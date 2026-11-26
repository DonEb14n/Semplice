import QtQuick

// One track line of the queue (Figma "Support panel"): small cover, title in bold, artist below. The current
// track sits on a raised card, moved a little to the right.
Item {
    id: root

    property string cover: ""
    property string title: ""
    property string artist: ""
    property string duration: ""
    property bool current: false
    signal clicked

    implicitHeight: Theme.px(52)
    implicitWidth: Theme.px(200)

    Rectangle {
        id: card
        visible: root.current || (hover.hovered && !root.current)
        anchors.fill: parent
        anchors.leftMargin: root.current ? Theme.space4 : 0
        radius: Theme.radiusControl
        color: root.current ? Theme.highlight : "transparent"
        border.width: root.current ? 1 : 0
        border.color: Theme.border
    }

    Avatar {
        id: picture
        x: card.anchors.leftMargin + Theme.space1
        anchors.verticalCenter: parent.verticalCenter
        width: Theme.px(44)
        height: Theme.px(44)
        round: false
        source: root.cover
    }

    Text {
        id: length
        visible: root.duration.length > 0
        anchors {
            right: parent.right
            rightMargin: Theme.space2
            verticalCenter: parent.verticalCenter
        }
        text: root.duration
        color: Theme.textSecondary
        font.pixelSize: Theme.fontCaption
    }

    Column {
        anchors {
            left: picture.right
            leftMargin: Theme.space2
            right: length.visible ? length.left : parent.right
            rightMargin: Theme.space2
            verticalCenter: parent.verticalCenter
        }
        ElideText {
            width: parent.width
            text: root.title
            font.weight: Font.Bold
            scrollOnHover: true
            scrollBackground: root.current ? Theme.highlight : Theme.surface
        }
        ElideText {
            width: parent.width
            text: root.artist
            scrollOnHover: true
            scrollBackground: root.current ? Theme.highlight : Theme.surface
            visible: text.length > 0
        }
    }

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }
    TapHandler {
        onTapped: root.clicked()
    }
}
