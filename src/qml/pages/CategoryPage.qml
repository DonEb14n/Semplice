import QtQuick
import QtQuick.Controls.Basic

// A category as tiles (UI doc p.25): label, big title, statistics, the play button, then every item.
Item {
    id: page

    required property var app
    readonly property var category: app.category
    readonly property var typeNames: [qsTr("Single"), qsTr("Album"), qsTr("EP"), qsTr("Live"), qsTr("Compilation"), qsTr("Other")]
    readonly property bool artistPage: category.key.indexOf("artist:") === 0

    GridView {
        id: grid
        anchors.fill: parent
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: page.category.items
        cellWidth: 124   // small tiles that wrap, left aligned (Figma "Middle window")
        cellHeight: cellWidth + 52
        ScrollBar.vertical: ScrollBar {}

        header: Item {
            width: grid.width
            height: headerColumn.implicitHeight + Theme.space4

            Column {
                id: headerColumn
                x: Theme.space4
                width: parent.width - Theme.space4 * 2
                spacing: Theme.space1
                ElideText {
                    width: parent.width
                    text: qsTr("Carousel")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontCaption
                }
                ElideText {
                    width: parent.width
                    text: page.category.title
                    font.pixelSize: Theme.fontDisplay
                    font.weight: Font.Black
                }
                ElideText {
                    width: parent.width
                    text: page.category.stats
                    color: Theme.textSecondary
                    visible: page.category.ready
                }
                IconButton {
                    icon: "play"
                    tooltip: qsTr("Play")
                    outlined: true
                    active: page.category.ready
                    onClicked: page.category.playAll()
                }
            }
        }

        delegate: LibraryItem {
            required property var model
            required property int index
            width: grid.cellWidth
            height: grid.cellHeight
            cellWidth: grid.cellWidth
            tab: model.albumType < 0 ? 0 : 2
            mode: 1
            title: model.title
            // the discography page shows the release year and the type next to each other
            subtitle: page.artistPage && model.albumType >= 0 ? model.year + " · " + page.typeNames[model.albumType] : model.subtitle
            cover: model.cover
            onPlayRequested: page.category.playItem(index)
            onOpenRequested: page.app.open(model.albumType < 0 ? 4 : 3, model.itemId)
            onOpenOwnerRequested: if (!page.artistPage) page.app.open(2, model.ownerId)
        }
    }
}
