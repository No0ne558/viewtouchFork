import QtQuick
import QtQuick.Shapes

// One zone on a page. Roles come from ZoneModel; styles are pre-resolved.
Item {
    id: zone

    required property string zoneId
    required property string kind
    required property string label
    required property int zoneX
    required property int zoneY
    required property int zoneW
    required property int zoneH
    required property string shape
    required property string behavior
    required property bool zoneEnabled
    required property bool inherited
    required property bool current
    required property string imagePath
    required property var styleNormal
    required property var styleSelected
    required property var styleDisabled
    required property var props
    required property bool soldOut   // its item is 86'd: shown, not orderable

    // POS session and controller, for widgets.
    // Screen pixels per canvas unit (the page is scaled to fit the screen).
    property real screenScale: 1
    // Form widgets lay out desktop-sized controls (25 px) and scale them by
    // this: big enough for a finger (46 screen px), unless the content would
    // then be narrower than `minWidth` (or shorter than `minHeight`) canvas units.
    function formZoom(minWidth, minHeight) {
        const touch = 46 / (25 * Math.max(0.1, screenScale))
        return Math.max(1, Math.min(touch, width / minWidth, minHeight ? height / minHeight : touch))
    }
    // Canvas units for `px` screen pixels: keys at least a finger tall.
    function touch(px) { return px / Math.max(0.1, screenScale) }
    property LayoutController controller
    property PosService pos

    // A widget's own buttons, hidden or renamed in the editor
    // (props.hideButtons, props.buttons.<id>.hide / .label).
    function keyShown(id) {
        if (!props) return true
        if (props.hideButtons === true) return false
        const b = props.buttons ? props.buttons[id] : undefined
        return !(b && b.hide === true)
    }
    function keyText(id, usual) {
        const b = props && props.buttons ? props.buttons[id] : undefined
        return b && b.label ? qsTranslate("Page", b.label) : usual
    }

    // Set by PageView for behavior "select" (one lit zone per page).
    property string selectedZoneId: ""

    // Edit mode: live drag/resize feedback from EditLayer.
    property bool editing: false
    property bool editSelected: false
    property real dragDX: 0
    property real dragDY: 0
    property var previewRect: null

    signal activated()
    signal selectRequested()

    readonly property bool isWidget: !["button", "label", "image", "comment"].includes(kind)
    // Widgets with a working implementation (Widget<Kind>.qml); the rest
    // show a placeholder until their milestone.
    readonly property var builtWidgets: ["table", "tableGrid", "staffPicker", "checkHistory", "modifierPicker",
        "soldOutList", "orderList", "loginPad", "guestCount", "numPad",
        "paymentPanel", "checkList", "keyboard", "clock", "logoutPanel", "statusBar",
        "adminPanel", "reportView", "drawerPanel", "endOfDay", "splitCheck", "kitchenDisplay", "customerInfo",
        "customerLookup", "giftCard", "waitlist", "schedule", "factoryReset", "messageComposer", "network", "receiveDelivery", "checkSearch", "orderLater"]
    readonly property bool hasWidget: isWidget && builtWidgets.includes(kind)
    readonly property bool interactive: zoneEnabled && behavior !== "passthrough"
                                        && (kind === "button" || kind === "image")

    property bool toggled: false
    property bool armed: false      // "double": first touch
    property bool flashing: false   // "blink": brief highlight after touch

    readonly property bool lit: current || toggled || armed || flashing
                                || (behavior === "select" && selectedZoneId === zoneId)
                                || (tap.pressed && behavior !== "none" && !editing)
    readonly property var st: !zoneEnabled ? styleDisabled : lit ? styleSelected : styleNormal

    x: previewRect ? previewRect.x : zoneX + (editSelected ? dragDX : 0)
    y: previewRect ? previewRect.y : zoneY + (editSelected ? dragDY : 0)
    width: previewRect ? previewRect.w : zoneW
    height: previewRect ? previewRect.h : zoneH
    visible: kind !== "comment" || editing   // notes are for the editor only
    // Template zones are dimmed while editing: they belong to another page.
    opacity: (st.opacity ?? 1) * (editing && inherited ? 0.45 : 1)

    ZoneShape {
        anchors.fill: parent
        shape: zone.shape
        st: zone.st
    }

    Image {
        id: picture
        visible: zone.imagePath !== ""
        source: zone.imagePath
        anchors.fill: parent
        anchors.margins: (zone.st.frameWidth ?? 3) + 6
        anchors.bottomMargin: caption.visible ? parent.height * 0.3 : anchors.margins
        fillMode: Image.PreserveAspectFit
        asynchronous: true
    }

    Text {
        id: caption
        visible: text !== "" && !zone.hasWidget
        // Page text is translated too (i18n/<lang>.json, or the store's own phrases).
        text: zone.isWidget ? zone.kind + (zone.label ? "\n" + zone.label : "") : qsTranslate("Page", zone.label)
        anchors.fill: parent
        anchors.margins: (zone.st.frameWidth ?? 3) + 8
        anchors.topMargin: picture.visible ? parent.height * 0.7 : anchors.margins
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.WordWrap
        fontSizeMode: Text.Fit
        minimumPixelSize: 10
        color: zone.st.textColor ?? "white"
        font.family: zone.st.font ?? "DejaVu Sans"
        font.pixelSize: zone.st.fontSize ?? 28
        font.bold: zone.st.bold ?? true
        style: zone.st.textStyle === "embossed" ? Text.Raised
             : zone.st.textStyle === "outline" ? Text.Outline : Text.Normal
        styleColor: Qt.darker(color, 3)
    }

    // 86'd: the item can't be ordered right now.
    Rectangle {
        visible: zone.soldOut
        anchors.fill: parent
        radius: zone.st.radius ?? 14
        color: "#a0000000"
        Rectangle {
            anchors.centerIn: parent
            width: Math.min(parent.width * 0.9, soldOutText.implicitWidth + 24)
            height: soldOutText.implicitHeight + 10
            radius: 6
            rotation: -8
            color: "#b83232"
            Text {
                id: soldOutText
                anchors.centerIn: parent
                text: qsTr("SOLD OUT")
                color: "white"
                font.family: zone.st.font ?? "DejaVu Sans"
                font.pixelSize: Math.max(12, Math.min(zone.height * 0.22, 34))
                font.bold: true
            }
        }
    }

    // The window's own palette, for what the zone's style leaves unset.
    Item { id: plain; visible: false }
    Loader {
        id: widget
        anchors.fill: parent
        active: zone.hasWidget
        // Its form buttons take the built-in button look (style keyFill...).
        palette.button: zone.st.keyFill ?? plain.palette.button
        palette.buttonText: zone.st.keyTextColor ?? plain.palette.buttonText
        palette.highlight: zone.st.keyLitFill ?? plain.palette.highlight
        enabled: !zone.editing   // arranging a page must not ring up sales
        onActiveChanged: load()
        Component.onCompleted: load()
        function load() {
            if (active)
                setSource("Widget" + zone.kind[0].toUpperCase() + zone.kind.slice(1) + ".qml", { zone: zone })
        }
        Connections {
            target: zone
            function onKindChanged() { widget.load() }
        }
    }

    // Placeholder for widgets not built yet.
    Shape {
        visible: zone.isWidget && !zone.hasWidget
        anchors.fill: parent
        ShapePath {
            strokeColor: "#80ffffff"
            strokeWidth: 2
            strokeStyle: ShapePath.DashLine
            dashPattern: [6, 4]
            fillColor: "transparent"
            startX: 4; startY: 4
            PathLine { x: zone.width - 4; y: 4 }
            PathLine { x: zone.width - 4; y: zone.height - 4 }
            PathLine { x: 4; y: zone.height - 4 }
            PathLine { x: 4; y: 4 }
        }
    }

    Text {
        visible: zone.armed
        text: qsTr("tap again")
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 6 }
        color: caption.color
        font.pixelSize: 16
    }

    TapHandler {
        id: tap
        enabled: zone.interactive && !zone.editing
        onTapped: {
            switch (zone.behavior) {
            case "toggle":
                zone.toggled = !zone.toggled
                break
            case "select":
                zone.selectRequested()
                break
            case "double":
                if (!zone.armed) {
                    zone.armed = true
                    armTimer.restart()
                    return
                }
                zone.armed = false
                break
            case "blink":
                zone.flashing = true
                flashTimer.restart()
                break
            }
            zone.activated()
        }
    }

    Timer { id: flashTimer; interval: 180; onTriggered: zone.flashing = false }
    Timer { id: armTimer; interval: 1500; onTriggered: zone.armed = false }
}
