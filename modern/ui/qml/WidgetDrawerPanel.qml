import QtQuick
import QtQuick.Layouts

// Cash drawer: start it with a bank, see expected cash, count it at the end
// of the shift. Amounts come from the number pad on the same page.
Item {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    readonly property var d: pos ? pos.drawer : ({})
    readonly property string face: zone.st.font ?? "DejaVu Sans"
    readonly property color ink: zone.st.textColor ?? "white"
    readonly property real unit: Math.max(14, Math.min(30, w.width * 0.045))

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
            text: !w.d.exists ? qsTr("Not started today")
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
            Line { name: qsTr("Expected in drawer"); value: w.d.expected ?? ""; strong: true }
            Line { visible: !w.d.open; name: qsTr("Counted"); value: w.d.counted ?? "" }
            Line {
                visible: !w.d.open
                name: (w.d.overShortCents ?? 0) < 0 ? qsTr("Short") : (w.d.overShortCents ?? 0) > 0 ? qsTr("Over") : qsTr("Balanced")
                value: w.d.overShort ?? ""
                strong: true
                tint: (w.d.overShortCents ?? 0) === 0 ? "#7ee2a8" : "#ff9a9e"
            }
        }

        Item { Layout.fillHeight: true }

        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: w.d.open ? qsTr("At the end of the shift, count the cash, type the total on the keypad, then Count Drawer.")
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
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: w.unit * 3
            Layout.fillHeight: false   // nested layouts fill by default
            spacing: w.unit * 0.4
            WidgetKey {
                Layout.fillWidth: true
                Layout.fillHeight: true
                text: w.d.open ? qsTr("Count Drawer") : qsTr("Start Drawer")
                accent: true
                fontScale: 0.34
                onClicked: w.d.open ? w.pos.countDrawer() : w.pos.openDrawerSession()
            }
            WidgetKey {
                Layout.fillWidth: true
                Layout.fillHeight: true
                text: qsTr("No Sale")
                fontScale: 0.34
                onClicked: w.pos.noSale()
            }
        }
    }
}
