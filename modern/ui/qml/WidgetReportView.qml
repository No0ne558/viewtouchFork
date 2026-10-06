import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// Reports on screen: pick a report and a day (today is live; closed days
// show what was saved at End of Day). props.report picks the first report.
Item {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    readonly property string face: zone.st.font ?? "DejaVu Sans"
    readonly property color ink: zone.st.textColor ?? "white"
    readonly property real unit: Math.max(12, Math.min(26, w.width * 0.022))

    readonly property var reportIds: [
        { id: "sales", label: qsTr("Sales") }, { id: "items", label: qsTr("Items") },
        { id: "categories", label: qsTr("Categories") }, { id: "hourly", label: qsTr("By Hour") },
        { id: "servers", label: qsTr("Servers") }, { id: "tips", label: qsTr("Tips") },
        { id: "labor", label: qsTr("Labor") },
        { id: "drawer", label: qsTr("Drawer") }, { id: "expenses", label: qsTr("Expenses") }, { id: "purchases", label: qsTr("Purchases") }, { id: "audit", label: qsTr("Audit") },
        { id: "exceptions", label: qsTr("Exceptions") }, { id: "deposit", label: qsTr("Deposit") }, { id: "customers", label: qsTr("Customers") },
        { id: "royalty", label: qsTr("Royalty") }, { id: "accounting", label: qsTr("Accounting") },
        { id: "accounts", label: qsTr("Gift Cards") },
        { id: "kitchen", label: qsTr("Kitchen") },
        { id: "foodcost", label: qsTr("Food Cost") },
        { id: "turns", label: qsTr("Turns") },
    ]
    property string reportId: zone && zone.props && zone.props.report ? zone.props.report : "sales"
    property int dayIndex: 0
    readonly property var days: pos ? pos.days : []
    readonly property var day: days[Math.min(dayIndex, days.length - 1)] ?? { id: 0, label: "" }
    // "day" (one business day, above), or several: week, lastWeek, month,
    // lastMonth, year, custom - read from the saved checks, maybe beside
    // the same days a year before.
    property string period: "day"
    property bool compare: false
    property string fromDate: ""
    property string toDate: ""
    readonly property var periods: [
        { id: "day", label: qsTr("Day") }, { id: "week", label: qsTr("This Week") },
        { id: "lastWeek", label: qsTr("Last Week") }, { id: "month", label: qsTr("This Month") },
        { id: "lastMonth", label: qsTr("Last Month") }, { id: "year", label: qsTr("This Year") },
        { id: "custom", label: qsTr("Dates…") },
    ]
    readonly property var range: pos ? pos.rangeReport : ({})
    function refresh() {
        if (period === "day" || !pos)
            return
        if (period === "custom" && (fromDate === "" || toDate === ""))
            return
        pos.requestRangeReport(reportId, period, fromDate, toDate, compare)
    }
    onReportIdChanged: refresh()
    onPeriodChanged: refresh()
    onCompareChanged: refresh()
    readonly property var report: {
        if (!pos) return ({ rows: [] })
        if (period !== "day")
            return range.report ?? ({ title: range.loading ? qsTr("Reading the checks…") : "", rows: [] })
        void pos.day          // live: refresh when checks close
        void pos.drawer
        void pos.queryRevision   // remote terminals: the server's answer arrived
        return pos.report(reportId, day.id)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: w.unit * 0.6
        spacing: w.unit * 0.5

        // Two rows, so each name has room to be read.
        GridLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 2 * Math.max(w.unit * 2.2, w.zone ? w.zone.touch(46) : 0) + w.unit * 0.3
            Layout.fillHeight: false   // nested layouts fill by default
            columns: Math.ceil(w.reportIds.length / 2)
            rowSpacing: w.unit * 0.3
            columnSpacing: w.unit * 0.3
            Repeater {
                model: w.reportIds
                delegate: WidgetKey {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    text: modelData.label
                    fontScale: 0.36
                    baseColor: w.reportId === modelData.id ? "#2f6fd6" : "#343c49"
                    onClicked: w.reportId = modelData.id
                }
            }
        }

        // Which days.
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(w.unit * 2.2, w.zone ? w.zone.touch(46) : 0)
            Layout.fillHeight: false
            spacing: w.unit * 0.3
            Repeater {
                model: w.periods
                delegate: WidgetKey {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    text: modelData.label
                    fontScale: 0.34
                    baseColor: w.period === modelData.id ? "#2f6fd6" : "#343c49"
                    onClicked: w.period = modelData.id
                }
            }
            WidgetKey {
                visible: w.period !== "day"
                Layout.preferredWidth: w.unit * 7
                Layout.fillHeight: true
                text: qsTr("vs Last Year")
                fontScale: 0.34
                baseColor: w.compare ? "#1f8a4c" : "#343c49"
                onClicked: w.compare = !w.compare
            }
        }

        // Dates… : from and to.
        RowLayout {
            visible: w.period === "custom"
            Layout.fillWidth: true
            Layout.fillHeight: false
            spacing: w.unit * 0.4
            Text { text: qsTr("From"); color: w.ink; font.family: w.face; font.pixelSize: w.unit * 0.8 }
            TextField {
                implicitWidth: w.unit * 8
                placeholderText: "2026-09-01"
                inputMethodHints: Qt.ImhPreferNumbers
                onTextEdited: w.fromDate = text
            }
            Text { text: qsTr("to"); color: w.ink; font.family: w.face; font.pixelSize: w.unit * 0.8 }
            TextField {
                implicitWidth: w.unit * 8
                placeholderText: "2026-09-30"
                inputMethodHints: Qt.ImhPreferNumbers
                onTextEdited: w.toDate = text
                onAccepted: w.refresh()
            }
            WidgetKey {
                Layout.preferredWidth: w.unit * 5
                Layout.preferredHeight: Math.max(w.unit * 2, w.zone ? w.zone.touch(46) : 0)
                text: qsTr("Show")
                fontScale: 0.34
                onClicked: w.refresh()
            }
            Item { Layout.fillWidth: true }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(w.unit * 2.4, w.zone ? w.zone.touch(46) : 0)
            Layout.fillHeight: false
            spacing: w.unit * 0.3
            Text {
                visible: w.period !== "day"
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: (w.range.label ?? "") + (w.range.loading ? "  ·  " + qsTr("reading…")
                                               : w.range.checks !== undefined ? "  ·  " + qsTr("%1 checks").arg(w.range.checks) : "")
                color: w.ink
                font.family: w.face
                font.pixelSize: w.unit
                elide: Text.ElideRight
            }
            WidgetKey {
                visible: w.period === "day"
                Layout.preferredWidth: w.unit * 3
                Layout.fillHeight: true
                text: "◀"
                baseColor: w.dayIndex < w.days.length - 1 ? "#343c49" : "#23282f"
                onClicked: if (w.dayIndex < w.days.length - 1) w.dayIndex++
            }
            Text {
                visible: w.period === "day"
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: w.day.label
                color: w.ink
                font.family: w.face
                font.pixelSize: w.unit
                elide: Text.ElideRight
            }
            WidgetKey {
                visible: w.period === "day"
                Layout.preferredWidth: w.unit * 3
                Layout.fillHeight: true
                text: "▶"
                baseColor: w.dayIndex > 0 ? "#343c49" : "#23282f"
                onClicked: if (w.dayIndex > 0) w.dayIndex--
            }
            WidgetKey {
                visible: w.period === "day"
                Layout.preferredWidth: w.unit * 5
                Layout.fillHeight: true
                text: qsTr("Print")
                fontScale: 0.36
                onClicked: w.pos.printReport(w.reportId, w.day.id)
            }
            WidgetKey {
                Layout.preferredWidth: w.unit * 4
                Layout.fillHeight: true
                text: "CSV"
                fontScale: 0.36
                onClicked: w.zone.controller.exportReport(w.report, "csv")
            }
            WidgetKey {
                Layout.preferredWidth: w.unit * 4
                Layout.fillHeight: true
                text: "PDF"
                fontScale: 0.36
                onClicked: w.zone.controller.exportReport(w.report, "pdf")
            }
        }

        Text {
            text: w.report.title ?? ""
            color: w.ink
            font.family: w.face
            font.pixelSize: w.unit * 1.4
            font.bold: true
        }

        // Column headings (reports with more than one value column)
        RowLayout {
            visible: (w.report.columns ?? []).length > 2
            Layout.fillWidth: true
            Layout.fillHeight: false
            Repeater {
                model: w.report.columns ?? []
                delegate: Text {
                    required property string modelData
                    required property int index
                    Layout.fillWidth: index === 0
                    Layout.preferredWidth: index === 0 ? -1 : w.unit * 7
                    horizontalAlignment: index === 0 ? Text.AlignLeft : Text.AlignRight
                    text: modelData
                    color: "#8a94a6"
                    font.family: w.face
                    font.pixelSize: w.unit * 0.8
                }
            }
        }

        ListView {
            id: rows
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.vertical: TouchScrollBar { id: rowsBar }
            model: w.report.rows ?? []
            delegate: Item {
                id: row
                required property var modelData
                readonly property bool section: modelData.kind === "section"
                readonly property bool total: modelData.kind === "total"
                width: ListView.view.width - rowsBar.room
                height: (section ? w.unit * 2.4 : w.unit * 1.6)

                Rectangle {
                    visible: row.total
                    anchors.top: parent.top
                    width: parent.width
                    height: 1
                    color: "#3a4250"
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.topMargin: row.section ? w.unit * 0.8 : 0
                    Repeater {
                        model: row.modelData.cells
                        delegate: Text {
                            required property string modelData
                            required property int index
                            Layout.fillWidth: index === 0
                            Layout.preferredWidth: index === 0 ? -1 : w.unit * 7
                            horizontalAlignment: index === 0 ? Text.AlignLeft : Text.AlignRight
                            text: modelData
                            color: row.modelData.kind === "note" ? "#8a94a6" : w.ink
                            font.family: w.face
                            font.pixelSize: row.section ? w.unit * 1.1 : w.unit
                            font.bold: row.section || row.total
                            font.italic: row.modelData.kind === "note"
                            elide: index === 0 ? Text.ElideRight : Text.ElideNone
                        }
                    }
                }
            }
        }
    }
}
