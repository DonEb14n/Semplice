import QtQuick
import QtQuick.Controls.Basic

// Circular carousel (UI doc p.24, Figma "Main Window"): the active tile is in the middle, big and dark; the
// tiles beside it get smaller and lighter and touch each other. Drag sideways (or use the wheel) to turn it;
// click the middle tile to play it, a side tile to bring it forward. At most 50 items; only seven tiles exist.
Item {
    id: root

    required property var items        // a LibraryListModel (rows with title / subtitle / cover / albumType)
    property bool showArtist: true     // the discography carousel shows only the album title
    readonly property int count: items ? items.count : 0
    readonly property int currentIndex: count > 0 ? (((Math.round(position) % count) + count) % count) : 0
    // every artist of the active album ([{ id, name }]): each name is a link of its own
    readonly property var owners: count > 0 && items.ownersAt ? (rev, items.ownersAt(currentIndex)) : []
    // the seven tiles together are 4.74 middle tiles wide: the middle tile is sized so that they span the whole width
    property real tileSize: Math.max(80, width / 4.74)
    property real position: 0          // continuous index; the fraction is the drag in progress
    property bool animateChanges: false     // a new first item slides in from the side (the Recently Played carousel)
    // opening: the tiles come out from the middle tile (0 = all on top of each other, 1 = in place)
    property real spread: 0
    NumberAnimation on spread { from: 0; to: 1; duration: Theme.durationNormal * 2.2; easing.type: Easing.OutCubic }
    readonly property int slots: Math.min(count, 7)

    signal playRequested(int index)
    signal openRequested(int index)
    signal openOwnerRequested(int index)       // the line under the title when the row has no artist links
    signal artistRequested(var artistId)       // one of the artists under the title

    implicitHeight: tileSize + 56

    // the rows are read through functions, which do not announce a change: this counter makes every reader read again when the model
    // is replaced (the "Recently Played" carousel is replaced after each play)
    property int rev: 0
    Connections {
        target: root.items
        ignoreUnknownSignals: true
        function onModelReset() {
            root.rev++
            if (root.animateChanges && root.count > 1 && root.spread >= 1) {   // something new at the front: the carousel turns to it
                settle.stop()
                root.position = -1
                root.turnTo(0)
            }
        }
        function onDataChanged() { root.rev++ }
        function onRowsInserted() { root.rev++ }
        function onRowsRemoved() { root.rev++ }
    }

    // size of a tile at distance a (in tiles) from the middle: 1, 0.8, 0.62, 0.45 ...
    function sizeAt(a) {
        if (a <= 1) return 1 - 0.2 * a
        if (a <= 2) return 0.8 - 0.18 * (a - 1)
        if (a <= 3) return 0.62 - 0.17 * (a - 2)
        return 0.45
    }
    // centre of a tile at signed distance rel: neighbours touch, so the centres follow the sizes
    function centreAt(rel) {
        const a = Math.abs(rel)
        let c = 0
        const whole = Math.floor(a)
        for (let j = 1; j <= whole; ++j)
            c += (sizeAt(j - 1) + sizeAt(j)) / 2 * root.tileSize
        const frac = a - whole
        if (frac > 0)
            c += frac * (sizeAt(whole) + sizeAt(whole + 1)) / 2 * root.tileSize
        return rel < 0 ? -c : c
    }

    NumberAnimation {
        id: settle
        target: root
        property: "position"
        duration: Theme.durationNormal * 1.5
        easing.type: Easing.OutCubic
    }
    function turnTo(target) {
        settle.stop()
        settle.from = root.position
        settle.to = target
        settle.start()
    }

    Item {
        id: stage
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
        }
        height: root.tileSize + Theme.space2

        Repeater {
            model: root.slots
            delegate: Item {
                id: tile
                required property int index
                readonly property int offset: index - Math.floor(root.slots / 2)
                readonly property real rel: offset - (root.position - Math.round(root.position))
                readonly property int itemIndex: root.count > 0 ? ((((Math.round(root.position) + offset) % root.count) + root.count) % root.count) : 0
                readonly property real size: root.tileSize * root.sizeAt(Math.abs(rel))
                readonly property bool isCurrent: Math.abs(rel) < 0.5
                width: size
                height: size
                x: stage.width / 2 + root.centreAt(rel) * root.spread - width / 2
                y: (stage.height - height) / 2
                z: 10 - Math.round(Math.abs(rel) * 2)
                opacity: Math.max(0.3, 1 - 0.38 * Math.abs(rel)) * Math.min(1, root.spread * 1.5)
                visible: Math.abs(rel) <= 3.5

                Avatar {
                    anchors.fill: parent
                    round: false
                    source: root.items ? (root.rev, root.items.coverAt(tile.itemIndex)) : ""

                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radiusControl
                        color: Theme.overlay
                        visible: tile.isCurrent && hover.hovered
                        Icon {
                            anchors.centerIn: parent
                            name: "play"
                            size: Theme.iconLarge
                            color: Theme.onImage
                        }
                    }
                }
                HoverHandler {
                    id: hover
                    cursorShape: Qt.PointingHandCursor
                }
                TapHandler {
                    onTapped: {
                        if (tile.isCurrent)
                            root.playRequested(tile.itemIndex)
                        else
                            root.turnTo(Math.round(root.position) + tile.offset)
                    }
                }
            }
        }

        // drag sideways to turn
        DragHandler {
            id: drag
            target: null
            yAxis.enabled: false
            enabled: root.count > 1
            property real startPosition: 0
            onActiveChanged: {
                if (active) {
                    settle.stop()
                    startPosition = root.position
                } else {
                    root.turnTo(Math.round(root.position))
                }
            }
            onTranslationChanged: if (active) root.position = startPosition - translation.x / (root.tileSize * 0.75)
        }
        // the wheel turns the carousel only over the middle cover (a little larger than it), so the page can still scroll
        Item {
            id: wheelZone
            anchors.centerIn: parent
            width: root.tileSize * 1.25
            height: parent.height
            property real accumulated: 0
            WheelHandler {
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                enabled: root.count > 1
                onWheel: (event) => {
                    wheelZone.accumulated += event.angleDelta.y + event.angleDelta.x
                    if (Math.abs(wheelZone.accumulated) < 120)
                        return
                    const step = wheelZone.accumulated > 0 ? -1 : 1
                    wheelZone.accumulated = 0
                    root.turnTo(Math.round(settle.running ? settle.to : root.position) + step)
                }
            }
        }
    }

    // title (and artist) of the active item
    Column {
        anchors {
            top: stage.bottom
            horizontalCenter: parent.horizontalCenter
        }
        width: Math.min(parent.width - Theme.space6, root.tileSize * 2.2)
        spacing: 0
        ElideText {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: root.count > 0 ? (root.rev, root.items.titleAt(root.currentIndex)) : ""
            font.weight: Font.Bold
            underlineOnHover: true
            onClicked: root.openRequested(root.currentIndex)
        }
        ElideText {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            visible: root.showArtist && root.owners.length === 0 && text.length > 0
            text: root.count > 0 ? (root.rev, root.items.subtitleAt(root.currentIndex)) : ""
            underlineOnHover: true
            onClicked: root.openOwnerRequested(root.currentIndex)
        }
        Item {
            width: parent.width
            height: visible ? links.implicitHeight : 0
            visible: root.showArtist && root.owners.length > 0
            ArtistLinks {
                id: links
                width: parent.width
                centered: true
                artists: root.owners
                pixelSize: Theme.fontBody
                onArtistClicked: (id) => root.artistRequested(id)
            }
        }
    }
}
