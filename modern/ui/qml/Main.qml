import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

ApplicationWindow {
    id: root

    required property LayoutController controller
    readonly property bool editing: controller.editing
    readonly property EditorController editor: controller.editor
    // Kiosk screens stay full screen; elsewhere F11 switches to a window.
    property bool kiosk: false
    // The customer display in the second monitor's part of this window (a
    // kiosk's one window spans both monitors). With one screen (trying it
    // out), the right third.
    property bool customerDisplay: false
    // Where the first monitor ends (set from the screens; 0: one screen).
    property real customerDisplayAt: 0
    readonly property real posWidth: !customerDisplay ? width
                                     : customerDisplayAt > 0 ? Math.min(width, customerDisplayAt)
                                     : Math.round(width * 0.66)
    // An on-screen keyboard for text fields (touch screens with no keyboard).
    property bool touchKeyboard: kiosk
    // The text field being typed in, if any.
    readonly property Item typingIn: {
        const line = activeFocusItem as TextInput
        if (line && !line.readOnly)
            return line
        const area = activeFocusItem as TextEdit
        return area && !area.readOnly ? area : null
    }

    // Phones get phone pages (the controller decides; see formFactor).
    onWidthChanged: controller.windowResized(width, height)
    onHeightChanged: controller.windowResized(width, height)
    Component.onCompleted: controller.windowResized(width, height)

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

    Loader {
        active: root.customerDisplay
        x: root.posWidth
        width: root.width - root.posWidth
        height: root.height
        sourceComponent: CustomerDisplay { pos: root.controller.pos as PosService }
    }

    ColumnLayout {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: root.posWidth
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

        // Docked below the page (which shrinks to fit), so the field being
        // typed in stays in view.
        TouchKeyboard {
            id: touchKeys
            objectName: "touchKeys"
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            visible: root.touchKeyboard && root.typingIn !== null
            target: visible ? root.typingIn : null
            onDismissed: pageView.forceActiveFocus()
        }
    }

    // --- keyboard --------------------------------------------------------------
    // Text fields take their own editing keys first (Qt shortcut override), so
    // Delete/arrows in the inspector edit text instead of zones.

    Shortcut {
        // Ctrl+E works on keyboards whose F-keys need Fn (Macs, many laptops).
        sequences: ["F1", "Ctrl+E"]
        // Entering checks the layout.edit permission (a manager must be logged in).
        onActivated: root.editing ? root.requestLeaveEdit() : root.controller.requestEditMode()
    }
    // Android's Back button goes back a page instead of closing the app.
    Shortcut {
        enabled: !root.editing
        sequence: "Back"
        onActivated: root.controller.goBack()
    }
    Shortcut {
        enabled: !root.kiosk
        sequences: [StandardKey.FullScreen, "F11", "Ctrl+Shift+F"]
        onActivated: root.visibility === Window.FullScreen ? root.showNormal() : root.showFullScreen()
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

    // A terminal that lost its server says so until it is back.
    Rectangle {
        visible: !!root.controller.pos && !root.controller.pos.online
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 12
        width: offlineText.implicitWidth + 40
        height: offlineText.implicitHeight + 20
        radius: height / 2
        color: "#e0b83232"
        z: 10
        Text {
            id: offlineText
            anchors.centerIn: parent
            color: "white"
            font.pixelSize: 18
            font.bold: true
            text: qsTr("Reconnecting to the server…")
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
