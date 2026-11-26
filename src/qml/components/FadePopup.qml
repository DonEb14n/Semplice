import QtQuick
import QtQuick.Controls.Basic

// A popup that fades and grows a little when it opens and shrinks back when it closes (menus, pickers, dialogs).
Popup {
    id: fadePopup
    // modal without dimming: while a menu or a picker is open nothing under it gets a click (the first click outside only closes it)
    modal: true
    dim: false
    // counted in a Connections so that the pages using the popup can have their own onAboutToShow / onClosed
    property bool counted: false
    Connections {
        target: fadePopup
        function onAboutToShow() { if (!fadePopup.counted) { fadePopup.counted = true; Theme.openPopups++ } }
        function onAboutToHide() { Theme.popupClosedAt = Date.now() }
        function onClosed() { if (fadePopup.counted) { fadePopup.counted = false; Theme.openPopups-- } }
    }
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: Theme.durationFast; easing.type: Easing.OutCubic }
        NumberAnimation { property: "scale"; from: 0.94; to: 1.0; duration: Theme.durationNormal; easing.type: Easing.OutCubic }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1.0; to: 0.0; duration: Theme.durationFast; easing.type: Easing.InCubic }
        NumberAnimation { property: "scale"; from: 1.0; to: 0.96; duration: Theme.durationFast; easing.type: Easing.InCubic }
    }
}
