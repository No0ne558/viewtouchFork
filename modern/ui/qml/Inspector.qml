import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Right-hand panel: properties of the selected zones, the page, or the theme.
// Everything shown comes from the field schema, so new properties need no
// changes here.
Rectangle {
    id: inspector

    required property LayoutController controller
    required property EditorController editor

    // "zone" | "page" | "theme"
    property string mode: editor.selection.length > 0 ? "zone" : "page"

    color: EditorStyle.panel

    Connections {
        target: inspector.editor
        function onSelectionChanged() {
            if (inspector.editor.selection.length > 0)
                inspector.mode = "zone"
            else if (inspector.mode === "zone")
                inspector.mode = "page"
        }
    }

    readonly property string zoneKind: {
        editor.revision
        if (editor.selection.length === 0) return ""
        return editor.selectionKind !== "" ? editor.selectionKind
             : (editor.fieldInfo("zone", "kind").value ?? "button")
    }
    readonly property var fields: {
        editor.revision
        if (mode === "zone") return editor.selection.length ? editor.zoneFields(zoneKind) : []
        if (mode === "theme") return editor.themeFields()
        return editor.pageFields()
    }
    // [{ name, fields }] in schema order.
    readonly property var groups: {
        const out = []
        for (const f of fields) {
            let g = out.find(x => x.name === f.group)
            if (!g) { g = { name: f.group, fields: [] }; out.push(g) }
            g.fields.push(f)
        }
        return out
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TabBar {
            id: tabs
            Layout.fillWidth: true
            currentIndex: ["zone", "page", "theme"].indexOf(inspector.mode)
            TabButton {
                text: inspector.editor.selection.length > 1
                      ? qsTr("%1 zones").arg(inspector.editor.selection.length) : qsTr("Zone")
                enabled: inspector.editor.selection.length > 0
                onClicked: inspector.mode = "zone"
            }
            TabButton { text: qsTr("Page"); onClicked: inspector.mode = "page" }
            TabButton { text: qsTr("Theme"); onClicked: inspector.mode = "theme" }
        }

        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                width: scroll.availableWidth
                spacing: 10

                Item { implicitHeight: 4 }

                // Kind switcher for zones
                ColumnLayout {
                    visible: inspector.mode === "zone"
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    Label { text: qsTr("Type"); color: EditorStyle.muted; font.pixelSize: 12 }
                    ComboBox {
                        Layout.fillWidth: true
                        readonly property var kinds: inspector.editor.basicKinds.concat(inspector.editor.widgetKinds)
                        model: kinds
                        currentIndex: inspector.editor.selectionKind === "" ? -1 : kinds.indexOf(inspector.editor.selectionKind)
                        displayText: inspector.editor.selectionKind === "" ? qsTr("(mixed)") : currentText
                        onActivated: index => inspector.editor.setField("zone", "kind", kinds[index])
                    }
                }

                Label {
                    visible: inspector.mode === "page"
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    text: qsTr("Nothing selected — these are the settings of this page.")
                    color: EditorStyle.muted
                    wrapMode: Text.WordWrap
                    font.pixelSize: 12
                }

                Label {
                    visible: inspector.mode === "theme"
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    text: qsTr("The theme is the default look for every page. Pages and zones override it.")
                    color: EditorStyle.muted
                    wrapMode: Text.WordWrap
                    font.pixelSize: 12
                }

                Repeater {
                    model: inspector.groups

                    delegate: ColumnLayout {
                        id: group
                        required property var modelData
                        required property int index
                        // Secondary style states start collapsed.
                        property bool open: !modelData.name.startsWith("Look when")

                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                        spacing: 6

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 1
                            color: EditorStyle.border
                        }
                        ItemDelegate {
                            Layout.fillWidth: true
                            implicitHeight: 30
                            padding: 0
                            text: (group.open ? "▾  " : "▸  ") + group.modelData.name
                            font.bold: true
                            onClicked: group.open = !group.open
                        }

                        Repeater {
                            model: group.open ? group.modelData.fields : []
                            delegate: Loader {
                                id: row
                                required property var modelData
                                Layout.fillWidth: true
                                sourceComponent: modelData.type === "actions" ? actionsComponent : fieldComponent

                                Component {
                                    id: fieldComponent
                                    FieldEditor {
                                        readonly property var info: {
                                            inspector.editor.revision
                                            return inspector.editor.fieldInfo(inspector.mode, row.modelData.path)
                                        }
                                        field: row.modelData
                                        editor: inspector.editor
                                        value: info.value
                                        mixed: info.mixed ?? false
                                        isSet: info.isSet ?? false
                                        resolved: info.resolved
                                        onCommit: v => inspector.editor.setField(inspector.mode, row.modelData.path, v)
                                        onReset: inspector.editor.clearField(inspector.mode, row.modelData.path)
                                    }
                                }
                                Component {
                                    id: actionsComponent
                                    Loader {
                                        sourceComponent: inspector.editor.selection.length === 1 ? actionList : multiNote
                                        Component {
                                            id: actionList
                                            ActionListEditor { editor: inspector.editor }
                                        }
                                        Component {
                                            id: multiNote
                                            Label {
                                                text: qsTr("Select a single zone to edit what it does.")
                                                color: EditorStyle.muted
                                                wrapMode: Text.WordWrap
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // Page: who points here, and layout problems
                ColumnLayout {
                    visible: inspector.mode === "page"
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    spacing: 4

                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: EditorStyle.border }
                    Label { text: qsTr("Used by"); font.bold: true }
                    Label {
                        visible: inspector.editor.references.length === 0
                        text: qsTr("No buttons lead here.")
                        color: EditorStyle.muted
                        font.italic: true
                    }
                    Repeater {
                        model: inspector.editor.references
                        delegate: Label {
                            required property string modelData
                            text: "• " + modelData
                            color: EditorStyle.muted
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }
                }

                ColumnLayout {
                    visible: inspector.editor.issues.length > 0
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    spacing: 4

                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: EditorStyle.border }
                    Label {
                        text: qsTr("Problems (%1)").arg(inspector.editor.issues.length)
                        font.bold: true
                        color: EditorStyle.warning
                    }
                    Repeater {
                        model: inspector.editor.issues
                        delegate: Label {
                            required property string modelData
                            text: "• " + modelData
                            color: EditorStyle.warning
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                            font.pixelSize: 12
                        }
                    }
                }

                Item { implicitHeight: 16 }
            }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: EditorStyle.border
    }
}
