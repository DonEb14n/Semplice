pragma Singleton
import QtQuick

// Design tokens ("True White", and the dark theme from the owner's palette). Views use ONLY these values; see docs/DESIGN_TOKENS.md.
QtObject {
    // the dark theme is on (set by Main.qml from the setting "Theme": True White, Dark or System)
    property bool dark: false

    // Menus and pickers that are open (FadePopup counts itself in and out). A click that a popup took must not also be taken by a link or a
    // button under it: the pointer handlers of the page below still see that press, so they ask `underPopup(item)` first.
    property int openPopups: 0
    // is the item part of a popup (the content of a Popup hangs below its "popup item")
    function inOverlay(item) {
        for (let p = item; p; p = p.parent)
            if (String(p).indexOf("PopupItem") >= 0)
                return true
        return false
    }
    function underPopup(item) { return openPopups > 0 && !inOverlay(item) }
    // The release that ends a click on a menu item is also seen by the handlers under the menu (it closes on that very click):
    // a click that comes within a moment of a popup closing is the popup's, not theirs.
    property double popupClosedAt: 0
    function tapAllowed(item) { return !underPopup(item) && Date.now() - popupClosedAt > 200 }

    // ---- colours (SPEC 8.2; dark palette: owner's file "Need to fix pt4")
    readonly property color background: dark ? "#2C2C2C" : "#9C9C9C"       // the window behind the panels
    readonly property color surface: dark ? "#1E1E1E" : "#FFFFFF"          // the panels (blocks)
    readonly property color text: dark ? "#FFFFFF" : "#1E1E1E"
    readonly property color textSecondary: dark ? "#B3B3B3" : "#5A5A5A"
    readonly property color disabled: dark ? "#5A5A5A" : "#B3B3B3"
    readonly property color danger: "#C00F0C"
    readonly property color border: dark ? "#3A3A3A" : "#D9D9D9"
    readonly property color coverPlaceholder: "#414141"
    readonly property color coverPlaceholderIcon: "#8A8A8A"
    readonly property color avatarBackground: dark ? "#FFFFFF" : "#2C2C2C"   // the round picture without a picture (the letter uses `surface`)
    readonly property color coverBackground: dark ? "#414141" : "#2C2C2C"       // a cover (square picture) without a picture
    readonly property color carouselTone1: dark ? "#5A5A5A" : "#787878"      // tiles next to the middle one (Figma)
    readonly property color carouselTone2: dark ? "#787878" : "#9C9C9C"      // the outer tiles
    readonly property color highlight: dark ? "#2C2C2C" : "#F5F5F5"           // the current row of the queue, hovered rows
    readonly property color sliderTrack: dark ? "#3A3A3A" : "#F5F5F5"
    readonly property color hairline: dark ? "#5A5A5A" : "#B3B3B3"            // line under panel titles, scroll bars
    readonly property color overlay: "#66000000"
    readonly property color onImage: "#FFFFFF"                                  // an icon on a picture / on the dark overlay
    readonly property color scrim: dark ? "#99000000" : "#99FFFFFF"             // what covers the program behind a dialog
    readonly property color dropZone: dark ? "#B31E1E1E" : "#B3FFFFFF"         // the square behind the import icon
    readonly property color bannerBackground: dark ? "#3A3A3A" : "#2B2B2B"
    readonly property color bannerError: "#7A1210"

    // ---- scaling: the window size (uiScale, set by Main.qml: 1 at 1280 x 760, up to 1.6 when maximised) and the
    // owner's text size setting (textScale, Settings -> Visual) multiply the sizes below
    property real uiScale: 1.0
    property real textScale: 1.0
    readonly property real fontScale: uiScale * textScale

    // ---- spacing (4/8 px grid)
    readonly property int space1: Math.round(4 * uiScale)
    readonly property int space2: Math.round(8 * uiScale)
    readonly property int space3: Math.round(12 * uiScale)
    readonly property int space4: Math.round(16 * uiScale)
    readonly property int space5: Math.round(24 * uiScale)
    readonly property int space6: Math.round(32 * uiScale)

    // ---- radii
    readonly property int radiusPanel: 8
    readonly property int radiusControl: 8
    readonly property int radiusPill: 999
    readonly property color focusRing: dark ? "#FFFFFF" : "#1E1E1E"
    readonly property int focusRingWidth: 2
    readonly property int strokeWidth: 2          // the outline of buttons
    readonly property real hoverScale: 1.06       // a button grows a little under the mouse

    // ---- type scale (pixels)
    function px(n) { return Math.round(n * uiScale) }

    readonly property int fontDisplay: Math.round(38 * fontScale)
    readonly property int fontTitle: Math.round(24 * fontScale)
    readonly property int fontHeading: Math.round(19 * fontScale)
    readonly property int fontBody: Math.round(15 * fontScale)
    readonly property int fontCaption: Math.round(13 * fontScale)

    // ---- control sizes (grow with the text and the window)
    readonly property int rowHeight: Math.round(52 * fontScale)       // a track row
    readonly property int controlHeight: Math.round(32 * fontScale)   // pill buttons, chips, fields
    readonly property int listRowHeight: Math.round(40 * fontScale)   // simple list / table rows

    // ---- icon sizes
    readonly property int iconSmall: Math.round(18 * uiScale)
    readonly property int iconMedium: Math.round(22 * uiScale)
    readonly property int iconLarge: Math.round(26 * uiScale)

    // ---- animation durations (ms)
    readonly property int durationFast: 120
    readonly property int durationNormal: 200

    // ---- window layout (SPEC 8.1)
    readonly property int windowDefaultWidth: 1280
    readonly property int windowDefaultHeight: 760
    readonly property int windowMinWidth: 1000
    readonly property int windowMinHeight: 620
    readonly property int gap: 8
    readonly property int titleBarHeight: Math.round(32 * uiScale)   // the window header (TitleBar.qml)
    readonly property int hotbarHeight: Math.round(60 * uiScale)
    readonly property real libraryFraction: 0.24
    readonly property int libraryMinWidth: 260
    readonly property int libraryMaxWidth: Math.round(360 * uiScale)
    readonly property real rightColumnFraction: 0.26
    readonly property int rightColumnMinWidth: 300
    readonly property int rightColumnMaxWidth: Math.round(400 * uiScale)
    readonly property int middleMinWidth: 420
    readonly property real playerHeightFraction: 0.63
    readonly property int sidePanelMinHeight: 200
}
