import QtQuick
import QtQuick.Controls

Rectangle {
  width: 480; height: 320
  color: "black"
  Text {
    anchors.centerIn: parent
    text: "Hello, Pi + Qt Quick!"
    color: "white"
    font.pixelSize: 28
  }
}
