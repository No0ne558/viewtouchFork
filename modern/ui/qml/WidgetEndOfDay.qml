import QtQuick
import QtQuick.Layouts

// End of Day: shows what is still in the way (open checks, uncounted
// drawer), then closes the business day and starts the next one.
Item {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    readonly property var day: pos ? pos.day : ({})
    readonly property var backup: day.backup ?? ({})
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
        // Still on the clock: clock them out now (or fix it later in Time Punches).
        Text {
            visible: (w.day.clockedIn ?? []).length > 0
            Layout.topMargin: w.unit * 0.4
            text: qsTr("Still clocked in")
            color: "#f5b940"
            font.family: w.face
            font.pixelSize: w.unit
            font.bold: true
        }
        Repeater {
            model: w.day.clockedIn ?? []
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: w.unit * 0.5
                Text {
                    Layout.fillWidth: true
                    text: modelData.name + "  ·  " + qsTr("since %1").arg(modelData.since)
                          + (modelData.onBreak ? "  ·  " + qsTr("on break") : "")
                          + (modelData.long ? "  ·  " + qsTr("forgot to clock out?") : "")
                    color: modelData.long ? "#ff9a9e" : w.ink
                    font.family: w.face
                    font.pixelSize: w.unit * 0.85
                    elide: Text.ElideRight
                }
                WidgetKey {
                    objectName: "eodClockOut-" + modelData.id
                    Layout.preferredWidth: w.unit * 7
                    Layout.preferredHeight: Math.max(w.unit * 1.8, w.zone ? w.zone.touch(46) : 0)
                    text: qsTr("Clock Out Now")
                    fontScale: 0.32
                    onClicked: w.pos.clockOutPunch(modelData.id)
                }
            }
        }
        Text {
            visible: (w.day.clockedIn ?? []).some(p => p.long)
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("Forgot hours ago? Clock them out, then fix the time in Schedule → Time Punches.")
            color: "#8a94a6"
            font.family: w.face
            font.pixelSize: w.unit * 0.7
        }
        Text {
            visible: w.day.ready ?? false
            text: "✓  " + qsTr("Everything is settled and counted.")
            color: "#7ee2a8"
            font.family: w.face
            font.pixelSize: w.unit
        }

        Item { Layout.fillHeight: true }

        // The last backup, and its second copy when one is set up.
        RowLayout {
            Layout.fillWidth: true
            spacing: w.unit * 0.5
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                readonly property var b: w.backup
                text: !b.at ? qsTr("No backup yet this session.")
                     : b.ok ? qsTr("Backed up at %1.").arg(b.at) + (b.copy ? "  " + b.copy : "")
                            : qsTr("Backup failed at %1: %2").arg(b.at).arg(b.error)
                color: !b.at ? "#8a94a6" : (b.ok && b.copyOk !== false) ? "#7ee2a8" : "#ff9a9e"
                font.family: w.face
                font.pixelSize: w.unit * 0.75
            }
            WidgetKey {
                Layout.preferredWidth: w.unit * 8
                Layout.preferredHeight: Math.max(w.unit * 2, w.zone ? w.zone.touch(46) : 0)
                visible: w.zone.keyShown("backup")
                text: w.zone.keyText("backup", qsTr("Back Up Now"))
                fontScale: 0.3
                onClicked: w.pos.backupNow()
            }
        }

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
            Layout.preferredHeight: Math.max(w.unit * 3.2, w.zone ? w.zone.touch(46) : 0)
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
