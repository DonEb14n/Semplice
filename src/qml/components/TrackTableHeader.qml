import QtQuick

// The header line of a track table ("#", "Title", "Album", "Time"). Every label sits over the column it names: the
// positions follow those of TrackRowItem (number, cover, album text, time, the "..." button).
Item {
    id: root

    property bool showAlbum: false

    readonly property real moreWidth: Theme.iconSmall + Theme.space1 * 2
    readonly property real timeX: width - Theme.space3 - moreWidth - Theme.space1 - Theme.px(44)
    readonly property real albumWidth: Math.max(80, width * 0.22)

    height: Theme.px(24)

    component HeaderText: Text {
        color: Theme.text
        font.pixelSize: Theme.fontBody
        font.weight: Font.Bold
        anchors.verticalCenter: parent.verticalCenter
    }

    HeaderText { x: Theme.space4; text: "#" }
    HeaderText { x: Theme.space4 + Theme.px(24) + Theme.space2; text: qsTr("Title") }
    HeaderText {
        visible: root.showAlbum
        x: root.timeX - Theme.space3 - root.albumWidth
        text: qsTr("Album")
    }
    HeaderText { x: root.timeX; text: qsTr("Time") }
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.hairline }
}
