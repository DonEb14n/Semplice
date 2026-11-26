import QtQuick
import QtQuick.Effects

// Information / error banners at the bottom of the middle window (UI doc p.27-28, Figma "Middle window all
// conditions"): white cards with a bold title, one sentence and a round close button; at most three, the newest
// below, they stay until closed.
Column {
    id: host

    required property var banners
    spacing: Theme.space2

    Repeater {
        model: host.banners
        delegate: Rectangle {
            id: banner
            required property int bannerId
            required property string title
            required property string text
            required property bool isError
            width: host.width
            height: Math.max(52, content.implicitHeight + Theme.space3 * 2)
            radius: Theme.radiusControl
            color: Theme.surface
            border.width: 1
            border.color: banner.isError ? Theme.danger : Theme.hairline

            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowColor: "#40000000"
                shadowBlur: 0.6
                shadowVerticalOffset: 2
            }

            Column {
                id: content
                anchors {
                    left: parent.left
                    right: closeButton.left
                    leftMargin: Theme.space3
                    rightMargin: Theme.space2
                    verticalCenter: parent.verticalCenter
                }
                Text {
                    width: parent.width
                    text: banner.title
                    color: Theme.text
                    font.pixelSize: Theme.fontBody
                    font.weight: Font.Bold
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    text: banner.text
                    color: Theme.text
                    font.pixelSize: Theme.fontBody
                    wrapMode: Text.WordWrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                }
            }
            IconButton {
                id: closeButton
                anchors {
                    right: parent.right
                    rightMargin: Theme.space2
                    top: parent.top
                    topMargin: Theme.space2
                }
                icon: "close"
                outlined: true
                iconSize: 12
                padding: Theme.space1
                tooltip: qsTr("Close")
                onClicked: host.banners.dismiss(banner.bannerId)
            }
        }
    }
}
