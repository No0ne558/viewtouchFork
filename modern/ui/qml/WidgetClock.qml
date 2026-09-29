import QtQuick

// Current time and date.
Item {
    id: w
    property ZoneItem zone
    property date now: new Date()

    Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: w.now = new Date()
    }

    Text {
        anchors.centerIn: parent
        width: parent.width - 16
        horizontalAlignment: Text.AlignHCenter
        text: Qt.formatTime(w.now, Qt.locale().timeFormat(Locale.ShortFormat)) + "   ·   "
              + Qt.formatDate(w.now, Qt.locale().dateFormat(Locale.LongFormat))
        color: w.zone.st.textColor ?? "white"
        font.family: w.zone.st.font ?? "DejaVu Sans"
        font.pixelSize: w.height * 0.4
        fontSizeMode: Text.HorizontalFit
        minimumPixelSize: 10
    }
}
