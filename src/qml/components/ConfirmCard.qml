import QtQuick
import QtQuick.Controls.Basic

// The small confirmation card of the delete flows (Figma: title, grey sentence, a round x and a round red check).
// Opens centred over the window with the page dimmed behind it.
FadePopup {
    id: root

    property string title: ""
    property string text: ""
    property bool busy: false
    signal accepted

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: Theme.px(340)
    padding: Theme.space3
    closePolicy: Popup.CloseOnEscape

    Overlay.modal: Rectangle { color: Theme.scrim }

    background: Rectangle {
        color: Theme.surface
        radius: Theme.radiusControl
        border.color: Theme.hairline
    }
    contentItem: Column {
        spacing: Theme.space2
        ElideText {
            width: parent.width
            text: root.title
            font.pixelSize: Theme.fontHeading
            font.weight: Font.Bold
        }
        Text {
            width: parent.width
            text: root.text
            color: Theme.textSecondary
            font.pixelSize: Theme.fontBody
            wrapMode: Text.WordWrap
        }
        Row {
            anchors.right: parent.right
            spacing: Theme.space2
            IconButton {
                icon: "close"
                outlined: true
                iconSize: Theme.iconSmall
                padding: Theme.space2
                tooltip: qsTr("Cancel")
                onClicked: root.close()
            }
            IconButton {
                icon: "check"
                outlined: true
                tint: Theme.danger
                iconSize: Theme.iconSmall
                padding: Theme.space2
                active: !root.busy
                tooltip: qsTr("Confirm")
                onClicked: {
                    root.accepted()
                    root.close()
                }
            }
        }
    }
}
