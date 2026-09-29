import QtQuick

// Every open check as a card; touch one to work on it.
Item {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    readonly property string face: zone.st.font ?? "DejaVu Sans"
    // A table with several checks (after a split) shows only its checks.
    readonly property string filter: pos ? pos.checkFilter : ""

    Rectangle {
        id: banner
        visible: w.filter !== ""
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 16
        height: visible ? 64 : 0
        radius: 10
        color: "#2b62b0"
        Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 20
            text: qsTr("Checks at %1 — touch here to show all").arg(w.filter)
            color: "white"
            font.family: w.face
            font.pixelSize: 26
        }
        TapHandler { onTapped: w.pos.checkFilter = "" }
    }

    GridView {
        id: grid
        anchors.fill: parent
        anchors.margins: 16
        anchors.topMargin: banner.visible ? 96 : 16
        clip: true
        cellWidth: Math.max(280, width / Math.max(1, Math.floor(width / 320)))
        cellHeight: 170
        model: !w.pos ? [] : w.filter === "" ? w.pos.openChecks
                                             : w.pos.openChecks.filter(c => c.label === w.filter)

        delegate: Item {
            id: card
            required property var modelData
            width: grid.cellWidth
            height: grid.cellHeight

            ZoneShape {
                anchors.fill: parent
                anchors.margins: 8
                st: ({ fill: tap.pressed ? "#3b4556" : (card.modelData.mine ? "#1f5f3a" : "#2d3440"),
                       frame: "raised", frameWidth: 3, radius: 14, shadow: 4 })
            }
            Column {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 4
                Text {
                    width: parent.width
                    text: card.modelData.label
                    color: "white"
                    font.family: w.face
                    font.pixelSize: 30
                    font.bold: true
                    elide: Text.ElideRight
                }
                Text {
                    text: card.modelData.total
                    color: "white"
                    font.family: w.face
                    font.pixelSize: 26
                }
                Text {
                    width: parent.width
                    text: qsTr("%1 · %2 min").arg(card.modelData.server).arg(card.modelData.minutes)
                    color: "#b8c0cc"
                    font.family: w.face
                    font.pixelSize: 20
                    elide: Text.ElideRight
                }
            }
            TapHandler {
                id: tap
                onTapped: w.zone.controller.openCheck(card.modelData.id)
            }
        }

        Text {
            anchors.centerIn: parent
            visible: grid.count === 0
            text: qsTr("No open checks")
            color: "#8a94a6"
            font.family: w.face
            font.pixelSize: 32
        }
    }
}
