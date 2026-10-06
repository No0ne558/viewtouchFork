import QtQuick
import QtQuick.Layouts

// The menu, laid out by itself: a button for every item of a family (or of
// every family, with a chip for each across the top). New items, prices and
// sold-out items show up with no page editing. props.family: one family, or
// empty for all; props.columns; props.photos: show item photos.
Item {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    readonly property string fixedFamily: zone && zone.props && zone.props.family ? zone.props.family : ""
    readonly property int columns: zone && zone.props && zone.props.columns > 0 ? zone.props.columns : 4
    readonly property bool photos: zone && zone.props && zone.props.photos === true
    readonly property string face: zone.st.font ?? "DejaVu Sans"

    readonly property var items: pos ? pos.menuItems.filter(i => !i.modifier) : []
    readonly property var families: {
        const seen = []
        for (const i of items)
            if (!seen.includes(i.family))
                seen.push(i.family)
        return seen
    }
    // Kept by the controller: pages are rebuilt when settings change.
    property string chosen: zone && zone.controller ? (zone.controller.widgetState(zone.zoneId + ".family") ?? "") : ""
    readonly property string family: fixedFamily !== "" ? fixedFamily
                                    : families.includes(chosen) ? chosen : (families[0] ?? "")
    readonly property var shown: items.filter(i => i.family === family)
    function title(f) { return f === "" ? qsTr("Other") : qsTranslate("Page", f.charAt(0).toUpperCase() + f.slice(1)) }
    function img(ref) { return w.pos && ref ? (w.pos.imageRevision, w.pos.imageUrl(ref)) : "" }

    readonly property real gap: 12

    ColumnLayout {
        anchors.fill: parent
        spacing: w.gap

        // Families across the top (only when showing them all).
        Flow {
            Layout.fillWidth: true
            visible: w.fixedFamily === "" && w.families.length > 1
            spacing: w.gap * 0.6
            Repeater {
                model: w.families
                delegate: WidgetKey {
                    required property string modelData
                    objectName: "menuFamily-" + modelData
                    width: Math.max(160, (w.width - w.gap * 0.6 * 5) / 6)
                    height: Math.max(56, w.zone ? w.zone.touch(52) : 56)
                    text: w.title(modelData)
                    accent: modelData === w.family
                    fontScale: 0.38
                    onClicked: {
                        w.chosen = modelData
                        w.zone.controller.setWidgetState(w.zone.zoneId + ".family", modelData)
                    }
                }
            }
        }

        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: w.shown
            cellWidth: width / w.columns
            cellHeight: Math.min(cellWidth * (w.photos ? 0.9 : 0.6), Math.max(110, height / 3))
            boundsBehavior: Flickable.StopAtBounds
            delegate: Item {
                id: cell
                required property var modelData
                width: grid.cellWidth
                height: grid.cellHeight
                Rectangle {
                    id: card
                    objectName: "menuItem-" + cell.modelData.id
                    anchors.fill: parent
                    anchors.margins: w.gap / 2
                    readonly property var st: w.zone ? w.zone.st : ({})
                    radius: st.keyRadius !== undefined ? st.keyRadius : 14
                    color: !cell.modelData.available ? "#3a3f48"
                         : press.pressed ? (st.keyLitFill ?? "#4c8dff") : (st.keyFill ?? "#343c49")
                    border.color: Qt.darker(color, 1.4)
                    border.width: 2
                    opacity: cell.modelData.available ? 1 : 0.55
                    Image {
                        id: photo
                        visible: w.photos && status === Image.Ready
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.margins: 6
                        height: parent.height * 0.55
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        source: w.photos ? w.img(cell.modelData.image) : ""
                    }
                    Column {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.top: photo.visible ? photo.bottom : parent.top
                        anchors.margins: 8
                        spacing: 2
                        Item { width: 1; height: Math.max(0, (parent.height - name.height - price.height) / 2 - 2) }
                        Text {
                            id: name
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            text: qsTranslate("Page", cell.modelData.name)
                            color: card.st.keyTextColor ?? "white"
                            font.family: card.st.keyFont ?? w.face
                            font.pixelSize: Math.max(14, Math.min(cell.height * 0.16, cell.width * 0.11))
                            font.bold: true
                        }
                        Text {
                            id: price
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: !cell.modelData.available ? qsTr("SOLD OUT")
                                : cell.modelData.price + (cell.modelData.byWeight ? " / " + cell.modelData.unit : "")
                            color: !cell.modelData.available ? "#ff6b6b" : Qt.rgba(1, 1, 1, 0.75)
                            font.family: card.st.keyFont ?? w.face
                            font.pixelSize: Math.max(12, name.font.pixelSize * 0.8)
                        }
                    }
                    // A MouseArea: inside a GridView a TapHandler loses the press.
                    MouseArea {
                        id: press
                        anchors.fill: parent
                        enabled: cell.modelData.available
                        onClicked: w.zone.controller.orderItem(cell.modelData.id)
                    }
                }
            }
        }
        Text {
            visible: w.shown.length === 0
            Layout.alignment: Qt.AlignHCenter
            text: w.pos && w.pos.loggedIn ? qsTr("Nothing on the menu here yet (Manager -> Menu).") : ""
            color: "#8a94a6"
            font.pixelSize: 24
        }
    }
}
