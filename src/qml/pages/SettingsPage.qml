import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs

// Settings (UI doc p.49, Figma "Middle window all conditions"): sections with a label and a value button per row.
// Language, name and theme are a draft until Save; language and theme need a restart (marked).
Item {
    id: page

    required property var app
    readonly property var st: app.settingsVm
    signal licenseRequested

    Component.onCompleted: st.reload()

    // created only when wanted: the native dialog takes about a second to set up and freezes the application meanwhile
    Loader {
        id: folderLoader
        active: false
        sourceComponent: FolderDialog {
            title: qsTr("Select an empty folder or an existing library")
            onAccepted: page.app.selectFolder(selectedFolder)
        }
        onLoaded: item.open()
    }
    function openFolderDialog() {
        if (folderLoader.item)
            folderLoader.item.open()
        else
            folderLoader.active = true
    }

    component SectionTitle: Text {
        width: parent ? parent.width : 0
        color: Theme.text
        font.pixelSize: Theme.fontHeading
        font.weight: Font.Bold
        topPadding: Theme.space2
    }
    component SettingRow: Item {
        id: row
        property string label: ""
        property string note: ""
        default property alias content: slot.data
        width: parent ? parent.width : 0
        height: Theme.listRowHeight
        Text {
            id: lab
            anchors.verticalCenter: parent.verticalCenter
            text: row.label
            color: Theme.textSecondary
            font.pixelSize: Theme.fontBody
        }
        Text {
            anchors { left: lab.right; leftMargin: Theme.space2; verticalCenter: parent.verticalCenter }
            visible: row.note.length > 0
            text: row.note
            color: Theme.danger
            font.pixelSize: Theme.fontCaption
        }
        Row {
            id: slot
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            spacing: Theme.space2
        }
    }

    Flickable {
        id: scroller
        anchors { left: parent.left; right: parent.right; top: parent.top; bottom: footer.top }
        contentWidth: width
        contentHeight: column.implicitHeight + Theme.space4
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        Column {
            id: column
            x: Theme.space4
            y: Theme.space2
            width: scroller.width - Theme.space4 * 2
            spacing: Theme.space1

            SectionTitle { text: qsTr("Language") }
            SettingRow {
                label: qsTr("Interface language")
                note: page.st.restartNeeded ? qsTr("Restart required") : ""
                PillButton {
                    id: languageButton
                    text: page.st.languageName
                    onClicked: languageMenu.toggle()
                    FadePopup {
                        id: languageMenu
                        property double closedAt: 0
                        onAboutToHide: closedAt = Date.now()
                        function toggle() { if (visible) close(); else if (Date.now() - closedAt > 250) open() }
                        y: languageButton.height + Theme.space1
                        x: languageButton.width - width
                        padding: Theme.space2
                        background: PopupSurface {}
                        contentItem: Column {
                            spacing: 0
                            Repeater {
                                model: page.st.languages
                                delegate: ElideText {
                                    required property var modelData
                                    width: Theme.px(160)
                                    height: Theme.px(30)
                                    text: modelData.name
                                    font.weight: modelData.code === page.st.language ? Font.Bold : Font.Normal
                                    underlineOnHover: true
                                    onClicked: {
                                        page.st.language = modelData.code
                                        languageMenu.close()
                                    }
                                }
                            }
                        }
                    }
                }
            }

            SectionTitle { text: qsTr("Personal information") }
            SettingRow {
                label: qsTr("Name")
                TextField {
                    width: Theme.px(220)
                    text: page.st.userName
                    placeholderText: qsTr("Listener")
                    placeholderTextColor: Theme.disabled
                    horizontalAlignment: Text.AlignRight
                    color: Theme.text
                    font.pixelSize: Theme.fontBody
                    font.weight: Font.Medium
                    selectByMouse: true
                    maximumLength: 60
                    padding: 0
                    background: Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.hairline }
                    onTextEdited: page.st.userName = text
                }
            }

            SectionTitle { text: qsTr("Visual") }
            SettingRow {
                label: qsTr("Theme")
                PillButton {
                    id: themeButton
                    text: page.st.themeName
                    onClicked: themeMenu.toggle()
                    FadePopup {
                        id: themeMenu
                        property double closedAt: 0
                        onAboutToHide: closedAt = Date.now()
                        function toggle() { if (visible) close(); else if (Date.now() - closedAt > 250) open() }
                        y: themeButton.height + Theme.space1
                        x: themeButton.width - width
                        padding: Theme.space2
                        background: PopupSurface {}
                        contentItem: Column {
                            Repeater {
                                model: page.st.themes
                                delegate: ElideText {
                                    required property var modelData
                                    width: Theme.px(160)
                                    height: Theme.px(30)
                                    text: modelData.name
                                    font.weight: modelData.code === page.st.theme ? Font.Bold : Font.Normal
                                    underlineOnHover: true
                                    onClicked: {
                                        page.st.theme = modelData.code
                                        themeMenu.close()
                                    }
                                }
                            }
                        }
                    }
                }
            }
            SettingRow {
                label: qsTr("Text size")
                PillButton {
                    id: sizeButton
                    text: page.st.textScale + " %"
                    onClicked: sizeMenu.toggle()
                    FadePopup {
                        id: sizeMenu
                        property double closedAt: 0
                        onAboutToHide: closedAt = Date.now()
                        function toggle() { if (visible) close(); else if (Date.now() - closedAt > 250) open() }
                        y: sizeButton.height + Theme.space1
                        x: sizeButton.width - width
                        padding: Theme.space2
                        background: PopupSurface {}
                        contentItem: Column {
                            Repeater {
                                model: page.st.textScales
                                delegate: ElideText {
                                    required property int modelData
                                    width: Theme.px(120)
                                    height: Theme.listRowHeight
                                    text: modelData + " %"
                                    font.weight: modelData === page.st.textScale ? Font.Bold : Font.Normal
                                    underlineOnHover: true
                                    onClicked: {
                                        page.st.textScale = modelData
                                        sizeMenu.close()
                                    }
                                }
                            }
                        }
                    }
                }
            }

            SettingRow {
                label: qsTr("Window placement")
                PillButton {
                    text: qsTr("Configure")
                    onClicked: placementPopup.openWith()
                }
            }

            SectionTitle { text: qsTr("Your Library") }
            SettingRow {
                label: qsTr("Current directory")
                ElideText {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, column.width * 0.45)
                    text: page.st.libraryPath
                    font.pixelSize: Theme.fontBody
                    font.weight: Font.Medium
                    horizontalAlignment: Text.AlignRight
                }
                PillButton { text: qsTr("Change"); active: !page.st.rescanning; onClicked: page.openFolderDialog() }
            }
            SettingRow {
                label: qsTr("Rescan library")
                Rectangle {
                    visible: page.st.rescanning
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.px(120)
                    height: 8
                    radius: 4
                    color: Theme.sliderTrack
                    Rectangle {
                        width: Math.max(8, parent.width * page.st.rescanProgress)
                        height: parent.height
                        radius: 4
                        color: Theme.text
                    }
                }
                PillButton {
                    text: page.st.rescanning ? qsTr("Cancel") : qsTr("Rescan")
                    onClicked: page.st.rescanning ? page.st.cancelRescan() : page.st.requestRescan()
                }
            }
            SettingRow {
                label: qsTr("Logs folder")
                PillButton { text: qsTr("Open"); onClicked: page.st.openLogs() }
            }

            SectionTitle { text: qsTr("About") }
            SettingRow {
                label: qsTr("GitHub")
                PillButton { text: qsTr("Open"); onClicked: page.st.openSite() }
            }
            SettingRow {
                label: qsTr("License")
                PillButton { text: qsTr("Open"); onClicked: page.licenseRequested() }
            }

            SectionTitle { text: qsTr("Support") }
            SettingRow {
                label: qsTr("Patreon")
                PillButton { text: qsTr("Open"); onClicked: page.st.openSupport() }
            }
        }
    }

    Item {
        id: footer
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: Theme.px(56)
        Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.hairline }
        ElideText {
            anchors { left: parent.left; leftMargin: Theme.space4; right: buttonsRow.left; rightMargin: Theme.space3; verticalCenter: parent.verticalCenter }
            text: qsTr("Version: %1").arg(page.st.version)
            color: Theme.textSecondary
            font.pixelSize: Theme.fontCaption
        }
        Row {
            id: buttonsRow
            anchors { right: parent.right; rightMargin: Theme.space4; verticalCenter: parent.verticalCenter }
            spacing: Theme.space2
            IconButton {
                icon: "close"
                outlined: true
                tooltip: qsTr("Cancel")
                iconSize: Theme.iconSmall
                padding: Theme.space2
                onClicked: { page.st.reload(); page.app.nav.back() }
            }
            IconButton {
                icon: "save"
                outlined: true
                tooltip: qsTr("Save")
                iconSize: Theme.iconSmall
                padding: Theme.space2
                active: page.st.dirty
                onClicked: page.st.save()
            }
        }
    }
    // ---- Window placement: a plan of the window; the blocks are dragged to where they should be. Saving keeps and applies it at once.
    FadePopup {
        id: placementPopup
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        padding: Theme.space3
        closePolicy: Popup.CloseOnEscape
        function openWith() {
            board.order = page.app.settingsVm.layoutOrder
            board.queueAbove = page.app.settingsVm.layoutQueueAbove
            board.hotbarTop = page.app.settingsVm.layoutHotbarTop
            open()
        }
        background: PopupSurface { radius: Theme.radiusPanel }
        contentItem: Column {
            spacing: Theme.space2
            Text { text: qsTr("Window placement"); color: Theme.text; font.pixelSize: Theme.fontHeading; font.weight: Font.Bold }
            LayoutBoard {
                id: board
                width: Theme.px(340)
                height: Theme.px(210)
            }
            Item {
                width: parent.width
                height: closeButton.height
                Row {
                    anchors.right: parent.right
                    spacing: Theme.space2
                    IconButton {
                        id: closeButton
                        icon: "close"
                        outlined: true
                        tooltip: qsTr("Cancel")
                        iconSize: Theme.iconSmall
                        padding: Theme.space2
                        onClicked: placementPopup.close()
                    }
                    IconButton {
                        icon: "save"
                        outlined: true
                        tooltip: qsTr("Save")
                        iconSize: Theme.iconSmall
                        padding: Theme.space2
                        onClicked: {
                            page.app.settingsVm.setLayout(board.order, board.queueAbove, board.hotbarTop)
                            placementPopup.close()
                        }
                    }
                }
            }
        }
    }
}
