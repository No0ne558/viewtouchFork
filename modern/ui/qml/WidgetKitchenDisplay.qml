import QtQuick
import QtQuick.Layouts

// Kitchen / bar display: every order sent and not yet made, oldest first.
// Touch a ticket when it is ready (bump); Recall brings the last one back.
// props.station: "kitchen", "bar"... shows only that printer's lines
// (empty = everything). The Station button picks one of the store's
// stations (grill, fryer...) for this screen instead; it stays picked.
Item {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    readonly property string pageStation: zone && zone.props && zone.props.station ? zone.props.station : ""
    readonly property string station: !expo && pos && pos.kitchenStation ? pos.kitchenStation : pageStation
    readonly property var stations: pos ? pos.kitchenStations.filter(s => !s.printer) : []
    function stationName(id) {
        if (id === "") return qsTr("All stations")
        const all = w.pos ? w.pos.kitchenStations : []
        const s = all.find(x => x.id === id)
        return s ? s.name : id.charAt(0).toUpperCase() + id.slice(1)
    }
    // The page's own, then each station, and around again.
    function nextStation() {
        const ids = [w.pageStation].concat(w.stations.map(s => s.id).filter(id => id !== w.pageStation))
        const next = ids[(ids.indexOf(w.station) + 1) % ids.length]
        w.pos.setKitchenStation(next === w.pageStation ? "" : next)
    }
    readonly property string face: zone.st.font ?? "DejaVu Sans"
    property real now: Date.now()

    // Tickets with only this station's lines.
    // props.mode "expo": the expediter - every station, what each has made,
    // green when the whole ticket is ready; touching it sends it out.
    readonly property bool expo: zone && zone.props && zone.props.mode === "expo"
    property var armed: null   // expo: a ticket touched once while not ready

    // Kitchen highlight colors (Manager -> Menu), readable on the ticket.
    function inkFor(name) {
        return ({ red: "#c92a2a", orange: "#d9480f", yellow: "#9c6f00", green: "#2b8a3e",
                  blue: "#1864ab", purple: "#7048e8" })[name] ?? "#1b1b1b"
    }

    readonly property var tickets: {
        if (!pos) return []
        if (expo) return pos.expoTickets
        const out = []
        for (const t of pos.kitchenTickets) {
            const lines = station === "" ? t.lines
                        : t.lines.filter(l => l.station === station || l.printer === station || l.comment)
            if (lines.some(l => !l.comment))
                out.push(Object.assign({}, t, { lines: lines }))
        }
        return out
    }

    Timer { id: disarmExpo; interval: 3000; onTriggered: w.armed = null }

    Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: w.now = Date.now()
    }

    function elapsed(sentAt) {
        const s = Math.max(0, Math.floor((now - sentAt) / 1000))
        return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0")
    }
    // Green, then yellow after the store's warn minutes, red when late.
    function ageColor(ticket) {
        const minutes = (now - ticket.sentAt) / 60000
        return minutes < (ticket.warnMinutes ?? 8) ? "#1f8a4c" : minutes < (ticket.lateMinutes ?? 15) ? "#b7791f" : "#c53030"
    }

    // "All day": everything still to make here, by item, most first.
    property bool showAllDay: false
    readonly property var allDay: {
        const counts = {}
        for (const t of tickets)
            for (const l of t.lines)
                if (!l.comment)
                    counts[l.name] = (counts[l.name] ?? 0) + l.quantity
        return Object.keys(counts).map(k => ({ name: k, count: counts[k] })).sort((a, b) => b.count - a.count)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 72
            Layout.fillHeight: false   // nested layouts fill by default
            Text {
                Layout.fillWidth: true
                text: (w.expo ? qsTr("Expo") : w.stationName(w.station))
                      + "  ·  " + (w.tickets.length === 1 ? qsTr("1 order") : qsTr("%1 orders").arg(w.tickets.length))
                color: "white"
                font.family: w.face
                font.pixelSize: 40
                font.bold: true
            }
            WidgetKey {
                objectName: "kdsStation"
                visible: !w.expo && w.stations.length > 0
                Layout.preferredWidth: 240
                Layout.fillHeight: true
                text: qsTr("Station…")
                fontScale: 0.4
                onClicked: w.nextStation()
            }
            WidgetKey {
                Layout.preferredWidth: 240
                Layout.fillHeight: true
                text: qsTr("Message…")
                fontScale: 0.4
                onClicked: w.zone.controller.jumpTo("message")
            }
            WidgetKey {
                Layout.preferredWidth: 240
                Layout.fillHeight: true
                text: w.showAllDay ? qsTr("Hide All Day") : qsTr("All Day")
                fontScale: 0.4
                onClicked: w.showAllDay = !w.showAllDay
            }
            WidgetKey {
                Layout.preferredWidth: 240
                Layout.fillHeight: true
                text: qsTr("Recall")
                fontScale: 0.4
                onClicked: w.expo ? w.pos.expoRecall() : w.pos.recallTicket()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

        Flow {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12
            clip: true

            Repeater {
                model: w.tickets
                delegate: Rectangle {
                    id: card
                    required property var modelData
                    width: 360
                    height: Math.min(parent ? parent.height : 600, body.implicitHeight + 72 + 14 + 22)
                    radius: 10
                    color: "#f4f1ea"
                    // Rush tickets wear a red frame, VIP gold.
                    border.color: tap.pressed ? "#2f6fd6" : card.modelData.rush ? "#ff3b3b"
                                : card.modelData.vip ? "#d4af37" : "transparent"
                    border.width: card.modelData.rush || card.modelData.vip ? 8 : 4
                    clip: true

                    Rectangle {
                        id: head
                        width: parent.width
                        height: 72
                        radius: 10
                        color: w.expo && card.modelData.ready ? "#1f6fd6" : w.ageColor(card.modelData)
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 10; color: parent.color }
                        Column {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 14
                            Text {
                                text: card.modelData.label
                                color: "white"
                                font.family: w.face
                                font.pixelSize: 28
                                font.bold: true
                            }
                            Text {
                                text: card.modelData.customer || card.modelData.server
                                color: "white"
                                font.family: w.face
                                font.pixelSize: 18
                            }
                        }
                        Text {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.rightMargin: 14
                            text: w.expo && card.modelData.ready ? qsTr("READY") : w.elapsed(card.modelData.sentAt)
                            color: "white"
                            font.family: w.face
                            font.pixelSize: 30
                            font.bold: true
                        }
                    }

                    Column {
                        id: body
                        anchors.top: head.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.margins: 14
                        spacing: 4
                        Text {
                            visible: card.modelData.rush || card.modelData.vip || !!card.modelData.due
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: [card.modelData.rush ? qsTr("RUSH") : "", card.modelData.vip ? qsTr("VIP") : "",
                                   card.modelData.due ? qsTr("READY %1").arg(card.modelData.due) : ""]
                                  .filter(s => s).join("  ·  ")
                            color: card.modelData.rush ? "#d62828" : "#9a7b12"
                            font.family: w.face
                            font.pixelSize: 28
                            font.bold: true
                        }
                        Text {
                            visible: w.expo && !card.modelData.ready
                            width: parent.width
                            wrapMode: Text.WordWrap
                            readonly property bool isArmed: w.armed !== null && w.armed.checkId === card.modelData.checkId
                                                            && w.armed.sentAt === card.modelData.sentAt
                            text: isArmed ? qsTr("Not all made. Touch again to send it out anyway.")
                                          : qsTr("Waiting on: %1").arg((card.modelData.waitingOn ?? []).join(", "))
                            color: isArmed ? "#c92a2a" : "#6b6b6b"
                            font.family: w.face
                            font.pixelSize: 20
                            font.bold: isArmed
                        }
                        Text {
                            visible: !!card.modelData.note
                            width: parent.width
                            wrapMode: Text.WordWrap
                            text: qsTr("Note: %1").arg(card.modelData.note)
                            color: "#b83232"
                            font.family: w.face
                            font.pixelSize: 22
                            font.bold: true
                        }
                        Repeater {
                            model: card.modelData.lines
                            delegate: Column {
                                id: line
                                required property var modelData
                                width: body.width
                                Text {
                                    width: parent.width
                                    wrapMode: Text.WordWrap
                                    // "S2 1  Cobb Salad", course 2+ marked: the kitchen
                                    // plates by seat and knows what was fired later.
                                    text: (line.modelData.seat > 0 ? "S" + line.modelData.seat + "  " : "")
                                          + (line.modelData.comment ? "** " + line.modelData.name + " **"
                                                                    : line.modelData.quantity + "  " + line.modelData.name)
                                          + (line.modelData.course > 1 ? "   (course " + line.modelData.course + ")" : "")
                                          + (w.expo ? "   " + (line.modelData.made ? "✓" : "· " + line.modelData.station) : "")
                                    color: line.modelData.comment ? "#b83232"
                                         : w.expo && line.modelData.made ? "#2b8a3e" : w.inkFor(line.modelData.color)
                                    font.family: w.face
                                    font.pixelSize: 26
                                    font.bold: true
                                    font.italic: line.modelData.comment
                                }
                                Repeater {
                                    model: line.modelData.modifiers
                                    delegate: Text {
                                        required property string modelData
                                        leftPadding: 28
                                        text: "› " + modelData
                                        color: "#444"
                                        font.family: w.face
                                        font.pixelSize: 22
                                    }
                                }
                            }
                        }
                    }

                    TapHandler {
                        id: tap
                        onTapped: {
                            const t = card.modelData
                            if (!w.expo) {
                                w.pos.bumpTicket(t.checkId, t.sentAt, w.station)
                            } else if (t.ready || (w.armed && w.armed.checkId === t.checkId && w.armed.sentAt === t.sentAt)) {
                                w.armed = null
                                w.pos.expoBump(t.checkId, t.sentAt)   // out to the table
                            } else {
                                w.armed = { checkId: t.checkId, sentAt: t.sentAt }   // not ready: touch again to send anyway
                                disarmExpo.restart()
                            }
                        }
                    }
                }
            }
        }

        // All day: the totals to make, for batching (twelve burgers on the grill...).
        Rectangle {
            visible: w.showAllDay
            Layout.preferredWidth: 340
            Layout.fillHeight: true
            radius: 10
            color: "#232933"
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 6
                Text {
                    text: qsTr("All day")
                    color: "white"
                    font.family: w.face
                    font.pixelSize: 30
                    font.bold: true
                }
                Repeater {
                    model: w.allDay
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Text {
                            text: modelData.count
                            color: "#f5b940"
                            font.family: w.face
                            font.pixelSize: 30
                            font.bold: true
                            Layout.preferredWidth: 60
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.name
                            color: "white"
                            font.family: w.face
                            font.pixelSize: 24
                            elide: Text.ElideRight
                        }
                    }
                }
                Item { Layout.fillHeight: true }
            }
        }
        }
    }

    Text {
        anchors.centerIn: parent
        visible: w.tickets.length === 0
        text: qsTr("All caught up")
        color: "#8a94a6"
        font.family: w.face
        font.pixelSize: 48
    }
}
