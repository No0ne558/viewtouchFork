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
    // An on-screen keyboard for text fields: most touch screens have no
    // keyboard. --touch-keyboard yes|no, else the terminal's setting (Manager
    // -> Terminals), else on (Android: the device's own keyboard instead).
    property string keyboardFlag: ""
    readonly property string keyboardSetting: controller.pos ? ((controller.pos as PosService).terminalKeyboard ?? "") : ""
    property bool touchKeyboard: keyboardFlag !== "" ? keyboardFlag === "on"
                               : keyboardSetting !== "" ? keyboardSetting === "on"
                               : Qt.platform.os !== "android"
    // The text field being typed in, if any.
    readonly property Item typingIn: {
        const line = activeFocusItem as TextInput
        if (line && !line.readOnly)
            return line
        const area = activeFocusItem as TextEdit
        return area && !area.readOnly ? area : null
    }
    // The keyboard pops up over the screen (the page keeps its size). When it
    // would cover the field being typed in, the page (or the setup guide)
    // slides up just enough to keep the field in view, and back down after.
    readonly property real keysTop: touchKeys.visible ? root.height - touchKeys.height : root.height
    property real lift: 0
    Behavior on lift { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
    function inside(item, container) {
        for (let p = item; p; p = p.parent)
            if (p === container) return true
        return false
    }
    function updateLift() {
        const field = typingIn
        if (!touchKeys.visible || !field || !(inside(field, pageColumn) || inside(field, setupLoader))) {
            lift = 0
            return
        }
        // Where the field's bottom is with the page where it belongs.
        const bottom = field.mapToItem(null, 0, field.height).y + lift
        lift = Math.max(0, Math.min(bottom + 16 - keysTop, touchKeys.height))
    }
    onTypingInChanged: Qt.callLater(updateLift)
    onKeysTopChanged: Qt.callLater(updateLift)

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
        id: pageColumn
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: root.posWidth
        spacing: 0
        transform: Translate { y: -root.lift }

        Loader {
            Layout.fillWidth: true
            active: root.editing
            visible: active
            sourceComponent: EditorToolbar {
                controller: root.controller
                editor: root.editor
                pageSurface: pageView.surfaceItem
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
                // Asleep: the touch that wakes it does nothing here. A PIN pad or
                // chooser on top: the buttons under it can't be touched.
                enabled: !root.controller.asleep && !approvalPad.visible && !jobChooser.visible

                Keys.onPressed: event => {
                    if (root.editing)
                        return
                    if (event.key === Qt.Key_Escape && root.controller.numberKey("escape")) {
                        event.accepted = true
                    } else if (event.key === Qt.Key_Escape) {
                        root.controller.goBack()
                        event.accepted = true
                    } else if (event.key === Qt.Key_Home) {
                        root.controller.goHome()
                        event.accepted = true
                    } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                               && root.controller.numberKey("enter")) {
                        event.accepted = true      // the item with the number typed
                    } else if (event.key === Qt.Key_Backspace && root.controller.numberKey("back")) {
                        event.accepted = true
                    } else if (event.text.length === 1 && event.text >= "0" && event.text <= "9"
                               && root.controller.numberKey(event.text)) {
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

    // The on-screen keyboard: over the screen, above every dialog, and it
    // never takes the focus from the field.
    Popup {
        id: touchKeys
        objectName: "touchKeys"
        parent: Overlay.overlay
        visible: root.touchKeyboard && root.typingIn !== null && !selfOrder.visible
        modal: false
        focus: false
        closePolicy: Popup.NoAutoClose
        z: 1000
        padding: 0
        x: 0
        width: root.posWidth
        height: Math.min(root.height * 0.42, 420)
        y: root.height - height
        background: null
        contentItem: TouchKeyboard {
            id: keyboard
            target: touchKeys.visible ? root.typingIn : null
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
    // With the store's standby ready, it takes over by itself shortly; a
    // manager can make it take over now.
    Rectangle {
        id: offline
        readonly property bool standby: !!root.controller.pos && (root.controller.pos as PosService).standbyReady
        visible: !!root.controller.pos && !root.controller.pos.online
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 12
        width: offlineRow.implicitWidth + 40
        height: offlineRow.implicitHeight + 20
        radius: Math.min(height / 2, 30)
        color: "#e0b83232"
        z: 10
        RowLayout {
            id: offlineRow
            anchors.centerIn: parent
            spacing: 16
            Text {
                color: "white"
                font.pixelSize: 18
                font.bold: true
                text: offline.standby ? qsTr("The main server isn't answering.\nThe standby computer takes over in a few seconds.")
                                      : qsTr("Reconnecting to the server…")
            }
            WidgetKey {
                objectName: "takeOverNow"
                visible: offline.standby
                Layout.preferredWidth: 190
                Layout.preferredHeight: 56
                text: qsTr("Take Over Now")
                fontScale: 0.3
                baseColor: "#343c49"
                onClicked: approvalPad.takingOver = true
            }
        }
    }

    // Practice (training): everyone can see nothing here is real.
    Rectangle {
        visible: !root.editing && root.controller.pos !== null && (root.controller.pos as PosService).training
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        width: practiceText.implicitWidth + 40
        height: practiceText.implicitHeight + 10
        radius: 6
        color: "#f5b940"
        z: 50
        Text {
            id: practiceText
            anchors.centerIn: parent
            text: qsTr("PRACTICE - nothing here is a real sale")
            font.bold: true
            font.pixelSize: 16
            color: "#1b1b1b"
        }
    }

    // Messages from other screens, across the top.
    MessageBanner {
        anchors.top: parent.top
        anchors.left: parent.left
        width: root.posWidth
        z: 55
        pos: root.controller.pos as PosService
        kitchenScreen: root.controller.pageKind === "kitchen"
    }

    // A manager's PIN for something this person may not do on their own.
    // A self-order kiosk: guests order on their own; nothing else shows.
    SelfOrder {
        id: selfOrder
        objectName: "selfOrder"
        anchors.fill: parent
        z: 55
        visible: !root.editing && root.controller.pos !== null && ((root.controller.pos as PosService).selfOrder.on ?? false)
        pos: root.controller.pos as PosService
        onManagerExit: approvalPad.leavingKiosk = true
    }

    // Screen saver: dimmed after no touches; the first touch only wakes it.
    Rectangle {
        id: screenSaver
        objectName: "screenSaver"
        anchors.fill: parent
        z: 90
        visible: root.controller.asleep
        color: "#f2050608"
        Column {
            anchors.centerIn: parent
            spacing: 12
            opacity: 0.55
            // The store's logo (Store Settings), when there is one.
            Image {
                objectName: "screenSaverLogo"
                readonly property var pos: root.controller.pos
                anchors.horizontalCenter: parent.horizontalCenter
                visible: status === Image.Ready
                source: pos && screenSaver.visible ? (pos.imageRevision, pos.imageUrl("logo:")) : ""
                sourceSize.height: Math.min(root.width, root.height) * 0.3
                fillMode: Image.PreserveAspectFit
                asynchronous: true
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: root.controller.pos ? root.controller.pos.storeName : ""
                color: "white"
                font.pixelSize: Math.min(root.width, root.height) * 0.06
                font.bold: true
            }
            Text {
                id: saverClock
                anchors.horizontalCenter: parent.horizontalCenter
                color: "#c9d1de"
                font.pixelSize: Math.min(root.width, root.height) * 0.1
                Timer {
                    interval: 1000
                    repeat: true
                    triggeredOnStart: true
                    running: screenSaver.visible
                    onTriggered: saverClock.text = Qt.formatTime(new Date(), "h:mm AP")
                }
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Touch to wake")
                color: "#8a94a6"
                font.pixelSize: Math.min(root.width, root.height) * 0.03
            }
        }
        // Wakes when the finger lifts: the page is still off during the press.
        MouseArea { anchors.fill: parent; onReleased: root.controller.wake() }
    }

    // A new store's setup guide (managers; Manager -> Setup Guide…).
    Loader {
        id: setupLoader
        anchors.fill: parent
        transform: Translate { y: -root.lift }
        z: 70
        active: root.controller.setupOpen
        sourceComponent: SetupGuide { controller: root.controller }
    }

    JobChooser {
        id: jobChooser
        anchors.fill: parent
        z: 59
        pos: root.controller.pos as PosService
    }

    ApprovalPad {
        id: approvalPad
        anchors.fill: parent
        z: 60
        pos: root.controller.pos as PosService
    }

    Rectangle {
        id: toastBox
        objectName: "toast"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        // At the top while the keyboard is up (not over the field being typed in).
        anchors.bottomMargin: touchKeys.visible ? root.height - height - 24 : 24
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

    // A held button: what it does (touch to close; gone after a while).
    Rectangle {
        id: explainCard
        objectName: "explainCard"
        readonly property var info: root.controller.explanation
        visible: !!info.text
        z: 50
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 24
        width: Math.min(root.width - 40, 640)
        height: explainCol.implicitHeight + 32
        radius: 14
        color: "#f0101418"
        border.color: "#4c8dff"
        border.width: 2
        onInfoChanged: if (info.text) explainTimer.restart()
        Timer { id: explainTimer; interval: 9000; onTriggered: root.controller.clearExplanation() }
        Column {
            id: explainCol
            x: 20; y: 16
            width: parent.width - 40
            spacing: 6
            Text {
                width: parent.width
                text: explainCard.info.title ?? ""
                color: "white"
                font.pixelSize: 22
                font.bold: true
                wrapMode: Text.WordWrap
            }
            Text {
                width: parent.width
                text: explainCard.info.text ?? ""
                color: "#d7dde8"
                font.pixelSize: 18
                wrapMode: Text.WordWrap
            }
            Text {
                width: parent.width
                text: qsTr("Hold any button to see what it does. Touch here to close.")
                color: "#8a94a6"
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }
        }
        MouseArea { anchors.fill: parent; onClicked: root.controller.clearExplanation() }
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
