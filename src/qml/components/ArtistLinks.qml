import QtQuick

// The artists of a track: one link per artist ("Птаха, Slim" = two links), separated by commas. A row too long for
// the space is cut at the right edge (the full text is in the tooltip of the title line).
Item {
    id: root

    property var artists: []          // [{ id, name }]
    property int pixelSize: Theme.fontCaption
    property int weight: Font.Normal
    property color color: Theme.text
    property bool centered: false     // the names sit in the middle of the width
    signal artistClicked(var id)

    implicitWidth: row.implicitWidth
    implicitHeight: row.implicitHeight
    clip: true

    Row {
        id: row
        x: root.centered ? Math.max(0, (root.width - implicitWidth) / 2) : 0
        Repeater {
            model: root.artists
            delegate: Row {
                id: item
                required property var modelData
                required property int index
                ElideText {
                    text: item.modelData.name
                    width: Math.min(implicitWidth, root.width)
                    font.pixelSize: root.pixelSize
                    font.weight: root.weight
                    color: root.color
                    tooltipEnabled: false
                    underlineOnHover: item.modelData.id > 0
                    onClicked: root.artistClicked(item.modelData.id)
                }
                Text {
                    visible: item.index < root.artists.length - 1
                    text: ", "
                    font.pixelSize: root.pixelSize
                    font.weight: root.weight
                    color: root.color
                }
            }
        }
    }
}
