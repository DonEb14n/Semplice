import QtQuick
import QtQuick.Effects

// The window header (the Windows one is replaced, see Main.qml): the name in the middle, minimise / maximise / close on the
// right. Drag the empty part to move the window (Windows' own snapping works), double-click it to maximise or restore.
Item {
    id: bar

    required property var win          // the application window
    readonly property bool maximized: win.visibility === Window.Maximized

    implicitHeight: Theme.titleBarHeight

    function toggleMaximize() {
        if (bar.maximized)
            win.showNormal()
        else
            win.showMaximized()
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.surface
    }
    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.border
    }

    // moving: the whole bar except the buttons
    Item {
        anchors {
            left: parent.left
            right: buttons.left
            top: parent.top
            bottom: parent.bottom
        }
        DragHandler {
            target: null
            acceptedButtons: Qt.LeftButton
            onActiveChanged: if (active) bar.win.startSystemMove()
        }
        TapHandler {
            acceptedButtons: Qt.LeftButton
            onDoubleTapped: bar.toggleMaximize()
        }
    }

    // the name of the program is its text logo (white in the file, coloured by the theme)
    Item {
        id: logo
        anchors.centerIn: parent
        height: Theme.px(17)
        width: height * 205.6 / 52.24
        Accessible.name: bar.win.title
        Image {
            id: logoImage
            anchors.fill: parent
            visible: false
            source: "qrc:/icons/text-logo.svg"
            sourceSize: Qt.size(width * 3, height * 3)
            fillMode: Image.PreserveAspectFit
            smooth: true
        }
        MultiEffect {
            anchors.fill: logoImage
            source: logoImage
            colorization: 1.0
            colorizationColor: Theme.text
        }
    }

    // a caption button: the glyph is drawn here (no picture files); `danger` is the red close button
    component CaptionButton: Item {
        id: button
        property string glyph: "minimize"      // minimize, maximize, restore, close
        property bool danger: false
        signal clicked
        readonly property color ink: button.danger && hover.hovered ? "#FFFFFF" : Theme.text
        readonly property int size: Theme.px(10)

        width: Theme.px(46)
        height: bar.height

        Rectangle {
            anchors.fill: parent
            color: hover.hovered ? (button.danger ? Theme.danger : Theme.highlight) : "transparent"
        }
        Item {
            anchors.centerIn: parent
            width: button.size
            height: button.size
            Rectangle {  // minimise: a line
                visible: button.glyph === "minimize"
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                height: 1
                color: button.ink
            }
            Rectangle {  // maximise: a square
                visible: button.glyph === "maximize"
                anchors.fill: parent
                color: "transparent"
                border.width: 1
                border.color: button.ink
            }
            Item {  // restore: two squares
                visible: button.glyph === "restore"
                anchors.fill: parent
                Rectangle {
                    x: 2
                    y: 0
                    width: parent.width - 2
                    height: parent.height - 2
                    color: "transparent"
                    border.width: 1
                    border.color: button.ink
                }
                Rectangle {
                    x: 0
                    y: 2
                    width: parent.width - 2
                    height: parent.height - 2
                    color: Theme.surface
                    border.width: 1
                    border.color: button.ink
                }
            }
            Item {  // close: a cross
                visible: button.glyph === "close"
                anchors.fill: parent
                Rectangle {
                    anchors.centerIn: parent
                    width: Math.round(parent.width * 1.35)
                    height: 1
                    rotation: 45
                    color: button.ink
                    antialiasing: true
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: Math.round(parent.width * 1.35)
                    height: 1
                    rotation: -45
                    color: button.ink
                    antialiasing: true
                }
            }
        }
        HoverHandler {
            id: hover
        }
        TapHandler {
            acceptedButtons: Qt.LeftButton
            onTapped: button.clicked()
        }
    }

    Row {
        id: buttons
        anchors {
            right: parent.right
            top: parent.top
            bottom: parent.bottom
        }
        CaptionButton {
            glyph: "minimize"
            onClicked: bar.win.showMinimized()
        }
        CaptionButton {
            glyph: bar.maximized ? "restore" : "maximize"
            onClicked: bar.toggleMaximize()
        }
        CaptionButton {
            glyph: "close"
            danger: true
            onClicked: bar.win.close()
        }
    }
}
