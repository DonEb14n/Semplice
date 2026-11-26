pragma Singleton
import QtQuick

// Small formatting helpers shared by the views.
QtObject {
    // 83000 -> "1:23", 3725000 -> "1:02:05"
    function time(ms) {
        var total = Math.max(0, Math.floor(ms / 1000))
        var h = Math.floor(total / 3600)
        var m = Math.floor((total % 3600) / 60)
        var s = total % 60
        var ss = (s < 10 ? "0" : "") + s
        if (h > 0)
            return h + ":" + (m < 10 ? "0" : "") + m + ":" + ss
        return m + ":" + ss
    }
}
