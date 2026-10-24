import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Every shortcut, shown with ?.
Rectangle {
    id: help
    color: "#cc0b0b0d"

    readonly property var groups: [
        [
            ["O", qsTr("Open a drawing")],
            ["S", qsTr("Set the sheet's scale")],
            ["Shift+S", qsTr("Calibrate from a known dimension")],
            ["N", qsTr("New item")],
            ["1 – 9", qsTr("Measure that item")],
            ["M", qsTr("Measure the active item")],
            ["R", qsTr("Edit the active item")],
            ["H", qsTr("Hide or show the active item")],
            ["Alt+↑ / Alt+↓", qsTr("Move the active item up or down")],
            ["V", qsTr("Select, move, and reshape")],
            ["E", qsTr("Export quantities to CSV")],
            ["U", qsTr("Switch between feet and meters")],
            ["L", qsTr("Show or hide labels")],
            ["Ctrl+Z / Ctrl+Shift+Z", qsTr("Undo and redo")],
            ["?", qsTr("Show or hide shortcuts")],
            ["Q", qsTr("Quit")]
        ],
        [
            [qsTr("Click"), qsTr("Place a corner, or count one")],
            ["Space", qsTr("Place a corner under the pointer")],
            [qsTr("Double-click, Enter"), qsTr("Finish the line or area")],
            [qsTr("Right-click"), qsTr("Finish the line or area")],
            ["Backspace", qsTr("Take back the last corner")],
            ["Shift", qsTr("Keep to 45° steps")],
            ["Alt", qsTr("Don't snap to corners")],
            ["Esc", qsTr("Cancel, then back to selecting")],
            ["Delete", qsTr("Delete the selected measurement")],
            [qsTr("Double-click an edge"), qsTr("Add a corner to the selection")],
            [qsTr("Shift+click a corner"), qsTr("Remove it from the selection")],
            ["J / K", qsTr("Next and previous sheet")],
            ["+ / − / 0", qsTr("Zoom in, out, and fit")],
            [qsTr("Wheel, drag"), qsTr("Zoom at the pointer, pan")]
        ]
    ]

    TapHandler { onTapped: help.visible = false }

    Pane {
        anchors.centerIn: parent
        padding: 24
        Material.elevation: 8
        Material.roundedScale: theme.radius
        background: Rectangle { color: "#17171a"; radius: theme.radius; border.color: "#2a2a30" }

        ColumnLayout {
            spacing: 14

            Label {
                text: qsTr("Keyboard shortcuts")
                font.pixelSize: 18
                font.bold: true
            }

            RowLayout {
                spacing: 40

                Repeater {
                    model: help.groups
                    delegate: GridLayout {
                        id: group
                        required property var modelData
                        Layout.alignment: Qt.AlignTop
                        columns: 2
                        columnSpacing: 20
                        rowSpacing: 7

                        Repeater {
                            model: group.modelData.length * 2
                            delegate: Label {
                                required property int index
                                readonly property var entry: group.modelData[Math.floor(index / 2)]
                                text: index % 2 === 0 ? entry[0] : entry[1]
                                color: index % 2 === 0 ? theme.accent : "#d8d8de"
                                font.family: index % 2 === 0 ? "monospace" : Qt.application.font.family
                                font.pixelSize: 13
                            }
                        }
                    }
                }
            }
        }
    }
}
