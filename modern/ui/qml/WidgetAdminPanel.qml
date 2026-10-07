import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// Manager editor for one kind of record (props.panel: menu | employees |
// tenders | printers | taxes | store | terminals | mealPeriods). List on the left, form on the right;
// the form's fields come from the service, like the page inspector.
Item {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    readonly property string panel: zone && zone.props && zone.props.panel ? zone.props.panel : "menu"
    readonly property bool single: panel === "taxes" || panel === "store"

    readonly property var fields: {
        if (!pos) return []
        void pos.adminRevision
        void pos.queryRevision
        return pos.adminFields(panel)
    }
    readonly property var records: {
        if (!pos) return []
        void pos.adminRevision
        void pos.queryRevision
        return pos.adminRecords(panel)
    }

    property int index: -2       // -2 nothing chosen, -1 new record
    property var draft: ({})
    property bool dirty: false
    property bool armDelete: false

    function copy(o) { return JSON.parse(JSON.stringify(o)) }
    function choose(i) {
        index = i
        draft = copy(i >= 0 ? records[i] : pos.adminNewRecord(panel))
        dirty = false
        armDelete = false
    }
    // Saves may be answered by a server: react when the data changes.
    property string pending: ""   // "save-new" | "save" | "delete" while waiting
    function save() {
        pending = index === -1 ? "save-new" : "save"
        pos.adminSave(panel, index, draft)
    }
    function remove() {
        pending = "delete"
        pos.adminDelete(panel, index)
    }
    Connections {
        target: w.pos
        function onAdminChanged() {
            const what = w.pending
            w.pending = ""
            if (what === "save-new") w.choose(w.records.length - 1)
            else if (what === "save") w.choose(w.index)
            else if (what === "delete") w.panel === "employees" ? w.choose(w.index) : (w.index = -2)
        }
        // A refused save only shows a notice; stop waiting.
        function onNotice() { if (w.pending !== "") Qt.callLater(() => w.pending = "") }
    }

    Component.onCompleted: if (single) choose(0)
    onPanelChanged: single ? choose(0) : (index = -2)

    // Controls are laid out at a comfortable size, then scaled with the page.
    // Upright (a phone): the list, then a record's form on its own, with ‹ List.
    readonly property bool narrow: zone ? zone.narrow : false
    readonly property real zoom: zone ? zone.formZoom(narrow ? 340 : 680) : 1.6
    Item {
        width: w.width / w.zoom
        height: w.height / w.zoom
        scale: w.zoom
        transformOrigin: Item.TopLeft

        // Terminals: pair a tablet or another screen. The code shows here
        // until the device uses it or 10 minutes pass.
        Rectangle {
            id: pairBar
            visible: w.panel === "terminals"
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
            height: visible ? 64 : 0
            radius: 8
            color: w.pos && w.pos.pairing.active ? "#173a26" : "#232933"
            RowLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 12
                Label {
                    visible: !(w.pos && w.pos.pairing.active)
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("Add a tablet or another screen: Pair a Device, then type the code on it (its Join screen).")
                    opacity: 0.8
                }
                Label {
                    visible: w.pos && w.pos.pairing.active
                    text: qsTr("Pairing code")
                    opacity: 0.8
                }
                Label {
                    visible: w.pos && w.pos.pairing.active
                    text: w.pos ? (w.pos.pairing.code ?? "") : ""
                    font.pixelSize: 30
                    font.bold: true
                    font.letterSpacing: 4
                    color: "#7ee2a8"
                }
                Label {
                    visible: w.pos && w.pos.pairing.active
                    Layout.fillWidth: true
                    text: qsTr("works once, until %1").arg(w.pos ? (w.pos.pairing.until ?? "") : "")
                    opacity: 0.8
                }
                Button {
                    text: w.pos && w.pos.pairing.active ? qsTr("Stop") : qsTr("Pair a Device")
                    highlighted: !(w.pos && w.pos.pairing.active)
                    onClicked: w.pos.pairing.active ? w.pos.stopPairing() : w.pos.startPairing()
                }
            }
        }

        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            anchors.topMargin: pairBar.visible ? pairBar.height + 20 : 10
            spacing: 12

            // --- list ---
            ColumnLayout {
                visible: !w.single && (!w.narrow || w.index === -2)
                Layout.fillWidth: w.narrow    // nested layouts fill by default
                Layout.preferredWidth: w.narrow ? parent.width : parent.width * 0.36
                Layout.fillHeight: true
                spacing: 6

                ListView {
                    id: list
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: w.records
                    currentIndex: w.index
                    ScrollBar.vertical: TouchScrollBar { id: listBar }
                    delegate: ItemDelegate {
                        id: row
                        required property var modelData
                        required property int index
                        width: ListView.view.width - listBar.room
                        highlighted: row.index === w.index
                        onClicked: w.choose(row.index)
                        contentItem: ColumnLayout {
                            spacing: 0
                            Label {
                                text: row.modelData._title || qsTr("(unnamed)")
                                font.bold: true
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label {
                                visible: text !== ""
                                text: row.modelData._detail ?? ""
                                opacity: 0.7
                                font.pixelSize: 11
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                    }
                }
                Button {
                    Layout.fillWidth: true
                    text: qsTr("+ Add")
                    onClicked: w.choose(-1)
                }
            }

            Rectangle {
                visible: !w.single && !w.narrow
                Layout.fillHeight: true
                implicitWidth: 1
                color: "#3a4250"
            }

            // --- form ---
            ColumnLayout {
                visible: !w.narrow || w.index !== -2 || w.single
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8

                Button {
                    objectName: "adminBackToList"
                    visible: w.narrow && !w.single && w.index !== -2
                    text: qsTr("‹ List")
                    onClicked: { w.index = -2; w.dirty = false }
                }
                Label {
                    visible: w.index === -2
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    text: qsTr("Choose something on the left, or Add.")
                    opacity: 0.7
                }

                Label {
                    visible: w.index !== -2
                    text: w.index === -1 ? qsTr("New") : (w.draft.name || w.draft.storeName || (w.single ? "" : qsTr("Edit")))
                    font.bold: true
                    font.pixelSize: 18
                }

                ScrollView {
                    id: scroll
                    visible: w.index !== -2
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: availableWidth
                    clip: true
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    // Finger-wide, shown when the form is longer than the panel.
                    ScrollBar.vertical: TouchScrollBar {
                        id: formBar
                        parent: scroll
                        x: scroll.width - width
                        y: scroll.topPadding
                        height: scroll.availableHeight
                    }

                    ColumnLayout {
                        width: scroll.availableWidth - formBar.room
                        spacing: 10
                        Repeater {
                            model: w.fields
                            delegate: FieldEditor {
                                required property var modelData
                                // A "look" field: an enum of this screen's looks.
                                field: modelData.type !== "look" || !w.zone ? modelData
                                     : Object.assign({}, modelData, {
                                           type: "enum",
                                           options: [{ value: "", text: qsTr("The store's look") }].concat(
                                               w.zone.controller.looks().map(l => ({ value: l.id, text: qsTranslate("Looks", l.name) })))
                                       })
                                pos: w.pos
                                pages: modelData.type === "page" && w.zone ? w.zone.controller.pageChoices() : []
                                value: w.draft[modelData.path]
                                isSet: true
                                readOnly: modelData.readonlyExisting === true && w.index >= 0
                                onCommit: v => {
                                    const d = w.copy(w.draft)
                                    d[modelData.path] = v
                                    w.draft = d
                                    w.dirty = true
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    visible: w.index !== -2
                    Layout.fillWidth: true
                    Layout.fillHeight: false
                    Button {
                        text: qsTr("Save")
                        highlighted: true
                        enabled: w.dirty || w.index === -1
                        onClicked: w.save()
                    }
                    Button {
                        text: qsTr("Undo changes")
                        enabled: w.dirty
                        onClicked: w.choose(w.index)
                    }
                    // Printers: a test page (saved settings), and its drawer.
                    Button {
                        objectName: "testPrint"
                        visible: w.panel === "printers" && w.index >= 0
                        enabled: !w.dirty
                        text: w.dirty ? qsTr("Save, then Test Print") : qsTr("Test Print")
                        onClicked: w.pos.testPrinter(w.draft.id, false)
                    }
                    Button {
                        visible: w.panel === "printers" && w.index >= 0 && w.draft.drawerKick === true
                        enabled: !w.dirty
                        text: qsTr("Test Print + Open Drawer")
                        onClicked: w.pos.testPrinter(w.draft.id, true)
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        visible: !w.single && w.index >= 0
                        text: w.armDelete ? qsTr("Tap again to confirm")
                                          : (w.panel === "employees" ? qsTr("Deactivate") : qsTr("Remove"))
                        onClicked: {
                            if (!w.armDelete) {
                                w.armDelete = true
                                return
                            }
                            w.remove()
                            w.armDelete = false
                        }
                    }
                }
            }
        }
    }
}
