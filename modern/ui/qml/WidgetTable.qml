import QtQuick

// One table, placed like any button. The zone's label is the table's name;
// props.seats shows while it is free. Its own style colors a free table;
// an open one shows whose it is: yours, someone else's, or the check you
// are on. Touching it opens the table as on the floor plan.
Item {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    readonly property string name: zone ? zone.label.trim() : ""
    readonly property var status: { w.pos ? w.pos.openChecks : null; return w.pos && w.name ? w.pos.tableStatus(w.name) : ({ open: false }) }
    readonly property color tint: !status.open ? (zone.st.fill ?? "#2d3440")
                                  : status.current ? "#2f6fd6"
                                  : status.mine ? "#1f8a4c" : "#a86a12"
    readonly property color ink: zone.st.textColor ?? "white"
    readonly property string face: zone.st.font ?? "DejaVu Sans"
    readonly property int seats: zone && zone.props ? (zone.props.seats ?? 0) : 0

    ZoneShape {
        anchors.fill: parent
        shape: w.zone.shape
        st: Object.assign({}, w.zone.st, { fill: tap.pressed ? Qt.lighter(w.tint, 1.3) : w.tint })
    }

    Column {
        anchors.centerIn: parent
        width: parent.width - 16
        spacing: 2
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: w.name !== "" ? w.name : qsTr("(no name)")
            color: w.ink
            font.family: w.face
            font.pixelSize: Math.min(w.height * 0.28, w.zone.st.fontSize ?? 40)
            font.bold: w.zone.st.bold ?? true
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            visible: w.status.open ?? false
            text: ((w.status.checks ?? 1) > 1 ? qsTr("%1 checks").arg(w.status.checks) : (w.status.server ?? ""))
                  + "  ·  " + (w.status.total ?? "")
            color: w.ink
            font.family: w.face
            font.pixelSize: Math.min(w.height * 0.13, 20)
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            visible: !(w.status.open ?? false) && w.seats > 0
            text: w.seats === 1 ? qsTr("1 seat") : qsTr("%1 seats").arg(w.seats)
            color: Qt.darker(w.ink, 1.4)
            font.family: w.face
            font.pixelSize: Math.min(w.height * 0.12, 18)
        }
    }

    TapHandler {
        id: tap
        enabled: w.name !== ""
        onTapped: w.zone.controller.selectTable(w.name)
    }
}
