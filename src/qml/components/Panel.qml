import QtQuick

// A rounded white block with an optional title; children go into the body area below it.
Rectangle {
    id: panel

    property string title: ""
    property bool titleLine: true
    property alias titleItem: heading
    default property alias content: body.data
    readonly property alias body: body

    color: Theme.surface
    radius: Theme.radiusPanel

    Text {
        id: heading
        visible: panel.title.length > 0
        text: panel.title
        color: Theme.text
        font.pixelSize: Theme.fontTitle
        font.weight: Font.DemiBold
        elide: Text.ElideRight
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
            topMargin: Theme.space3
            leftMargin: Theme.space3
            rightMargin: Theme.space3
        }
        height: visible ? implicitHeight : 0
    }

    // the thin line under the title (Figma)
    Rectangle {
        visible: heading.visible && panel.titleLine
        anchors {
            top: heading.bottom
            topMargin: 2
            left: parent.left
            right: parent.right
            leftMargin: Theme.space3
            rightMargin: Theme.space3
        }
        height: 1
        color: Theme.hairline
    }

    Item {
        id: body
        anchors {
            top: heading.visible ? heading.bottom : parent.top
            topMargin: heading.visible ? Theme.space3 : 0
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
    }
}
