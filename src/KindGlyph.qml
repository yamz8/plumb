import QtQuick

// What kind of quantity an item is, in its color: a line for a length, a
// filled square for an area, a dot for a count.
Item {
    id: glyph
    property string kind: "length"
    property color color: "white"
    implicitWidth: 18
    implicitHeight: 18

    Rectangle {
        visible: glyph.kind === "length"
        anchors.centerIn: parent
        width: 16
        height: 4
        radius: 2
        color: glyph.color
    }
    Rectangle {
        visible: glyph.kind === "area"
        anchors.centerIn: parent
        width: 14
        height: 14
        radius: Math.min(2, theme.radius)
        color: Qt.alpha(glyph.color, 0.35)
        border.color: glyph.color
        border.width: 2
    }
    Rectangle {
        visible: glyph.kind === "count"
        anchors.centerIn: parent
        width: 12
        height: 12
        radius: 6
        color: glyph.color
        border.color: "white"
        border.width: 1.5
    }
}
