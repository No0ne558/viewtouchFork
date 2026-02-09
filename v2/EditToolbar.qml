import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: editToolbar
    height: 50
    width: parent.width
    color: "#2E3440"
    border.color: "#4C566A"

    // Properties
    property var mainWindow
    border.width: 1

    // Toolbar content
    RowLayout {
        anchors.fill: parent
        anchors.margins: 5
        spacing: 10

        Text {
            text: "Edit Toolbar"
            color: "white"
            font.pixelSize: 16
            font.bold: true
            Layout.alignment: Qt.AlignVCenter
        }

        // Spacer
        Item {
            Layout.fillWidth: true
        }

        // Create New Page button
        Button {
            text: "Create New Page"
            font.pixelSize: 12
            Layout.preferredWidth: 150
            Layout.preferredHeight: 35

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
                pagePropertiesDialog.visible = true
            }
        }
    }

    // Page Properties Dialog
    PagePropertiesDialog {
        id: pagePropertiesDialog
        transientParent: mainWindow
        onPageCreated: {
            createNewPage(properties)
        }
    }
}