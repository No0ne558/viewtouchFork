import QtQuick

// A key on a widget keypad. Its look comes from the zone it is in: the
// "Built-in button look" (style keyFill, keyTextColor, keyLitFill, keyFont,
// keyRadius), inherited from the page and the theme like any style.
Rectangle {
    id: key

    // The style of the zone this key is in (the nearest ZoneItem up the tree).
    readonly property var keySt: {
        for (let p = key.parent; p; p = p.parent)
            if (p.styleNormal !== undefined && p.st !== undefined)
                return p.st
        return ({})
    }
    property string text
    property color baseColor: keySt.keyFill ?? "#343c49"
    property color textColor: keySt.keyTextColor ?? "white"
    property string fontFamily: "DejaVu Sans"
    property real fontScale: 0.42
    property bool accent: false

    signal clicked()

    radius: keySt.keyRadius !== undefined ? keySt.keyRadius : Math.min(width, height) * 0.14
    color: tap.pressed || accent ? (keySt.keyLitFill ?? (tap.pressed ? "#4c8dff" : "#1f8a4c")) : baseColor
    border.color: Qt.darker(color, 1.5)
    border.width: 2

    Text {
        anchors.centerIn: parent
        width: parent.width - 8
        horizontalAlignment: Text.AlignHCenter
        text: key.text
        color: key.textColor
        font.family: key.keySt.keyFont ?? key.fontFamily
        font.pixelSize: Math.max(10, Math.min(key.height, key.width * 1.2) * key.fontScale)
        font.bold: true
        fontSizeMode: Text.HorizontalFit
        minimumPixelSize: 10
    }

    TapHandler {
        id: tap
        onTapped: key.clicked()
    }
}
