import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects

ApplicationWindow {
    id: root

    required property var shell
    required property var app

    // Shown by C++ (ShellVm.attach) after the saved geometry has been restored.
    visible: false
    title: "Semplice"
    // no Windows header: TitleBar.qml draws the app's own (the same on every Windows version, in the colours of the theme)
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowMinMaxButtonsHint | Qt.WindowSystemMenuHint
    color: Theme.background
    width: Theme.windowDefaultWidth
    height: Theme.windowDefaultHeight
    minimumWidth: Theme.windowMinWidth
    minimumHeight: Theme.windowMinHeight

    // AppController.Startup values
    readonly property int stateReady: 3
    // the other layout of the player: only the queue stays in the right column, the controls and the progress bar are in the hotbar
    property bool playerCollapsed: false
    readonly property bool typing: activeFocusItem !== null && activeFocusItem.hasOwnProperty("cursorPosition")

    // the sizes of everything follow the window (1 at 1280 x 760, up to 1.6) and the text size of the settings
    Binding {
        target: Theme
        property: "uiScale"
        value: Math.max(1.0, Math.min(1.6, Math.min(root.width / Theme.windowDefaultWidth, root.height / Theme.windowDefaultHeight)))
    }
    Binding {
        target: Theme
        property: "dark"
        value: root.app.settingsVm.darkApplied
    }
    Binding {
        target: Theme
        property: "textScale"
        value: root.app.settingsVm.appliedTextScale / 100
    }

    // ---- keyboard (SPEC 8.7)
    Shortcut { sequence: "Space"; enabled: !root.typing; onActivated: root.app.player.toggle() }
    Shortcut { sequence: "Ctrl+Right"; onActivated: root.app.player.next() }
    Shortcut { sequence: "Ctrl+Left"; onActivated: root.app.player.previous() }
    Shortcut { sequence: "Ctrl+Up"; onActivated: root.app.player.stepVolume(0.05) }
    Shortcut { sequence: "Ctrl+Down"; onActivated: root.app.player.stepVolume(-0.05) }
    Shortcut { sequence: "M"; enabled: !root.typing; onActivated: root.app.player.toggleMute() }
    Shortcut { sequence: "Ctrl+F"; onActivated: hotbar.focusSearch() }
    Shortcut { sequence: "Alt+Left"; onActivated: root.app.nav.back() }
    Shortcut { sequence: "Alt+Right"; onActivated: root.app.nav.forward() }
    Shortcut { sequence: "Escape"; onActivated: root.app.closePopups() }

    // Fixed-proportion zones (no splitters in v1.0): library | middle | right column, hotbar below.
    TitleBar {
        id: titleBar
        win: root
        z: 90
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
        }
    }
    ResizeEdges {
        anchors.fill: parent
        z: 100
        win: root
    }

    Item {
        id: content
        anchors {
            left: parent.left
            right: parent.right
            top: titleBar.bottom
            bottom: parent.bottom
            margins: Theme.gap
        }

        // a click anywhere but in the search field takes the cursor out of it
        TapHandler {
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: (point) => {
                const p = hotbar.searchField.mapFromItem(content, point.position)
                if (!hotbar.searchField.contains(p))
                    content.forceActiveFocus()
            }
        }

        readonly property real bodyHeight: height - Theme.hotbarHeight - Theme.gap
        readonly property real libraryWidth: Math.max(Theme.libraryMinWidth,
                                                      Math.min(Theme.libraryMaxWidth, width * Theme.libraryFraction))
        readonly property real rightWidth: Math.max(Theme.rightColumnMinWidth,
                                                    Math.min(Theme.rightColumnMaxWidth,
                                                             width * Theme.rightColumnFraction))
        // where the windows stand (Settings -> Window placement): the columns left to right, the queue above the player, the hotbar on top
        readonly property var columnOrder: root.app.settingsVm.layoutOrder
        readonly property bool queueAbove: root.app.settingsVm.layoutQueueAbove
        readonly property bool hotbarTop: root.app.settingsVm.layoutHotbarTop
        readonly property real mainWidth: width - libraryWidth - rightWidth - Theme.gap * 2
        readonly property real bodyY: hotbarTop ? Theme.hotbarHeight + Theme.gap : 0
        function columnWidth(name) { return name === "library" ? libraryWidth : name === "right" ? rightWidth : mainWidth }
        function columnX(name) {
            let x = 0
            for (const n of columnOrder) {
                if (n === name)
                    return x
                x += columnWidth(n) + Theme.gap
            }
            return 0
        }
        readonly property real playerHeight: Math.min(bodyHeight * Theme.playerHeightFraction,
                                                      bodyHeight - Theme.sidePanelMinHeight - Theme.gap)

        // all panels: blurred while no library is open (Figma "First launch")
        Item {
            id: panelsLayer
            anchors.fill: parent
            layer.enabled: root.app.startup !== root.stateReady
            layer.effect: MultiEffect {
                blurEnabled: true
                blur: 1.0
                blurMax: 40
            }

        LibraryPanel {
            id: library
            app: root.app
            x: content.columnX("library")
            y: content.bodyY
            width: content.libraryWidth
            height: content.bodyHeight
        }

        MiddleHost {
            id: middle
            app: root.app
            shell: root.shell
            x: content.columnX("main")
            y: content.bodyY
            width: content.mainWidth
            height: content.bodyHeight
        }

        Item {
            id: rightColumn
            x: content.columnX("right")
            y: content.bodyY
            width: content.rightWidth
            height: content.bodyHeight

            PlayerPanel {
                id: player
                app: root.app
                y: content.queueAbove ? parent.height - height : 0
                width: parent.width
                collapsed: root.playerCollapsed
                height: root.playerCollapsed ? Math.max(Theme.px(120), content.playerHeight - player.foldedSaving) : content.playerHeight
                Behavior on height { NumberAnimation { duration: Theme.durationNormal * 1.6; easing.type: Easing.InOutCubic } }
                onToggleRequested: root.playerCollapsed = !root.playerCollapsed
            }
            SidePanel {
                app: root.app
                wide: root.playerCollapsed
                x: 0
                width: parent.width
                y: content.queueAbove ? 0 : player.height + Theme.gap
                height: parent.height - player.height - Theme.gap
            }
        }

        HotbarPanel {
            id: hotbar
            app: root.app
            libraryX: library.x
            libraryWidth: library.width
            middleX: middle.x
            middleWidth: middle.width
            rightX: rightColumn.x
            rightWidth: rightColumn.width
            collapsed: root.playerCollapsed
            x: 0
            y: content.hotbarTop ? 0 : parent.height - height
            width: parent.width
            height: Theme.hotbarHeight
        }
        }  // panelsLayer

        // ---- first launch / library not available: everything is dimmed, the card in the middle asks for a folder
        MouseArea {  // nothing reacts but the card
            anchors.fill: parent
            z: 40
            visible: root.app.startup !== root.stateReady
            hoverEnabled: true
        }
        SelectFolderCard {
            app: root.app
            z: 41
            visible: root.app.startup === 1 || root.app.startup === 2 || root.app.startup === 4
            x: middle.x + (middle.width - width) / 2
            y: middle.y + (middle.height - height) / 2
        }

        // ---- popups: any click outside closes them (the hotbar closes them itself)
        MouseArea {
            x: 0
            y: content.bodyY
            width: parent.width
            height: content.bodyHeight
            z: 50
            visible: root.app.popup !== ""
            onPressed: (mouse) => {
                root.app.closePopups()
                content.forceActiveFocus()  // the search field loses the cursor too
            }
        }
        SearchPopup {
            app: root.app
            z: 60
            opacity: root.app.popup === "search" ? 1 : 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
            x: library.x
            width: Math.min(380, content.width * 0.4)
            height: Math.min(content.bodyHeight * 0.8, 520)
            y: content.hotbarTop ? hotbar.height + Theme.gap : hotbar.y - height - Theme.gap   // opens away from the hotbar
        }
        QueuePopup {
            app: root.app
            z: 60
            opacity: root.app.popup === "queue" ? 1 : 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
            width: Math.min(380, content.width * 0.4)
            height: Math.min(content.bodyHeight * 0.8, 520)
            x: content.width - width
            y: content.hotbarTop ? hotbar.height + Theme.gap : hotbar.y - height - Theme.gap
        }
    }

    // First launch: the license must be accepted (SPEC 8.8); "Exit app" closes the program
    LicenseDialog {
        id: firstLaunchLicense
        acceptRequired: true
        licenseText: root.app.settingsVm.licenseText
        onAccepted: root.app.settingsVm.acceptLicense()
        onExitRequested: Qt.quit()
    }
    Component.onCompleted: if (!root.app.settingsVm.licenseAccepted) firstLaunchLicense.open()
}
