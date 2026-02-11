import QtQuick
import QtQuick.Controls

Rectangle {
    id: zone

    property string zoneType: "button"
    property string zoneText: ""
    property int zoneId: 0
    property bool editMode: false
    property color zoneColor: "#81A1C1"
    property color zoneBorderColor: "#4C566A"
    property int zoneBorderWidth: 1
    property int zoneRadius: 8

    signal zoneClicked(int zoneId)
    signal zoneEdited(int zoneId, var properties)

    color: editMode ? "#88C0D0" : zoneColor
    border.color: editMode ? "#5E81AC" : zoneBorderColor
    border.width: editMode ? 3 : zoneBorderWidth
    radius: zoneRadius

    // Zone content based on type
    Loader {
        id: contentLoader
        anchors.fill: parent
        anchors.margins: 5

        sourceComponent: {
            switch (zoneType) {
                case "button":
                    return buttonComponent
                case "login":
                    return loginButtonComponent
                case "label":
                    return labelComponent
                case "input":
                    return inputComponent
                default:
                    return buttonComponent
            }
        }
    }

    // Button zone component
    Component {
        id: buttonComponent
        Button {
            text: zoneText
            font.pixelSize: 16
            flat: true

            background: Rectangle {
                color: "transparent"
            }

            contentItem: Text {
                text: parent.text
                color: "white"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font: parent.font
                wrapMode: Text.Wrap
            }

            onClicked: zoneClicked(zoneId)
        }
    }

    // Login button zone component
    Component {
        id: loginButtonComponent
        Button {
            text: zoneText || "Login"
            font.pixelSize: 18
            font.bold: true
            flat: true

            background: Rectangle {
                color: "transparent"
            }

            contentItem: Text {
                text: parent.text
                color: "white"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font: parent.font
                wrapMode: Text.Wrap
            }

            onClicked: zoneClicked(zoneId)
        }
    }

    // Label zone component
    Component {
        id: labelComponent
        Text {
            text: zoneText
            color: "white"
            font.pixelSize: 16
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.Wrap
        }
    }

    // Input zone component
    Component {
        id: inputComponent
        TextField {
            text: zoneText
            font.pixelSize: 16
            color: "white"

            background: Rectangle {
                color: "#4C566A"
                radius: 4
            }

            onTextChanged: {
                if (editMode) {
                    zoneEdited(zoneId, {text: text})
                }
            }
        }
    }

    // Edit mode interactions
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        enabled: editMode

        onClicked: function(mouse) {
            if (mouse.button === Qt.RightButton) {
                showZoneEditor()
            }
        }
    }

    // Zone ID label in edit mode
    Text {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: 2
        text: zoneId.toString()
        color: "yellow"
        font.pixelSize: 10
        font.bold: true
        visible: editMode
    }

    // Resize handles in edit mode
    Rectangle {
        width: 10
        height: 10
        color: "#BF616A"
        visible: editMode
        anchors.bottom: parent.bottom
        anchors.right: parent.right

        MouseArea {
            anchors.fill: parent
            drag.target: zone
            drag.axis: Drag.XAndYAxis
            drag.minimumX: 0
            drag.minimumY: 0
            drag.maximumX: zone.parent.width - zone.width
            drag.maximumY: zone.parent.height - zone.height

            onPositionChanged: {
                zone.width = Math.max(50, zone.width + mouse.x - pressX)
                zone.height = Math.max(30, zone.height + mouse.y - pressY)
                pressX = mouse.x
                pressY = mouse.y
            }

            property real pressX: 0
            property real pressY: 0

            onPressed: {
                pressX = mouse.x
                pressY = mouse.y
            }

            onReleased: {
                zoneEdited(zoneId, {width: zone.width, height: zone.height, x: zone.x, y: zone.y})
            }
        }
    }

    function showZoneEditor() {
        // Open a comprehensive zone properties dialog
        zoneEdited(zoneId, {
            action: "openPropertiesDialog",
            zoneType: zoneType,
            text: zoneText,
            color: zoneColor,
            borderColor: zoneBorderColor,
            borderWidth: zoneBorderWidth,
            radius: zoneRadius,
            x: zone.x,
            y: zone.y,
            width: zone.width,
            height: zone.height
        })
    }
}