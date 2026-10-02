import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// The self-order kiosk: guests order on their own, then pay at the counter.
//   Welcome (for here / to go) -> menu and their order -> name -> number.
// No way to the staff pages from here: a manager holds the top-left corner
// for three seconds and types their PIN (`managerExit`).
Rectangle {
    id: k
    property PosService pos
    signal managerExit()

    readonly property var info: pos ? pos.selfOrder : ({})
    readonly property var menu: pos ? pos.kioskMenu : ({})
    readonly property var brand: pos ? pos.customerPrompt : ({})
    readonly property color accent: brand.accent || "#2f6fd6"
    readonly property string logo: !brand.logo ? "" : brand.logo.startsWith("/") ? "file://" + brand.logo : brand.logo
    readonly property bool portrait: height > width
    // One unit: about 20 px on a 1280x800 screen.
    readonly property real u: Math.max(12, Math.min(width / 64, height / 40))

    readonly property var lastOrder: info.lastOrder ?? ({})
    readonly property bool done: (lastOrder.number ?? 0) > 0
    readonly property bool ordering: info.ordering ?? false
    property bool reviewing: false
    property string family: ""
    readonly property var families: menu.families ?? []
    readonly property string shownFamily: families.includes(family) ? family : (families[0] ?? "")
    readonly property var items: (menu.items ?? []).filter(i => i.family === shownFamily)
    readonly property var lines: pos ? pos.lines.filter(l => !l.voided && !l.comment) : []
    readonly property var choosing: pos ? pos.choosing : ({})

    color: "#14181f"
    onOrderingChanged: if (!ordering) { reviewing = false; family = "" }

    // --- idle: an order nobody is finishing is cleared ---------------------------------
    property int idleLeft: 0
    function touched() { idle.restart(); stillThere.visible = false }
    PointHandler { onActiveChanged: if (active) k.touched() }   // any touch, without taking it
    Timer {
        id: idle
        interval: (k.info.idleSeconds ?? 90) * 1000
        running: k.ordering
        onTriggered: { k.idleLeft = 15; stillThere.visible = true; countdown.start() }
    }
    Timer {
        id: countdown
        interval: 1000
        repeat: true
        running: stillThere.visible
        onTriggered: if (--k.idleLeft <= 0) { stillThere.visible = false; k.pos.kioskCancel() }
    }
    // The confirmation goes back to the welcome screen by itself.
    Timer {
        interval: 20000
        running: k.done && !k.ordering
        onTriggered: k.pos.kioskCancel()
    }

    component Big: Rectangle {
        id: big
        property string text
        property color base: "#2a313d"
        property real size: 1.4
        signal clicked()
        radius: k.u * 0.6
        color: !enabled ? "#20252e" : tap.pressed ? Qt.lighter(base, 1.3) : base
        implicitHeight: k.u * 3.4
        Text {
            anchors.fill: parent
            anchors.margins: k.u * 0.4
            text: big.text
            color: big.enabled ? "white" : "#6b7383"
            font.pixelSize: k.u * big.size
            font.bold: true
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            fontSizeMode: Text.Fit
            minimumPixelSize: k.u * 0.7
        }
        // A MouseArea: these sit in scrolling lists too (a TapHandler there
        // loses the press to the list).
        MouseArea { id: tap; anchors.fill: parent; onClicked: big.clicked() }
    }

    // --- welcome ---------------------------------------------------------------------------
    ColumnLayout {
        visible: !k.ordering && !k.done
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.8, k.u * 50)
        spacing: k.u * 2
        Image {
            visible: k.logo !== ""
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredHeight: k.u * 10
            Layout.preferredWidth: k.u * 30
            source: k.logo
            fillMode: Image.PreserveAspectFit
        }
        Text {
            visible: k.logo === ""
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: k.pos ? k.pos.storeName : ""
            color: "white"
            font.pixelSize: k.u * 3.2
            font.bold: true
            wrapMode: Text.WordWrap
        }
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Order here")
            color: "#c9d1de"
            font.pixelSize: k.u * 2
        }
        GridLayout {
            Layout.fillWidth: true
            columns: k.portrait ? 1 : 2
            columnSpacing: k.u * 1.5
            rowSpacing: k.u * 1.5
            Big {
                objectName: "kioskForHere"
                Layout.fillWidth: true
                Layout.preferredHeight: k.u * 9
                text: qsTr("For Here")
                size: 2.4
                base: k.accent
                onClicked: k.pos.kioskStart(false)
            }
            Big {
                objectName: "kioskToGo"
                Layout.fillWidth: true
                Layout.preferredHeight: k.u * 9
                text: qsTr("To Go")
                size: 2.4
                base: k.accent
                onClicked: k.pos.kioskStart(true)
            }
        }
    }

    // --- the menu and the order --------------------------------------------------------------
    GridLayout {
        visible: k.ordering && !k.reviewing
        anchors.fill: parent
        anchors.margins: k.u * 0.8
        columns: k.portrait ? 1 : 3
        rowSpacing: k.u * 0.8
        columnSpacing: k.u * 0.8

        // Categories: a column, or a row on a portrait screen.
        ListView {
            id: familyList
            Layout.preferredWidth: k.portrait ? -1 : k.u * 12
            Layout.fillWidth: k.portrait
            Layout.fillHeight: !k.portrait
            Layout.preferredHeight: k.portrait ? k.u * 3.6 : -1
            orientation: k.portrait ? ListView.Horizontal : ListView.Vertical
            spacing: k.u * 0.5
            clip: true
            model: k.families
            delegate: Big {
                required property string modelData
                width: k.portrait ? k.u * 11 : ListView.view.width
                height: k.u * 3.6
                // "burgers" -> "Burgers" (and in the guest's language when it has the phrase)
                text: qsTranslate("Page", modelData.charAt(0).toUpperCase() + modelData.slice(1))
                size: 1.2
                base: modelData === k.shownFamily ? k.accent : "#2a313d"
                onClicked: k.family = modelData
            }
        }

        // What there is.
        GridView {
            id: grid
            objectName: "kioskItems"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            readonly property int across: Math.max(1, Math.floor(width / (k.u * 13)))
            cellWidth: width / across
            cellHeight: k.u * 15
            model: k.items
            ScrollBar.vertical: TouchScrollBar { needed: grid.contentHeight > grid.height + 1 }
            delegate: Item {
                id: card
                required property var modelData
                width: grid.cellWidth
                height: grid.cellHeight
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: k.u * 0.35
                    radius: k.u * 0.6
                    color: cardTap.pressed ? "#323b49" : "#232a35"
                    clip: true
                    Rectangle {
                        id: photo
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        height: parent.height * 0.5
                        color: Qt.darker(k.accent, 2.2)
                        Text {
                            anchors.centerIn: parent
                            visible: !card.modelData.image
                            text: card.modelData.name.charAt(0)
                            color: Qt.lighter(k.accent, 1.6)
                            font.pixelSize: k.u * 3
                            font.bold: true
                        }
                        Image {
                            anchors.fill: parent
                            visible: !!card.modelData.image
                            source: card.modelData.image
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                        }
                    }
                    ColumnLayout {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: photo.bottom
                        anchors.bottom: parent.bottom
                        anchors.margins: k.u * 0.5
                        spacing: k.u * 0.15
                        Text {
                            Layout.fillWidth: true
                            text: card.modelData.name
                            color: "white"
                            font.pixelSize: k.u * 1.05
                            font.bold: true
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            text: card.modelData.description
                            color: "#9aa4b5"
                            font.pixelSize: k.u * 0.75
                            wrapMode: Text.WordWrap
                            elide: Text.ElideRight
                            maximumLineCount: 2
                        }
                        Text {
                            text: card.modelData.price
                            color: Qt.lighter(k.accent, 1.5)
                            font.pixelSize: k.u * 1.05
                            font.bold: true
                        }
                    }
                    // Sold out: shown, but can't be ordered.
                    Rectangle {
                        anchors.fill: parent
                        visible: !card.modelData.available
                        color: "#c0141820"
                        Text {
                            anchors.centerIn: parent
                            text: qsTr("SOLD OUT")
                            color: "#ff9a9e"
                            font.pixelSize: k.u * 1.4
                            font.bold: true
                            rotation: -12
                        }
                    }
                }
                // A MouseArea: it works inside the scrolling grid (a drag still scrolls).
                MouseArea {
                    id: cardTap
                    anchors.fill: parent
                    enabled: card.modelData.available
                    onClicked: k.pos.kioskAdd(card.modelData.id)
                }
            }
        }

        // Their order.
        Rectangle {
            Layout.preferredWidth: k.portrait ? -1 : k.u * 19
            Layout.fillWidth: k.portrait
            Layout.fillHeight: !k.portrait
            Layout.preferredHeight: k.portrait ? parent.height * 0.32 : -1
            radius: k.u * 0.6
            color: "#1d232c"
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: k.u * 0.7
                spacing: k.u * 0.4
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: k.info.toGo ? qsTr("Your order · to go") : qsTr("Your order · for here")
                        color: "white"
                        font.pixelSize: k.u * 1.2
                        font.bold: true
                        elide: Text.ElideRight
                    }
                }
                ListView {
                    id: cart
                    objectName: "kioskCart"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: k.u * 0.3
                    model: k.lines
                    ScrollBar.vertical: TouchScrollBar { id: cartBar; needed: cart.contentHeight > cart.height + 1 }
                    delegate: RowLayout {
                        id: row
                        required property var modelData
                        width: ListView.view.width - cartBar.room
                        spacing: k.u * 0.4
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Text {
                                Layout.fillWidth: true
                                text: row.modelData.name
                                color: "white"
                                font.pixelSize: k.u * 0.95
                                wrapMode: Text.WordWrap
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: text !== ""
                                text: (row.modelData.modifiers ?? []).map(m => m.name).join(", ")
                                color: "#9aa4b5"
                                font.pixelSize: k.u * 0.75
                                wrapMode: Text.WordWrap
                            }
                        }
                        Text {
                            text: row.modelData.price
                            color: "white"
                            font.pixelSize: k.u * 0.95
                        }
                        Big {
                            objectName: "kioskRemove"
                            Layout.preferredWidth: k.u * 2.6
                            Layout.preferredHeight: k.u * 2.6
                            text: "×"
                            size: 1.4
                            base: "#3a2a2e"
                            onClicked: k.pos.kioskRemove(row.modelData.id)
                        }
                    }
                }
                Text {
                    visible: k.lines.length === 0
                    Layout.fillWidth: true
                    text: qsTr("Touch something on the menu to add it.")
                    color: "#9aa4b5"
                    font.pixelSize: k.u * 0.9
                    wrapMode: Text.WordWrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: qsTr("Total"); color: "white"; font.pixelSize: k.u * 1.2; font.bold: true; Layout.fillWidth: true }
                    Text {
                        text: k.pos ? (k.pos.totals.total ?? "") : ""
                        color: "white"
                        font.pixelSize: k.u * 1.4
                        font.bold: true
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: k.u * 0.5
                    Big {
                        objectName: "kioskStartOver"
                        Layout.preferredWidth: k.u * 6
                        Layout.preferredHeight: k.u * 3.4
                        text: qsTr("Start Over")
                        size: 0.9
                        onClicked: k.pos.kioskCancel()
                    }
                    Big {
                        objectName: "kioskReview"
                        Layout.fillWidth: true
                        Layout.preferredHeight: k.u * 3.4
                        enabled: k.lines.length > 0
                        text: qsTr("Done ›")
                        base: "#1f8a4c"
                        onClicked: k.reviewing = true
                    }
                }
            }
        }
    }

    // --- review: a name to call the order by ------------------------------------------------
    ColumnLayout {
        visible: k.ordering && k.reviewing
        anchors.fill: parent
        anchors.margins: k.u
        spacing: k.u * 0.8
        Text {
            text: qsTr("Almost done: %1").arg(k.pos ? (k.pos.totals.total ?? "") : "")
            color: "white"
            font.pixelSize: k.u * 2
            font.bold: true
        }
        Text {
            Layout.fillWidth: true
            text: k.lines.map(l => l.name).join(" · ")
            color: "#9aa4b5"
            font.pixelSize: k.u
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        Text { text: qsTr("Your name, so we can call your order"); color: "white"; font.pixelSize: k.u * 1.2 }
        TextField {
            id: nameField
            objectName: "kioskName"
            Layout.fillWidth: true
            implicitHeight: k.u * 3.4
            font.pixelSize: k.u * 1.6
            maximumLength: 40
            placeholderText: qsTr("Name")
            onVisibleChanged: if (visible) { text = ""; forceActiveFocus() }
            onAccepted: place.clicked()
        }
        TouchKeyboard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            target: nameField
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: k.u * 0.6
            Big {
                Layout.preferredWidth: k.u * 12
                Layout.preferredHeight: k.u * 3.6
                text: qsTr("‹ Back to the Menu")
                size: 1
                onClicked: k.reviewing = false
            }
            Big {
                id: place
                objectName: "kioskPlace"
                Layout.fillWidth: true
                Layout.preferredHeight: k.u * 3.6
                enabled: nameField.text.trim().length > 0
                text: qsTr("Place My Order")
                base: "#1f8a4c"
                onClicked: k.pos.kioskFinish({ name: nameField.text })
            }
        }
    }

    // --- the order number ---------------------------------------------------------------------
    ColumnLayout {
        visible: k.done && !k.ordering
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.85, k.u * 50)
        spacing: k.u
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Thank you, %1!").arg(k.lastOrder.name ?? "")
            color: "white"
            font.pixelSize: k.u * 2.4
            font.bold: true
            wrapMode: Text.WordWrap
        }
        Text { Layout.alignment: Qt.AlignHCenter; text: qsTr("Your order number"); color: "#c9d1de"; font.pixelSize: k.u * 1.4 }
        Text {
            objectName: "kioskNumber"
            Layout.alignment: Qt.AlignHCenter
            text: String(k.lastOrder.number ?? "")
            color: Qt.lighter(k.accent, 1.5)
            font.pixelSize: k.u * 8
            font.bold: true
        }
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: k.lastOrder.sent ? qsTr("We're making it now. Please pay at the counter.")
                                   : qsTr("Please pay at the counter (%1), and we'll start making it.").arg(k.lastOrder.total ?? "")
            color: "white"
            font.pixelSize: k.u * 1.4
            wrapMode: Text.WordWrap
        }
        Big {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: k.u * 16
            Layout.topMargin: k.u
            text: qsTr("New Order")
            base: k.accent
            onClicked: k.pos.kioskCancel()
        }
    }

    // --- choices (temperature, sides...) for what was just added ------------------------------
    Rectangle {
        visible: k.ordering && (k.choosing.active ?? false)
        anchors.fill: parent
        color: "#d0101318"
        TapHandler {}   // touches stop here
        Rectangle {
            anchors.centerIn: parent
            width: Math.min(parent.width * 0.92, k.u * 46)
            height: Math.min(parent.height * 0.9, choicesColumn.implicitHeight + k.u * 2)
            radius: k.u * 0.8
            color: "#1d232c"
            ColumnLayout {
                id: choicesColumn
                anchors.fill: parent
                anchors.margins: k.u
                spacing: k.u * 0.6
                Text {
                    text: k.choosing.item ?? ""
                    color: "white"
                    font.pixelSize: k.u * 1.6
                    font.bold: true
                }
                TouchScrollColumn {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredHeight: implicitHeight
                    spacing: k.u * 0.5
                    Repeater {
                        model: k.choosing.groups ?? []
                        delegate: ColumnLayout {
                            id: group
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: k.u * 0.3
                            Text {
                                text: group.modelData.name + "  ·  " + group.modelData.rule
                                color: group.modelData.done ? "#c9d1de" : "#f5b940"
                                font.pixelSize: k.u * 1.1
                                font.bold: true
                            }
                            Flow {
                                Layout.fillWidth: true
                                spacing: k.u * 0.4
                                Repeater {
                                    model: group.modelData.options
                                    delegate: Big {
                                        required property var modelData
                                        width: k.u * 10
                                        height: k.u * 3.2
                                        text: modelData.name + (modelData.price ? "\n+" + modelData.price : "")
                                        size: 0.95
                                        base: modelData.chosen ? k.accent : "#2a313d"
                                        onClicked: k.pos.chooseOption(group.modelData.id, modelData.index)
                                    }
                                }
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: k.u * 0.6
                    Big {
                        Layout.preferredWidth: k.u * 10
                        Layout.preferredHeight: k.u * 3.4
                        text: qsTr("Remove It")
                        size: 1
                        base: "#3a2a2e"
                        onClicked: k.pos.kioskRemove(k.choosing.lineId)
                    }
                    Big {
                        objectName: "kioskChoicesDone"
                        Layout.fillWidth: true
                        Layout.preferredHeight: k.u * 3.4
                        enabled: (k.choosing.groups ?? []).every(g => g.done)
                        text: qsTr("Add to My Order")
                        base: "#1f8a4c"
                        onClicked: k.pos.finishChoosing()
                    }
                }
            }
        }
    }

    // --- still there? ---------------------------------------------------------------------------
    Rectangle {
        id: stillThere
        visible: false
        anchors.fill: parent
        color: "#e0101318"
        ColumnLayout {
            anchors.centerIn: parent
            width: Math.min(parent.width * 0.8, k.u * 36)
            spacing: k.u
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("Are you still there?")
                color: "white"
                font.pixelSize: k.u * 2.4
                font.bold: true
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("Your order will be cleared in %1 seconds.").arg(k.idleLeft)
                color: "#c9d1de"
                font.pixelSize: k.u * 1.3
                wrapMode: Text.WordWrap
            }
            Big {
                Layout.fillWidth: true
                Layout.preferredHeight: k.u * 4.4
                text: qsTr("Yes, Keep Ordering")
                base: k.accent
                onClicked: k.touched()
            }
        }
    }

    // --- staff only: hold the top-left corner for 3 seconds -------------------------------------
    Item {
        width: k.u * 4
        height: k.u * 4
        z: 10
        TapHandler {
            longPressThreshold: 3
            onLongPressed: k.managerExit()
        }
    }
}
