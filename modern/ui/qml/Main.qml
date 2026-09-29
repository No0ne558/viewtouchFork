import QtQuick
import QtQuick.Window

Window {
    id: root

    required property LayoutController controller

    width: 1280
    height: 720
    visible: true
    title: qsTr("ViewTouch — %1").arg(controller.pageName)
    color: "black"

    PageView {
        id: pageView
        anchors.fill: parent
        controller: root.controller
        focus: true

        Keys.onPressed: event => {
            if (event.key === Qt.Key_Escape) {
                root.controller.goBack()
                event.accepted = true
            } else if (event.key === Qt.Key_Home) {
                root.controller.goHome()
                event.accepted = true
            } else if (event.text !== "" && root.controller.triggerHotkey(event.text)) {
                event.accepted = true
            }
        }
    }

    // Status toast for action feedback.
    Rectangle {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        width: toastText.implicitWidth + 40
        height: toastText.implicitHeight + 20
        radius: height / 2
        color: "#e0101418"
        opacity: 0
        visible: opacity > 0

        Text {
            id: toastText
            anchors.centerIn: parent
            color: "white"
            font.pixelSize: 18
            text: root.controller.statusText
        }

        Behavior on opacity { NumberAnimation { duration: 150 } }
        Timer { id: toastTimer; interval: 2000; onTriggered: toast.opacity = 0 }

        Connections {
            target: root.controller
            function onStatusChanged() {
                toast.opacity = 1
                toastTimer.restart()
            }
        }
    }
}
