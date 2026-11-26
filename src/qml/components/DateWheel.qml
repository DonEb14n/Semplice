import QtQuick
import QtQuick.Controls.Basic

// The release date: a button with the date ("2024-05-17", "-" for an unknown month or day) that opens three wheels.
// Invalid dates (a day without a month, a day the month does not have, a year out of range) are red.
Item {
    id: root

    property int year: 0
    property int month: 0
    property int day: 0
    property bool valid: true
    readonly property int minYear: 1744
    readonly property int maxYear: new Date().getFullYear() + 1
    signal edited(int year, int month, int day)

    implicitHeight: Theme.px(28)
    implicitWidth: label.implicitWidth + Theme.space4

    function two(n) { return n < 10 ? "0" + n : "" + n }
    readonly property string display: (year > 0 ? year : "----") + " / " + (month > 0 ? two(month) : "-") + " / " + (day > 0 ? two(day) : "-")

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusPill
        color: "transparent"
        border.width: 1
        border.color: root.valid ? Theme.text : Theme.danger
    }
    Text {
        id: label
        anchors.centerIn: parent
        text: root.display
        color: root.valid ? Theme.text : Theme.danger
        font.pixelSize: Theme.fontBody
        font.weight: Font.Medium
    }
    HoverHandler { cursorShape: Qt.PointingHandCursor }
    TapHandler {
        onTapped: {
            // a second tap on the button closes the wheels (the press had closed them already: do not open them again)
            if (popup.visible) popup.close()
            else if (Date.now() - popup.closedAt > 250) popup.open()
        }
    }

    FadePopup {
        id: popup
        property double closedAt: 0
        property int startYear: -1      // where the wheels stood when they opened: a date is only changed when a wheel was moved
        property int startMonth: -1
        property int startDay: -1
        onAboutToHide: closedAt = Date.now()
        y: root.height + Theme.space1
        padding: Theme.space2
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        // the wheels start on the date at once (the built-in wheel would roll from the first year to it, which takes seconds)
        function jump(wheel, index) {
            wheel.positionViewAtIndex(index, Tumbler.Center)   // at once, without rolling there ...
            wheel.currentIndex = index                         // ... and the wheel is told where it is (the jump leaves it one row off)
        }
        function showDate() {
            const y = root.year > 0 ? root.year : new Date().getFullYear()
            jump(yearWheel, Math.max(0, Math.min(root.maxYear - root.minYear, y - root.minYear)))
            jump(monthWheel, root.month)
            jump(dayWheel, root.day)
            startYear = yearWheel.currentIndex
            startMonth = monthWheel.currentIndex
            startDay = dayWheel.currentIndex
        }
        // the wheels are laid out when the popup is about to show (not yet when this signal comes): the next turn of the event loop
        // puts them on the date, before the first frame
        onAboutToShow: Qt.callLater(showDate)
        onClosed: {
            if (startYear >= 0 && (yearWheel.currentIndex !== startYear || monthWheel.currentIndex !== startMonth || dayWheel.currentIndex !== startDay))
                root.edited(root.minYear + yearWheel.currentIndex, monthWheel.currentIndex, dayWheel.currentIndex)
            startYear = -1
        }

        background: PopupSurface {}
        contentItem: Row {
            spacing: Theme.space2
            component Wheel: Tumbler {
                id: wheel
                property int first: 0
                property bool dash: false
                visibleItemCount: 5
                implicitWidth: Theme.px(64)
                implicitHeight: 5 * 28
                wrap: false
                // the built-in wheel moves three rows per notch: one row per notch is what a date needs
                Item {
                    anchors.fill: parent
                    z: 10
                    property real accumulated: 0
                    WheelHandler {
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        onWheel: (event) => {
                            parent.accumulated += event.angleDelta.y
                            if (Math.abs(parent.accumulated) < 120)
                                return
                            const step = parent.accumulated > 0 ? -1 : 1
                            parent.accumulated = 0
                            wheel.currentIndex = Math.max(0, Math.min(wheel.count - 1, wheel.currentIndex + step))
                        }
                    }
                }
                delegate: Text {
                    required property int index
                    readonly property int v: wheel.first + index
                    text: wheel.dash && v === 0 ? "-" : v
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    color: Theme.text
                    opacity: 1.0 - Math.min(1.0, Math.abs(Tumbler.displacement)) * 0.7
                    font.pixelSize: Theme.fontHeading
                    font.weight: Tumbler.displacement === 0 ? Font.Bold : Font.Normal
                }
            }
            Wheel { id: yearWheel; first: root.minYear; model: root.maxYear - root.minYear + 1 }
            Wheel { id: monthWheel; dash: true; model: 13 }
            Wheel { id: dayWheel; dash: true; model: 32 }
        }
    }
}
