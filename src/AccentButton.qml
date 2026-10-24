import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// The main action where it appears, in the theme's accent.
Button {
    id: button
    leftPadding: 16
    rightPadding: 16
    topInset: 0
    bottomInset: 0
    topPadding: 0
    bottomPadding: 0
    font.pixelSize: 13
    implicitHeight: 32
    Material.foreground: enabled ? theme.accentForeground : "#6a6a74"
    background: Rectangle {
        implicitHeight: 32
        radius: Math.min(6, theme.radius)
        color: !button.enabled ? "#24242a" : button.down ? Qt.darker(theme.accent, 1.15)
             : button.hovered ? Qt.lighter(theme.accent, 1.08) : theme.accent
    }
}
