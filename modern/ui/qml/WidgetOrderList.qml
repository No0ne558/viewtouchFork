import QtQuick
import QtQuick.Layouts

// The current check: header, order lines with modifiers, totals.
// Touch a line to select it (modifiers and Void apply to it).
Item {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    readonly property color ink: zone.st.textColor ?? "white"
    readonly property string face: zone.st.font ?? "DejaVu Sans"
    readonly property real unit: Math.max(12, Math.min(zone.st.fontSize ?? 28, w.width * 0.05))
    readonly property var check: pos ? pos.check : ({})
    readonly property var totals: pos ? pos.totals : ({})
    readonly property bool paid: pos !== null && pos.payments.length > 0

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: w.unit * 0.6
        spacing: w.unit * 0.3

        // Header
        RowLayout {
            Layout.fillWidth: true
            visible: w.pos && w.pos.hasCheck
            Text {
                text: w.check.label ?? ""
                color: w.ink
                font.family: w.face
                font.pixelSize: w.unit * 1.2
                font.bold: true
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            Text {
                text: qsTr("#%1").arg(w.check.id ?? "")
                color: "#8a94a6"
                font.family: w.face
                font.pixelSize: w.unit * 0.8
            }
        }
        Text {
            visible: w.pos && w.pos.hasCheck
            text: (w.check.server ?? "") + "  ·  "
                  + ((w.check.guests ?? 1) === 1 ? qsTr("1 guest") : qsTr("%1 guests").arg(w.check.guests))
                  + "  ·  " + (w.check.opened ?? "")
            color: "#8a94a6"
            font.family: w.face
            font.pixelSize: w.unit * 0.7
            Layout.fillWidth: true
            elide: Text.ElideRight
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#3a4250"; visible: w.pos && w.pos.hasCheck }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            model: w.pos ? w.pos.lines : []
            onCountChanged: positionViewAtEnd()

            delegate: Rectangle {
                id: row
                required property var modelData
                width: ListView.view.width
                height: col.implicitHeight + w.unit * 0.4
                radius: 6
                color: modelData.selected ? "#2f6fd6" : "transparent"

                TapHandler { onTapped: w.pos.selectedLine = row.modelData.id }

                ColumnLayout {
                    id: col
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: w.unit * 0.3
                    anchors.rightMargin: w.unit * 0.3
                    spacing: 0

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            // ● not yet sent to the kitchen
                            text: row.modelData.sent ? " " : "●"
                            color: "#f5b940"
                            font.pixelSize: w.unit * 0.5
                            Layout.preferredWidth: w.unit * 0.8
                        }
                        Text {
                            Layout.fillWidth: true
                            text: row.modelData.name
                            color: row.modelData.voided ? "#8a94a6" : w.ink
                            font.family: w.face
                            font.pixelSize: w.unit
                            font.italic: row.modelData.comment
                            font.strikeout: row.modelData.voided
                            elide: Text.ElideRight
                        }
                        Text {
                            text: row.modelData.voided ? qsTr("VOID") : row.modelData.price
                            color: row.modelData.voided ? "#ff6369" : w.ink
                            font.family: w.face
                            font.pixelSize: w.unit
                        }
                    }
                    Repeater {
                        model: row.modelData.modifiers
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.leftMargin: w.unit * 1.6
                            Text {
                                Layout.fillWidth: true
                                text: modelData.name
                                color: "#b8c0cc"
                                font.family: w.face
                                font.pixelSize: w.unit * 0.8
                                elide: Text.ElideRight
                            }
                            Text {
                                text: modelData.price
                                color: "#b8c0cc"
                                font.family: w.face
                                font.pixelSize: w.unit * 0.8
                            }
                        }
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                width: parent.width * 0.8
                visible: list.count === 0
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: w.pos && w.pos.hasCheck ? qsTr("Touch menu items to add them.")
                                             : qsTr("No check open.\nTouch an item to start a quick check.")
                color: "#8a94a6"
                font.family: w.face
                font.pixelSize: w.unit * 0.8
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#3a4250"; visible: w.pos && w.pos.hasCheck }

        GridLayout {
            Layout.fillWidth: true
            visible: w.pos && w.pos.hasCheck
            columns: 2
            rowSpacing: 0

            component Cell: Text {
                property bool strong: false
                color: w.ink
                font.family: w.face
                font.pixelSize: strong ? w.unit * 1.1 : w.unit * 0.8
                font.bold: strong
            }

            Cell { text: qsTr("Subtotal"); Layout.fillWidth: true }
            Cell { text: w.totals.subtotal ?? "" }
            Cell { text: qsTr("Discount"); visible: w.totals.hasDiscount ?? false; Layout.fillWidth: true }
            Cell { text: w.totals.discounts ?? ""; visible: w.totals.hasDiscount ?? false }
            Cell { text: qsTr("Tax"); Layout.fillWidth: true }
            Cell { text: w.totals.tax ?? "" }
            Cell { text: qsTr("Total"); strong: true; Layout.fillWidth: true }
            Cell { text: w.totals.total ?? ""; strong: true }
            Cell { text: qsTr("Paid"); visible: w.paid; Layout.fillWidth: true }
            Cell { text: w.totals.paid ?? ""; visible: w.paid }
            Cell { text: qsTr("Balance due"); visible: w.paid; Layout.fillWidth: true }
            Cell { text: w.totals.balance ?? ""; visible: w.paid }
        }
    }
}
