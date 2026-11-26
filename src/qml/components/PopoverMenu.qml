import QtQuick
import QtQuick.Controls.Basic

// A small white menu of text links (the "..." menu of a track, Figma "Playlists"). `entries` is a list of
// { label, id, enabled }; the chosen id comes out through `chosen`.
FadePopup {
    id: root

    property var entries: []
    signal chosen(string id)
    property double closedAt: 0
    onAboutToHide: closedAt = Date.now()

    padding: Theme.space2
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: PopupSurface {}
    contentItem: Column {
        spacing: 2
        Repeater {
            model: root.entries
            delegate: ElideText {
                required property var modelData
                width: Math.max(110, implicitWidth)
                text: modelData.label
                color: modelData.enabled === false ? Theme.disabled : (modelData.danger ? Theme.danger : Theme.text)
                underlineOnHover: modelData.enabled !== false
                onClicked: {
                    if (modelData.enabled === false)
                        return
                    root.close()
                    root.chosen(modelData.id)
                }
            }
        }
    }
}
