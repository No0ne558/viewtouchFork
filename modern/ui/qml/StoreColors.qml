pragma Singleton
import QtQuick

// The colors offered for categories, item buttons and page buttons: one list
// for the Menu Builder, the order screen's Arrange and the page editor. Ten
// colors, each dark, middle and light (a row each, in that order); the middle
// row holds the eight colors offered before, so stores' colors still match.
QtObject {
    readonly property var dark:   ["#7a1f1f", "#a8521a", "#7a5410", "#1f5f3a", "#145257", "#1d4580", "#4a2f8f", "#8a1f5c", "#5c3b1d", "#2b3038"]
    readonly property var middle: ["#b83232", "#d9732b", "#a86a12", "#1f8a4c", "#1f6f73", "#2b62b0", "#6b46c1", "#c2408a", "#8a5a2b", "#4a5260"]
    readonly property var light:  ["#e06666", "#f2a65a", "#f2d16b", "#6cc58a", "#4fb3b0", "#6c9ce0", "#a58be0", "#ec8fc0", "#c49a6c", "#9aa3b0"]
    readonly property var all: dark.concat(middle).concat(light)
    // New categories take these in turn.
    readonly property var starters: ["#a86a12", "#b83232", "#1f8a4c", "#1f6f73", "#2b62b0", "#6b46c1", "#8a5a2b", "#4a5260",
                                     "#d9732b", "#c2408a", "#145257", "#7a1f1f"]

    // Text that reads on it: dark on light colors, white on the rest.
    function ink(c) {
        if (!c) return "white"
        const k = Qt.color(c)
        return 0.299 * k.r + 0.587 * k.g + 0.114 * k.b > 0.6 ? "#14171c" : "white"
    }
    function same(a, b) {
        return !!a && !!b && String(a).toLowerCase() === String(b).toLowerCase()
    }
    function valid(c) {
        return /^#[0-9a-fA-F]{6}$/.test(String(c ?? "").trim())
    }
}
