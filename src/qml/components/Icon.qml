import QtQuick
import QtQuick.Effects

// Single-colour SVG icon. The SVG files are white; the colour comes from the theme.
Item {
    id: root

    property string name: ""
    property color color: Theme.text
    property int size: Theme.iconMedium

    implicitWidth: size
    implicitHeight: size

    Image {
        id: image
        anchors.fill: parent
        visible: false
        source: root.name.length > 0 ? "qrc:/icons/" + root.name + ".svg" : ""
        sourceSize: Qt.size(root.size * 2, root.size * 2)
        fillMode: Image.PreserveAspectFit
        smooth: true
    }

    MultiEffect {
        anchors.fill: image
        source: image
        colorization: 1.0
        colorizationColor: root.color
    }
}
