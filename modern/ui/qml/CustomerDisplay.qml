import QtQuick
import QtQuick.Layouts

// What the guest sees at the counter: a welcome; then their order as it is
// rung up with the total; tip choices when the cashier asks; "thank you"
// (and their change) once it is paid.
Rectangle {
    id: d
    property PosService pos
    readonly property bool hasCheck: pos !== null && pos.hasCheck
    readonly property var lines: hasCheck ? pos.lines : []
    readonly property var totals: hasCheck ? pos.totals : ({})
    readonly property var prompt: pos ? pos.customerPrompt : ({})
    readonly property real unit: Math.max(14, Math.min(width, height * 1.6) / 40)

    // The last totals seen, for the thank-you once the check is closed.
    property var lastTotals: ({})
    onTotalsChanged: if (hasCheck) lastTotals = totals
    property bool thanking: false
    Timer { id: thanks; interval: 10000; onTriggered: d.thanking = false }
    Connections {
        target: d.pos
        function onCheckClosed() { d.thanking = true; thanks.restart() }
    }
    onHasCheckChanged: if (hasCheck) thanking = false

    color: "#0f1318"

    // --- welcome ---
    ColumnLayout {
        anchors.centerIn: parent
        visible: !d.hasCheck && !d.thanking
        spacing: d.unit
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: d.pos ? d.pos.storeName : ""
            color: "white"
            font.pixelSize: d.unit * 3
            font.bold: true
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Welcome!")
            color: "#8fb6ff"
            font.pixelSize: d.unit * 2
        }
    }

    // --- thank you ---
    ColumnLayout {
        anchors.centerIn: parent
        visible: d.thanking && !d.hasCheck
        spacing: d.unit
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Thank you!")
            color: "#7ee2a8"
            font.pixelSize: d.unit * 3.5
            font.bold: true
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            visible: d.lastTotals.hasChange ?? false
            text: qsTr("Your change: %1").arg(d.lastTotals.change ?? "")
            color: "white"
            font.pixelSize: d.unit * 2.2
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: d.pos ? qsTr("Come back soon to %1").arg(d.pos.storeName) : ""
            color: "#8a94a6"
            font.pixelSize: d.unit * 1.2
        }
    }

    // --- the order ---
    RowLayout {
        anchors.fill: parent
        anchors.margins: d.unit * 1.5
        spacing: d.unit * 2
        visible: d.hasCheck

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: d.lines
            spacing: d.unit * 0.4
            // The newest item stays in view.
            onCountChanged: positionViewAtEnd()
            delegate: Column {
                required property var modelData
                width: ListView.view.width
                visible: !modelData.comment
                RowLayout {
                    width: parent.width
                    Text {
                        Layout.fillWidth: true
                        text: (modelData.quantity > 1 ? modelData.quantity + " × " : "") + modelData.name
                        color: modelData.voided ? "#6b7385" : "white"
                        font.strikeout: modelData.voided ?? false
                        font.pixelSize: d.unit * 1.3
                        elide: Text.ElideRight
                    }
                    Text {
                        text: modelData.voided ? "" : modelData.price
                        color: "white"
                        font.pixelSize: d.unit * 1.3
                    }
                }
                Repeater {
                    model: modelData.modifiers ?? []
                    delegate: Text {
                        required property var modelData
                        leftPadding: d.unit
                        text: (modelData.name ?? modelData) + (modelData.price ? "  " + modelData.price : "")
                        color: "#9aa4b5"
                        font.pixelSize: d.unit * 0.95
                    }
                }
            }
        }

        ColumnLayout {
            Layout.preferredWidth: parent.width * 0.38
            Layout.fillWidth: false
            Layout.fillHeight: true
            spacing: d.unit * 0.4

            component Line: RowLayout {
                property alias label: l.text
                property alias value: v.text
                property real size: 1.2
                Layout.fillWidth: true
                Text { id: l; Layout.fillWidth: true; color: "#c7cedb"; font.pixelSize: d.unit * parent.size }
                Text { id: v; color: "white"; font.pixelSize: d.unit * parent.size }
            }
            Item { Layout.fillHeight: true }
            Line { label: qsTr("Subtotal"); value: d.totals.subtotal ?? "" }
            Line { label: qsTr("Discounts"); value: d.totals.discounts ?? ""; visible: d.totals.hasDiscount ?? false }
            Line { label: qsTr("Tax"); value: d.totals.tax ?? "" }
            Line { label: qsTr("Gratuity"); value: d.totals.gratuity ?? ""; visible: d.totals.hasGratuity ?? false }
            Line { label: qsTr("Tip"); value: d.prompt.tip || d.totals.tips; visible: (d.prompt.tipChosen ?? false) || (d.totals.hasTips ?? false) }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 2; color: "#2c3442" }
            Line { label: qsTr("Total"); value: d.totals.total ?? ""; size: 2.4 }
            Line { label: qsTr("Paid"); value: d.totals.paid ?? ""; visible: d.hasCheck && d.pos.payments.length > 0 }
            Line { label: qsTr("Change"); value: d.totals.change ?? ""; visible: d.totals.hasChange ?? false; size: 1.8 }
        }
    }

    // --- tip choices, when the cashier asks ---
    Rectangle {
        anchors.fill: parent
        visible: d.prompt.askingTip ?? false
        color: "#e60f1318"
        ColumnLayout {
            anchors.centerIn: parent
            width: Math.min(parent.width * 0.85, d.unit * 50)
            spacing: d.unit
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Add a tip?")
                color: "white"
                font.pixelSize: d.unit * 2.6
                font.bold: true
            }
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Total %1").arg(d.totals.total ?? "")
                color: "#9aa4b5"
                font.pixelSize: d.unit * 1.3
            }
            GridLayout {
                Layout.fillWidth: true
                columns: Math.min(4, Math.max(1, (d.prompt.choices ?? []).length))
                rowSpacing: d.unit * 0.6
                columnSpacing: d.unit * 0.6
                Repeater {
                    model: d.prompt.choices ?? []
                    delegate: Rectangle {
                        id: choice
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: d.unit * 6
                        radius: d.unit * 0.6
                        color: hit.pressed ? "#3d6fd1" : "#2a5bb8"
                        Column {
                            anchors.centerIn: parent
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: choice.modelData.percent + "%"
                                color: "white"
                                font.pixelSize: d.unit * 2.2
                                font.bold: true
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: choice.modelData.amount
                                color: "#d8e4ff"
                                font.pixelSize: d.unit * 1.1
                            }
                        }
                        MouseArea { id: hit; anchors.fill: parent; onClicked: d.pos.customerTip("percent", choice.modelData.percent * 100) }
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: d.unit * 3.4
                radius: d.unit * 0.6
                color: noTip.pressed ? "#3a414f" : "#2a313d"
                Text { anchors.centerIn: parent; text: qsTr("No tip"); color: "white"; font.pixelSize: d.unit * 1.4 }
                MouseArea { id: noTip; anchors.fill: parent; onClicked: d.pos.customerTip("none", 0) }
            }
        }
    }
}
