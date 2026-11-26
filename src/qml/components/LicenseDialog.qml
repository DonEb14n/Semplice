import QtQuick
import QtQuick.Controls.Basic

// The license and user agreement (UI doc p.50). Two modes: `acceptRequired` (first launch: "Exit app" / "Accept")
// and read-only (Settings -> License: just a close button).
FadePopup {
    id: root

    property bool acceptRequired: false
    property string licenseText: ""
    signal accepted
    signal exitRequested

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(520, (Overlay.overlay ? Overlay.overlay.width : 520) - Theme.space6 * 2)
    height: Math.min(460, (Overlay.overlay ? Overlay.overlay.height : 460) - Theme.space6 * 2)
    padding: Theme.space4
    closePolicy: root.acceptRequired ? Popup.NoAutoClose : Popup.CloseOnEscape

    Overlay.modal: Rectangle { color: Theme.scrim }

    background: Rectangle {
        color: Theme.surface
        radius: Theme.radiusControl
        border.color: Theme.hairline
    }
    contentItem: Item {
        ElideText {
            id: heading
            width: parent.width
            text: qsTr("License and user agreement")
            font.pixelSize: Theme.fontHeading
            font.weight: Font.Bold
        }
        Flickable {
            id: scroller
            anchors { left: parent.left; right: parent.right; top: heading.bottom; topMargin: Theme.space3; bottom: footerRow.top; bottomMargin: Theme.space3 }
            clip: true
            contentWidth: width
            contentHeight: body.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            Text {
                id: body
                width: scroller.width - Theme.space3
                text: root.licenseText
                color: Theme.textSecondary
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.fontBody
            }
        }
        Row {
            id: footerRow
            anchors { right: parent.right; bottom: parent.bottom }
            spacing: Theme.space2
            PillButton {
                visible: root.acceptRequired
                text: qsTr("Exit app")
                tint: Theme.danger
                onClicked: root.exitRequested()
            }
            PillButton {
                text: root.acceptRequired ? qsTr("Accept") : qsTr("Close")
                onClicked: {
                    if (root.acceptRequired)
                        root.accepted()
                    root.close()
                }
            }
        }
    }
}
