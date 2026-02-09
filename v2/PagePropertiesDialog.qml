import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Window {
    id: pagePropertiesDialog
    title: "Create New Page Properties"
    width: 600
    height: 700
    minimumWidth: 500
    minimumHeight: 600
    modality: Qt.NonModal
    flags: Qt.Window | Qt.WindowTitleHint | Qt.WindowSystemMenuHint | Qt.WindowMinMaxButtonsHint | Qt.WindowCloseButtonHint | Qt.WindowStaysOnTopHint

    // Set dark background to match app theme
    color: "#2E3440"

    signal pageCreated(var properties)

    ScrollView {
        anchors.fill: parent
        anchors.margins: 10
        anchors.bottomMargin: 70  // Leave space for buttons
        clip: true

        // Set dark background for content
        background: Rectangle {
            color: "#3B4252"
            radius: 4
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 15

            // Page Settings Section Header
            Label {
                text: "Page Settings"
                color: "#81A1C1"
                font.pixelSize: 14
                font.bold: true
                Layout.bottomMargin: 5
            }

            // Page Type
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "Page Type:"
                    color: "white"
                    Layout.preferredWidth: 200
                    font.bold: true
                }
                ComboBox {
                    id: pageTypeCombo
                    Layout.fillWidth: true
                    model: ["System Page"]
                    currentIndex: 0

                    background: Rectangle {
                        color: "#4C566A"
                        border.color: "#81A1C1"
                        border.width: 1
                        radius: 4
                    }

                    contentItem: Text {
                        text: pageTypeCombo.displayText
                        color: "white"
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }

            // Page Name
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "Page Name:"
                    color: "white"
                    Layout.preferredWidth: 200
                    font.bold: true
                }
                TextField {
                    id: pageNameField
                    Layout.fillWidth: true
                    placeholderText: "Enter page name"
                    color: "white"

                    background: Rectangle {
                        color: "#4C566A"
                        border.color: "#81A1C1"
                        border.width: 1
                        radius: 4
                    }
                }
            }

            // Page Number
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "Page Number:"
                    color: "white"
                    Layout.preferredWidth: 200
                    font.bold: true
                }
                SpinBox {
                    id: pageNumberSpin
                    from: -9999
                    to: 9999
                    value: 1
                    editable: true

                    background: Rectangle {
                        color: "#4C566A"
                        border.color: "#81A1C1"
                        border.width: 1
                        radius: 4
                    }

                    contentItem: Text {
                        text: pageNumberSpin.displayText
                        color: "white"
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }

            // Appearance Section Header
            Label {
                text: "Appearance"
                color: "#81A1C1"
                font.pixelSize: 14
                font.bold: true
                Layout.topMargin: 10
                Layout.bottomMargin: 5
            }

            // Title Bar Color
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "Title Bar Color:"
                    color: "white"
                    Layout.preferredWidth: 200
                    font.bold: true
                }
                Rectangle {
                    id: titleBarColorRect
                    width: 50
                    height: 30
                    color: "#4C566A"
                    border.color: "white"
                    border.width: 1
                    radius: 4
                }
                Button {
                    text: "Choose..."
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

                    onClicked: titleBarColorDialog.open()
                }
            }

            // Background Color
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "Background Color:"
                    color: "white"
                    Layout.preferredWidth: 200
                    font.bold: true
                }
                Rectangle {
                    id: backgroundColorRect
                    width: 50
                    height: 30
                    color: "#3B4252"
                    border.color: "white"
                    border.width: 1
                    radius: 4
                }
                Button {
                    text: "Choose..."
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

                    onClicked: backgroundColorDialog.open()
                }
            }

            // Button Settings Section Header
            Label {
                text: "Button Settings"
                color: "#81A1C1"
                font.pixelSize: 14
                font.bold: true
                Layout.topMargin: 10
                Layout.bottomMargin: 5
            }

            // Font For All Buttons
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "Font For All Buttons:"
                    color: "white"
                    Layout.preferredWidth: 200
                }
                ComboBox {
                    id: buttonFontCombo
                    Layout.fillWidth: true
                    model: ["Default", "Arial", "Helvetica", "Times", "Courier", "Verdana"]
                    currentIndex: 0
                }
            }

            // Edge & Texture For All Buttons
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "Edge & Texture For All Buttons:"
                    color: "white"
                    Layout.preferredWidth: 200
                }
                ComboBox {
                    id: buttonEdgeCombo
                    Layout.fillWidth: true
                    model: ["Flat", "Raised", "Sunken", "Rounded", "Beveled"]
                    currentIndex: 0
                }
            }

            // Text Color For All Buttons
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "Text Color For All Buttons:"
                    color: "white"
                    Layout.preferredWidth: 200
                    font.bold: true
                }
                Rectangle {
                    id: buttonTextColorRect
                    width: 50
                    height: 30
                    color: "white"
                    border.color: "white"
                    border.width: 1
                    radius: 4
                }
                Button {
                    text: "Choose..."
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

                    onClicked: buttonTextColorDialog.open()
                }
            }

            // Edge & Texture For All Buttons When Selected
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "Edge & Texture When Selected:"
                    color: "white"
                    Layout.preferredWidth: 200
                }
                ComboBox {
                    id: buttonSelectedEdgeCombo
                    Layout.fillWidth: true
                    model: ["Flat", "Raised", "Sunken", "Rounded", "Beveled", "Inverted"]
                    currentIndex: 1
                }
            }

            // Text Color For All Buttons When Selected
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "Text Color When Selected:"
                    color: "white"
                    Layout.preferredWidth: 200
                    font.bold: true
                }
                Rectangle {
                    id: buttonSelectedTextColorRect
                    width: 50
                    height: 30
                    color: "#81A1C1"
                    border.color: "white"
                    border.width: 1
                    radius: 4
                }
                Button {
                    text: "Choose..."
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

                    onClicked: buttonSelectedTextColorDialog.open()
                }
            }

            // Spacing
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "Spacing:"
                    color: "white"
                    Layout.preferredWidth: 200
                }
                SpinBox {
                    id: spacingSpin
                    from: 0
                    to: 50
                    value: 10
                    editable: true
                }
            }

            // Shadow Intensity For All Buttons
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "Shadow Intensity:"
                    color: "white"
                    Layout.preferredWidth: 200
                }
                SpinBox {
                    id: shadowIntensitySpin
                    from: 0
                    to: 100
                    value: 25
                    editable: true
                }
            }

            // Parent Page
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "Parent Page:"
                    color: "white"
                    Layout.preferredWidth: 200
                }
                SpinBox {
                    id: parentPageSpin
                    from: -1
                    to: 9999
                    value: -1
                    editable: true
                }
            }
        }
    }

    // OK/Cancel buttons
    RowLayout {
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.margins: 10
        spacing: 10

        Button {
            text: "Cancel"
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

            onClicked: pagePropertiesDialog.close()
        }

        Button {
            text: "OK"
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

            onClicked: {
                var properties = {
                    pageType: pageTypeCombo.currentText,
                    pageName: pageNameField.text,
                    pageNumber: pageNumberSpin.value,
                    titleBarColor: titleBarColorRect.color,
                    backgroundColor: backgroundColorRect.color,
                    buttonFont: buttonFontCombo.currentText,
                    buttonEdge: buttonEdgeCombo.currentText,
                    buttonTextColor: buttonTextColorRect.color,
                    buttonSelectedEdge: buttonSelectedEdgeCombo.currentText,
                    buttonSelectedTextColor: buttonSelectedTextColorRect.color,
                    spacing: spacingSpin.value,
                    shadowIntensity: shadowIntensitySpin.value,
                    parentPage: parentPageSpin.value
                }
                pagePropertiesDialog.pageCreated(properties)
                pagePropertiesDialog.close()
            }
        }
    }

    onVisibleChanged: {
        if (visible && transientParent) {
            x = transientParent.x + (transientParent.width - width) / 2
            y = transientParent.y + (transientParent.height - height) / 2
        }
        if (visible) {
            // Reset to defaults when dialog becomes visible
            pageTypeCombo.currentIndex = 0
            pageNameField.text = ""
            pageNumberSpin.value = 1
            titleBarColorRect.color = "#4C566A"
            backgroundColorRect.color = "#3B4252"
            buttonFontCombo.currentIndex = 0
            buttonEdgeCombo.currentIndex = 0
            buttonTextColorRect.color = "white"
            buttonSelectedEdgeCombo.currentIndex = 1
            buttonSelectedTextColorRect.color = "#81A1C1"
            spacingSpin.value = 10
            shadowIntensitySpin.value = 25
            parentPageSpin.value = -1
        }
    }

    ColorDialog {
        id: titleBarColorDialog
        title: "Choose Title Bar Color"
        onAccepted: titleBarColorRect.color = selectedColor
    }

    ColorDialog {
        id: backgroundColorDialog
        title: "Choose Background Color"
        onAccepted: backgroundColorRect.color = selectedColor
    }

    ColorDialog {
        id: buttonTextColorDialog
        title: "Choose Button Text Color"
        onAccepted: buttonTextColorRect.color = selectedColor
    }

    ColorDialog {
        id: buttonSelectedTextColorDialog
        title: "Choose Selected Button Text Color"
        onAccepted: buttonSelectedTextColorRect.color = selectedColor
    }
}