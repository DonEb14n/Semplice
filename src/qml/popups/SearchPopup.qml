import QtQuick
import QtQuick.Controls.Basic

// Search results (UI doc p.18): artists, albums, playlists, tracks; 5 per category and "Show more".
Rectangle {
    id: popup

    required property var app
    readonly property var search: app.search

    color: Theme.surface
    radius: Theme.radiusPanel
    border.color: Theme.border

    // a click on the popup belongs to the popup (see QueuePopup)
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: (wheel) => wheel.accepted = true   // the end of a list must not scroll the queue panel behind the popup
    }

    readonly property var sections: [
        { "category": 0, "title": qsTr("Artists"), "model": search.artists, "total": search.artistsTotal, "playKind": 0, "openKind": 2 },
        { "category": 1, "title": qsTr("Albums"), "model": search.albums, "total": search.albumsTotal, "playKind": 2, "openKind": 3 },
        { "category": 2, "title": qsTr("Playlists"), "model": search.playlists, "total": search.playlistsTotal, "playKind": 3, "openKind": 4 },
        { "category": 3, "title": qsTr("Tracks"), "model": search.tracks, "total": search.tracksTotal, "playKind": 4, "openKind": 4 }
    ]

    ElideText {
        id: heading
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
            margins: Theme.space3
        }
        text: qsTr("Search results")
        font.weight: Font.Bold
    }

    ElideText {
        anchors.centerIn: parent
        width: parent.width - Theme.space6
        horizontalAlignment: Text.AlignHCenter
        visible: !popup.search.hasResults
        color: Theme.textSecondary
        text: popup.search.text.trim().length === 0 ? qsTr("Type to search") : qsTr("Nothing found")
    }

    Flickable {
        anchors {
            top: heading.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
            margins: Theme.space3
        }
        visible: popup.search.hasResults
        clip: true
        contentWidth: width
        contentHeight: sectionsColumn.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        Column {
            id: sectionsColumn
            width: parent.width
            spacing: Theme.space2

            Repeater {
                model: popup.sections
                delegate: Column {
                    id: section
                    required property var modelData
                    width: sectionsColumn.width
                    visible: modelData.total > 0
                    spacing: 0

                    ElideText {
                        width: parent.width
                        text: section.modelData.title
                        font.weight: Font.Bold
                        font.pixelSize: Theme.fontHeading
                    }
                    Repeater {
                        model: section.modelData.model
                        delegate: Item {
                            id: row
                            required property var model
                            width: section.width
                            height: Theme.px(40)
                            Avatar {
                                id: rowAvatar
                                anchors.verticalCenter: parent.verticalCenter
                                width: Theme.px(32)
                                height: Theme.px(32)
                                round: section.modelData.category === 0
                                letter: section.modelData.category === 0 ? row.model.letter : ""
                                HoverHandler { cursorShape: Qt.PointingHandCursor }
                                TapHandler { onTapped: popup.app.play(section.modelData.playKind, row.model.itemId) }
                            }
                            ElideText {
                                anchors {
                                    left: rowAvatar.right
                                    leftMargin: Theme.space2
                                    right: parent.right
                                    verticalCenter: parent.verticalCenter
                                }
                                text: row.model.title
                                color: row.model.dimmed ? Theme.disabled : Theme.text
                                underlineOnHover: true
                                onClicked: {
                                    // a track has no page of its own: it plays (a queue of just that track)
                                    if (section.modelData.category === 3)
                                        popup.app.play(4, row.model.itemId)
                                    else
                                        popup.app.open(section.modelData.openKind, row.model.itemId)
                                }
                            }
                        }
                    }
                    ElideText {
                        width: parent.width
                        height: Theme.px(28)
                        visible: popup.search.canShowMore(section.modelData.category)
                        text: qsTr("Show more")
                        color: Theme.textSecondary
                        underlineOnHover: true
                        onClicked: popup.search.showMore(section.modelData.category)
                    }
                    Item { width: 1; height: Theme.space2 }
                }
            }
        }
    }
}
