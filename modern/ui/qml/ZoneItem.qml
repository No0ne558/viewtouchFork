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

    // Set by PageView for behavior "select" (one lit zone per page).
    property string selectedZoneId: ""

    signal activated()
    signal selectRequested()

    readonly property bool isWidget: !["button", "label", "image", "comment"].includes(kind)
    readonly property bool interactive: zoneEnabled && behavior !== "passthrough"
                                        && (kind === "button" || kind === "image")

    property bool toggled: false
    property bool armed: false      // "double": first touch
    property bool flashing: false   // "blink": brief highlight after touch

    readonly property bool lit: current || toggled || armed || flashing
                                || (behavior === "select" && selectedZoneId === zoneId)
                                || (tap.pressed && behavior !== "none")
    readonly property var st: !zoneEnabled ? styleDisabled : lit ? styleSelected : styleNormal

    x: zoneX
    y: zoneY
    width: zoneW
    height: zoneH
    visible: kind !== "comment"   // comments show only in edit mode (M2)
    opacity: st.opacity ?? 1

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
        visible: text !== ""
        text: zone.isWidget ? zone.kind + (zone.label ? "\n" + zone.label : "") : zone.label
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

    // Widgets are placeholders until their implementations land (M3).
    Shape {
        visible: zone.isWidget
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
        enabled: zone.interactive
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
