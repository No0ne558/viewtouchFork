import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Manager editor for one kind of record (props.panel: menu | employees |
// tenders | printers | taxes | store). List on the left, form on the right;
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
        return pos.adminFields(panel)
    }
    readonly property var records: {
        if (!pos) return []
        void pos.adminRevision
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
    function save() {
        const creating = index === -1
        if (!pos.adminSave(panel, index, draft))
            return
        choose(creating ? records.length - 1 : index)
    }

    Component.onCompleted: if (single) choose(0)
    onPanelChanged: single ? choose(0) : (index = -2)

    // Controls are laid out at a comfortable size, then scaled with the page.
    readonly property real zoom: 1.6
    Item {
        width: w.width / w.zoom
        height: w.height / w.zoom
        scale: w.zoom
        transformOrigin: Item.TopLeft

        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 12

            // --- list ---
            ColumnLayout {
                visible: !w.single
                Layout.fillWidth: false    // nested layouts fill by default
                Layout.preferredWidth: parent.width * 0.36
                Layout.fillHeight: true
                spacing: 6

                ListView {
                    id: list
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: w.records
                    currentIndex: w.index
                    ScrollBar.vertical: ScrollBar {}
                    delegate: ItemDelegate {
                        id: row
                        required property var modelData
                        required property int index
                        width: ListView.view.width
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
                visible: !w.single
                Layout.fillHeight: true
                implicitWidth: 1
                color: "#3a4250"
            }

            // --- form ---
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8

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

                    ColumnLayout {
                        width: scroll.availableWidth
                        spacing: 10
                        Repeater {
                            model: w.fields
                            delegate: FieldEditor {
                                required property var modelData
                                field: modelData
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
                            if (w.pos.adminDelete(w.panel, w.index))
                                w.index = w.panel === "employees" ? w.index : -2
                            w.armDelete = false
                        }
                    }
                }
            }
        }
    }
}
