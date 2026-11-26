import QtQuick
import QtQuick.Controls.Basic

// Text that never overflows: it elides, and shows the full text as a tooltip when it had to.
// The tooltip and the running copy of the text are created only while they are needed (a page has dozens of these texts, and a
// popup plus an animation for each of them made a page take a second to build).
Text {
    id: root

    property bool tooltipEnabled: true
    property bool underlineOnHover: false
    property bool scrollOnHover: false          // a cut-off text runs sideways while the mouse is over it, to be read in full
    property color scrollBackground: Theme.surface   // what is behind the text (the running copy covers the cut one)
    property alias hovered: hover.hovered
    signal clicked

    color: Theme.text
    elide: Text.ElideRight
    font.pixelSize: Theme.fontBody
    maximumLineCount: 1
    verticalAlignment: Text.AlignVCenter

    // the mouse reacts over the text itself, not over the whole width the text was given (a title in a wide column is not a wide link)
    Item {
        x: root.horizontalAlignment === Text.AlignHCenter ? (root.width - width) / 2
           : root.horizontalAlignment === Text.AlignRight ? root.width - width : 0
        width: Math.min(root.contentWidth, root.width)
        height: root.height
        HoverHandler {
            id: hover
            cursorShape: root.underlineOnHover ? Qt.PointingHandCursor : Qt.ArrowCursor
        }
        TapHandler {
            enabled: root.underlineOnHover
            onTapped: if (Theme.tapAllowed(root)) root.clicked()   // not a click that belongs to a menu above this text
        }
    }
    // the underline of a link under the mouse: below the letters (not through them, as the font's own underline did)
    Loader {
        active: root.underlineOnHover && hover.hovered && root.text.length > 0
        sourceComponent: Rectangle {
            readonly property real lineWidth: Math.max(1, Math.round(root.font.pixelSize / 14))
            width: Math.min(root.contentWidth, root.width)
            height: lineWidth
            x: root.horizontalAlignment === Text.AlignHCenter ? (root.width - width) / 2
               : root.horizontalAlignment === Text.AlignRight ? root.width - width : 0
            y: Math.round(root.baselineOffset + Math.max(2, root.font.pixelSize * 0.14))
            color: root.color
        }
    }
    Loader {
        id: marquee
        anchors.fill: parent
        active: root.scrollOnHover && hover.hovered && root.truncated
        sourceComponent: Item {
            clip: true
            Rectangle { anchors.fill: parent; color: root.scrollBackground }
            Text {
                id: runner
                height: parent.height
                text: root.text
                font: root.font
                color: root.color
                verticalAlignment: root.verticalAlignment
                textFormat: root.textFormat
                property real overflow: Math.max(0, implicitWidth - parent.width)
                SequentialAnimation on x {
                    running: true
                    loops: Animation.Infinite
                    PropertyAction { value: 0 }
                    PauseAnimation { duration: 500 }
                    NumberAnimation { to: -runner.overflow; duration: Math.max(400, runner.overflow * 14); easing.type: Easing.InOutSine }
                    PauseAnimation { duration: 700 }
                    NumberAnimation { to: 0; duration: 350; easing.type: Easing.InOutSine }
                }
            }
        }
    }
    Loader {
        anchors.fill: parent
        active: root.tooltipEnabled && hover.hovered && root.truncated && root.text.length > 0
        sourceComponent: ToolTip {
            visible: true
            delay: 500
            text: root.text
        }
    }
}
