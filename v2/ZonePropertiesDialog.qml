import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Window {
    id: zonePropertiesDialog
    title: "Create New Zone"
    width: 500
    height: 600
    modality: Qt.NonModal
    flags: Qt.Window | Qt.WindowStaysOnTopHint

    // Center the dialog
    x: (Screen.width - width) / 2
    y: (Screen.height - height) / 2

    signal zoneCreated(var properties)

    color: "#2E3440"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 15

        Text {
            text: "Create New Zone"
            color: "white"
            font.pixelSize: 20
            font.bold: true
            Layout.alignment: Qt.AlignHCenter
        }

        // Zone Type
        GroupBox {
            title: "Zone Type"
            Layout.fillWidth: true

            ColumnLayout {
                anchors.fill: parent

                ComboBox {
                    id: zoneTypeCombo
                    model: ["button", "login"]
                    currentIndex: 0
                    Layout.fillWidth: true

                    background: Rectangle {
                        color: "#434C5E"
                        border.color: "#4C566A"
                        radius: 4
                    }

                    contentItem: Text {
                        text: zoneTypeCombo.currentText
                        color: "white"
                        leftPadding: 10
                        verticalAlignment: Text.AlignVCenter
                    }

                    delegate: ItemDelegate {
                        width: zoneTypeCombo.width
                        contentItem: Text {
                            text: modelData
                            color: "white"
                        }
                        background: Rectangle {
                            color: highlighted ? "#5E81AC" : "#434C5E"
                        }
                    }
                }
            }
        }

        // Zone Text
        GroupBox {
            title: "Zone Text"
            Layout.fillWidth: true

            TextField {
                id: zoneTextField
                text: zoneTypeCombo.currentText === "login" ? "Login" : "Button"
                placeholderText: "Enter zone text"
                width: parent.width

                background: Rectangle {
                    color: "#434C5E"
                    border.color: "#4C566A"
                    radius: 4
                }

                color: "white"
            }
        }

        // Appearance
        GroupBox {
            title: "Appearance"
            Layout.fillWidth: true

            GridLayout {
                columns: 2
                rowSpacing: 10
                columnSpacing: 10
                anchors.fill: parent

                Text { text: "Background Color:"; color: "white" }
                RowLayout {
                    Rectangle {
                        id: colorPreview
                        width: 30
                        height: 30
                        color: colorDialog.selectedColor
                        border.color: "#4C566A"
                        border.width: 1
                        radius: 4
                    }

                    Button {
                        text: "Choose Color"
                        font.pixelSize: 12

                        background: Rectangle {
                            color: parent.pressed ? "#5E81AC" : "#81A1C1"
                            radius: 4
                        }

                        contentItem: Text {
                            text: parent.text
                            color: "white"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            font: parent.font
                        }

                        onClicked: colorDialog.open()
                    }
                }

                Text { text: "Border Color:"; color: "white" }
                RowLayout {
                    Rectangle {
                        width: 30
                        height: 30
                        color: borderColorDialog.selectedColor
                        border.color: "#4C566A"
                        border.width: 1
                        radius: 4
                    }

                    Button {
                        text: "Choose Color"
                        font.pixelSize: 12

                        background: Rectangle {
                            color: parent.pressed ? "#5E81AC" : "#81A1C1"
                            radius: 4
                        }

                        contentItem: Text {
                            text: parent.text
                            color: "white"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            font: parent.font
                        }

                        onClicked: borderColorDialog.open()
                    }
                }

                Text { text: "Border Width:"; color: "white" }
                SpinBox {
                    id: borderWidthSpin
                    from: 0
                    to: 10
                    value: 1
                    editable: true

                    background: Rectangle {
                        color: "#434C5E"
                        border.color: "#4C566A"
                        radius: 4
                    }

                    contentItem: Text {
                        text: borderWidthSpin.value
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                Text { text: "Corner Radius:"; color: "white" }
                SpinBox {
                    id: radiusSpin
                    from: 0
                    to: 50
                    value: 8
                    editable: true

                    background: Rectangle {
                        color: "#434C5E"
                        border.color: "#4C566A"
                        radius: 4
                    }

                    contentItem: Text {
                        text: radiusSpin.value
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }

        // Size and Position
        GroupBox {
            title: "Size and Position"
            Layout.fillWidth: true

            GridLayout {
                columns: 4
                rowSpacing: 10
                columnSpacing: 10
                anchors.fill: parent

                Text { text: "X:"; color: "white" }
                SpinBox {
                    id: xSpin
                    from: 0
                    to: 1920
                    value: 100
                    editable: true

                    background: Rectangle {
                        color: "#434C5E"
                        border.color: "#4C566A"
                        radius: 4
                    }

                    contentItem: Text {
                        text: xSpin.value
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                Text { text: "Y:"; color: "white" }
                SpinBox {
                    id: ySpin
                    from: 0
                    to: 1080
                    value: 100
                    editable: true

                    background: Rectangle {
                        color: "#434C5E"
                        border.color: "#4C566A"
                        radius: 4
                    }

                    contentItem: Text {
                        text: ySpin.value
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                Text { text: "Width:"; color: "white" }
                SpinBox {
                    id: widthSpin
                    from: 50
                    to: 500
                    value: 140
                    editable: true

                    background: Rectangle {
                        color: "#434C5E"
                        border.color: "#4C566A"
                        radius: 4
                    }

                    contentItem: Text {
                        text: widthSpin.value
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                Text { text: "Height:"; color: "white" }
                SpinBox {
                    id: heightSpin
                    from: 30
                    to: 200
                    value: 100
                    editable: true

                    background: Rectangle {
                        color: "#434C5E"
                        border.color: "#4C566A"
                        radius: 4
                    }

                    contentItem: Text {
                        text: heightSpin.value
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }

        // Buttons
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 20

            Button {
                text: "Create Zone"
                font.pixelSize: 14
                Layout.preferredWidth: 120

                background: Rectangle {
                    color: parent.pressed ? "#5E81AC" : "#88C0D0"
                    radius: 4
                }

                contentItem: Text {
                    text: parent.text
                    color: "white"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font: parent.font
                }

                onClicked: {
                    var properties = {
                        zoneType: zoneTypeCombo.currentText,
                        text: zoneTextField.text,
                        color: colorDialog.selectedColor,
                        borderColor: borderColorDialog.selectedColor,
                        borderWidth: borderWidthSpin.value,
                        radius: radiusSpin.value,
                        x: xSpin.value,
                        y: ySpin.value,
                        width: widthSpin.value,
                        height: heightSpin.value
                    }
                    zoneCreated(properties)
                    zonePropertiesDialog.visible = false
                }
            }

            Button {
                text: "Cancel"
                font.pixelSize: 14
                Layout.preferredWidth: 120

                background: Rectangle {
                    color: parent.pressed ? "#5E81AC" : "#81A1C1"
                    radius: 4
                }

                contentItem: Text {
                    text: parent.text
                    color: "white"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font: parent.font
                }

                onClicked: {
                    zonePropertiesDialog.visible = false
                }
            }
        }
    }

    // Color Dialogs
    ColorDialog {
        id: colorDialog
        title: "Choose Background Color"
        selectedColor: "#81A1C1"
        onAccepted: {
            // Color is automatically updated in the preview
        }
    }

    ColorDialog {
        id: borderColorDialog
        title: "Choose Border Color"
        selectedColor: "#4C566A"
        onAccepted: {
            // Color is automatically updated in the preview
        }
    }

    // Update default text when zone type changes
    Connections {
        target: zoneTypeCombo
        function onCurrentTextChanged() {
            if (zoneTypeCombo.currentText === "login") {
                zoneTextField.text = "Login"
            } else {
                zoneTextField.text = "Button"
            }
        }
    }
}