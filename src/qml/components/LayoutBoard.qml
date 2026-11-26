import QtQuick

// A small plan of the application window (Settings -> Window placement): Library, Main window, Player, Queue and the
// Hotbar as blocks that are dragged to where they should be. The other blocks move out of the way while a block is
// dragged, so the place where it will land is always visible. The player and the queue always share a column (the
// column stands left or right of the other two); the queue is above or below the player; the hotbar is at the top or the bottom.
Item {
    id: board

    // the plan being edited
    property var order: ["library", "main", "right"]
    property bool queueAbove: false
    property bool hotbarTop: false

    readonly property real gap: Theme.px(4)
    readonly property real hotbarHeight: Theme.px(24)
    readonly property real bodyY: hotbarTop ? hotbarHeight + gap : 0
    readonly property real bodyHeight: height - hotbarHeight - gap
    readonly property real sideWidth: (width - gap * 2) * 0.26
    readonly property real mainWidth: width - gap * 2 - sideWidth * 2

    function widthOf(name) { return name === "main" ? mainWidth : sideWidth }
    function homeX(name) {
        let x = 0
        for (const n of order) {
            if (n === name)
                return x
            x += widthOf(n) + gap
        }
        return 0
    }

    // a column block was dragged: where its centre is decides its place among the columns
    function placeColumn(name, centerX) {
        const others = order.filter(n => n !== name)
        let index = 0
        for (const o of others)
            if (homeX(o) + widthOf(o) / 2 < centerX)
                ++index
        const next = others.slice()
        next.splice(index, 0, name)
        if (next[1] === "right") {
            if (name !== "right")
                return                      // the player column stays at an end
            next.splice(1, 1)
            if (centerX < width / 2)
                next.unshift("right")
            else
                next.push("right")
        }
        order = next
    }

    component Block: Rectangle {
        id: block
        property string label: ""
        property real homeX: 0
        property real homeY: 0
        property bool dragging: handler.active
        property real startX: 0
        property real startY: 0
        signal moved(real centerX, real centerY)

        radius: Theme.radiusControl
        color: Theme.surface
        border.width: Theme.strokeWidth
        border.color: block.dragging ? Theme.text : Theme.border
        z: block.dragging ? 10 : 0
        x: block.dragging ? block.startX + handler.translation.x : block.homeX
        y: block.dragging ? block.startY + handler.translation.y : block.homeY
        Behavior on x { enabled: !block.dragging; NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic } }
        Behavior on y { enabled: !block.dragging; NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic } }

        Text {
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: Theme.space1 }
            text: block.label
            color: Theme.text
            font.pixelSize: Theme.fontCaption
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
        }
        DragHandler {
            id: handler
            target: null
            cursorShape: Qt.SizeAllCursor
            onActiveChanged: if (active) { block.startX = block.homeX; block.startY = block.homeY }
            onTranslationChanged: if (active) block.moved(block.startX + handler.translation.x + block.width / 2,
                                                          block.startY + handler.translation.y + block.height / 2)
        }
    }

    Block {
        label: qsTr("Library")
        width: board.sideWidth
        height: board.bodyHeight
        homeX: board.homeX("library")
        homeY: board.bodyY
        onMoved: (cx) => board.placeColumn("library", cx)
    }
    Block {
        label: qsTr("Main window")
        width: board.mainWidth
        height: board.bodyHeight
        homeX: board.homeX("main")
        homeY: board.bodyY
        onMoved: (cx) => board.placeColumn("main", cx)
    }
    // the player and the queue: either one drags the whole column; up and down puts the queue above or below the player
    readonly property real halfBody: (bodyHeight - gap) / 2
    Block {
        label: qsTr("Player")
        width: board.sideWidth
        height: board.halfBody
        homeX: board.homeX("right")
        homeY: board.bodyY + (board.queueAbove ? board.halfBody + board.gap : 0)
        onMoved: (cx, cy) => {
            board.placeColumn("right", cx)
            board.queueAbove = cy > board.bodyY + board.bodyHeight / 2
        }
    }
    Block {
        label: qsTr("Queue")
        width: board.sideWidth
        height: board.halfBody
        homeX: board.homeX("right")
        homeY: board.bodyY + (board.queueAbove ? 0 : board.halfBody + board.gap)
        onMoved: (cx, cy) => {
            board.placeColumn("right", cx)
            board.queueAbove = cy < board.bodyY + board.bodyHeight / 2
        }
    }
    Block {
        label: qsTr("Hotbar")
        width: board.width
        height: board.hotbarHeight
        homeX: 0
        homeY: board.hotbarTop ? 0 : board.height - board.hotbarHeight
        onMoved: (cx, cy) => board.hotbarTop = cy < board.height / 2
    }
}
