import QtQuick

// One entry of the library lists in the three view modes (UI doc p.5-16):
//   list (0): picture + title (+ artist line), large (1): 2 per row with texts, extra large (2): 3 per row, picture only.
Item {
    id: root

    required property int tab          // 0 playlists, 1 artists, 2 albums
    required property int mode
    required property real cellWidth
    property string cover: ""
    property string title: ""
    property string subtitle: ""
    property string letter: ""
    property bool dimmed: false       // an artist hidden by hand: grey name

    signal playRequested
    signal openRequested
    signal openOwnerRequested

    readonly property bool round: tab === 1
    readonly property bool showTexts: mode !== 2
    readonly property real pictureSize: mode === 0 ? 40 : Math.max(0, cellWidth - Theme.space3 * 2)

    implicitWidth: cellWidth
    implicitHeight: mode === 0 ? 52 : pictureSize + (showTexts ? textColumn.implicitHeight + Theme.space2 : 0) + Theme.space3 * 2

    Avatar {
        id: picture
        x: mode === 0 ? Theme.space3 : Theme.space3
        y: mode === 0 ? (root.height - height) / 2 : Theme.space3
        width: root.pictureSize
        height: root.pictureSize
        round: root.round
        source: root.cover
        letter: root.tab === 1 ? root.letter : ""

        Rectangle {
            anchors.fill: parent
            radius: root.round ? width / 2 : Theme.radiusControl
            color: Theme.overlay
            visible: hover.hovered
            Icon {
                anchors.centerIn: parent
                name: "play"
                size: Math.max(Theme.iconMedium, parent.width * 0.3)
                color: Theme.onImage
            }
        }
        HoverHandler {
            id: hover
            cursorShape: Qt.PointingHandCursor
        }
        TapHandler {
            onTapped: root.playRequested()
        }
    }

    Column {
        id: textColumn
        visible: root.showTexts
        x: root.mode === 0 ? picture.x + picture.width + Theme.space3 : picture.x
        y: root.mode === 0 ? (root.height - height) / 2 : picture.y + picture.height + Theme.space2
        width: root.mode === 0 ? root.width - x - Theme.space3 : picture.width
        spacing: 0

        ElideText {
            width: parent.width
            text: root.title
            font.weight: Font.DemiBold
            color: root.dimmed ? Theme.disabled : Theme.text
            horizontalAlignment: root.mode === 0 ? Text.AlignLeft : Text.AlignHCenter
            underlineOnHover: true
            onClicked: root.openRequested()
        }
        ElideText {
            width: parent.width
            visible: root.subtitle.length > 0
            text: root.subtitle
            color: Theme.textSecondary
            font.pixelSize: Theme.fontCaption
            horizontalAlignment: root.mode === 0 ? Text.AlignLeft : Text.AlignHCenter
            underlineOnHover: true
            onClicked: root.openOwnerRequested()
        }
    }
}
