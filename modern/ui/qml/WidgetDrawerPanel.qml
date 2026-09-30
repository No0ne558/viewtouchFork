import QtQuick
import QtQuick.Layouts

// Cash drawer: start it with a bank, see expected cash, count it at the end
// of the shift. With server banks it is the logged-in person's own bank
// (Start Bank / Check Out). Managers also see everyone else's open drawers
// or banks and can count them. Amounts come from the number pad.
Item {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    readonly property var d: pos ? pos.drawer : ({})
    readonly property string face: zone.st.font ?? "DejaVu Sans"
    readonly property color ink: zone.st.textColor ?? "white"
    readonly property real unit: Math.max(14, Math.min(30, w.width * 0.045))
    readonly property bool bank: (d.mode ?? "") === "serverBank"

    component Line: RowLayout {
        property string name
        property string value
        property bool strong: false
        property color tint: w.ink
        Layout.fillWidth: true
        Text { Layout.fillWidth: true; text: parent.name; color: parent.tint; font.family: w.face; font.pixelSize: w.unit; font.bold: parent.strong }
        Text { text: parent.value; color: parent.tint; font.family: w.face; font.pixelSize: w.unit; font.bold: parent.strong }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: w.unit * 0.8
        spacing: w.unit * 0.4

        Text {
            text: w.d.name ?? qsTr("Drawer 1")
            color: w.ink
            font.family: w.face
            font.pixelSize: w.unit * 1.5
            font.bold: true
        }
        Text {
            text: !w.d.exists ? (w.bank ? qsTr("Starts with your first cash sale") : qsTr("Not started today"))
                 : w.d.open ? qsTr("Open since %1 (%2)").arg(w.d.opened).arg(w.d.openedBy)
                            : qsTr("Counted by %1").arg(w.d.closedBy)
            color: w.d.open ? "#7ee2a8" : "#f5b940"
            font.family: w.face
            font.pixelSize: w.unit * 0.85
        }

        ColumnLayout {
            visible: w.d.exists ?? false
            Layout.fillWidth: true
            Layout.fillHeight: false
            spacing: w.unit * 0.2
            Line { name: qsTr("Starting cash"); value: w.d.startingCash ?? "" }
            Line { name: qsTr("Cash sales"); value: w.d.cashSales ?? "" }
            Repeater {
                model: w.d.movements ?? []
                delegate: Line {
                    required property var modelData
                    name: modelData.what + "  (" + modelData.time + ")"
                    value: modelData.amount
                    tint: "#b8c0cc"
                }
            }
            Line { name: w.bank ? qsTr("Cash to turn in") : qsTr("Expected in drawer"); value: w.d.expected ?? ""; strong: true }
            Line { visible: !w.d.open; name: qsTr("Counted"); value: w.d.counted ?? "" }
            Line {
                visible: !w.d.open
                name: (w.d.overShortCents ?? 0) < 0 ? qsTr("Short") : (w.d.overShortCents ?? 0) > 0 ? qsTr("Over") : qsTr("Balanced")
                value: w.d.overShort ?? ""
                strong: true
                tint: (w.d.overShortCents ?? 0) === 0 ? "#7ee2a8" : "#ff9a9e"
            }
        }

        // Manager: other people's open banks / other terminals' drawers.
        Text {
            visible: (w.d.others ?? []).length > 0
            text: w.bank ? qsTr("Other open banks") : qsTr("Other open drawers")
            color: "#8a94a6"
            font.family: w.face
            font.pixelSize: w.unit * 0.8
        }
        Repeater {
            model: w.d.others ?? []
            delegate: RowLayout {
                id: other
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredHeight: w.unit * 2.2
                Layout.fillHeight: false
                spacing: w.unit * 0.4
                Text {
                    Layout.fillWidth: true
                    text: other.modelData.name
                    color: w.ink
                    font.family: w.face
                    font.pixelSize: w.unit * 0.9
                    elide: Text.ElideRight
                }
                Text {
                    text: other.modelData.expected
                    color: w.ink
                    font.family: w.face
                    font.pixelSize: w.unit * 0.9
                }
                WidgetKey {
                    Layout.preferredWidth: w.unit * 5
                    Layout.fillHeight: true
                    text: qsTr("Count")
                    fontScale: 0.4
                    onClicked: w.pos.countDrawerById(other.modelData.id)
                }
            }
        }

        Item { Layout.fillHeight: true }

        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: w.bank ? (w.d.open ? qsTr("At the end of your shift, count your cash, type it on the keypad, then Check Out.")
                                     : qsTr("Cash you take goes in your bank. To start with cash on hand, type it, then Start Bank."))
                         : w.d.open ? qsTr("At the end of the shift, count the cash, type the total on the keypad, then Count Drawer.")
                                    : qsTr("Type the starting cash on the keypad, then Start Drawer.")
            color: "#8a94a6"
            font.family: w.face
            font.pixelSize: w.unit * 0.8
        }
        Text {
            visible: w.pos && w.pos.entry !== ""
            text: qsTr("Entered: %1").arg(w.pos ? w.pos.entryAmount : "")
            color: "#f5b940"
            font.family: w.face
            font.pixelSize: w.unit
        }
        Text {
            visible: (w.d.open ?? false) && w.pos && w.pos.textEntry !== ""
            text: qsTr("Reason: %1").arg(w.pos ? w.pos.textEntry : "")
            color: "#f5b940"
            font.family: w.face
            font.pixelSize: w.unit * 0.8
            Layout.fillWidth: true
            elide: Text.ElideRight
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: w.unit * 3
            Layout.fillHeight: false   // nested layouts fill by default
            spacing: w.unit * 0.4
            WidgetKey {
                Layout.fillWidth: true
                Layout.fillHeight: true
                text: w.bank ? (w.d.open ? qsTr("Check Out") : qsTr("Start Bank"))
                             : w.d.open ? qsTr("Count Drawer") : qsTr("Start Drawer")
                accent: true
                fontScale: 0.34
                onClicked: w.d.open ? w.pos.countDrawer() : w.pos.openDrawerSession()
            }
            WidgetKey {
                visible: !w.bank   // server banks: no drawer to open
                Layout.fillWidth: true
                Layout.fillHeight: true
                text: qsTr("No Sale")
                fontScale: 0.34
                onClicked: w.pos.noSale()
            }
        }
        // Manager: cash taken out (vendor, ice…) or put in (change). The
        // reason comes from the Reason… keyboard page when typed.
        RowLayout {
            visible: w.d.open ?? false
            Layout.fillWidth: true
            Layout.preferredHeight: w.unit * 3
            Layout.fillHeight: false
            spacing: w.unit * 0.4
            WidgetKey {
                Layout.fillWidth: true
                Layout.fillHeight: true
                text: qsTr("Pay Out")
                fontScale: 0.34
                onClicked: w.pos.payout("payout")
            }
            WidgetKey {
                Layout.fillWidth: true
                Layout.fillHeight: true
                text: qsTr("Paid In")
                fontScale: 0.34
                onClicked: w.pos.payout("paidIn")
            }
        }
    }
}
