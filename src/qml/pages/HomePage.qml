import QtQuick
import QtQuick.Controls.Basic

// Home (UI doc p.23-26): "Recently Played" first, then the generated carousels; more of them are made while
// the user scrolls down and there is still content.
Item {
    readonly property var scroller: list
    id: page

    required property var app
    readonly property var home: app.home

    // one section: title (opens the category page) and its carousel
    component Section: Column {
        id: section
        property string title: ""
        property string key: ""
        property var items: null
        property bool showArtist: true
        property string emptyText: ""
        width: list.width
        spacing: Theme.space1

        ElideText {
            x: Theme.space4
            width: Math.min(implicitWidth, parent.width - Theme.space4 * 2)
            text: section.title
            font.pixelSize: Theme.fontTitle
            font.weight: Font.Bold
            underlineOnHover: true
            onClicked: page.app.open(5, 0, section.key)
        }
        ElideText {
            x: Theme.space4
            width: parent.width - Theme.space4 * 2
            height: visible ? 64 : 0
            visible: section.items !== null && section.items.count === 0 && section.emptyText.length > 0
            text: section.emptyText
            color: Theme.textSecondary
        }
        Carousel {
            width: parent.width
            height: visible ? implicitHeight : 0
            visible: section.items !== null && section.items.count > 0
            items: section.items
            showArtist: section.showArtist
            animateChanges: true
            onPlayRequested: (index) => page.home.playFromCarousel(section.items, index)
            onOpenRequested: (index) => page.app.open(section.items.typeAt(index) < 0 ? 4 : 3, section.items.idAt(index))
            onOpenOwnerRequested: (index) => page.app.open(2, section.items.ownerAt(index))
            onArtistRequested: (id) => page.app.open(2, id)
        }
    }

    ListView {
        id: list
        objectName: "homeView"
        anchors.fill: parent
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: page.home.categories
        spacing: Theme.space2
        ScrollBar.vertical: ScrollBar {}

        // "Recently Played" is always there and always first, also while it is empty
        header: Item {
            width: list.width
            visible: page.home.loaded && page.home.hasContent
            height: visible ? (recentSection.height + Theme.space2) : 0
            Section {
                id: recentSection
                y: Theme.space2
                title: qsTr("Recently Played")
                key: "recent"
                items: page.home.recent
                emptyText: qsTr("Albums and playlists you play appear here.")
            }
        }

        delegate: Section {
            required property var model
            title: model.title
            key: model.key
            items: model.items
            showArtist: true   // the artist names are under the albums of a discography carousel, too
        }

        // more categories while the end is near (and while there are any)
        onAtYEndChanged: if (atYEnd) page.home.loadMore()
        onContentHeightChanged: if (contentHeight > 0 && contentHeight < height + 40) page.home.loadMore()
    }
}
