import QtQuick
import QtQuick.Controls.Fusion
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
    property var draft: ({})
    property string stage: "categories"   // a phone: categories | items | card

    Component.onCompleted: if (categories.length) categoryId = categories[0].id
    // A new category, once saved, is the one shown.
    property string waitingForCategory: ""
    onCategoriesChanged: {
        const added = waitingForCategory === "" ? null
                    : categories.find(c => c.name.toLowerCase() === waitingForCategory.toLowerCase())
        if (added) {
            waitingForCategory = ""
            pickCategory(added.id)
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
        draft = c ? { id: c.id, name: c.name, color: c.color, periods: c.periods.slice(), printer: c.printer,
                      station: c.station, taxClass: c.taxClass }
                  : { id: "", name: "", color: swatches[categories.length % swatches.length], periods: [],
                      printer: "kitchen", station: "", taxClass: "food" }
        editingCategory = true
        editingItem = false
        stage = "card"
    }
    function editItem(i) {
        draft = i ? { id: i.id, name: i.name, price: i.priceValue.toFixed(2), family: i.family, image: i.image,
                      groups: i.groups.slice(), onIt: i.onIt.join(", "), available: i.availableSet,
                      kioskHide: i.kioskHide, description: i.description }
                  : { id: "", name: "", price: "", family: categoryId, image: "", groups: [], onIt: "",
                      available: true, kioskHide: false, description: "" }
        itemId = i ? i.id : ""
        editingItem = true
        editingCategory = false
        stage = "card"
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
    onAllItemsChanged: {
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

        RowLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 12

            // --- categories ---
            ColumnLayout {
                visible: !w.narrow || w.stage === "categories"
                Layout.preferredWidth: w.narrow ? -1 : 270
                Layout.fillWidth: w.narrow
                Layout.fillHeight: true
                spacing: 8
                Label { text: qsTr("Categories"); font.pixelSize: 22; font.bold: true }
                ListView {
                    id: categoryList
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
                        MouseArea { anchors.fill: parent; onClicked: w.pickCategory(modelData.id) }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    TouchButton {
                        objectName: "builderAddCategory"
                        Layout.fillWidth: true
                        text: qsTr("+ Category")
                        onClicked: w.editCategory(null)
                    }
                    TouchButton { text: "▲"; enabled: !!w.category; onClicked: w.pos.moveCategory(w.categoryId, -1) }
                    TouchButton { text: "▼"; enabled: !!w.category; onClicked: w.pos.moveCategory(w.categoryId, 1) }
                }
            }

            // --- the category's items ---
            ColumnLayout {
                visible: !w.narrow || w.stage === "items"
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
                        objectName: "builderEditCategory"
                        visible: !!w.category
                        text: qsTr("Edit Category")
                        onClicked: w.editCategory(w.category)
                    }
                }
                GridView {
                    id: itemGrid
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    cellWidth: width / 2
                    cellHeight: 96
                    model: [{ add: true }].concat(w.items)
                    delegate: Item {
                        required property var modelData
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
                            MouseArea {
                                anchors.fill: parent
                                enabled: !!w.category
                                onClicked: w.editItem(modelData.add ? null : modelData)
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
                    visible: !w.editingItem && !w.editingCategory
                    width: parent.width - 40
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Touch an item to change it, or + Add Item.")
                    opacity: 0.6
                    font.pixelSize: 18
                }

                Flickable {
                    id: cardFlick
                    anchors { left: parent.left; right: parent.right; top: parent.top; bottom: cardButtons.top; margins: 14 }
                    visible: w.editingItem || w.editingCategory
                    clip: true
                    contentHeight: cardColumn.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: TouchScrollBar { id: cardBar }

                    ColumnLayout {
                        id: cardColumn
                        width: cardFlick.width - cardBar.room
                        spacing: 10

                        TouchButton { visible: w.narrow; text: qsTr("‹ Back"); onClicked: w.stage = "items" }

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
                                    onClicked: w.zone.controller.jumpTo("admin-modifier-groups")
                                }
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
                    visible: w.editingItem || w.editingCategory
                    height: visible ? implicitHeight : 0
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
                            onClicked: {
                                const c = w.copy(w.draft)
                                if (!c.id)
                                    w.waitingForCategory = c.name.trim()
                                w.pos.saveCategory(c)
                                if (!c.id)
                                    w.editingCategory = false
                            }
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
