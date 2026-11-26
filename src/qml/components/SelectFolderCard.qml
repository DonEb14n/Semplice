import QtQuick
import QtQuick.Dialogs

// First launch / lost library (UI doc p.3-4, SPEC 6.6): choose an empty folder or an existing library.
Rectangle {
    id: card

    required property var app
    // AppController.Startup: 1 first launch, 2 stored folder is gone, 4 stored library unusable
    readonly property int mode: app.startup

    width: Math.min(360, parent ? parent.width - Theme.space6 : 360)
    height: column.implicitHeight + Theme.space4 * 2
    radius: Theme.radiusPanel
    color: Theme.surface
    border.color: Theme.border

    // The native folder dialog is created only when it is wanted: creating it loads the Windows shell dialog code (about a second
    // during which the whole application stands still).
    Loader {
        id: dialogLoader
        active: false
        sourceComponent: FolderDialog {
            title: qsTr("Select folder")
            onAccepted: card.app.selectFolder(selectedFolder)
        }
        onLoaded: item.open()
    }
    function openDialog() {
        if (dialogLoader.item)
            dialogLoader.item.open()
        else
            dialogLoader.active = true
    }

    Column {
        id: column
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            margins: Theme.space4
        }
        spacing: Theme.space2

        ElideText {
            width: parent.width
            text: card.mode === 2 ? qsTr("Locate your library") : qsTr("Select folder")
            font.pixelSize: Theme.fontTitle
            font.weight: Font.Medium
        }
        ElideText {
            width: parent.width
            wrapMode: Text.WordWrap
            maximumLineCount: 5
            color: Theme.textSecondary
            font.pixelSize: Theme.fontCaption
            text: card.mode === 2 ? qsTr("The folder of your music collection was not found: %1. Choose its new place, or an empty folder for a new collection.").arg(card.app.libraryPath)
                : card.mode === 4 ? qsTr("The library in %1 cannot be used. Choose another folder.").arg(card.app.libraryPath)
                : qsTr("Select an empty folder for your future music collection.")
        }
        Rectangle {
            anchors.right: parent.right
            width: buttonRow.implicitWidth + Theme.space4
            height: Theme.px(32)
            radius: Theme.radiusPill
            color: "transparent"
            border.width: 1
            border.color: Theme.text

            Row {
                id: buttonRow
                anchors.centerIn: parent
                spacing: Theme.space2
                Icon { name: "folder"; size: Theme.iconSmall; anchors.verticalCenter: parent.verticalCenter }
                Text {
                    text: qsTr("Select folder")
                    color: Theme.text
                    font.pixelSize: Theme.fontBody
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            HoverHandler { cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: card.openDialog() }
        }
    }
}
