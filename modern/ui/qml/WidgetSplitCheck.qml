import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// Split check: touch an item on the left, then the check to move it to on
// the right (another check at this table, or a new one).
Item {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    readonly property string face: zone.st.font ?? "DejaVu Sans"
    readonly property color ink: zone.st.textColor ?? "white"
    readonly property real unit: Math.max(14, Math.min(28, w.width * 0.022))
    readonly property var targets: {
        if (!pos) return []
        void pos.openChecks
        void pos.lines
        return pos.splitTargets()
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: w.unit * 0.6
        spacing: w.unit

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Text {
                text: w.pos && w.pos.hasCheck ? qsTr("%1 · check #%2").arg(w.pos.check.label).arg(w.pos.check.id)
                                               : qsTr("No check open")
                color: w.ink
                font.family: w.face
                font.pixelSize: w.unit * 1.2
                font.bold: true
            }
            Text {
                text: qsTr("1. Touch an item")
                color: "#8a94a6"
                font.family: w.face
                font.pixelSize: w.unit * 0.8
            }
            ListView {
                id: lines1
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                ScrollBar.vertical: TouchScrollBar { id: linesBar }
                spacing: 4
                model: w.pos ? w.pos.lines : []
                delegate: Rectangle {
                    id: line
                    required property var modelData
                    width: ListView.view.width - linesBar.room
                    height: w.unit * 2.2
                    radius: 8
                    color: modelData.selected ? "#2f6fd6" : "#2d3440"
                    TapHandler { onTapped: w.pos.selectedLine = line.modelData.id }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: w.unit * 0.5
                        anchors.rightMargin: w.unit * 0.5
                        Text {
                            Layout.fillWidth: true
                            text: line.modelData.name
                                  + (line.modelData.modifiers.length ? "  (" + line.modelData.modifiers.map(m => m.name).join(", ") + ")" : "")
                            color: w.ink
                            font.family: w.face
                            font.pixelSize: w.unit
                            elide: Text.ElideRight
                        }
                        Text {
                            text: line.modelData.price
                            color: w.ink
                            font.family: w.face
                            font.pixelSize: w.unit
                        }
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: false    // nested layouts fill by default
            Layout.preferredWidth: parent.width * 0.4
            Layout.fillHeight: true
            Text {
                text: qsTr("2. Move it to")
                color: "#8a94a6"
                font.family: w.face
                font.pixelSize: w.unit * 0.8
            }
            Repeater {
                model: w.targets
                delegate: WidgetKey {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: w.unit * 3
                    text: modelData.id === 0 ? "+ " + modelData.label : modelData.label + "   " + modelData.total
                    accent: modelData.id === 0
                    fontScale: 0.3
                    onClicked: w.pos.splitLine(modelData.id)
                }
            }
            Item { Layout.fillHeight: true }
        }
    }
}
