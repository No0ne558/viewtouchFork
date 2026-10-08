pragma Singleton
import QtQuick

// Where the pop-up keyboard is, for the page's tap handlers. Qt offers a
// touch to the tap handlers underneath whatever took it, so a key on the
// keyboard also pressed the page button behind it; those handlers ignore a
// press inside the keyboard. (MouseAreas don't need this: the keyboard's
// own takes the touch from them.)
QtObject {
    // The keyboard's top edge on the screen; -1: no keyboard.
    property real keyboardTop: -1

    function covered(scenePoint) {
        return keyboardTop >= 0 && scenePoint.y >= keyboardTop
    }
}
