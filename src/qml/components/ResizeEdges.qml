import QtQuick

// Invisible strips along the window edges and corners: drag one to resize the window (Windows does the resizing, so
// snapping and the minimum size work as usual). Without the Windows frame the window has no edges of its own.
Item {
    id: root

    required property var win
    readonly property int thickness: 6
    readonly property int corner: 12
    visible: win.visibility !== Window.Maximized && win.visibility !== Window.FullScreen

    component Edge: Item {
        id: edge
        property int edges: 0
        property alias cursor: hover.cursorShape
        HoverHandler {
            id: hover
        }
        DragHandler {
            target: null
            acceptedButtons: Qt.LeftButton
            onActiveChanged: if (active) root.win.startSystemResize(edge.edges)
        }
    }

    Edge { edges: Qt.LeftEdge; cursor: Qt.SizeHorCursor; x: 0; y: root.corner; width: root.thickness; height: root.height - root.corner * 2 }
    Edge { edges: Qt.RightEdge; cursor: Qt.SizeHorCursor; x: root.width - root.thickness; y: root.corner; width: root.thickness; height: root.height - root.corner * 2 }
    Edge { edges: Qt.TopEdge; cursor: Qt.SizeVerCursor; x: root.corner; y: 0; width: root.width - root.corner * 2; height: root.thickness }
    Edge { edges: Qt.BottomEdge; cursor: Qt.SizeVerCursor; x: root.corner; y: root.height - root.thickness; width: root.width - root.corner * 2; height: root.thickness }
    Edge { edges: Qt.LeftEdge | Qt.TopEdge; cursor: Qt.SizeFDiagCursor; x: 0; y: 0; width: root.corner; height: root.corner }
    Edge { edges: Qt.RightEdge | Qt.BottomEdge; cursor: Qt.SizeFDiagCursor; x: root.width - root.corner; y: root.height - root.corner; width: root.corner; height: root.corner }
    Edge { edges: Qt.RightEdge | Qt.TopEdge; cursor: Qt.SizeBDiagCursor; x: root.width - root.corner; y: 0; width: root.corner; height: root.corner }
    Edge { edges: Qt.LeftEdge | Qt.BottomEdge; cursor: Qt.SizeBDiagCursor; x: 0; y: root.height - root.corner; width: root.corner; height: root.corner }
}
