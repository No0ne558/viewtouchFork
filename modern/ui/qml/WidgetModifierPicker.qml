import QtQuick
import QtQuick.Controls.Fusion

// Choices for the item just ordered: each modifier group with its rule
// ("Choose 1", "Up to 3"...) and its options as buttons. Done checks the
// required groups and goes back; Cancel Item takes the item off.
Item {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    readonly property var choosing: pos ? pos.choosing : ({})
    readonly property string face: zone.st.font ?? "DejaVu Sans"
    readonly property color ink: zone.st.textColor ?? "#e6e9ef"
    readonly property real unit: Math.max(16, Math.min(34, w.width / 30))

    Text {
        id: title
        x: 16
        y: 8
        width: parent.width - 32
        text: w.choosing.active ? w.choosing.item : qsTr("Nothing to choose")
        color: w.ink
        font.family: w.face
        font.pixelSize: w.unit * 1.3
        font.bold: true
        elide: Text.ElideRight
    }

    Flickable {
        id: flick
        anchors { left: parent.left; right: parent.right; top: title.bottom; bottom: buttons.top; margins: 16 }
        clip: true
        contentHeight: groups.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: TouchScrollBar { id: flickBar }

        Column {
            id: groups
            width: flick.width - flickBar.room
            spacing: w.unit * 0.6
            Repeater {
                model: w.choosing.groups ?? []
                delegate: Column {
                    id: group
                    required property var modelData
                    width: groups.width
                    spacing: w.unit * 0.3
                    Row {
                        spacing: w.unit * 0.5
                        Text {
                            text: group.modelData.name
                            color: w.ink
                            font.family: w.face
                            font.pixelSize: w.unit
                            font.bold: true
                        }
                        Text {
                            anchors.baseline: parent.children[0].baseline
                            text: group.modelData.rule
                            color: group.modelData.done ? "#8a94a6" : "#f5b940"
                            font.family: w.face
                            font.pixelSize: w.unit * 0.7
                        }
                    }
                    Flow {
                        width: parent.width
                        spacing: w.unit * 0.3
                        Repeater {
                            model: group.modelData.options
                            delegate: WidgetKey {
                                required property var modelData
                                objectName: "option-" + modelData.name
                                width: (groups.width - w.unit * 0.9) / 4
                                height: w.unit * 2.6
                                readonly property bool leftOff: modelData.qualifier === "No"
                                // "No Onion", "Extra Cheese +$1.50": how it was chosen.
                                text: (modelData.qualifier ? modelData.qualifier + " " : "") + modelData.name
                                      + (modelData.soldOut ? "\n" + qsTr("sold out")
                                         : modelData.chosen ? (modelData.chosenPrice ? "\n+" + modelData.chosenPrice : "")
                                         : modelData.price ? "\n+" + modelData.price : "")
                                accent: modelData.chosen && !leftOff
                                baseColor: leftOff ? "#8a2c30" : (keySt.keyFill ?? "#343c49")
                                enabled: !modelData.soldOut || modelData.chosen
                                opacity: enabled ? 1 : 0.4
                                fontScale: 0.3
                                holdable: true
                                onClicked: w.pos.chooseOption(group.modelData.id, modelData.index)
                                // Held: No / Lite / Extra / Side for it.
                                onHeld: w.askHow(group.modelData.id, modelData.index, modelData.name)
                            }
                        }
                    }
                }
            }
        }
    }

    // How to have one choice: held down on it.
    function askHow(groupId, index, name) {
        how.groupId = groupId
        how.index = index
        how.name = name
        how.visible = true
    }
    function chooseAs(qualifier) {
        how.visible = false
        pos.chooseOptionAs(how.groupId, how.index, qualifier)
    }
    Rectangle {
        id: how
        objectName: "qualifierSheet"
        property string groupId
        property int index: -1
        property string name
        visible: false
        anchors.fill: parent
        z: 5
        color: Qt.rgba(0.06, 0.07, 0.09, 0.94)
        radius: 8
        MouseArea { anchors.fill: parent; onClicked: how.visible = false }
        Column {
            anchors.centerIn: parent
            width: parent.width * 0.85
            spacing: w.unit * 0.4
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: how.name
                color: w.ink
                font.family: w.face
                font.pixelSize: w.unit * 1.2
                font.bold: true
                elide: Text.ElideRight
            }
            Grid {
                width: parent.width
                columns: 2
                spacing: w.unit * 0.3
                Repeater {
                    model: [["no", qsTr("No")], ["lite", qsTr("Lite")], ["extra", qsTr("Extra")], ["side", qsTr("On the Side")]]
                    delegate: WidgetKey {
                        required property var modelData
                        objectName: "how-" + modelData[0]
                        width: (parent.width - w.unit * 0.3) / 2
                        height: w.unit * 2.6
                        fontScale: 0.36
                        text: modelData[1]
                        baseColor: modelData[0] === "no" ? "#8a2c30" : "#343c49"
                        onClicked: w.chooseAs(modelData[0])
                    }
                }
            }
            WidgetKey {
                width: parent.width
                height: w.unit * 2
                fontScale: 0.36
                text: qsTr("Cancel")
                onClicked: how.visible = false
            }
        }
    }

    Row {
        id: buttons
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 16 }
        readonly property int shown: (w.zone.keyShown("cancel") ? 1 : 0) + (w.zone.keyShown("done") ? 1 : 0)
        height: shown > 0 ? w.unit * 2.6 : 0
        spacing: w.unit * 0.4
        // Done first, if the editor says so.
        layoutDirection: w.zone.keyOrder("cancel", ["cancel", "done"]) === 1 ? Qt.RightToLeft : Qt.LeftToRight
        WidgetKey {
            width: (parent.width - parent.spacing * (buttons.shown - 1)) / Math.max(1, buttons.shown)
            height: parent.height
            visible: w.zone.keyShown("cancel")
            text: w.zone.keyText("cancel", qsTr("Cancel Item"))
            baseColor: "#5a2a2a"
            fontScale: 0.32
            onClicked: {
                w.pos.cancelChoosing()
                w.zone.controller.goBack()
            }
        }
        WidgetKey {
            width: (parent.width - parent.spacing * (buttons.shown - 1)) / Math.max(1, buttons.shown)
            height: parent.height
            visible: w.zone.keyShown("done")
            text: w.zone.keyText("done", qsTr("Done"))
            accent: true
            fontScale: 0.32
            // Back only once the required choices are made.
            onClicked: w.zone.controller.finishChoosing()
        }
    }
}
