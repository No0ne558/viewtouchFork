import QtQuick
import QtQuick.Window

// M0 placeholder. M1 replaces this with PageView driven by the layout model:
// every page is authored on a logical canvas and scaled uniformly to the screen.
Window {
    id: root
    width: 1280
    height: 720
    visible: true
    title: qsTr("ViewTouch")
    color: "#1e1f24"

    readonly property size canvas: Qt.size(1920, 1080)
    readonly property real scaleFactor: Math.min(width / canvas.width, height / canvas.height)

    Item {
        id: page
        width: root.canvas.width
        height: root.canvas.height
        scale: root.scaleFactor
        transformOrigin: Item.TopLeft
        x: (root.width - width * scale) / 2
        y: (root.height - height * scale) / 2

        Rectangle {
            anchors.fill: parent
            color: "#2a2c33"
        }

        Rectangle {
            x: 760; y: 440; width: 400; height: 200
            radius: 16
            color: tap.pressed ? "#3d7bd9" : "#2f6fd0"

            Text {
                anchors.centerIn: parent
                text: qsTr("ViewTouch Modern")
                color: "white"
                font.pixelSize: 40
                font.bold: true
            }

            TapHandler { id: tap }
        }
    }
}
