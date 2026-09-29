import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root

    required property LayoutController controller
    readonly property bool editing: controller.editing
    readonly property EditorController editor: controller.editor

    width: 1280
    height: 720
    visible: true
    title: (editing ? qsTr("Editing — ") : "") + qsTr("ViewTouch — %1").arg(controller.pageName)
    color: editing ? EditorStyle.chrome : "black"

    palette {
        window: EditorStyle.panel
        windowText: EditorStyle.text
        base: EditorStyle.chrome
        alternateBase: EditorStyle.panelRaised
        text: EditorStyle.text
        button: EditorStyle.panelRaised
        buttonText: EditorStyle.text
        highlight: EditorStyle.accent
        highlightedText: "white"
        placeholderText: EditorStyle.muted
        toolTipBase: EditorStyle.panelRaised
        toolTipText: EditorStyle.text
        mid: EditorStyle.border
        dark: EditorStyle.chrome
        light: EditorStyle.panelRaised
    }

    function toast(text) {
        toastText.text = text
        toastBox.opacity = 1
        toastTimer.restart()
    }

    // Leave edit mode, asking first when there are unsaved changes.
    function requestLeaveEdit() {
        if (!editing)
            return
        if (editor.dirty)
            leaveDialog.open()
        else
            controller.leaveEditMode(false)
    }

    onEditingChanged: if (!editing) pageView.forceActiveFocus()

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Loader {
            Layout.fillWidth: true
            active: root.editing
            visible: active
            sourceComponent: EditorToolbar {
                controller: root.controller
                editor: root.editor
                onExitRequested: root.requestLeaveEdit()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Loader {
                Layout.fillHeight: true
                Layout.preferredWidth: EditorStyle.pagesWidth
                active: root.editing
                visible: active
                sourceComponent: PagePanel {
                    controller: root.controller
                    editor: root.editor
                }
            }

            PageView {
                id: pageView
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: root.editing ? 12 : 0
                controller: root.controller
                editor: root.editing ? root.editor : null
                focus: true

                Keys.onPressed: event => {
                    if (root.editing)
                        return
                    if (event.key === Qt.Key_Escape) {
                        root.controller.goBack()
                        event.accepted = true
                    } else if (event.key === Qt.Key_Home) {
                        root.controller.goHome()
                        event.accepted = true
                    } else if (event.text !== "" && root.controller.triggerHotkey(event.text)) {
                        event.accepted = true
                    }
                }
            }

            Loader {
                Layout.fillHeight: true
                Layout.preferredWidth: EditorStyle.panelWidth
                active: root.editing
                visible: active
                sourceComponent: Inspector {
                    controller: root.controller
                    editor: root.editor
                }
            }
        }
    }

    // --- keyboard --------------------------------------------------------------
    // Text fields take their own editing keys first (Qt shortcut override), so
    // Delete/arrows in the inspector edit text instead of zones.

    Shortcut {
        sequence: "F1"
        onActivated: root.editing ? root.requestLeaveEdit() : root.controller.enterEditMode()
    }
    Shortcut { enabled: root.editing; sequences: [StandardKey.Save]; onActivated: root.controller.saveEdits() }
    Shortcut { enabled: root.editing; sequences: [StandardKey.Undo]; onActivated: root.editor.undo() }
    Shortcut { enabled: root.editing; sequences: [StandardKey.Redo, "Ctrl+Y"]; onActivated: root.editor.redo() }
    Shortcut { enabled: root.editing; sequences: [StandardKey.Copy]; onActivated: root.editor.copy() }
    Shortcut { enabled: root.editing; sequences: [StandardKey.Cut]; onActivated: root.editor.cut() }
    Shortcut { enabled: root.editing; sequences: [StandardKey.Paste]; onActivated: root.editor.paste() }
    Shortcut { enabled: root.editing; sequences: [StandardKey.SelectAll]; onActivated: root.editor.selectAll() }
    Shortcut { enabled: root.editing; sequence: "Ctrl+D"; onActivated: root.editor.duplicateSelection() }
    Shortcut { enabled: root.editing; sequences: [StandardKey.Delete, "Backspace"]; onActivated: root.editor.deleteSelection() }
    Shortcut { enabled: root.editing; sequence: "Esc"; onActivated: root.editor.clearSelection() }
    Shortcut { enabled: root.editing; sequence: "Ctrl+]"; onActivated: root.editor.bringToFront() }
    Shortcut { enabled: root.editing; sequence: "Ctrl+["; onActivated: root.editor.sendToBack() }

    // Arrows move by one grid step; Shift+arrows by one unit.
    Shortcut { enabled: root.editing; sequence: "Left"; onActivated: root.editor.nudge(-root.controller.pageGrid, 0) }
    Shortcut { enabled: root.editing; sequence: "Right"; onActivated: root.editor.nudge(root.controller.pageGrid, 0) }
    Shortcut { enabled: root.editing; sequence: "Up"; onActivated: root.editor.nudge(0, -root.controller.pageGrid) }
    Shortcut { enabled: root.editing; sequence: "Down"; onActivated: root.editor.nudge(0, root.controller.pageGrid) }
    Shortcut { enabled: root.editing; sequence: "Shift+Left"; onActivated: root.editor.nudge(-1, 0) }
    Shortcut { enabled: root.editing; sequence: "Shift+Right"; onActivated: root.editor.nudge(1, 0) }
    Shortcut { enabled: root.editing; sequence: "Shift+Up"; onActivated: root.editor.nudge(0, -1) }
    Shortcut { enabled: root.editing; sequence: "Shift+Down"; onActivated: root.editor.nudge(0, 1) }

    // Page Up / Page Down walk through the page list.
    function stepPage(delta) {
        const pages = editor.pages
        const i = pages.findIndex(p => p.id === editor.pageId)
        const next = pages[(i + delta + pages.length) % pages.length]
        if (next) controller.showPage(next.id)
    }
    Shortcut { enabled: root.editing; sequence: "PgUp"; onActivated: root.stepPage(-1) }
    Shortcut { enabled: root.editing; sequence: "PgDown"; onActivated: root.stepPage(1) }

    // --- dialogs and feedback ------------------------------------------------------

    Dialog {
        id: leaveDialog
        title: qsTr("Save your changes?")
        anchors.centerIn: parent
        modal: true
        width: 420
        standardButtons: Dialog.Save | Dialog.Discard | Dialog.Cancel

        Label {
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("You changed pages in this session. Save them before leaving edit mode?")
        }
        onAccepted: root.controller.leaveEditMode(true)
        onDiscarded: {
            root.controller.leaveEditMode(false)
            close()
        }
    }

    Rectangle {
        id: toastBox
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        width: Math.min(toastText.implicitWidth + 40, root.width - 40)
        height: toastText.implicitHeight + 20
        radius: 18
        color: "#e0101418"
        opacity: 0
        visible: opacity > 0

        Text {
            id: toastText
            anchors.centerIn: parent
            width: parent.width - 40
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: "white"
            font.pixelSize: 16
        }

        Behavior on opacity { NumberAnimation { duration: 150 } }
        Timer { id: toastTimer; interval: 2600; onTriggered: toastBox.opacity = 0 }
    }

    Connections {
        target: root.controller
        function onStatusChanged() { root.toast(root.controller.statusText) }
    }
    Connections {
        target: root.editor
        ignoreUnknownSignals: true
        function onNoticeChanged() { root.toast(root.editor.notice) }
    }
}
