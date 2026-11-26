import QtQuick

// The background of a menu or a picker that opens over the page: the usual white rounded surface, and every click or wheel turn
// on it stops here. (Pointer handlers of the page below - the tabs, the rows - also see a click that a popup took, unless
// something under the popup's content takes it first.)
Rectangle {
    color: Theme.surface
    radius: Theme.radiusControl
    border.color: Theme.border

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        hoverEnabled: true
        onWheel: (wheel) => wheel.accepted = true
    }
}
