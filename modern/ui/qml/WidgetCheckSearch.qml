import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// Find any check, today's or from earlier days: "#123", "17.62", a name,
// a phone number, a table, a server, an item or a gift card. Touch one to
// see it; Reprint Receipt prints a copy.
Item {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    readonly property var info: pos ? pos.checkSearch : ({})
    readonly property var found: info.results ?? []
    readonly property var check: info.selected ?? null

    // Upright (a phone): the results, then a check on its own, with ‹ Results.
    readonly property bool narrow: zone ? zone.narrow : false
    readonly property real zoom: zone ? zone.formZoom(narrow ? 360 : 900) : 1.4
    Item {
        width: w.width / w.zoom
        height: w.height / w.zoom
        scale: w.zoom
        transformOrigin: Item.TopLeft

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: false
                spacing: 8
                TextField {
                    id: query
                    objectName: "searchQuery"
                    Layout.fillWidth: true
                    implicitHeight: 48
                    font.pixelSize: 18
                    placeholderText: qsTr("#123, 17.62, a name, phone, table, server, item…")
                    onAccepted: if (text.trim().length > 1) w.pos.searchChecks(text)
                }
                Button {
                    objectName: "searchGo"
                    text: qsTr("Search")
                    highlighted: true
                    implicitHeight: 48
                    implicitWidth: 140
                    font.pixelSize: 17
                    enabled: query.text.trim().length > 1
                    onClicked: w.pos.searchChecks(query.text)
                }
            }
            Label {
                Layout.fillWidth: true
                visible: !!w.info.query
                text: w.info.loading ? qsTr("Looking through the last year…")
                    : w.found.length === 0 ? qsTr("No checks match \"%1\".").arg(w.info.query)
                    : (w.info.more ? qsTr("The newest %1 that match \"%2\".") : qsTr("%1 found for \"%2\".")).arg(w.found.length).arg(w.info.query)
                opacity: 0.7
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 12

                ListView {
                    id: list
                    objectName: "searchResults"
                    visible: !w.narrow || !w.check
                    Layout.preferredWidth: (w.width / w.zoom - 32) * (w.narrow ? 1 : 0.45)   // not the row's width: that loops
                    Layout.fillWidth: w.narrow
                    Layout.fillHeight: true
                    clip: true
                    spacing: 4
                    model: w.found
                    ScrollBar.vertical: TouchScrollBar { id: listBar; needed: list.contentHeight > list.height + 1 }
                    delegate: Rectangle {
                        id: row
                        required property var modelData
                        objectName: "found-" + modelData.id
                        width: ListView.view.width - listBar.room
                        height: 56
                        radius: 6
                        color: w.check && w.check.id === modelData.id ? "#2f4f86" : rowArea.pressed ? "#323b49" : "#232a35"
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            spacing: 0
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: "#" + row.modelData.id + "  " + row.modelData.label; font.bold: true; font.pixelSize: 15; Layout.fillWidth: true; elide: Text.ElideRight }
                                Label { text: row.modelData.total; font.bold: true; font.pixelSize: 15 }
                            }
                            Label {
                                Layout.fillWidth: true
                                text: row.modelData.when + "  ·  " + row.modelData.server
                                      + (row.modelData.customer ? "  ·  " + row.modelData.customer : "")
                                      + (row.modelData.status !== "closed" ? "  ·  " + row.modelData.status : "")
                                opacity: 0.65
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                        }
                        MouseArea { id: rowArea; anchors.fill: parent; onClicked: w.pos.selectFoundCheck(row.modelData.id) }
                    }
                }

                Rectangle {
                    visible: !w.narrow || !!w.check
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 8
                    color: "#1d232c"
                    Label {
                        anchors.centerIn: parent
                        visible: !w.check
                        text: w.found.length ? qsTr("Touch a check to see it.") : ""
                        opacity: 0.6
                    }
                    ColumnLayout {
                        visible: !!w.check
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 4
                        Button {
                            visible: w.narrow
                            text: qsTr("‹ Results")
                            onClicked: w.pos.selectFoundCheck(0)
                        }
                        Label { text: w.check ? "#" + w.check.id + "  " + w.check.label : ""; font.bold: true; font.pixelSize: 20 }
                        Label {
                            Layout.fillWidth: true
                            text: w.check ? w.check.server + "  ·  " + qsTr("%n guest(s)", "", w.check.guests ?? 1)
                                            + (w.check.customer ? "  ·  " + w.check.customer : "") : ""
                            opacity: 0.7
                            elide: Text.ElideRight
                        }
                        Label {
                            Layout.fillWidth: true
                            text: w.check ? qsTr("Opened %1").arg(w.check.opened) + (w.check.closed ? "  ·  " + qsTr("closed %1").arg(w.check.closed) : "") : ""
                            opacity: 0.6
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                        ListView {
                            id: lines
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.topMargin: 6
                            clip: true
                            model: w.check ? w.check.lines.concat(w.check.payments.map(p => ({ name: p.name, price: p.amount, payment: true, modifiers: p.tip ? qsTr("tip %1").arg(p.tip) : "" }))) : []
                            ScrollBar.vertical: TouchScrollBar { id: linesBar; needed: lines.contentHeight > lines.height + 1 }
                            delegate: ColumnLayout {
                                required property var modelData
                                width: ListView.view.width - linesBar.room
                                spacing: 0
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label {
                                        Layout.fillWidth: true
                                        text: (modelData.quantity > 1 ? modelData.quantity + " × " : "") + modelData.name
                                        font.strikeout: modelData.voided ?? false
                                        color: modelData.payment ? "#7ee2a8" : "white"
                                        elide: Text.ElideRight
                                    }
                                    Label { text: modelData.price ?? ""; color: modelData.payment ? "#7ee2a8" : "white" }
                                }
                                Label {
                                    Layout.fillWidth: true
                                    visible: !!modelData.modifiers
                                    text: modelData.modifiers ?? ""
                                    opacity: 0.6
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("Total"); font.bold: true; font.pixelSize: 18; Layout.fillWidth: true }
                            Label { text: w.check ? w.check.total : ""; font.bold: true; font.pixelSize: 18 }
                        }
                        Button {
                            objectName: "reprint"
                            Layout.fillWidth: true
                            implicitHeight: 50
                            font.pixelSize: 16
                            text: qsTr("Reprint Receipt")
                            onClicked: w.pos.reprintCheck(w.check.id)
                        }
                    }
                }
            }
        }
    }
}
