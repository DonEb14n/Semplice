import QtQuick
import QtQuick.Controls.Basic

// A table row that turned into an editor (Figma "Edit track"): the title as an editable line, the artists as
// chips with x and a "+" to add one, and cancel / save at the right. The draft is `app.trackEditor`.
Rectangle {
    id: root

    required property var app
    property string cover: ""
    property int position: 0
    readonly property var ed: app.trackEditor

    implicitHeight: Theme.px(76)
    radius: Theme.radiusControl
    color: Theme.surface
    border.width: 1
    border.color: Theme.border

    PickerPopup {
        id: artistPicker
        app: root.app
        kind: 0
        addLabel: qsTr("Add new artist")
        onPicked: (id, title, cover) => root.ed.addAuthor(id, title, false, 0, cover)
        // a new artist made for a track is hidden (SPEC 8.3): it is created when the track is saved
        onAddNewRequested: (text) => { if (text.trim().length > 0) root.ed.addAuthor(0, text, true) }
    }

    Text {
        id: number
        x: Theme.space3
        anchors.verticalCenter: parent.verticalCenter
        text: root.position
        color: Theme.text
        font.pixelSize: Theme.fontBody
    }
    Avatar {
        id: art
        anchors { left: number.right; leftMargin: Theme.space3; verticalCenter: parent.verticalCenter }
        width: Theme.px(40)
        height: Theme.px(40)
        round: false
        source: root.cover
    }

    Column {
        anchors {
            left: art.right
            leftMargin: Theme.space2
            right: buttons.left
            rightMargin: Theme.space2
            verticalCenter: parent.verticalCenter
        }
        spacing: Theme.space1

        TextField {
            id: title
            width: parent.width
            text: root.ed.title
            placeholderText: qsTr("Title")
            placeholderTextColor: Theme.disabled
            color: Theme.text
            font.pixelSize: Theme.fontBody
            font.weight: Font.Bold
            selectByMouse: true
            padding: 0
            maximumLength: 200
            onTextEdited: root.ed.title = text
            Component.onCompleted: forceActiveFocus()
            Keys.onEscapePressed: root.ed.cancelEdit()
            background: Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: title.activeFocus ? Theme.text : Theme.hairline
            }
        }
        Flow {
            width: parent.width
            spacing: Theme.space1
            Repeater {
                model: root.ed.authors
                delegate: Chip {
                    required property var model
                    required property int index
                    text: model.name
                    avatarLetter: model.letter
                    avatarSource: model.avatar
                    removable: true
                    reorderable: root.ed.authors.count > 1
                    chipIndex: index
                    onMoveRequested: (target) => root.ed.moveAuthor(index, target)
                    onRemoveRequested: root.ed.removeAuthor(index)
                }
            }
            IconButton {
                id: addArtistButton
                icon: "plus"
                outlined: true
                tooltip: qsTr("Add artist")
                iconSize: 12
                padding: Theme.space1
                onClicked: artistPicker.toggleAt(addArtistButton)
            }
        }
    }

    Row {
        id: buttons
        anchors { right: parent.right; rightMargin: Theme.space3; verticalCenter: parent.verticalCenter }
        spacing: Theme.space2
        IconButton {
            icon: "close"
            outlined: true
            tooltip: qsTr("Cancel")
            iconSize: Theme.iconSmall
            padding: Theme.space2
            onClicked: root.ed.cancelEdit()
        }
        IconButton {
            icon: "save"
            outlined: true
            tooltip: qsTr("Save")
            iconSize: Theme.iconSmall
            padding: Theme.space2
            active: root.ed.canSave
            onClicked: root.ed.save()
        }
    }
}
