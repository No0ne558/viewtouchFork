import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// An item changed (or a new one added) right from the order screen, by a
// manager: its name, price, sold out and color. All Settings… opens it in
// the Menu Builder.
Popup {
    id: card
    objectName: "quickItemCard"
    property PosService pos
    property var controller
    // The item (from pos.menuItems), or null: a new one in `family`.
    property var item: null
    property string family: ""
    property string name: ""
    property string price: ""
    property bool soldOut: false
    property string color: ""
    // A new one, once saved: the item it became (All Settings… opens it).
    property string waitingFor: ""

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 32 : 640, 640)
    modal: true
    padding: 18

    function edit(i) {
        item = i
        family = i.family
        name = i.name
        price = i.priceValue.toFixed(2)
        soldOut = !i.availableSet
        color = i.buttonColor ?? ""
        open()
    }
    function add(f) {
        item = null
        family = f
        name = ""
        price = ""
        soldOut = false
        color = ""
        open()
        nameField.forceActiveFocus()
    }
    readonly property bool ready: name.trim() !== "" && /^\s*\$?\s*\d+([.,]\d{1,2})?\s*$/.test(price)
    function save(thenOpenAll) {
        const c = { name: name.trim(), price: price.trim(), family: family, available: !soldOut, buttonColor: color }
        if (item) c.id = item.id
        if (thenOpenAll && controller)
            controller.keep("menuBuilderOpen", item ? item.id : name.trim())
        pos.saveMenuItemCard(c)
        close()
        if (thenOpenAll && controller)
            controller.jumpTo("menu-builder")
    }

    component Big: Button {
        implicitHeight: 56
        font.pixelSize: 17
    }

    contentItem: ColumnLayout {
        spacing: 10
        Label {
            text: card.item ? qsTr("Change %1").arg(card.item.name) : qsTr("New item")
            font.pixelSize: 22
            font.bold: true
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 10
            rowSpacing: 8
            Label { text: qsTr("Name"); font.pixelSize: 16 }
            TextField {
                id: nameField
                objectName: "quickName"
                Layout.fillWidth: true
                implicitHeight: 52
                font.pixelSize: 18
                text: card.name
                placeholderText: qsTr("e.g. Fish Tacos")
                onTextEdited: card.name = text
            }
            Label { text: qsTr("Price"); font.pixelSize: 16 }
            TextField {
                objectName: "quickPrice"
                Layout.fillWidth: true
                implicitHeight: 52
                font.pixelSize: 18
                text: card.price
                placeholderText: "0.00"
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                onTextEdited: card.price = text
            }
            Item { width: 1; height: 1 }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Big {
                    objectName: "quickForSale"
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    checkable: true
                    checked: !card.soldOut
                    highlighted: checked
                    text: qsTr("For sale")
                    onClicked: card.soldOut = false
                }
                Big {
                    objectName: "quickSoldOut"
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    checkable: true
                    checked: card.soldOut
                    highlighted: checked
                    palette.button: checked ? "#7a2e2e" : undefined
                    text: qsTr("Sold out")
                    onClicked: card.soldOut = true
                }
            }
        }
        Label { text: qsTr("Color"); font.pixelSize: 16 }
        ColorPicker {
            objectName: "quickColor"
            Layout.fillWidth: true
            pos: card.pos
            noneText: qsTr("Category's")
            color: card.color
            onPicked: c => card.color = c
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 6
            spacing: 8
            Big {
                objectName: "quickSave"
                Layout.fillWidth: true
                highlighted: true
                enabled: card.ready
                text: card.item ? qsTr("Save") : qsTr("Add to the Menu")
                onClicked: card.save(false)
            }
            Big {
                objectName: "quickAll"
                enabled: card.ready
                text: qsTr("All Settings…")
                onClicked: card.save(true)
            }
            Big {
                text: qsTr("Cancel")
                onClicked: card.close()
            }
        }
    }
}
