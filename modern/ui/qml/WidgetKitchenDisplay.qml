import QtQuick
import QtQuick.Layouts

// Kitchen / bar display: every order sent and not yet made, oldest first.
// Touch a ticket when it is ready (bump); Recall brings the last one back.
// props.station: "kitchen", "bar"... shows only that printer's lines
// (empty = everything).
Item {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    readonly property string station: zone && zone.props && zone.props.station ? zone.props.station : ""
    readonly property string face: zone.st.font ?? "DejaVu Sans"
    property real now: Date.now()

    // Tickets with only this station's lines.
    readonly property var tickets: {
        if (!pos) return []
        const out = []
        for (const t of pos.kitchenTickets) {
            const lines = station === "" ? t.lines : t.lines.filter(l => l.printer === station || l.comment)
            if (lines.some(l => !l.comment))
                out.push(Object.assign({}, t, { lines: lines }))
        }
        return out
    }

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
    function ageColor(sentAt) {
        const minutes = (now - sentAt) / 60000
        return minutes < 5 ? "#1f8a4c" : minutes < 10 ? "#b7791f" : "#c53030"
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
                text: (w.station === "" ? qsTr("All stations") : w.station.charAt(0).toUpperCase() + w.station.slice(1))
                      + "  ·  " + (w.tickets.length === 1 ? qsTr("1 order") : qsTr("%1 orders").arg(w.tickets.length))
                color: "white"
                font.family: w.face
                font.pixelSize: 40
                font.bold: true
            }
            WidgetKey {
                Layout.preferredWidth: 240
                Layout.fillHeight: true
                text: qsTr("Recall")
                fontScale: 0.4
                onClicked: w.pos.recallTicket()
            }
        }

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
                    height: Math.min(parent ? parent.height : 600, body.implicitHeight + 90)
                    radius: 10
                    color: "#f4f1ea"
                    border.color: tap.pressed ? "#2f6fd6" : "transparent"
                    border.width: 4
                    clip: true

                    Rectangle {
                        id: head
                        width: parent.width
                        height: 72
                        radius: 10
                        color: w.ageColor(card.modelData.sentAt)
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
                            text: w.elapsed(card.modelData.sentAt)
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
                                    color: line.modelData.comment ? "#b83232" : "#1b1b1b"
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
                        onTapped: w.pos.bumpTicket(card.modelData.checkId, card.modelData.sentAt, w.station)
                    }
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
