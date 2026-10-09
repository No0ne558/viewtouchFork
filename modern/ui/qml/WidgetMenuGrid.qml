import QtQuick
import QtQuick.Layouts

// The menu, laid out by itself: a button for every item of a family (or of
// every family, with a chip for each across the top). New items, prices and
// sold-out items show up with no page editing. props.family: one family, or
// empty for all; props.columns; props.photos: show item photos;
// props.search: the items whose name has what's typed (the Find page);
// props.popular: today's best sellers, most sold first (the Popular page).
Item {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    readonly property string fixedFamily: zone && zone.props && zone.props.family ? zone.props.family : ""
    // Columns: set, or (none set) as many as fit the items without scrolling,
    // as big as they can be.
    readonly property int setColumns: zone && zone.props && zone.props.columns > 0 ? zone.props.columns : 0
    readonly property int columns: setColumns > 0 ? setColumns : fitColumns
    readonly property int fitColumns: {
        const n = Math.max(1, shown.length)
        const W = grid.width, H = grid.height
        if (W <= 0 || H <= 0)
            return 4
        let best = 3, bestHeight = 0
        for (let c = 2; c <= 7; ++c) {
            const h = Math.min(W / c * (photos ? 0.9 : 0.62), H / Math.ceil(n / c))
            if (h > bestHeight + 0.5) { best = c; bestHeight = h }
        }
        return best
    }
    readonly property bool photos: zone && zone.props && zone.props.photos === true
    readonly property bool search: zone && zone.props && zone.props.search === true
    readonly property bool popular: zone && zone.props && zone.props.popular === true
    readonly property string typed: pos && search ? pos.textEntry.trim().toLowerCase() : ""
    readonly property string face: zone.st.font ?? "DejaVu Sans"

    readonly property var items: pos ? pos.menuItems.filter(i => !i.modifier) : []
    // The menu's categories with items, in their order (Menu Builder).
    readonly property var categories: pos ? pos.menuCategories.filter(c => c.count > 0) : []
    // The guest's allergies (the check's): items that contain one are marked.
    readonly property var allergies: pos && pos.hasCheck ? (pos.check.allergies ?? []) : []
    function allergyHits(item) { return (item.allergens ?? []).filter(a => allergies.includes(a)) }
    readonly property var allergenNames: pos ? pos.allergenList() : []
    function allergenName(id) { return (allergenNames.find(a => a.id === id) ?? { name: id }).name }
    // ★ Favorites first: the items marked so, then today's best sellers (12 in all).
    readonly property var favorites: {
        const out = items.filter(i => i.favorite)
        for (const id of (pos ? pos.popularItems : []))
            if (out.length < 12 && !out.some(i => i.id === id)) {
                const i = items.find(x => x.id === id)
                if (i) out.push(i)
            }
        return out.slice(0, Math.max(12, items.filter(i => i.favorite).length))
    }
    readonly property var families: (favorites.length ? ["★"] : []).concat(categories.map(c => c.id))
    // The category being shown: the one touched on the menu screen.
    readonly property string chosen: zone && zone.controller ? zone.controller.menuCategory : ""
    readonly property string family: fixedFamily !== "" ? fixedFamily
                                    : families.includes(chosen) ? chosen
                                    : ((categories.find(c => c.now) ?? categories[0] ?? {}).id ?? "")
    function categoryOf(id) { return categories.find(c => c.id === id) ?? ({}) }
    // Searching: every family, best matches first (the name starts with it, then a word does).
    readonly property var shown: {
        if (popular)
            return (pos ? pos.popularItems : []).map(id => items.find(i => i.id === id)).filter(i => i !== undefined)
        if (!search)
            return family === "★" ? favorites : items.filter(i => i.family === family)
        if (typed === "")
            return []
        const digits = /^[0-9]+$/.test(typed)
        const rank = i => {
            if (digits && i.number && i.number.startsWith(typed))
                return i.number === typed ? 0 : 1            // its number first
            const n = i.name.toLowerCase()
            return n.startsWith(typed) ? 2 : n.split(/[^a-z0-9]+/).some(word => word.startsWith(typed)) ? 3
                 : n.includes(typed) ? 4 : -1
        }
        return items.map(i => ({ item: i, rank: rank(i) })).filter(x => x.rank >= 0)
                    .sort((a, b) => a.rank - b.rank || a.item.name.localeCompare(b.item.name)).map(x => x.item)
    }
    function title(f) {
        if (f === "★")
            return qsTr("★ Favorites")
        const name = categoryOf(f).name ?? ""
        return f === "" ? qsTr("Other") : qsTranslate("Page", name !== "" ? name : f.charAt(0).toUpperCase() + f.slice(1))
    }
    function img(ref) { return w.pos && ref ? (w.pos.imageRevision < 0 ? undefined : w.pos.imageUrl(ref)) : "" }

    readonly property real gap: 12
    // Arranging (managers): touch an item, then move it or color it.
    // Kept by the controller: a menu change rebuilds the page.
    property bool arranging: zone && zone.controller ? zone.controller.widgetState(zone.zoneId + ".arranging") === true : false
    property string picked: zone && zone.controller ? (zone.controller.widgetState(zone.zoneId + ".picked") ?? "") : ""
    function setArranging(on) {
        arranging = on
        picked = ""
        zone.controller.setWidgetState(zone.zoneId + ".arranging", on)
        zone.controller.setWidgetState(zone.zoneId + ".picked", "")
    }
    function pick(id) {
        picked = id
        zone.controller.setWidgetState(zone.zoneId + ".picked", id)
    }
    readonly property bool mayArrange: pos !== null && pos.loggedIn && pos.can("manager") && !search && !popular
    readonly property var swatches: ["#a86a12", "#1f8a4c", "#1f6f73", "#2b62b0", "#6b46c1", "#b83232", "#8a5a2b", "#4a5260"]
    // Dark text on light buttons.
    function inkOn(c) {
        const k = Qt.color(c)
        return 0.299 * k.r + 0.587 * k.g + 0.114 * k.b > 0.6 ? "#14171c" : "white"
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: w.gap

        // Families across the top (only when showing them all).
        Flow {
            Layout.fillWidth: true
            visible: !w.search && !w.popular && w.fixedFamily === "" && w.families.length > 1
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
                    onClicked: w.zone.controller.menuCategory = modelData
                }
            }
            WidgetKey {
                objectName: "menuArrange"
                visible: w.mayArrange && !w.arranging
                width: Math.max(160, (w.width - w.gap * 0.6 * 5) / 6)
                height: Math.max(56, w.zone ? w.zone.touch(52) : 56)
                text: qsTr("Arrange…")
                fontScale: 0.38
                onClicked: w.setArranging(true)
            }
        }

        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: w.shown
            cellWidth: width / w.columns
            // Set columns: as before. Fitted: as tall as the rows allow.
            cellHeight: w.setColumns > 0 ? Math.min(cellWidth * (w.photos ? 0.9 : 0.6), Math.max(110, height / 3))
                                        : Math.max(90, Math.min(cellWidth * (w.photos ? 0.9 : 0.75),
                                                                height / Math.ceil(Math.max(1, w.shown.length) / w.columns)))
            boundsBehavior: Flickable.StopAtBounds
            delegate: Item {
                id: cell
                required property var modelData
                // Its own color, else its category's.
                readonly property string itemColor: modelData.buttonColor || (w.categoryOf(modelData.family).color ?? "")
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
                         : press.pressed ? (st.keyLitFill ?? "#4c8dff") : (cell.itemColor || (st.keyFill ?? "#343c49"))
                    readonly property bool isPicked: w.arranging && w.picked === cell.modelData.id
                    readonly property var hits: w.allergyHits(cell.modelData)
                    border.color: isPicked ? "#f5b940" : hits.length ? "#ff3b3b" : Qt.darker(color, 1.4)
                    border.width: isPicked ? 6 : hits.length ? 6 : 2
                    // Has something the guest is allergic to.
                    Rectangle {
                        objectName: "allergyMark-" + cell.modelData.id
                        visible: parent.hits.length > 0
                        z: 3
                        anchors.bottom: parent.bottom
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottomMargin: 4
                        width: Math.min(parent.width - 8, hitText.implicitWidth + 14)
                        height: hitText.implicitHeight + 4
                        radius: height / 2
                        color: "#c62828"
                        Text {
                            id: hitText
                            anchors.centerIn: parent
                            width: Math.min(implicitWidth, parent.parent.width - 22)
                            elide: Text.ElideRight
                            text: "⚠ " + parent.parent.hits.map(a => w.allergenName(a)).join(", ")
                            color: "white"
                            font.family: w.face
                            font.pixelSize: Math.max(15, Math.min(cell.height * 0.09, 22))
                            font.bold: true
                        }
                    }
                    opacity: cell.modelData.available ? 1 : 0.55
                    // Running low: "5 left".
                    readonly property int itemsLeft: w.pos ? (w.pos.stockLeft[cell.modelData.id] ?? -1) : -1
                    Rectangle {
                        visible: parent.itemsLeft > 0 && cell.modelData.available
                        z: 2
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.margins: 6
                        width: leftLabel.implicitWidth + 12
                        height: leftLabel.implicitHeight + 4
                        radius: height / 2
                        color: "#e0161a20"
                        border.color: "#f5b940"
                        border.width: 2
                        Text {
                            id: leftLabel
                            anchors.centerIn: parent
                            text: qsTr("%1 left").arg(parent.parent.itemsLeft)
                            color: "#f5b940"
                            font.family: w.face
                            font.pixelSize: 14
                            font.bold: true
                        }
                    }
                    Text {
                        visible: !!cell.modelData.number
                        z: 2
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.margins: 8
                        text: cell.modelData.number ?? ""
                        color: "#8a94a6"
                        font.family: w.face
                        font.pixelSize: 14
                        font.bold: true
                    }
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
                            color: cell.itemColor ? w.inkOn(cell.itemColor) : (card.st.keyTextColor ?? "white")
                            font.family: card.st.keyFont ?? w.face
                            // Smaller for a long word, so it never breaks mid-word.
                            readonly property int longest: Math.max(4, ...cell.modelData.name.split(/\s+/).map(x => x.length))
                            font.pixelSize: Math.max(13, Math.min(cell.height * 0.16, cell.width * 0.11, cell.width * 1.6 / longest))
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
                        enabled: cell.modelData.available || w.arranging
                        // Searching: the typed text is cleared too, ready for the next one.
                        // Arranging: it's picked instead.
                        onClicked: w.arranging ? w.pick(cell.modelData.id)
                                               : w.zone.controller.orderItem(cell.modelData.id, w.search)
                    }
                }
            }
        }
        // Arranging: move the picked item, color it, Done.
        RowLayout {
            objectName: "arrangeBar"
            visible: w.arranging
            Layout.fillWidth: true
            Layout.fillHeight: false
            spacing: w.gap * 0.5
            readonly property real keyH: Math.max(56, w.zone ? w.zone.touch(52) : 56)
            WidgetKey {
                objectName: "arrangeEarlier"
                enabled: w.picked !== ""
                opacity: enabled ? 1 : 0.4
                Layout.preferredWidth: 150; Layout.preferredHeight: parent.keyH
                text: qsTr("◀ Earlier")
                fontScale: 0.34
                onClicked: w.pos.moveMenuItem(w.picked, -1)
            }
            WidgetKey {
                objectName: "arrangeLater"
                enabled: w.picked !== ""
                opacity: enabled ? 1 : 0.4
                Layout.preferredWidth: 150; Layout.preferredHeight: parent.keyH
                text: qsTr("Later ▶")
                fontScale: 0.34
                onClicked: w.pos.moveMenuItem(w.picked, 1)
            }
            Repeater {
                model: w.swatches
                delegate: Rectangle {
                    required property string modelData
                    required property int index
                    objectName: "swatch-" + index
                    Layout.preferredWidth: parent.keyH
                    Layout.preferredHeight: parent.keyH
                    radius: 10
                    color: modelData
                    opacity: w.picked !== "" ? 1 : 0.4
                    border.color: "white"
                    border.width: 2
                    MouseArea {
                        anchors.fill: parent
                        enabled: w.picked !== ""
                        onClicked: w.pos.setMenuItemColor(w.picked, parent.modelData)
                    }
                }
            }
            WidgetKey {
                objectName: "swatch-none"
                enabled: w.picked !== ""
                opacity: enabled ? 1 : 0.4
                Layout.preferredWidth: 120; Layout.preferredHeight: parent.keyH
                text: qsTr("No color")
                fontScale: 0.3
                onClicked: w.pos.setMenuItemColor(w.picked, "")
            }
            Item { Layout.fillWidth: true }
            WidgetKey {
                objectName: "arrangeDone"
                Layout.preferredWidth: 150; Layout.preferredHeight: parent.keyH
                text: qsTr("Done")
                baseColor: "#1f6b40"
                fontScale: 0.34
                onClicked: w.setArranging(false)
            }
        }
        Text {
            visible: w.shown.length === 0
            Layout.alignment: Qt.AlignHCenter
            text: !w.pos || !w.pos.loggedIn ? ""
                : w.popular ? qsTr("Today's best sellers show up here as orders come in.")
                : w.search ? (w.typed === "" ? qsTr("Type part of a name: \"cob\" finds Cobb.")
                                              : qsTr("Nothing on the menu has \"%1\".").arg(w.typed))
                : qsTr("Nothing on the menu here yet (Manager -> Menu).")
            color: "#8a94a6"
            font.pixelSize: 24
        }
    }
}
