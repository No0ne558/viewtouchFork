import QtQuick

// A key on a widget keypad. Colors derive from the owning zone's style so
// keypads follow the theme.
Rectangle {
    id: key

    property string text
    property color baseColor: "#343c49"
    property color textColor: "white"
    property string fontFamily: "DejaVu Sans"
    property real fontScale: 0.42
    property bool accent: false

    signal clicked()

    radius: Math.min(width, height) * 0.14
    color: tap.pressed ? "#4c8dff" : (accent ? "#1f8a4c" : baseColor)
    border.color: Qt.darker(color, 1.5)
    border.width: 2

    Text {
        anchors.centerIn: parent
        width: parent.width - 8
        horizontalAlignment: Text.AlignHCenter
        text: key.text
        color: key.textColor
        font.family: key.fontFamily
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
