import QtQuick
import QtQuick.Window
import QtQuick.Controls
import ViewTouch

ApplicationWindow {
    id: mainWindow
    width: screenWidth
    height: screenHeight
    visible: true
    title: "ViewTouch V2"

    // Force fullscreen
    visibility: Window.FullScreen

    // Main terminal interface
    Terminal {
        anchors.fill: parent
        mainWindow: mainWindow
    }

    // Global shortcuts
    Shortcut {
        sequence: "Ctrl+Q"
        onActivated: Qt.quit()
    }

    Shortcut {
        sequence: "F11"
        onActivated: {
            if (mainWindow.visibility === Window.FullScreen) {
                mainWindow.visibility = Window.Windowed
            } else {
                mainWindow.visibility = Window.FullScreen
            }
        }
    }

    Shortcut {
        sequence: "Ctrl+F"
        onActivated: mainWindow.visibility = Window.FullScreen
    }
}