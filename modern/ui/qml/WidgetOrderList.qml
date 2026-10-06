import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// The current check: header, seat / course controls, order lines with
// modifiers, totals. Touch a line to select it (modifiers, Void, seat and
// course apply to it). props.controls: false hides the seat / course row.
// A table's checks (one per guest paying alone): 1 2 3 + across the top.
Item {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    readonly property color ink: zone.st.textColor ?? "white"
    readonly property string face: zone.st.font ?? "DejaVu Sans"
    readonly property real unit: Math.max(12, Math.min((zone.st.fontSize ?? 28) * zone.textScale, w.width * 0.05 * zone.textScale))
    readonly property var check: pos ? pos.check : ({})
    readonly property var totals: pos ? pos.totals : ({})
    readonly property bool paid: pos !== null && pos.payments.length > 0
    readonly property bool controls: !(zone && zone.props && zone.props.controls === false)
    // The course Fire would send next (the lowest one on hold).
    readonly property int nextCourse: {
        let next = 0
        for (const l of (pos ? pos.lines : []))
            if (l.held && (next === 0 || l.course < next)) next = l.course
        return next
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: w.unit * 0.6
        spacing: w.unit * 0.3

        // Header
        RowLayout {
            Layout.fillWidth: true
            visible: w.pos && w.pos.hasCheck
            Text {
                readonly property var mine: (w.pos ? w.pos.tableChecks : []).find(c => c.current)
                text: (w.check.label ?? "") + (mine && w.pos.tableChecks.length > 1 ? "  ·  " + qsTr("Check %1").arg(mine.number) : "")
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
                  + "  ·  " + (w.check.due ? qsTr("ready %1").arg(w.check.due) : (w.check.opened ?? ""))
            color: "#8a94a6"
            font.family: w.face
            font.pixelSize: w.unit * 0.7
            Layout.fillWidth: true
            elide: Text.ElideRight
        }
        // The table's checks: touch one to switch to it here; + opens another.
        // One line however many there are: what doesn't fit is a page away
        // (‹ ›), and the open check is always on the page shown.
        Item {
            id: tableChecks
            readonly property var checks: w.pos ? w.pos.tableChecks : []
            visible: w.pos && w.pos.hasCheck && checks.length > 0 && w.zone.keyShown("tableChecks")
            Layout.fillWidth: true
            Layout.preferredHeight: key
            Layout.fillHeight: false
            readonly property real key: w.unit * 1.8
            readonly property real gap: w.unit * 0.25
            // Three or more: just the numbers.
            readonly property bool compact: checks.length >= 3
            readonly property real slot: key * (compact ? 1.25 : 2.6)
            readonly property real arrow: key * 0.9
            // How many fit beside +, with and without the arrows.
            readonly property int fitAll: Math.max(1, Math.floor((width - slot - gap + gap) / (slot + gap)))
            readonly property bool paged: checks.length > fitAll
            readonly property int perPage: paged ? Math.max(1, Math.floor((width - slot - 2 * (arrow + gap)) / (slot + gap))) : checks.length
            readonly property int current: Math.max(0, checks.findIndex(c => c.current))
            property int first: 0
            // The open check stays on the page shown.
            function follow() {
                if (current < first || current >= first + perPage)
                    first = Math.max(0, Math.min(current - perPage + 1, checks.length - perPage))
                first = Math.max(0, Math.min(first, checks.length - perPage))
            }
            onCurrentChanged: follow()
            onPerPageChanged: follow()
            onChecksChanged: follow()

            Row {
                spacing: tableChecks.gap
                height: tableChecks.key
                WidgetKey {
                    objectName: "tableCheck-prev"
                    visible: tableChecks.paged
                    enabled: tableChecks.first > 0
                    opacity: enabled ? 1 : 0.35
                    width: tableChecks.arrow; height: tableChecks.key
                    fontScale: 0.6
                    text: "‹"
                    onClicked: tableChecks.first = Math.max(0, tableChecks.first - tableChecks.perPage)
                }
                Repeater {
                    model: tableChecks.checks.slice(tableChecks.first, tableChecks.first + tableChecks.perPage)
                    delegate: WidgetKey {
                        required property var modelData
                        objectName: "tableCheck-" + modelData.number
                        width: tableChecks.slot; height: tableChecks.key
                        fontScale: tableChecks.compact ? 0.6 : 0.48
                        text: tableChecks.compact ? String(modelData.number)
                                                  : w.zone.keyText("tableChecks", qsTr("Check")) + " " + modelData.number
                        accent: modelData.current
                        enabled: modelData.current || modelData.busyOn === ""
                        onClicked: if (!modelData.current) w.pos.switchCheck(modelData.id)
                    }
                }
                WidgetKey {
                    objectName: "tableCheck-next"
                    visible: tableChecks.paged
                    enabled: tableChecks.first + tableChecks.perPage < tableChecks.checks.length
                    opacity: enabled ? 1 : 0.35
                    width: tableChecks.arrow; height: tableChecks.key
                    fontScale: 0.6
                    text: "›"
                    onClicked: tableChecks.first = Math.min(tableChecks.checks.length - tableChecks.perPage,
                                                            tableChecks.first + tableChecks.perPage)
                }
            }
            // Always in the same place, at the right end.
            WidgetKey {
                objectName: "tableCheck-new"
                anchors.right: parent.right
                width: tableChecks.slot; height: tableChecks.key
                fontScale: tableChecks.compact ? 0.6 : 0.48
                text: tableChecks.compact ? "+" : qsTr("+ Check")
                onClicked: w.pos.newTableCheck()
            }
        }
        Text {
            readonly property var customer: w.check.customer ?? ({})
            visible: w.pos && w.pos.hasCheck && !!(customer.name || customer.phone)
            text: (customer.name ?? "") + (customer.phone ? "  ·  " + customer.phone : "")
            color: "#7ec8ff"
            font.family: w.face
            font.pixelSize: w.unit * 0.75
            Layout.fillWidth: true
            elide: Text.ElideRight
        }
        // Seat and course for new items (or a touched line); Fire. Sized from
        // the panel's width so it never pushes the panel wider.
        Item {
            id: bar
            visible: w.controls && w.pos && w.pos.hasCheck
            Layout.fillWidth: true
            Layout.preferredHeight: w.unit * 1.6
            Layout.fillHeight: false
            readonly property real key: Math.min(w.unit * 1.5, width / 13)
            Row {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                height: parent.height
                spacing: bar.key * 0.15
                Text {
                    visible: w.zone.keyShown("seat")
                    anchors.verticalCenter: parent.verticalCenter
                    text: w.zone.keyText("seat", qsTr("Seat"))
                    color: "#8a94a6"
                    font.family: w.face
                    font.pixelSize: bar.key * 0.45
                }
                WidgetKey {
                    visible: w.zone.keyShown("seat")
                    width: bar.key; height: parent.height
                    text: "−"
                    onClicked: w.pos.setSeat(Math.max(0, (w.check.seat ?? 0) - 1))
                }
                Text {
                    visible: w.zone.keyShown("seat")
                    width: bar.key * 0.8
                    anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignHCenter
                    text: (w.check.seat ?? 0) > 0 ? w.check.seat : "–"
                    color: w.ink
                    font.family: w.face
                    font.pixelSize: bar.key * 0.6
                    font.bold: true
                }
                WidgetKey {
                    visible: w.zone.keyShown("seat")
                    width: bar.key; height: parent.height
                    text: "+"
                    onClicked: w.pos.setSeat((w.check.seat ?? 0) + 1)
                }
                Item { visible: w.zone.keyShown("seat"); width: bar.key * 0.3; height: 1 }
                Text {
                    visible: w.zone.keyShown("course")
                    anchors.verticalCenter: parent.verticalCenter
                    text: w.zone.keyText("course", qsTr("Course"))
                    color: "#8a94a6"
                    font.family: w.face
                    font.pixelSize: bar.key * 0.45
                }
                Repeater {
                    model: [1, 2, 3]
                    delegate: WidgetKey {
                        required property int modelData
                        visible: w.zone.keyShown("course")
                        width: bar.key; height: parent.height
                        text: modelData
                        accent: (w.check.course ?? 1) === modelData
                        onClicked: w.pos.setCourse(modelData)
                    }
                }
            }
            WidgetKey {
                visible: w.nextCourse > 0 && w.zone.keyShown("fire")
                anchors.right: parent.right
                width: bar.key * 3.4
                height: parent.height
                text: w.zone.keyText("fire", qsTr("Fire Course %1").arg(w.nextCourse))
                baseColor: "#a86a12"
                fontScale: 0.28
                onClicked: w.pos.fireCourse()
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#3a4250"; visible: w.pos && w.pos.hasCheck }

        ListView {
            id: list
            ScrollBar.vertical: TouchScrollBar { id: listBar }
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            model: w.pos ? w.pos.lines : []
            onCountChanged: positionViewAtEnd()

            delegate: Rectangle {
                id: row
                required property var modelData
                width: ListView.view.width - listBar.room
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
                            // ● not yet sent to the kitchen; ‖ held for a later course
                            text: row.modelData.held ? "‖" : row.modelData.sent ? " " : "●"
                            color: "#f5b940"
                            font.pixelSize: row.modelData.held ? w.unit * 0.8 : w.unit * 0.5
                            font.bold: true
                            Layout.preferredWidth: w.unit * 0.8
                        }
                        Text {
                            // S2 / C2: seat and (later) course
                            visible: text !== ""
                            text: (row.modelData.seat > 0 ? "S" + row.modelData.seat : "")
                                  + (row.modelData.course > 1 ? (row.modelData.seat > 0 ? " " : "") + "C" + row.modelData.course : "")
                                  + (row.modelData.held ? " " + qsTr("HOLD") : "")
                            color: row.modelData.held ? "#f5b940" : "#7ec8ff"
                            font.family: w.face
                            font.pixelSize: w.unit * 0.65
                            font.bold: true
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
                        // Choices for an unsent item: amber while a required one is missing.
                        WidgetKey {
                            visible: row.modelData.choices
                            Layout.preferredWidth: w.unit * 3.4
                            Layout.preferredHeight: w.unit * 1.4
                            text: qsTr("Choose")
                            baseColor: row.modelData.needsChoice ? "#a86a12" : "#343c49"
                            fontScale: 0.5
                            onClicked: w.zone.controller.chooseLine(row.modelData.id)
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
            Cell { text: qsTr("Gratuity %1%").arg(w.totals.gratuityPercent ?? 0); visible: w.totals.hasGratuity ?? false; Layout.fillWidth: true }
            Cell { text: w.totals.gratuity ?? ""; visible: w.totals.hasGratuity ?? false }
            Cell { text: qsTr("Total"); strong: true; Layout.fillWidth: true }
            Cell { text: w.totals.total ?? ""; strong: true }
            Cell { text: qsTr("Paid"); visible: w.paid; Layout.fillWidth: true }
            Cell { text: w.totals.paid ?? ""; visible: w.paid }
            Cell { text: qsTr("Balance due"); visible: w.paid; Layout.fillWidth: true }
            Cell { text: w.totals.balance ?? ""; visible: w.paid }
        }
    }
}
