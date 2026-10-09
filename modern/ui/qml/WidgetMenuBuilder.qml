import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Dialogs
import QtQuick.Layouts

// Manager -> Menu Builder: the whole menu on one screen. Categories (their
// order, color, meal periods, and what new items in them start with); a
// category's items as tiles, + Add Item; an item's card: name, price,
// category, photo, its choices picked from a list, and what's on it (No,
// Light, Extra, On the side then work everywhere). On a phone, one at a
// time: categories, items, the card.
Item {
    id: w
    // Every button a finger's size.
    component TouchButton: Button {
        implicitHeight: 52
        font.pixelSize: 16
    }
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    readonly property bool narrow: zone ? zone.narrow : false
    readonly property real zoom: zone ? zone.formZoom(narrow ? 340 : 1180) : 1

    readonly property var categories: pos ? pos.menuCategories : []
    readonly property var allItems: pos ? pos.menuItems.filter(i => !i.modifier) : []
    readonly property var groups: pos ? pos.choiceGroups.filter(g => !g.own) : []
    readonly property var periods: pos ? pos.mealPeriods : []
    readonly property var stations: pos ? pos.kitchenStations.filter(s => !s.printer) : []
    readonly property var printers: pos ? pos.kitchenStations.filter(s => s.printer) : []
    readonly property var taxes: [{ id: "food", name: qsTr("Food") }, { id: "alcohol", name: qsTr("Alcohol") },
                                  { id: "merchandise", name: qsTr("Merchandise") }, { id: "room", name: qsTr("Room") }]
    readonly property var swatches: ["#a86a12", "#b83232", "#1f8a4c", "#1f6f73", "#2b62b0", "#6b46c1", "#8a5a2b", "#4a5260"]

    // What's chosen, and the card being edited.
    property string categoryId: ""
    readonly property var category: categories.find(c => c.id === categoryId) ?? null
    readonly property var items: allItems.filter(i => i.family === categoryId)
    property string itemId: ""        // "" + editingItem: a new one
    property bool editingItem: false
    property bool editingCategory: false
    property bool editingGroup: false
    property string mode: "menu"          // menu | choices (Choice Groups)
    readonly property var editedGroup: editingGroup && draft.id ? (pos ? pos.choiceGroups.find(g => g.id === draft.id) : null) : null
    property var draft: ({})
    property string stage: "categories"   // a phone: categories | items | card
    // Dragging an item tile or a category (hold, then drag): which, and where to.
    property string dragId: ""
    property int dropIndex: -1

    Component.onCompleted: if (categories.length) categoryId = categories[0].id
    // A new category, once saved, is the one shown.
    property string waitingForCategory: ""
    onCategoriesChanged: {
        const added = waitingForCategory === "" ? null
                    : categories.find(c => c.name.toLowerCase() === waitingForCategory.toLowerCase())
        if (added) {
            waitingForCategory = ""
            pickCategory(added.id)
            Qt.callLater(() => categoryList.positionViewAtIndex(categories.findIndex(c => c.id === added.id), ListView.Contain))
        } else if (!category && categories.length) {
            categoryId = categories[0].id
        }
    }

    function copy(o) { return JSON.parse(JSON.stringify(o)) }
    function pickCategory(id) {
        categoryId = id
        editingItem = false
        editingCategory = false
        stage = "items"
    }
    function editCategory(c) {
        returnToItem = null
        draft = categoryDraft(c)
        editingCategory = true
        editingItem = false
        stage = "card"
    }
    function itemDraft(i) {
        return i ? { id: i.id, name: i.name, price: i.priceValue.toFixed(2), family: i.family, image: i.image,
                     groups: i.groups.slice(), onIt: i.onIt.join(", "), available: i.availableSet,
                     kioskHide: i.kioskHide, description: i.description, favorite: i.favorite }
                 : { id: "", name: "", price: "", family: categoryId, image: "", groups: [], onIt: "",
                     available: true, kioskHide: false, description: "", favorite: false }
    }
    function categoryDraft(c) {
        return c ? { id: c.id, name: c.name, color: c.color, periods: c.periods.slice(), printer: c.printer,
                     station: c.station, taxClass: c.taxClass }
                 : { id: "", name: "", color: swatches[categories.length % swatches.length], periods: [],
                     printer: "kitchen", station: "", taxClass: "food" }
    }
    function groupDraft(g) {
        const kind = !g ? "one" : g.max === 1 ? "one" : g.max === 0 ? "any" : "upTo"
        return g ? { id: g.id, name: g.name, kind: kind, upTo: g.max > 1 ? g.max : 3, required: g.min > 0,
                     atLeast: Math.max(1, g.min), askHow: g.askHow, options: g.options.map(o => ({ name: o.name,
                     price: o.price ? o.price.toFixed(2) : "", included: o.included, kitchenName: o.kitchenName })) }
                 : { id: "", name: "", kind: "one", upTo: 3, required: true, atLeast: 1, askHow: false,
                     options: [{ name: "", price: "", included: false }, { name: "", price: "", included: false }] }
    }
    // Unsaved changes on the open card? Compared with what's saved, the way
    // the store reads it ("3.5" is 3.50; groups and periods in any order).
    function same(a, b) {
        const norm = v => {
            if (Array.isArray(v)) {
                const n = v.map(norm)
                return n.every(x => typeof x === "string") ? n.slice().sort() : n
            }
            if (v && typeof v === "object") {
                const o = {}
                for (const k of Object.keys(v).sort()) o[k] = norm(v[k])
                return o
            }
            if (typeof v === "string") {
                const t = v.trim()
                if (t !== "" && !isNaN(Number(t.replace(",", ".")))) return String(Number(t.replace(",", ".")))
                return t.split(",").map(x => x.trim()).filter(x => x).join(",")
            }
            return v === undefined || v === null ? "" : v
        }
        return JSON.stringify(norm(a)) === JSON.stringify(norm(b))
    }
    function unsaved() {
        // A new one already saved: it's there by its name now.
        const named = list => !draft.id && (draft.name ?? "").trim() !== ""
                              && list.some(x => x.name.toLowerCase() === draft.name.trim().toLowerCase())
        if ((editingItem && named(allItems)) || (editingCategory && named(categories)) || (editingGroup && named(allGroups)))
            return false
        if (editingItem) {
            const saved = draft.id ? allItems.find(i => i.id === draft.id) : null
            if (draft.id && !saved) return false   // removed meanwhile
            const fresh = itemDraft(saved)
            if (!draft.id) fresh.family = draft.family
            return !same(fresh, draft)
        }
        if (editingCategory) {
            const saved = draft.id ? categories.find(c => c.id === draft.id) : null
            if (draft.id && !saved) return false
            const fresh = categoryDraft(saved)
            if (!draft.id) fresh.color = draft.color
            return !same(fresh, draft)
        }
        if (editingGroup) {
            const saved = draft.id ? allGroups.find(g => g.id === draft.id) : null
            if (draft.id && !saved) return false
            return !same(groupDraft(saved), draft)
        }
        return false
    }
    // Before leaving the open card for another: keep or drop its changes.
    function leave(then) {
        if (unsaved()) {
            leaveDialog.then = then
            leaveDialog.open()
        } else {
            then()
        }
    }
    function saveCard() {
        if (editingItem) saveItem()
        else if (editingCategory) saveCategoryCard()
        else if (editingGroup) saveGroupCard()
    }
    function saveCategoryCard() {
        const c = copy(draft)
        if (!c.id)
            waitingForCategory = c.name.trim()
        pos.saveCategory(c)
        if (!c.id)
            editingCategory = false
    }
    function saveGroupCard() {
        const g = groupRecord()
        if (!g.id)
            waitingForGroup = g.name.trim()
        pos.saveChoiceGroup(g)
    }
    // + New Choice Group on an item's card: back to the item afterwards.
    property var returnToItem: null
    function backToItem(groupId) {
        const d = returnToItem
        returnToItem = null
        if (!d) return
        if (groupId && !d.groups.includes(groupId))
            d.groups.push(groupId)
        mode = "menu"
        if (d.family) categoryId = d.family
        draft = d
        itemId = d.id
        editingGroup = false
        editingCategory = false
        editingItem = true
        stage = "card"
    }
    function editItem(i) {
        returnToItem = null
        draft = itemDraft(i)
        itemId = i ? i.id : ""
        editingItem = true
        editingCategory = false
        stage = "card"
    }
    // A choice group: its rule in words (how many; required), its options as rows.
    function editGroup(g) {
        draft = groupDraft(g)
        mode = "choices"
        editingGroup = true
        editingItem = false
        editingCategory = false
        stage = "card"
    }
    function setOption(i, key, value) {
        const d = copy(draft)
        d.options[i][key] = value
        draft = d
    }
    function moveOption(i, by) {
        const d = copy(draft)
        const j = i + by
        if (j < 0 || j >= d.options.length) return
        const t = d.options[i]; d.options[i] = d.options[j]; d.options[j] = t
        draft = d
    }
    function groupRecord() {
        const d = draft
        const max = d.kind === "one" ? 1 : d.kind === "upTo" ? Math.max(2, Number(d.upTo) || 2) : 0
        const min = !d.required ? 0 : d.kind === "any" ? Math.max(1, Number(d.atLeast) || 1) : 1
        return { id: d.id, name: d.name, min: min, max: max, askHow: d.askHow, options: d.options }
    }
    property string waitingForGroup: ""
    readonly property var allGroups: pos ? pos.choiceGroups : []
    onAllGroupsChanged: {
        if (waitingForGroup === "")
            return
        const added = allGroups.find(g => g.name.toLowerCase() === waitingForGroup.toLowerCase())
        if (added) {
            waitingForGroup = ""
            if (returnToItem) {
                backToItem(added.id)
                return
            }
            editGroup(added)
            Qt.callLater(() => groupList.positionViewAtIndex(groups.findIndex(g => g.id === added.id), ListView.Contain))
        }
    }
    function set(key, value) {
        const d = copy(draft)
        d[key] = value
        draft = d
    }
    function toggleIn(key, value) {
        const list = (draft[key] ?? []).slice()
        const at = list.indexOf(value)
        if (at >= 0) list.splice(at, 1); else list.push(value)
        set(key, list)
    }
    // A new item, once saved, is the one shown.
    property string waitingFor: ""
    // Duplicate: the item that wasn't there before is the one shown.
    property var idsBefore: null
    onAllItemsChanged: {
        if (idsBefore) {
            const copy = allItems.find(i => !idsBefore.includes(i.id))
            if (copy) {
                idsBefore = null
                categoryId = copy.family
                editItem(copy)
            }
            return
        }
        if (waitingFor === "")
            return
        const added = allItems.find(i => i.name.toLowerCase() === waitingFor.toLowerCase())
        if (added) {
            waitingFor = ""
            categoryId = added.family
            editItem(added)
        }
    }
    function saveItem() {
        const card = copy(draft)
        if (card.id === "")
            waitingFor = card.name.trim()
        pos.saveMenuItemCard(card)
    }
    function inkOn(c) {
        const k = Qt.color(c)
        return 0.299 * k.r + 0.587 * k.g + 0.114 * k.b > 0.6 ? "#14171c" : "white"
    }

    Item {
        width: w.width / w.zoom
        height: w.height / w.zoom
        scale: w.zoom
        transformOrigin: Item.TopLeft

        // Older stores: meal pages with category buttons placed by hand.
        Rectangle {
            id: handBuilt
            objectName: "builderHandBuilt"
            visible: w.zone && w.zone.controller ? w.zone.controller.menuScreensHandBuilt : false
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 8 }
            height: visible ? handBuiltRow.implicitHeight + 20 : 0
            radius: 8
            color: "#4a3a14"
            RowLayout {
                id: handBuiltRow
                anchors.fill: parent
                anchors.margins: 10
                spacing: 12
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: 15
                    text: qsTr("Your menu pages have buttons placed by hand, so new items and categories don't show up on them. Switch them to fill themselves from the menu? You'll see them in the page editor first, to Save or Undo.")
                }
                TouchButton {
                    objectName: "builderSwitchScreens"
                    highlighted: true
                    text: qsTr("Switch to Self-Filling")
                    onClicked: w.zone.controller.switchToSelfFillingMenu()
                }
            }
        }

        RowLayout {
            anchors { left: parent.left; right: parent.right; top: handBuilt.bottom; bottom: parent.bottom; margins: 8 }
            spacing: 12

            // --- categories ---
            ColumnLayout {
                visible: !w.narrow || w.stage === "categories"
                Layout.preferredWidth: w.narrow ? -1 : 270
                Layout.fillWidth: w.narrow
                Layout.fillHeight: true
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    TouchButton {
                        objectName: "builderModeMenu"
                        Layout.fillWidth: true
                        text: qsTr("Menu")
                        highlighted: w.mode === "menu"
                        onClicked: w.leave(() => { w.returnToItem = null; w.mode = "menu"; w.editingGroup = false })
                    }
                    TouchButton {
                        objectName: "builderModeChoices"
                        Layout.fillWidth: true
                        text: qsTr("Choice Groups")
                        highlighted: w.mode === "choices"
                        onClicked: w.leave(() => { w.mode = "choices"; w.editingItem = false; w.editingCategory = false })
                    }
                }
                // Ready to go? What would trip up service.
                TouchButton {
                    objectName: "builderCheck"
                    readonly property var problems: w.pos ? w.pos.menuProblems : []
                    readonly property int serious: problems.filter(p => p.serious).length
                    Layout.fillWidth: true
                    text: serious ? qsTr("⚠ %n to fix", "", serious)
                                  : problems.length ? qsTr("✓ Ready (%n note(s))", "", problems.length) : qsTr("✓ Ready to go")
                    palette.button: serious ? "#7a2e2e" : "#1f5f3a"
                    onClicked: checkDialog.open()
                }
                Label { visible: w.mode === "menu"; text: qsTr("Categories"); font.pixelSize: 22; font.bold: true }
                // Choice groups: every one, its rule and who uses it.
                ListView {
                    id: groupList
                    visible: w.mode === "choices"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 6
                    model: w.groups
                    delegate: Rectangle {
                        required property var modelData
                        objectName: "builderGroupRow-" + modelData.id
                        width: groupList.width
                        height: 64
                        radius: 8
                        color: w.editingGroup && w.draft.id === modelData.id ? "#2b3a52" : "#232933"
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 0
                            Label { text: modelData.name; font.pixelSize: 18; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                            Label {
                                text: modelData.rule + "  ·  " + qsTr("%n option(s)", "", modelData.options.length)
                                      + "  ·  " + qsTr("used by %n", "", modelData.usedBy.length)
                                font.pixelSize: 13
                                opacity: 0.7
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                        MouseArea { anchors.fill: parent; onClicked: w.leave(() => w.editGroup(modelData)) }
                    }
                }
                TouchButton {
                    objectName: "builderAddGroup"
                    visible: w.mode === "choices"
                    Layout.fillWidth: true
                    text: qsTr("+ Choice Group")
                    onClicked: w.leave(() => { w.returnToItem = null; w.editGroup(null) })
                }
                ListView {
                    id: categoryList
                    visible: w.mode === "menu"
                    interactive: w.dragId === ""
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 6
                    model: w.categories
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        objectName: "builderCategory-" + modelData.id
                        width: categoryList.width
                        height: 64
                        radius: 8
                        color: modelData.id === w.categoryId ? "#2b3a52" : "#232933"
                        border.color: modelData.id === w.categoryId ? "#4c8dff" : "transparent"
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 10
                            Rectangle { width: 18; height: 40; radius: 4; color: modelData.color || "#4a5260" }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                Label { text: modelData.name; font.pixelSize: 18; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                                Label {
                                    text: qsTr("%n item(s)", "", modelData.count) + "  ·  "
                                          + (modelData.periods.length === 0 ? qsTr("all day")
                                             : modelData.periods.map(p => (w.periods.find(x => x.id === p) ?? { name: p }).name).join(", "))
                                    font.pixelSize: 13
                                    opacity: 0.7
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }
                        }
                        Rectangle {
                            anchors.fill: parent
                            visible: w.dragId !== "" && w.dropIndex === index && w.dragId !== modelData.id && w.mode === "menu"
                            color: "transparent"
                            radius: parent.radius
                            border.color: "#f5b940"
                            border.width: 4
                        }
                        MouseArea {
                            anchors.fill: parent
                            pressAndHoldInterval: 350
                            property bool dragging: false
                            preventStealing: dragging
                            onClicked: w.leave(() => w.pickCategory(modelData.id))
                            onPressAndHold: m => {
                                dragging = true
                                w.dragId = modelData.id
                                ghost.text = modelData.name
                                ghost.color = modelData.color || "#4a5260"
                                moved(m)
                            }
                            function moved(m) {
                                const p = mapToItem(categoryList, m.x, m.y)
                                w.dropIndex = categoryList.indexAt(p.x + categoryList.contentX, p.y + categoryList.contentY)
                                const g = mapToItem(ghost.parent, m.x, m.y)
                                ghost.x = g.x - ghost.width / 2
                                ghost.y = g.y - ghost.height / 2
                            }
                            onPositionChanged: m => { if (dragging) moved(m) }
                            onReleased: {
                                if (!dragging) return
                                dragging = false
                                const to = w.dropIndex
                                const id = w.dragId
                                w.dragId = ""
                                w.dropIndex = -1
                                if (to >= 0 && w.categories[to] && w.categories[to].id !== id)
                                    w.pos.moveCategoryTo(id, to)
                            }
                            onCanceled: { dragging = false; w.dragId = ""; w.dropIndex = -1 }
                        }
                    }
                }
                RowLayout {
                    visible: w.mode === "menu"
                    Layout.fillWidth: true
                    spacing: 6
                    TouchButton {
                        objectName: "builderAddCategory"
                        Layout.fillWidth: true
                        text: qsTr("+ Category")
                        onClicked: w.leave(() => w.editCategory(null))
                    }
                    TouchButton { text: "▲"; enabled: !!w.category; onClicked: w.pos.moveCategory(w.categoryId, -1) }
                    TouchButton { text: "▼"; enabled: !!w.category; onClicked: w.pos.moveCategory(w.categoryId, 1) }
                }
                // Many at once: from a spreadsheet, or a starter menu.
                RowLayout {
                    visible: w.mode === "menu"
                    Layout.fillWidth: true
                    spacing: 6
                    TouchButton {
                        objectName: "builderImport"
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        text: qsTr("Import…")
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("A spreadsheet saved as CSV")
                        onClicked: importFile.open()
                    }
                    TouchButton {
                        objectName: "builderTemplates"
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        text: qsTr("Starter Menu…")
                        onClicked: templateDialog.open()
                    }
                }
            }

            // --- the category's items ---
            ColumnLayout {
                visible: (!w.narrow || w.stage === "items") && w.mode === "menu"
                Layout.preferredWidth: w.narrow ? -1 : 430
                Layout.fillWidth: w.narrow
                Layout.fillHeight: true
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    TouchButton { visible: w.narrow; text: qsTr("‹ Categories"); onClicked: w.stage = "categories" }
                    Label {
                        Layout.fillWidth: true
                        text: w.category ? w.category.name : ""
                        font.pixelSize: 22
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    TouchButton {
                        objectName: "builderAddSeveral"
                        visible: !!w.category
                        text: qsTr("Add Several…")
                        onClicked: severalDialog.open()
                    }
                    TouchButton {
                        objectName: "builderEditCategory"
                        visible: !!w.category
                        text: qsTr("Edit Category")
                        onClicked: w.leave(() => w.editCategory(w.category))
                    }
                }
                Label {
                    visible: w.items.length > 1
                    text: qsTr("Hold one and drag it to move it (the order screen follows).")
                    opacity: 0.6
                    font.pixelSize: 13
                }
                GridView {
                    id: itemGrid
                    interactive: w.dragId === ""
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    cellWidth: width / 2
                    cellHeight: 96
                    model: [{ add: true }].concat(w.items)
                    delegate: Item {
                        required property var modelData
                        required property int index
                        width: itemGrid.cellWidth
                        height: itemGrid.cellHeight
                        Rectangle {
                            objectName: modelData.add ? "builderAddItem" : "builderItem-" + modelData.id
                            anchors.fill: parent
                            anchors.margins: 4
                            radius: 10
                            readonly property string tint: modelData.add ? "#1f5f3a"
                                                          : (modelData.buttonColor || (w.category ? w.category.color : "") || "#343c49")
                            color: tint
                            opacity: modelData.add || modelData.availableSet ? 1 : 0.5
                            border.color: !modelData.add && w.editingItem && w.itemId === modelData.id ? "#f5b940" : Qt.darker(tint, 1.3)
                            border.width: !modelData.add && w.editingItem && w.itemId === modelData.id ? 4 : 1
                            Column {
                                anchors.centerIn: parent
                                width: parent.width - 16
                                Label {
                                    width: parent.width
                                    horizontalAlignment: Text.AlignHCenter
                                    text: modelData.add ? qsTr("+ Add Item") : modelData.name
                                    color: w.inkOn(parent.parent.tint)
                                    font.pixelSize: 18
                                    font.bold: true
                                    elide: Text.ElideRight
                                }
                                Label {
                                    width: parent.width
                                    horizontalAlignment: Text.AlignHCenter
                                    visible: !modelData.add
                                    text: modelData.add ? "" : (modelData.availableSet ? modelData.price : qsTr("sold out"))
                                    color: w.inkOn(parent.parent.tint)
                                    opacity: 0.8
                                    font.pixelSize: 15
                                }
                            }
                            // Outlined: where a dragged item will go.
                            Rectangle {
                                anchors.fill: parent
                                visible: w.dragId !== "" && !modelData.add && w.dropIndex === index && w.dragId !== modelData.id
                                color: "transparent"
                                radius: parent.radius
                                border.color: "#f5b940"
                                border.width: 4
                            }
                            MouseArea {
                                anchors.fill: parent
                                enabled: !!w.category
                                pressAndHoldInterval: 350
                                property bool dragging: false
                                preventStealing: dragging
                                onClicked: w.leave(() => w.editItem(modelData.add ? null : modelData))
                                // Held: dragged to another place in the category.
                                onPressAndHold: m => {
                                    if (modelData.add) return
                                    dragging = true
                                    w.dragId = modelData.id
                                    ghost.text = modelData.name
                                    ghost.color = parent.tint
                                    moved(m)
                                }
                                function moved(m) {
                                    const p = mapToItem(itemGrid, m.x, m.y)
                                    w.dropIndex = itemGrid.indexAt(p.x + itemGrid.contentX, p.y + itemGrid.contentY)
                                    const g = mapToItem(ghost.parent, m.x, m.y)
                                    ghost.x = g.x - ghost.width / 2
                                    ghost.y = g.y - ghost.height / 2
                                }
                                onPositionChanged: m => { if (dragging) moved(m) }
                                onReleased: {
                                    if (!dragging) return
                                    dragging = false
                                    // Cleared first: the move rebuilds the tiles (this one too).
                                    const to = w.dropIndex - 1   // the first tile is + Add Item
                                    const id = w.dragId
                                    w.dragId = ""
                                    w.dropIndex = -1
                                    if (to >= 0 && to < w.items.length && w.items[to].id !== id)
                                        w.pos.moveMenuItemTo(id, to)
                                }
                                onCanceled: { dragging = false; w.dragId = ""; w.dropIndex = -1 }
                            }
                        }
                    }
                }
            }

            // --- the card: an item, or a category ---
            Rectangle {
                visible: !w.narrow || w.stage === "card"
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 10
                color: "#1c2129"
                Label {
                    anchors.centerIn: parent
                    visible: !w.editingItem && !w.editingCategory && !w.editingGroup
                    width: parent.width - 40
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                    text: w.mode === "choices" ? qsTr("Touch a choice group to change it, or + Choice Group.")
                                               : qsTr("Touch an item to change it, or + Add Item.")
                    opacity: 0.6
                    font.pixelSize: 18
                }

                Flickable {
                    id: cardFlick
                    anchors { left: parent.left; right: parent.right; top: parent.top; bottom: cardButtons.top; margins: 14 }
                    visible: w.editingItem || w.editingCategory || w.editingGroup
                    clip: true
                    contentHeight: cardColumn.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: TouchScrollBar { id: cardBar }

                    ColumnLayout {
                        id: cardColumn
                        width: cardFlick.width - cardBar.room
                        spacing: 10

                        TouchButton { visible: w.narrow; text: qsTr("‹ Back"); onClicked: w.stage = w.mode === "choices" ? "categories" : "items" }

                        // --- an item ---
                        ColumnLayout {
                            visible: w.editingItem
                            Layout.fillWidth: true
                            spacing: 10
                            Label { text: w.draft.id ? qsTr("Item") : qsTr("New item"); font.pixelSize: 22; font.bold: true }
                            GridLayout {
                                Layout.fillWidth: true
                                columns: 2
                                columnSpacing: 10
                                rowSpacing: 8
                                Label { text: qsTr("Name") }
                                TextField {
                                    objectName: "builderName"
                                    Layout.fillWidth: true
                                    text: w.draft.name ?? ""
                                    placeholderText: qsTr("e.g. Fish Tacos")
                                    onTextEdited: w.set("name", text)
                                }
                                Label { text: qsTr("Price") }
                                TextField {
                                    objectName: "builderPrice"
                                    Layout.fillWidth: true
                                    text: w.draft.price ?? ""
                                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                                    placeholderText: "0.00"
                                    onTextEdited: w.set("price", text)
                                }
                                Label { text: qsTr("Category") }
                                ComboBox {
                                    Layout.fillWidth: true
                                    model: w.categories.map(c => c.name)
                                    currentIndex: w.categories.findIndex(c => c.id === w.draft.family)
                                    onActivated: i => w.set("family", w.categories[i].id)
                                }
                                Label { text: qsTr("Photo") }
                                FieldEditor {
                                    Layout.fillWidth: true
                                    field: ({ path: "image", label: "", type: "image" })
                                    pos: w.pos
                                    value: w.draft.image || undefined
                                    onCommit: v => w.set("image", v ?? "")
                                    onReset: w.set("image", "")
                                }
                                Label { text: qsTr("Sold out") }
                                Switch {
                                    checked: !(w.draft.available ?? true)
                                    onToggled: w.set("available", !checked)
                                }
                                Label { text: qsTr("★ Favorite") }
                                Switch {
                                    objectName: "builderFavorite"
                                    checked: w.draft.favorite ?? false
                                    onToggled: w.set("favorite", checked)
                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTr("Always in the order screen's ★ Favorites, with today's best sellers.")
                                }
                                Label { text: qsTr("On the kiosk") }
                                Switch {
                                    checked: !(w.draft.kioskHide ?? false)
                                    onToggled: w.set("kioskHide", !checked)
                                }
                            }

                            // What's on it.
                            Label { text: qsTr("What's on it"); font.pixelSize: 18; font.bold: true }
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                opacity: 0.7
                                font.pixelSize: 14
                                text: qsTr("The ingredients it comes with, separated by commas. Guests and staff can have each No, Light, Extra or On the side.")
                            }
                            TextField {
                                objectName: "builderOnIt"
                                Layout.fillWidth: true
                                text: w.draft.onIt ?? ""
                                placeholderText: qsTr("e.g. lettuce, tomato, onion, mayo")
                                onTextEdited: w.set("onIt", text)
                            }

                            // Also on a page of buttons placed by hand (a Happy Hour page).
                            RowLayout {
                                readonly property var pages: w.draft.id && w.zone && w.zone.controller && w.pos
                                                             ? (w.pos.adminRevision >= 0 && w.zone.controller.menuScreensHandBuilt !== undefined   // again when pages change
                                                                ? w.zone.controller.handBuiltPages(w.draft.id) : []) : []
                                visible: pages.length > 0
                                Layout.fillWidth: true
                                spacing: 8
                                Label { text: qsTr("Also a button on") }
                                ComboBox {
                                    id: alsoOn
                                    objectName: "builderAlsoOn"
                                    Layout.fillWidth: true
                                    model: parent.pages.map(p => p.name + (p.has ? "  ✓" : ""))
                                }
                                TouchButton {
                                    objectName: "builderAlsoOnAdd"
                                    enabled: alsoOn.currentIndex >= 0 && !(parent.pages[alsoOn.currentIndex] ?? {}).has
                                    text: qsTr("Add Button")
                                    onClicked: w.zone.controller.addItemButton(parent.pages[alsoOn.currentIndex].id, w.draft.id, w.draft.name)
                                }
                            }

                            // Its choices, from the list.
                            Label { text: qsTr("Choices it asks for"); font.pixelSize: 18; font.bold: true }
                            Flow {
                                Layout.fillWidth: true
                                spacing: 6
                                Repeater {
                                    model: w.groups
                                    delegate: TouchButton {
                                        required property var modelData
                                        objectName: "builderGroup-" + modelData.id
                                        checkable: true
                                        checked: (w.draft.groups ?? []).includes(modelData.id)
                                        highlighted: checked
                                        text: modelData.name + "  ·  " + modelData.rule
                                        onClicked: w.toggleIn("groups", modelData.id)
                                    }
                                }
                                TouchButton {
                                    objectName: "builderNewGroup"
                                    text: qsTr("+ New Choice Group…")
                                    onClicked: {
                                        const d = w.copy(w.draft)
                                        w.editGroup(null)
                                        w.returnToItem = d
                                    }
                                }
                            }


                        }

                        // --- a choice group ---
                        ColumnLayout {
                            visible: w.editingGroup
                            Layout.fillWidth: true
                            spacing: 10
                            Label { text: w.draft.id ? qsTr("Choice group") : qsTr("New choice group"); font.pixelSize: 22; font.bold: true }
                            GridLayout {
                                Layout.fillWidth: true
                                columns: 2
                                columnSpacing: 10
                                rowSpacing: 8
                                Label { text: qsTr("Name") }
                                TextField {
                                    objectName: "builderGroupName"
                                    Layout.fillWidth: true
                                    text: w.draft.name ?? ""
                                    placeholderText: qsTr("e.g. Salsa, Size, Toppings")
                                    onTextEdited: w.set("name", text)
                                }
                                Label { text: qsTr("Guests pick") }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Repeater {
                                        model: [{ kind: "one", text: qsTr("One") }, { kind: "upTo", text: qsTr("Up to…") },
                                                { kind: "any", text: qsTr("Any number") }]
                                        delegate: TouchButton {
                                            required property var modelData
                                            objectName: "builderKind-" + modelData.kind
                                            text: modelData.text
                                            highlighted: w.draft.kind === modelData.kind
                                            onClicked: w.set("kind", modelData.kind)
                                        }
                                    }
                                    SpinBox {
                                        visible: w.draft.kind === "upTo"
                                        from: 2; to: 20
                                        value: Number(w.draft.upTo) || 3
                                        onValueModified: w.set("upTo", value)
                                    }
                                }
                                Label { text: qsTr("Required") }
                                Switch {
                                    objectName: "builderRequired"
                                    checked: w.draft.required ?? false
                                    onToggled: w.set("required", checked)
                                }
                                Label { text: qsTr("Light, Extra, On the side") }
                                Switch {
                                    checked: w.draft.askHow ?? false
                                    onToggled: w.set("askHow", checked)
                                }
                            }
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                opacity: 0.7
                                font.pixelSize: 14
                                text: qsTr("Required: the order can't go without a choice. Light, Extra, On the side: guests can ask for a choice that way (dressing on the side), not for temperatures or sizes.")
                            }
                            Label { text: qsTr("Options"); font.pixelSize: 18; font.bold: true }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                Label { Layout.fillWidth: true; text: qsTr("Name"); opacity: 0.7; font.pixelSize: 13 }
                                Label { Layout.preferredWidth: 90; text: qsTr("Adds"); opacity: 0.7; font.pixelSize: 13 }
                                Label { Layout.preferredWidth: 110; text: qsTr("Comes on it"); opacity: 0.7; font.pixelSize: 13 }
                                Item { Layout.preferredWidth: 3 * 52 + 12 }
                            }
                            // Counted: typing changes the draft, not the rows.
                            Repeater {
                                model: (w.draft.options ?? []).length
                                delegate: RowLayout {
                                    id: optionRow
                                    required property int index
                                    readonly property var option: (w.draft.options ?? [])[index] ?? ({})
                                    Layout.fillWidth: true
                                    spacing: 6
                                    TextField {
                                        objectName: "builderOption-" + optionRow.index
                                        Layout.fillWidth: true
                                        text: optionRow.option.name ?? ""
                                        placeholderText: qsTr("e.g. Pico de gallo")
                                        onTextEdited: w.setOption(optionRow.index, "name", text)
                                    }
                                    TextField {
                                        objectName: "builderOptionPrice-" + optionRow.index
                                        Layout.preferredWidth: 90
                                        text: optionRow.option.price ?? ""
                                        placeholderText: "0.00"
                                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                                        onTextEdited: w.setOption(optionRow.index, "price", text)
                                    }
                                    Switch {
                                        Layout.preferredWidth: 110
                                        checked: optionRow.option.included ?? false
                                        onToggled: w.setOption(optionRow.index, "included", checked)
                                    }
                                    TouchButton { Layout.preferredWidth: 52; text: "▲"; enabled: optionRow.index > 0; onClicked: w.moveOption(optionRow.index, -1) }
                                    TouchButton {
                                        Layout.preferredWidth: 52
                                        text: "▼"
                                        enabled: optionRow.index < (w.draft.options ?? []).length - 1
                                        onClicked: w.moveOption(optionRow.index, 1)
                                    }
                                    TouchButton {
                                        Layout.preferredWidth: 52
                                        text: "✕"
                                        onClicked: {
                                            const d = w.copy(w.draft)
                                            d.options.splice(optionRow.index, 1)
                                            w.draft = d
                                        }
                                    }
                                }
                            }
                            TouchButton {
                                objectName: "builderAddOption"
                                text: qsTr("+ Option")
                                onClicked: {
                                    const d = w.copy(w.draft)
                                    d.options.push({ name: "", price: "", included: false })
                                    w.draft = d
                                }
                            }
                            Label {
                                visible: !!w.editedGroup
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                opacity: 0.7
                                font.pixelSize: 14
                                text: w.editedGroup ? (w.editedGroup.usedBy.length
                                                       ? qsTr("Asked for by: %1").arg(w.editedGroup.usedBy.join(", "))
                                                       : qsTr("No item asks for it yet: choose it on an item's card."))
                                                    : ""
                            }
                        }

                        // --- a category ---
                        ColumnLayout {
                            visible: w.editingCategory
                            Layout.fillWidth: true
                            spacing: 10
                            Label { text: w.draft.id ? qsTr("Category") : qsTr("New category"); font.pixelSize: 22; font.bold: true }
                            GridLayout {
                                Layout.fillWidth: true
                                columns: 2
                                columnSpacing: 10
                                rowSpacing: 8
                                Label { text: qsTr("Name") }
                                TextField {
                                    objectName: "builderCategoryName"
                                    Layout.fillWidth: true
                                    text: w.draft.name ?? ""
                                    placeholderText: qsTr("e.g. Tacos")
                                    onTextEdited: w.set("name", text)
                                }
                                Label { text: qsTr("Color") }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Repeater {
                                        model: w.swatches
                                        delegate: Rectangle {
                                            required property string modelData
                                            width: 40; height: 40; radius: 6
                                            color: modelData
                                            border.color: w.draft.color === modelData ? "white" : "transparent"
                                            border.width: 3
                                            MouseArea { anchors.fill: parent; onClicked: w.set("color", modelData) }
                                        }
                                    }
                                }
                                Label { text: qsTr("On the menu") }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    TouchButton {
                                        checkable: true
                                        checked: (w.draft.periods ?? []).length === 0
                                        highlighted: checked
                                        text: qsTr("All day")
                                        onClicked: w.set("periods", [])
                                    }
                                    Repeater {
                                        model: w.periods
                                        delegate: TouchButton {
                                            required property var modelData
                                            checkable: true
                                            checked: (w.draft.periods ?? []).includes(modelData.id)
                                            highlighted: checked
                                            text: modelData.name
                                            onClicked: w.toggleIn("periods", modelData.id)
                                        }
                                    }
                                }
                            }
                            Label { text: qsTr("New items in it start with"); font.pixelSize: 16; font.bold: true }
                            GridLayout {
                                Layout.fillWidth: true
                                columns: 2
                                columnSpacing: 10
                                rowSpacing: 8
                                Label { text: qsTr("Kitchen ticket") }
                                ComboBox {
                                    Layout.fillWidth: true
                                    model: w.printers.map(p => p.name)
                                    currentIndex: w.printers.findIndex(p => p.id === w.draft.printer)
                                    onActivated: i => w.set("printer", w.printers[i].id)
                                }
                                Label { text: qsTr("Made at") }
                                ComboBox {
                                    Layout.fillWidth: true
                                    readonly property var opts: [{ id: "", name: qsTr("(anywhere)") }].concat(w.stations)
                                    model: opts.map(s => s.name)
                                    currentIndex: Math.max(0, opts.findIndex(s => s.id === (w.draft.station ?? "")))
                                    onActivated: i => w.set("station", opts[i].id)
                                }
                                Label { text: qsTr("Tax") }
                                ComboBox {
                                    Layout.fillWidth: true
                                    model: w.taxes.map(t => t.name)
                                    currentIndex: Math.max(0, w.taxes.findIndex(t => t.id === (w.draft.taxClass || "food")))
                                    onActivated: i => w.set("taxClass", w.taxes[i].id)
                                }
                            }

                        }
                    }
                }
                // Save and the rest: always in view, under the card.
                ColumnLayout {
                    id: cardButtons
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 14 }
                    visible: w.editingItem || w.editingCategory || w.editingGroup
                    height: visible ? implicitHeight : 0
                    RowLayout {
                        Layout.fillWidth: true
                        visible: w.editingGroup
                        spacing: 8
                        TouchButton {
                            objectName: "builderSaveGroup"
                            Layout.fillWidth: true
                            highlighted: true
                            text: w.draft.id ? qsTr("Save") : qsTr("Add Choice Group")
                            enabled: (w.draft.name ?? "").trim() !== ""
                            onClicked: w.saveGroupCard()
                        }
                        TouchButton {
                            objectName: "builderBackToItem"
                            visible: !!w.returnToItem
                            text: qsTr("‹ %1").arg(w.returnToItem ? (w.returnToItem.name || qsTr("New item")) : "")
                            onClicked: w.backToItem("")
                        }
                        TouchButton {
                            visible: !!w.draft.id
                            text: qsTr("Remove…")
                            onClicked: removeGroupDialog.open()
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: w.editingItem
                        spacing: 8
                        TouchButton {
                            objectName: "builderSave"
                            Layout.fillWidth: true
                            highlighted: true
                            text: w.draft.id ? qsTr("Save") : qsTr("Add to the Menu")
                            enabled: (w.draft.name ?? "").trim() !== ""
                            onClicked: w.saveItem()
                        }
                        TouchButton {
                            objectName: "builderDuplicate"
                            visible: !!w.draft.id
                            text: qsTr("Duplicate")
                            onClicked: {
                                w.idsBefore = w.allItems.map(i => i.id)
                                w.pos.duplicateMenuItem(w.draft.id)
                            }
                        }
                        TouchButton {
                            objectName: "builderRemove"
                            visible: !!w.draft.id
                            text: qsTr("Remove…")
                            onClicked: removeDialog.open()
                        }
                        TouchButton {
                            visible: !!w.draft.id
                            text: qsTr("More…")
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Every setting: kitchen name, prices by meal, takeout price, recipe…")
                            onClicked: w.zone.controller.jumpTo("admin-menu")
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: w.editingCategory
                        spacing: 8
                        TouchButton {
                            objectName: "builderSaveCategory"
                            Layout.fillWidth: true
                            highlighted: true
                            text: w.draft.id ? qsTr("Save") : qsTr("Add Category")
                            enabled: (w.draft.name ?? "").trim() !== ""
                            onClicked: w.saveCategoryCard()
                        }
                        TouchButton {
                            visible: !!w.draft.id
                            text: qsTr("Remove")
                            enabled: !!w.category && w.category.count === 0
                            ToolTip.visible: hovered && !enabled
                            ToolTip.text: qsTr("Move or remove its items first.")
                            onClicked: {
                                w.pos.deleteCategory(w.draft.id)
                                w.editingCategory = false
                            }
                        }
                    }
                }
            }
        }
    }

    // Many at once: "Carne Asada 3.50" a line, or "Tacos: Carne Asada 3.50, Al Pastor 3.25".
    // A spreadsheet saved as CSV (a USB stick, this computer): read here,
    // shown, then added.
    FileDialog {
        id: importFile
        title: qsTr("A menu saved as CSV")
        nameFilters: [qsTr("Spreadsheets saved as CSV (*.csv *.tsv *.txt)"), qsTr("All files (*)")]
        onAccepted: {
            importDialog.read = w.zone.controller.readMenuFile(selectedFile)
            importDialog.open()
        }
    }
    Dialog {
        id: importDialog
        objectName: "builderImportDialog"
        property var read: ({})
        readonly property var items: read.items ?? []
        readonly property var categoriesIn: [...new Set(items.map(i => i.category || (w.category ? w.category.name : "")))]
        title: read.error ? qsTr("Can't import that") : qsTr("Import %n item(s)", "", items.length)
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width - 40, 820)
        height: Math.min(parent.height - 40, 640)
        modal: true
        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: importDialog.read.error
                      ? importDialog.read.error
                      : qsTr("Into: %1. Rows without a category go in %2.").arg(importDialog.categoriesIn.join(", "))
                                                                          .arg(w.category ? w.category.name : "?")
            }
            Label {
                visible: (importDialog.read.problems ?? []).length > 0
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: "#f5b940"
                text: qsTr("Left out: %1").arg((importDialog.read.problems ?? []).join("; "))
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: importDialog.items
                delegate: RowLayout {
                    required property var modelData
                    width: ListView.view.width
                    Label { Layout.fillWidth: true; text: modelData.name; elide: Text.ElideRight }
                    Label { text: w.pos.currencySymbol + Number(modelData.price).toFixed(2) }
                    Label { Layout.preferredWidth: 180; text: modelData.category; opacity: 0.7; elide: Text.ElideRight }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Switch { id: updatePrices; objectName: "builderImportPrices" }
                Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: qsTr("Items already on the menu get the file's price (otherwise they're left as they are)") }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                TouchButton {
                    objectName: "builderImportGo"
                    Layout.fillWidth: true
                    highlighted: true
                    enabled: importDialog.items.length > 0
                    text: qsTr("Import")
                    onClicked: {
                        w.pos.importMenuRows(importDialog.items, w.categoryId, updatePrices.checked)
                        importDialog.close()
                    }
                }
                TouchButton { Layout.preferredWidth: 160; text: qsTr("Cancel"); onClicked: importDialog.close() }
            }
        }
    }

    // The menu checked: touch a problem to fix it.
    Dialog {
        id: checkDialog
        objectName: "builderCheckDialog"
        readonly property var problems: w.pos ? w.pos.menuProblems : []
        title: problems.length ? qsTr("Before service") : qsTr("Ready to go")
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width - 40, 760)
        height: Math.min(parent.height - 40, 600)
        modal: true
        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            Label {
                visible: checkDialog.problems.length === 0
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Every item has a price and a category, every choice it asks for is there, and its kitchen ticket goes to a printer that's set up.")
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 6
                model: checkDialog.problems
                delegate: TouchButton {
                    required property var modelData
                    width: ListView.view.width
                    text: (modelData.serious ? "⚠ " : "• ") + modelData.text
                    onClicked: {
                        checkDialog.close()
                        w.leave(() => open(modelData))
                    }
                    function open(modelData) {
                        if (modelData.item) {
                            const i = w.allItems.find(x => x.id === modelData.item)
                            if (i) { w.mode = "menu"; w.categoryId = i.family; w.editItem(i) }
                        } else if (modelData.group) {
                            const g = w.allGroups.find(x => x.id === modelData.group)
                            if (g) w.editGroup(g)
                        } else if (modelData.category) {
                            const c = w.categories.find(x => x.id === modelData.category)
                            if (c) { w.mode = "menu"; w.categoryId = c.id; w.editCategory(c) }
                        }
                    }
                }
            }
            TouchButton { Layout.alignment: Qt.AlignRight; text: qsTr("Close"); onClicked: checkDialog.close() }
        }
    }

    // Starter menus: a kind of place's categories, choices and items.
    Dialog {
        id: templateDialog
        objectName: "builderTemplateDialog"
        title: qsTr("Start from a starter menu")
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width - 40, 760)
        modal: true
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                opacity: 0.8
                text: qsTr("Its categories, choices and items, with typical prices, are added to your menu; nothing already on it changes. Then change anything you like.")
            }
            Repeater {
                model: w.pos ? w.pos.menuTemplates : []
                delegate: TouchButton {
                    required property var modelData
                    objectName: "builderTemplate-" + modelData.id
                    Layout.fillWidth: true
                    implicitHeight: 72
                    text: modelData.name + "  ·  " + qsTr("%1 categories, %2 items").arg(modelData.categories).arg(modelData.items)
                          + "\n" + modelData.description
                    onClicked: {
                        w.pos.applyMenuTemplate(modelData.id)
                        templateDialog.close()
                    }
                }
            }
            TouchButton { Layout.alignment: Qt.AlignRight; text: qsTr("Cancel"); onClicked: templateDialog.close() }
        }
    }

    // What's being dragged, under the finger.
    Rectangle {
        id: ghost
        objectName: "builderGhost"
        property string text
        visible: w.dragId !== ""
        z: 100
        width: 200 * w.zoom
        height: 72 * w.zoom
        radius: 10
        opacity: 0.9
        border.color: "#f5b940"
        border.width: 3
        Label {
            anchors.centerIn: parent
            width: parent.width - 12
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            text: ghost.text
            color: w.inkOn(ghost.color)
            font.pixelSize: 17 * w.zoom
            font.bold: true
        }
    }

    Dialog {
        id: severalDialog
        objectName: "builderSeveral"
        title: qsTr("Add several to %1").arg(w.category ? w.category.name : "")
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width - 40, 760)
        modal: true
        onOpened: severalText.text = ""
        // What will be added (the store reads it the same way).
        readonly property var preview: {
            const out = []
            let category = w.category ? w.category.name : ""
            for (let line of severalText.text.split("\n")) {
                line = line.trim()
                if (!line) continue
                const colon = line.indexOf(":")
                if (colon > 0 && !/\d/.test(line.slice(0, colon))) {
                    category = line.slice(0, colon).trim()
                    line = line.slice(colon + 1).trim()
                }
                for (let part of line.split(/,(?!\d{1,2}\s*(?:,|$))/)) {
                    part = part.trim()
                    if (!part) continue
                    const m = part.match(/^(.*?)[\s\-–:]*\$?\s*(\d+(?:[.,]\d{1,2})?)\s*$/)
                    out.push(m && m[1].trim() ? { name: m[1].trim(), price: Number(m[2].replace(",", ".")).toFixed(2), category: category }
                                              : { name: part, price: "", category: category })
                }
            }
            return out
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                opacity: 0.8
                text: qsTr("One per line, or separated by commas, each with its price: \"Carne Asada 3.50\". Start a line with another category's name and a colon to put what follows there (made if it's new): \"Drinks: Horchata 2.75\".")
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: 200
                TextArea {
                    id: severalText
                    objectName: "builderSeveralText"
                    placeholderText: qsTr("Carne Asada 3.50\nAl Pastor 3.25\nDrinks: Horchata 2.75")
                    wrapMode: TextEdit.Wrap
                }
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                visible: severalDialog.preview.length > 0
                text: severalDialog.preview.map(e => e.price ? e.name + " " + w.pos.currencySymbol + e.price + (e.category !== (w.category ? w.category.name : "") ? " (" + e.category + ")" : "")
                                                             : "⚠ " + e.name + " " + qsTr("(no price)")).join("  ·  ")
                font.pixelSize: 14
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                TouchButton {
                    objectName: "builderSeveralAdd"
                    Layout.fillWidth: true
                    highlighted: true
                    enabled: severalDialog.preview.length > 0 && severalDialog.preview.every(e => e.price !== "")
                    text: qsTr("Add %n item(s)", "", severalDialog.preview.length)
                    onClicked: {
                        w.pos.addMenuItemsFromText(w.categoryId, severalText.text)
                        severalDialog.close()
                    }
                }
                TouchButton {
                    Layout.preferredWidth: 160
                    text: qsTr("Cancel")
                    onClicked: severalDialog.close()
                }
            }
        }
    }

    // Leaving a card with changes: save them, drop them, or stay.
    Dialog {
        id: leaveDialog
        objectName: "builderLeaveDialog"
        property var then: null
        padding: 20
        font.pixelSize: 18
        title: qsTr("Save the changes to %1?").arg((w.draft.name ?? "").trim() || qsTr("this card"))
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        RowLayout {
            spacing: 8
            TouchButton {
                objectName: "builderLeaveSave"
                highlighted: true
                text: qsTr("Save")
                onClicked: {
                    const then = leaveDialog.then
                    leaveDialog.close()
                    w.saveCard()
                    w.waitingFor = ""; w.waitingForCategory = ""; w.waitingForGroup = ""
                    if (!w.unsaved() && then) then()   // not if the save was refused
                }
            }
            TouchButton {
                objectName: "builderLeaveDiscard"
                text: qsTr("Don't Save")
                onClicked: {
                    const then = leaveDialog.then
                    leaveDialog.close()
                    if (then) then()
                }
            }
            TouchButton { text: qsTr("Keep Editing"); onClicked: leaveDialog.close() }
        }
    }

    Dialog {
        id: removeGroupDialog
        title: qsTr("Remove %1?").arg(w.draft.name ?? "")
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Yes | Dialog.Cancel
        Label {
            text: w.editedGroup && w.editedGroup.usedBy.length
                  ? qsTr("%1 stop asking for it.").arg(w.editedGroup.usedBy.join(", ")) : qsTr("No item asks for it.")
            wrapMode: Text.WordWrap
            width: 420
        }
        onAccepted: {
            w.pos.deleteChoiceGroup(w.draft.id)
            w.editingGroup = false
        }
    }

    Dialog {
        id: removeDialog
        title: qsTr("Remove %1 from the menu?").arg(w.draft.name ?? "")
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Yes | Dialog.Cancel
        Label { text: qsTr("It comes off every menu screen. Checks it's already on keep it.") }
        onAccepted: {
            w.pos.deleteMenuItemCard(w.draft.id)
            w.editingItem = false
            w.stage = "items"
        }
    }
}
