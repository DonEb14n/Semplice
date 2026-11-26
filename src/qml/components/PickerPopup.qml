import QtQuick
import QtQuick.Controls.Basic

// Search and pick (Figma "Albums" / "Playlists": search field, "Add new ..." button, the list; five rows are
// visible, the list scrolls). `kind` 0 = artists (hidden ones included), 1 = playlists, 2 = albums. For tags use
// `fixedItems` (a plain list of { id, title }) instead of the engine.
FadePopup {
    id: root

    required property var app
    property int kind: 0
    property string addLabel: qsTr("Add new artist")
    property bool allowAddNew: true
    property var fixedItems: null            // tags: [{ id, title }]
    property string initialText: ""
    property var extraItems: []              // [{ id: 0, title }]: names that are not in the library (yet) but can be picked, like the artists of new files
    signal picked(var id, string title, string cover)
    signal addNewRequested(string text)

    readonly property var picker: app.picker
    property double closedAt: 0
    function toggle() { if (visible) close(); else if (Date.now() - closedAt > 250) open() }
    // opens right under the button that asked (a popup without a parent position would sit in the page corner)
    function toggleAt(anchor) {
        if (visible) { close(); return }
        if (Date.now() - closedAt <= 250) return
        parent = anchor
        x = 0
        y = anchor.height + Theme.space1
        open()
    }
    readonly property string needle: field.text

    padding: Theme.space2
    width: Theme.px(240)
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onAboutToShow: {
        field.text = root.initialText
        refresh()
        field.forceActiveFocus()
    }
    onAboutToHide: closedAt = Date.now()
    onClosed: if (!fixedItems) picker.clear()

    function refresh() {
        if (!fixedItems)
            picker.search(kind, field.text)
    }
    readonly property var visibleExtras: {
        const q = field.text.trim().toLowerCase()
        return extraItems.filter(function (t) { return q.length === 0 || t.title.toLowerCase().indexOf(q) >= 0 }).slice(0, 5)
    }
    readonly property var visibleFixed: {
        if (!fixedItems)
            return []
        const q = field.text.trim().toLowerCase()
        return fixedItems.filter(function (t) { return q.length === 0 || t.title.toLowerCase().indexOf(q) >= 0 })
    }

    background: PopupSurface {}
    contentItem: Column {
        spacing: Theme.space1

        Rectangle {
            width: parent.width
            height: Theme.px(30)
            radius: Theme.radiusPill
            border.width: 1
            border.color: Theme.hairline
            color: "transparent"
            Icon {
                id: glass
                anchors { left: parent.left; leftMargin: Theme.space2; verticalCenter: parent.verticalCenter }
                name: "search"
                size: Theme.iconSmall
            }
            TextField {
                id: field
                anchors { left: glass.right; leftMargin: Theme.space1; right: parent.right; rightMargin: Theme.space2; verticalCenter: parent.verticalCenter }
                background: null
                padding: 0
                color: Theme.text
                font.pixelSize: Theme.fontBody
                selectByMouse: true
                onTextEdited: root.refresh()
                Keys.onEscapePressed: root.close()
            }
        }

        // "Add new ..." button
        Item {
            visible: root.allowAddNew
            width: parent.width
            height: visible ? 32 : 0
            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space2
                IconButton { icon: "plus"; outlined: true; iconSize: 12; padding: Theme.space1 }
                ElideText { anchors.verticalCenter: parent.verticalCenter; text: root.addLabel; font.weight: Font.Medium }
            }
            // the whole row is the button; the mouse area also keeps the click from reaching the page under the popup
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: { root.close(); root.addNewRequested(field.text) }
            }
        }

        // names from the files being added (not in the library yet)
        Column {
            visible: !root.fixedItems && root.visibleExtras.length > 0
            width: parent.width
            Repeater {
                model: root.visibleExtras
                delegate: ElideText {
                    required property var modelData
                    width: parent.width
                    height: Theme.px(30)
                    text: modelData.title
                    font.weight: Font.Medium
                    underlineOnHover: true
                    onClicked: {
                        const chosen = modelData.title
                        root.close()
                        root.picked(0, chosen, "")
                    }
                }
            }
        }

        // engine results (artists / playlists / albums)
        ListView {
            id: list
            visible: !root.fixedItems
            width: parent.width
            height: visible ? Math.min(contentHeight, 5 * 34) : 0
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: root.picker.results
            ScrollBar.vertical: ScrollBar {}
            delegate: Item {
                id: row
                required property var model
                required property int index
                width: list.width
                height: Theme.px(34)
                Avatar {
                    id: face
                    anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                    width: Theme.px(26)
                    height: Theme.px(26)
                    round: root.kind === 0
                    letter: root.kind === 0 ? row.model.letter : ""
                    source: row.model.cover
                }
                ElideText {
                    anchors { left: face.right; leftMargin: Theme.space2; right: parent.right; verticalCenter: parent.verticalCenter }
                    text: row.model.title
                    font.weight: Font.Medium
                    underlineOnHover: true
                    onClicked: {
                        // read the row first: closing clears the results and destroys it
                        const chosenId = row.model.itemId
                        const chosenTitle = row.model.title
                        const chosenCover = row.model.cover
                        root.close()
                        root.picked(chosenId, chosenTitle, chosenCover)
                    }
                }
            }
        }

        // fixed choices (tags)
        ListView {
            id: fixedList
            visible: root.fixedItems !== null
            width: parent.width
            height: visible ? Math.min(contentHeight, 5 * 30) : 0
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: root.visibleFixed
            ScrollBar.vertical: ScrollBar {}
            delegate: ElideText {
                required property var modelData
                width: fixedList.width
                height: Theme.px(30)
                text: modelData.title
                font.weight: Font.Medium
                underlineOnHover: true
                onClicked: {
                    // read the choice first: closing starts the exit animation and the list is rebuilt
                    const chosenId = modelData.id
                    const chosenTitle = modelData.title
                    root.close()
                    root.picked(chosenId, chosenTitle, "")
                }
            }
        }
    }
}
