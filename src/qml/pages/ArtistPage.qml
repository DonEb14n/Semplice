import QtQuick
import QtQuick.Controls.Basic

// Artist card (UI doc p.34-35, Figma "Artists"): picture, name, statistics, tags, play and Edit, then the
// carousels Discography / Albums / Singles / EP's / ... that exist. A hidden artist has only the picture with a
// letter, "Add to Discography" and the "Unsorted tracks" / Feat's carousels.
ListView {
    id: page

    required property var app
    readonly property var card: app.artistCard
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    spacing: Theme.space2
    model: card.sections
    ScrollBar.vertical: ScrollBar {}

    header: Item {
        width: page.width
        height: head.implicitHeight + Theme.space4

        Column {
            id: head
            x: Theme.space4
            y: Theme.space2
            width: parent.width - Theme.space4 * 2
            spacing: Theme.space2

            Row {
                spacing: Theme.space3
                Avatar {
                    id: face
                    width: Theme.px(72)
                    height: Theme.px(72)
                    letter: page.card.letter
                    source: page.card.avatar
                }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    width: head.width - face.width - Theme.space3
                    ElideText {
                        width: parent.width
                        text: (page.card.hidden || page.card.userHidden) ? qsTr("Hidden Artist") : qsTr("Artist")
                        font.pixelSize: Theme.fontCaption
                    }
                    ElideText {
                        width: parent.width
                        text: page.card.name
                        font.pixelSize: Theme.fontDisplay
                        font.weight: Font.Black
                    }
                    ElideText {
                        width: parent.width
                        visible: text.length > 0
                        text: page.card.stats
                        font.pixelSize: Theme.fontCaption
                        font.weight: Font.Bold
                    }
                }
            }
            Item {
                width: parent.width
                height: Theme.px(36)
                IconButton {
                    id: playButton
                    icon: "play"
                    outlined: true
                    tooltip: qsTr("Play")
                    iconSize: Theme.iconSmall
                    padding: Theme.space2
                    onClicked: page.app.play(page.card.hidden ? Ops.playHiddenArtist : Ops.playArtist, page.card.artistId)
                }
                PillButton {
                    visible: !page.card.hidden
                    anchors { right: editButton.left; rightMargin: Theme.space2; verticalCenter: parent.verticalCenter }
                    icon: "share"
                    text: qsTr("Share")
                    onClicked: page.app.open(Ops.pageShare, page.card.artistId, "artist")
                }
                PillButton {
                    id: editButton
                    visible: !page.card.hidden
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                    icon: "edit"
                    text: qsTr("Edit")
                    onClicked: page.app.open(Ops.pageEditArtist, page.card.artistId)
                }
                PillButton {
                    visible: page.card.hidden
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                    icon: "plus-small"
                    text: qsTr("Add to Discography")
                    onClicked: page.app.operate(Ops.materialiseHiddenArtist, page.card.artistId)
                }
            }
        }
    }

    delegate: Column {
        id: section
        required property var model
        width: page.width
        spacing: Theme.space1

        ElideText {
            x: Theme.space4
            width: Math.min(implicitWidth, parent.width - Theme.space4 * 2)
            text: section.model.title
            font.pixelSize: Theme.fontHeading
            font.weight: Font.Bold
            underlineOnHover: true
            onClicked: page.app.open(Ops.pageCategory, 0, section.model.key)
        }
        Carousel {
            width: parent.width
            height: implicitHeight
            items: section.model.items
            // the discography shows "Type | Year" under the album; the other carousels the artists
            showArtist: true
            onPlayRequested: (index) => {
                const id = section.model.items.idAt(index)
                if (id < 0) page.app.play(Ops.playHiddenArtist, -id)     // the Unsorted tracks of a hidden artist
                else page.app.home.playFromCarousel(section.model.items, index)
            }
            onOpenRequested: (index) => page.app.open(Ops.pageAlbum, section.model.items.idAt(index))
            onOpenOwnerRequested: (index) => {}
        }
    }
}
