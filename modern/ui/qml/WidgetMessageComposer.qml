import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// Send a message to other screens: everyone, the kitchen screens, the
// floor, or one person - a ready-made one or typed - and the last ones sent.
Item {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    property string to: "all"
    readonly property var presets: [qsTr("Need a runner"), qsTr("Order up"), qsTr("86: "), qsTr("Manager please"),
                                    qsTr("Help at the host stand"), qsTr("Table needs bussing"), qsTr("Allergy: please check")]

    readonly property real zoom: Math.max(1, Math.min(1.6, width / 1100))
    Item {
        width: w.width / w.zoom
        height: w.height / w.zoom
        scale: w.zoom
        transformOrigin: Item.TopLeft

        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 14

            // Scrolls when many people are clocked in.
            TouchScrollColumn {
                Layout.preferredWidth: parent.width * 0.58
                Layout.fillWidth: false
                Layout.fillHeight: true
                spacing: 8
                Label { text: qsTr("To"); font.bold: true; font.pixelSize: 17 }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: [{ id: "all", name: qsTr("Everyone") }, { id: "kitchen", name: qsTr("Kitchen screens") },
                                { id: "floor", name: qsTr("Floor") }]
                                .concat((w.pos ? w.pos.staff : []).filter(p => p.clockedIn).map(p => ({ id: p.name, name: p.name })))
                        delegate: Button {
                            required property var modelData
                            text: modelData.name
                            checkable: true
                            checked: w.to === modelData.id
                            implicitHeight: 48
                            font.pixelSize: 16
                            onClicked: w.to = modelData.id
                        }
                    }
                }
                Label { text: qsTr("Message"); font.bold: true; font.pixelSize: 17; Layout.topMargin: 8 }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: w.presets
                        delegate: Button {
                            required property string modelData
                            text: modelData
                            implicitHeight: 48
                            font.pixelSize: 16
                            onClicked: { message.text = modelData; message.forceActiveFocus(); message.cursorPosition = message.length }
                        }
                    }
                }
                TextField {
                    id: message
                    Layout.fillWidth: true
                    implicitHeight: 52
                    font.pixelSize: 20
                    placeholderText: qsTr("Type a message…")
                    onAccepted: send.clicked()
                }
                Button {
                    id: send
                    Layout.fillWidth: true
                    implicitHeight: 60
                    highlighted: true
                    font.pixelSize: 19
                    text: qsTr("Send")
                    enabled: message.text.trim().length > 0
                    onClicked: { w.pos.sendMessage(w.to, message.text); message.text = "" }
                }
            }

            ToolSeparator { Layout.fillHeight: true }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Label { text: qsTr("The last hour"); font.bold: true; font.pixelSize: 17 }
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 6
                    model: w.pos ? w.pos.messages : []
                    ScrollBar.vertical: TouchScrollBar { id: listBar }
                    delegate: Column {
                        required property var modelData
                        width: ListView.view.width - listBar.room
                        Label {
                            text: modelData.time + "  ·  " + modelData.from + " → "
                                  + ({ all: qsTr("everyone"), kitchen: qsTr("kitchen"), floor: qsTr("floor") })[modelData.to] ?? modelData.to
                            opacity: 0.7
                        }
                        Label { text: modelData.text; font.pixelSize: 17; wrapMode: Text.WordWrap; width: parent.width }
                    }
                }
            }
        }
    }
}
