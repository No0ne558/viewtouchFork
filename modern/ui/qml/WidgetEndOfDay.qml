import QtQuick
import QtQuick.Layouts

// End of Day: shows what is still in the way (open checks, uncounted
// drawer), then closes the business day and starts the next one.
Item {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    readonly property var day: pos ? pos.day : ({})
    readonly property string face: zone.st.font ?? "DejaVu Sans"
    readonly property color ink: zone.st.textColor ?? "white"
    readonly property real unit: Math.max(14, Math.min(30, w.width * 0.04))
    property bool armed: false

    Timer { id: disarm; interval: 3000; onTriggered: w.armed = false }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: w.unit
        spacing: w.unit * 0.5

        Text {
            text: qsTr("Business day #%1").arg(w.day.id ?? "")
            color: w.ink
            font.family: w.face
            font.pixelSize: w.unit * 1.5
            font.bold: true
        }
        Text {
            text: qsTr("Opened %1").arg(w.day.opened ?? "")
            color: "#8a94a6"
            font.family: w.face
            font.pixelSize: w.unit * 0.85
        }
        Text {
            text: qsTr("%1 checks closed · net sales %2").arg(w.day.closedChecks ?? 0).arg(w.day.netSales ?? "")
            color: w.ink
            font.family: w.face
            font.pixelSize: w.unit
        }

        Repeater {
            model: w.day.blockers ?? []
            delegate: Text {
                required property string modelData
                Layout.fillWidth: true
                text: "✗  " + modelData
                color: "#ff9a9e"
                font.family: w.face
                font.pixelSize: w.unit
                wrapMode: Text.WordWrap
            }
        }
        Text {
            visible: w.day.ready ?? false
            text: "✓  " + qsTr("Everything is settled and counted.")
            color: "#7ee2a8"
            font.family: w.face
            font.pixelSize: w.unit
        }

        Item { Layout.fillHeight: true }

        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("Closing the day saves its reports (see Reports) and starts a new day. Checks cannot be reopened afterwards.")
            color: "#8a94a6"
            font.family: w.face
            font.pixelSize: w.unit * 0.75
        }
        WidgetKey {
            Layout.fillWidth: true
            Layout.preferredHeight: w.unit * 3.2
            text: w.armed ? qsTr("Tap again to close the day") : qsTr("Close the Day")
            baseColor: (w.day.ready ?? false) ? (w.armed ? "#b83232" : "#1f5f3a") : "#3a3f48"
            fontScale: 0.3
            onClicked: {
                if (!(w.day.ready ?? false)) {
                    w.pos.endOfDay()   // explains what is missing
                    return
                }
                if (!w.armed) {
                    w.armed = true
                    disarm.restart()
                    return
                }
                w.armed = false
                w.pos.endOfDay()
            }
        }
    }
}
