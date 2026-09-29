import QtQuick

// Renders the controller's current page on its logical canvas, scaled
// uniformly (letterboxed) to fill this item.
Item {
    id: view

    required property LayoutController controller
    property string selectedZoneId: ""

    readonly property size canvas: controller.canvasSize
    readonly property real scaleFactor: Math.min(width / canvas.width, height / canvas.height)
    readonly property var bg: controller.background

    Connections {
        target: view.controller
        function onPageChanged() { view.selectedZoneId = "" }
    }

    Item {
        id: surface
        width: view.canvas.width
        height: view.canvas.height
        scale: view.scaleFactor
        transformOrigin: Item.TopLeft
        x: (view.width - width * scale) / 2
        y: (view.height - height * scale) / 2
        clip: true

        Rectangle {
            anchors.fill: parent
            color: view.bg.fill ?? "#20232a"
        }

        Image {
            anchors.fill: parent
            visible: !!view.bg.texture
            source: view.bg.texture ? "qrc:/textures/" + view.bg.texture + ".xpm" : ""
            fillMode: Image.Tile
        }

        Repeater {
            model: view.controller.zones
            delegate: ZoneItem {
                selectedZoneId: view.selectedZoneId
                onSelectRequested: view.selectedZoneId = zoneId
                onActivated: view.controller.activate(zoneId)
            }
        }
    }
}
