import QtQuick
import QtQuick.Controls.Fusion

// A scrollbar wide enough to drag with a finger (about 8 mm), shown only when
// the list holds more than fits. Rows leave `room` for it on the right.
ScrollBar {
    id: bar
    readonly property real room: policy === ScrollBar.AlwaysOn ? width + 4 : 0
    // Grids set this from their row count: reading `size` there would loop
    // (the bar's room changes the cells, which change the content height).
    property bool needed: size < 0.999

    policy: needed ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
    width: Math.round(Math.max(28, Math.min(44, Screen.pixelDensity * 8)))
    padding: 3
    minimumSize: 0.12

    background: Rectangle {
        radius: width / 2
        color: "#141820"
        opacity: 0.85
    }
    contentItem: Rectangle {
        implicitWidth: bar.width - 6
        radius: width / 2
        color: bar.pressed ? "#7aa7ff" : "#56627a"
    }
}
