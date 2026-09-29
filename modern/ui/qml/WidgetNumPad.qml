import QtQuick
import QtQuick.Layouts

// Number entry for guest counts and payment amounts. props.mode:
// "number" (default) or "amount" (shows money, has a 00 key).
Item {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    readonly property bool amount: (zone && zone.props && zone.props.mode === "amount")

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: w.height * 0.035
        spacing: w.height * 0.025

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: w.height * 0.15
            radius: 8
            color: "#10141a"
            Text {
                anchors.fill: parent
                anchors.rightMargin: 16
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                text: !w.pos ? "" : w.amount ? (w.pos.entry !== "" ? w.pos.entryAmount
                                                : w.pos.hasCheck ? qsTr("Balance due") : w.pos.entryAmount)
                                              : (w.pos.entry === "" ? "0" : w.pos.entry)
                color: w.pos && w.pos.entry === "" ? "#8a94a6" : "white"
                font.family: w.zone.st.font ?? "DejaVu Sans"
                font.pixelSize: parent.height * 0.5
                font.bold: true
            }
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: 3
            rowSpacing: w.height * 0.02
            columnSpacing: w.height * 0.02

            Repeater {
                model: ["7", "8", "9", "4", "5", "6", "1", "2", "3", "clear", "0", w.amount ? "00" : "back"]
                delegate: WidgetKey {
                    required property string modelData
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    text: modelData === "clear" ? qsTr("Clear") : modelData === "back" ? "⌫" : modelData
                    fontScale: modelData === "clear" ? 0.3 : 0.42
                    textColor: w.zone.st.textColor ?? "white"
                    fontFamily: w.zone.st.font ?? "DejaVu Sans"
                    onClicked: w.pos.entryKey(modelData)
                }
            }
        }
    }
}
