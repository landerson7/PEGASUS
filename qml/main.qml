// qml/main.qml

import QtQuick
import QtQuick.Controls

Rectangle {
    width: 128
    height: 64
    color: "black"

    Text {
        id: scrollingText
        text: "PEGASUS HUD"
        color: "white"
        font.pixelSize: 10

        // Start just off the right edge
        x: parent.width
        y: 10

        NumberAnimation on x {
            from: scrollingText.parent.width
            to: -scrollingText.width
            duration: 6000          // 6 seconds for a full pass
            loops: Animation.Infinite
        }
    }
}
