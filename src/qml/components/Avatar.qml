import QtQuick
import QtQuick.Effects

// A round picture, or the first letter on dark grey when there is none (UI doc p.5, p.35).
Item {
    id: root

    property string source: ""
    property string letter: ""
    property bool round: true

    implicitWidth: Theme.px(40)
    implicitHeight: Theme.px(40)

    Rectangle {
        id: base
        anchors.fill: parent
        radius: root.round ? width / 2 : Theme.radiusControl
        color: root.round ? Theme.avatarBackground : Theme.coverBackground
        visible: image.status !== Image.Ready

        Text {
            anchors.centerIn: parent
            text: root.letter
            color: Theme.surface
            font.pixelSize: Math.max(8, Math.round(root.width * 0.46))  // follows the circle, not the text size
            font.weight: Font.Medium
        }
        Icon {
            anchors.centerIn: parent
            visible: root.letter.length === 0
            name: root.round ? "person" : "music-note"   // a person for an artist, a note for a cover
            size: Math.max(Theme.iconSmall, root.width * 0.4)
            color: Theme.coverPlaceholderIcon
        }
    }

    // The size asked from the image provider is rounded up to 64 / 128 / 256 / 512 / 1024 px, so resizing the
    // window does not reload the picture; a new picture is swapped in only when it is ready (no blank flash).
    readonly property int pixels: {
        const v = Math.max(width, height) * Screen.devicePixelRatio
        return v <= 64 ? 64 : v <= 128 ? 128 : v <= 256 ? 256 : v <= 512 ? 512 : 1024
    }
    // The round / rounded shape is cut by the image provider (a soft edge, once), so no off-screen layer and no mask effect is
    // needed here: one of each per picture made big tables very slow on some graphics drivers.
    // The corner radius is asked in steps of 25 %: while the window is being resized the same picture keeps being used
    // (a new radius is a new picture) and the eye cannot tell a difference of that size.
    readonly property real radiusRatio: {
        const r = Math.max(0.005, Math.min(0.5, Theme.radiusControl / Math.max(1, width)))
        return Math.round(Math.pow(1.25, Math.round(Math.log(r) / Math.log(1.25))) * 1000) / 1000
    }
    readonly property string shapedSource: source.indexOf("image://") === 0
        ? source + (source.indexOf("?") < 0 ? "?" : "&") + "shape=" + (round ? "circle" : "rounded") + "&r=" + radiusRatio
        : source
    property string readySource: ""
    property int readyPixels: 64            // the size that picture was loaded at (the shown image keeps it until the next one is ready)
    onSourceChanged: if (source.length === 0) readySource = ""
    onShapedSourceChanged: if (source.length === 0) readySource = ""

    Image {
        id: probe
        visible: false
        source: root.shapedSource
        sourceSize: Qt.size(root.pixels, root.pixels)
        asynchronous: true
        cache: true
        onStatusChanged: if (status === Image.Ready) {
            root.readyPixels = probe.sourceSize.width
            root.readySource = root.shapedSource
        }
    }
    // a picture that does not come from the provider (the one just chosen in an editor is a plain file) is cut to the shape here
    readonly property bool shapeHere: root.source.length > 0 && root.source.indexOf("image://") !== 0
    Rectangle {
        id: shapeMask
        anchors.fill: parent
        radius: root.round ? width / 2 : Theme.radiusControl
        visible: false
        layer.enabled: true
    }
    Image {
        id: image
        anchors.fill: parent
        layer.enabled: root.shapeHere
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: shapeMask
        }
        visible: status === Image.Ready
        source: root.readySource
        sourceSize: Qt.size(root.readyPixels, root.readyPixels)
        fillMode: root.shapeHere ? Image.PreserveAspectCrop : Image.PreserveAspectFit
        smooth: true
        asynchronous: true
        cache: true
    }
}
