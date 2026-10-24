import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A small text button that shows over a list row on hover.
Button {
    id: button
    flat: true
    font.pixelSize: 11
    leftPadding: 8
    rightPadding: 8
    topInset: 0
    bottomInset: 0
    topPadding: 0
    bottomPadding: 0
    implicitHeight: 22
    Material.foreground: "#d8d8de"
    background: Rectangle {
        radius: Math.min(3, theme.radius)
        color: button.hovered ? "#2e2e36" : "#24242a"
    }
}
