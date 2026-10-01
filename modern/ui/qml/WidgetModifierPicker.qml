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
        ScrollBar.vertical: ScrollBar { policy: flick.contentHeight > flick.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }

        Column {
            id: groups
            width: flick.width
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
                                width: (groups.width - w.unit * 0.9) / 4
                                height: w.unit * 2.6
                                text: modelData.name + (modelData.price ? "\n+" + modelData.price : "")
                                accent: modelData.chosen
                                fontScale: 0.3
                                onClicked: w.pos.chooseOption(group.modelData.id, modelData.index)
                            }
                        }
                    }
                }
            }
        }
    }

    Row {
        id: buttons
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 16 }
        height: w.unit * 2.6
        spacing: w.unit * 0.4
        WidgetKey {
            width: (parent.width - parent.spacing) / 2
            height: parent.height
            text: qsTr("Cancel Item")
            baseColor: "#5a2a2a"
            fontScale: 0.32
            onClicked: {
                w.pos.cancelChoosing()
                w.zone.controller.goBack()
            }
        }
        WidgetKey {
            width: (parent.width - parent.spacing) / 2
            height: parent.height
            text: qsTr("Done")
            accent: true
            fontScale: 0.32
            // Back only once the required choices are made.
            onClicked: w.zone.controller.finishChoosing()
        }
    }
}
