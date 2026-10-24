import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Adds an item, or changes one: its name, what kind of quantity it is, and
// the color its measurements are drawn in.
Dialog {
    id: dialog
    // The item being edited, or -1 for a new one.
    property int itemId: -1
    property string kind: "length"
    property string color: ""
    property bool confirmingDelete: false
    readonly property int measured: itemId >= 0 && takeoff.revision >= 0 ? takeoff.measurementCount(itemId, -1) : 0
    signal created(int id)

    title: itemId >= 0 ? qsTr("Edit item") : qsTr("New item")
    modal: true
    anchors.centerIn: parent
    width: Math.min(parent.width - 40, 440)
    Material.roundedScale: theme.radius
    Material.background: "#17171a"
    background: Rectangle {
        color: "#17171a"
        radius: Math.min(8, theme.radius)
        border.color: "#2a2a30"
    }
    Overlay.modal: Rectangle { color: "#99000000" }

    function create(kind) {
        itemId = -1
        dialog.kind = kind || "length"
        color = takeoff.nextColor()
        nameField.text = ""
        confirmingDelete = false
        open()
    }

    function edit(id) {
        const info = takeoff.itemInfo(id)
        if (!info.id)
            return
        itemId = id
        kind = info.kind
        color = info.color
        nameField.text = info.name
        confirmingDelete = false
        open()
    }

    // Deletes an item straight away when nothing's measured with it (undo
    // brings it back), and asks first when measurements would go with it.
    function remove(id) {
        if (takeoff.measurementCount(id, -1) === 0) {
            takeoff.removeItem(id)
            return
        }
        edit(id)
        confirmingDelete = true
    }

    onOpened: {
        nameField.forceActiveFocus()
        nameField.selectAll()
    }

    onAccepted: {
        if (itemId >= 0) {
            takeoff.updateItem(itemId, nameField.text, kind, color)
        } else {
            const id = takeoff.addItem(nameField.text, kind, color)
            if (id >= 0)
                created(id)
        }
    }

    footer: DialogButtonBox {
        background: Item {}
        Button {
            text: dialog.itemId >= 0 ? qsTr("Save") : qsTr("Add")
            flat: true
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
        Button {
            text: qsTr("Cancel")
            flat: true
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 14

        TextField {
            id: nameField
            Layout.fillWidth: true
            placeholderText: qsTr("Name, like Drywall or Concrete slab")
            onAccepted: dialog.accept()
        }

        ColumnLayout {
            spacing: 6
            Layout.fillWidth: true

            Label {
                text: qsTr("Measured as")
                color: "#b8b8c0"
                font.pixelSize: 12
            }
            RowLayout {
                spacing: 6
                Layout.fillWidth: true

                Repeater {
                    model: [
                        { kind: "length", text: qsTr("Length") },
                        { kind: "area", text: qsTr("Area") },
                        { kind: "count", text: qsTr("Count") }
                    ]
                    delegate: Button {
                        id: kindButton
                        required property var modelData
                        readonly property bool active: dialog.kind === modelData.kind
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        enabled: dialog.measured === 0 || active
                        topInset: 0
                        bottomInset: 0
                        implicitHeight: 40
                        background: Rectangle {
                            radius: Math.min(6, theme.radius)
                            color: kindButton.active ? Qt.alpha(theme.accent, 0.16) : kindButton.hovered ? "#24242a" : "#1c1c20"
                            border.color: kindButton.active ? theme.accent : "#2a2a30"
                        }
                        contentItem: Row {
                            spacing: 8
                            leftPadding: 4
                            KindGlyph {
                                kind: kindButton.modelData.kind
                                color: dialog.color || theme.accent
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Label {
                                text: kindButton.modelData.text
                                color: kindButton.enabled ? "#ececf0" : "#6a6a74"
                                font.pixelSize: 13
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                        onClicked: dialog.kind = modelData.kind
                    }
                }
            }
            Label {
                visible: dialog.measured > 0
                text: qsTr("Measured items keep their kind. Make a new item to measure this another way.")
                color: "#8a8a94"
                font.pixelSize: 11
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }

        ColumnLayout {
            spacing: 6

            Label {
                text: qsTr("Color")
                color: "#b8b8c0"
                font.pixelSize: 12
            }
            Flow {
                spacing: 6
                Layout.fillWidth: true
                Layout.preferredWidth: dialog.availableWidth

                Repeater {
                    model: takeoff.palette()
                    delegate: Rectangle {
                        required property string modelData
                        readonly property bool active: dialog.color.toUpperCase() === modelData.toUpperCase()
                        width: 28
                        height: 28
                        radius: 14
                        color: modelData
                        border.color: active ? "white" : "transparent"
                        border.width: 2

                        Rectangle {
                            visible: parent.active
                            anchors.centerIn: parent
                            width: 8
                            height: 8
                            radius: 4
                            color: "white"
                        }
                        TapHandler { onTapped: dialog.color = parent.modelData }
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                    }
                }
            }
        }

        // Deleting asks once more when measurements go with the item.
        RowLayout {
            visible: dialog.itemId >= 0
            Layout.fillWidth: true
            spacing: 8

            Label {
                visible: dialog.confirmingDelete
                text: qsTr("Delete it and its %n measurements?", "", dialog.measured)
                color: "#f4d0ce"
                font.pixelSize: 12
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Item { visible: !dialog.confirmingDelete; Layout.fillWidth: true }
            Button {
                text: dialog.confirmingDelete ? qsTr("Delete") : qsTr("Delete item…")
                flat: true
                Material.foreground: "#f08a84"
                onClicked: {
                    if (dialog.measured > 0 && !dialog.confirmingDelete) {
                        dialog.confirmingDelete = true
                        return
                    }
                    takeoff.removeItem(dialog.itemId)
                    dialog.close()
                }
            }
        }
    }
}
