import QtQuick

// Renders the controller's current page on its logical canvas, scaled
// uniformly (letterboxed) to fill this item.
Item {
    id: view

    required property LayoutController controller
    // Set while editing; enables the edit overlay.
    property EditorController editor: null
    readonly property bool editing: editor !== null
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
        objectName: "pageSurface"   // tests map canvas coordinates through this
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
                editing: view.editing
                editSelected: view.editing && editLayer.sel.includes(zoneId)
                dragDX: editLayer.dragDX
                dragDY: editLayer.dragDY
                previewRect: editLayer.resizeId === zoneId ? editLayer.previewRect : null
                onSelectRequested: view.selectedZoneId = zoneId
                onActivated: view.controller.activate(zoneId)
            }
        }

        EditLayer {
            id: editLayer
            anchors.fill: parent
            active: view.editing
            editor: view.editor
            controller: view.controller
            scaleFactor: view.scaleFactor
        }
    }

    // Canvas boundary while editing.
    Rectangle {
        visible: view.editing
        x: surface.x - 1
        y: surface.y - 1
        width: surface.width * surface.scale + 2
        height: surface.height * surface.scale + 2
        color: "transparent"
        border.color: EditorStyle.border
    }
}
