import QtQuick

// Floor plan. Tables come from props.tables ([{label, x, y, w, h, shape,
// seats}], positioned inside the widget) and show live status:
// free, yours, someone else's, or the check you are on.
Item {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    readonly property var tables: zone && zone.props && zone.props.tables ? zone.props.tables : []
    readonly property string face: zone.st.font ?? "DejaVu Sans"

    Text {
        anchors.centerIn: parent
        visible: w.tables.length === 0
        text: qsTr("No tables yet. Add them in the editor (props.tables).")
        color: "#8a94a6"
        font.pixelSize: 24
    }

    Repeater {
        model: w.tables
        delegate: Item {
            id: t
            required property var modelData
            readonly property var status: { w.pos ? w.pos.openChecks : null; return w.pos ? w.pos.tableStatus(modelData.label) : ({ open: false }) }
            readonly property color tint: !status.open ? "#2d3440"
                                          : status.current ? "#2f6fd6"
                                          : status.mine ? "#1f8a4c" : "#a86a12"
            x: modelData.x
            y: modelData.y
            width: modelData.w
            height: modelData.h

            ZoneShape {
                anchors.fill: parent
                shape: t.modelData.shape ?? "rect"
                st: ({ fill: tap.pressed ? Qt.lighter(t.tint, 1.3) : t.tint, frame: "raised", frameWidth: 3,
                       radius: 14, shadow: 5 })
            }
            Column {
                anchors.centerIn: parent
                spacing: 2
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: t.modelData.label
                    color: "white"
                    font.family: w.face
                    font.pixelSize: Math.min(t.height * 0.28, 40)
                    font.bold: true
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: t.status.open
                    text: (t.status.server ?? "") + "  ·  " + (t.status.total ?? "")
                    color: "#e6e9ef"
                    font.family: w.face
                    font.pixelSize: Math.min(t.height * 0.13, 20)
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: !t.status.open && !!t.modelData.seats
                    text: (t.modelData.seats ?? 0) === 1 ? qsTr("1 seat") : qsTr("%1 seats").arg(t.modelData.seats ?? 0)
                    color: "#8a94a6"
                    font.family: w.face
                    font.pixelSize: Math.min(t.height * 0.12, 18)
                }
            }
            TapHandler {
                id: tap
                onTapped: w.zone.controller.selectTable(t.modelData.label)
            }
        }
    }
}
