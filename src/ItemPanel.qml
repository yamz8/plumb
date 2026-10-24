import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The items being taken off, each with its running totals on this sheet and
// on every sheet, and below them where the quantities go.
Rectangle {
    id: panel
    property int activeItem: -1
    signal activated(int id)
    signal editRequested(int id)
    signal deleteRequested(int id)
    signal newRequested()
    signal sheetRequested(int sheet)
    signal scaleRequested()

    color: "#131316"

    // Re-read whenever the takeoff, the sheet, or the language changes.
    readonly property int revision: {
        backend.sheet
        translator.language
        return takeoff.revision
    }
    readonly property var items: revision >= 0 ? takeoff.items() : []
    readonly property int unscaled: revision >= 0 ? takeoff.unscaledCount(-1) : 0
    property var expanded: ({})

    function toggleExpanded(id) {
        const next = Object.assign({}, expanded)
        next[id] = !next[id]
        expanded = next
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 8
            Layout.topMargin: 10
            Layout.bottomMargin: 6
            spacing: 8

            Label {
                text: qsTr("Items")
                font.pixelSize: 15
                font.bold: true
                Layout.fillWidth: true
            }
            ToolButton {
                text: qsTr("+ New item")
                font.pixelSize: 13
                onClicked: panel.newRequested()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Add an item to measure (N)")
            }
        }

        // Column heads over the two totals.
        RowLayout {
            visible: panel.items.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 4
            Layout.bottomMargin: 4
            spacing: 8

            Item { Layout.fillWidth: true }
            Label {
                text: qsTr("This sheet")
                color: "#8a8a94"
                font.pixelSize: 11
                horizontalAlignment: Text.AlignRight
                Layout.preferredWidth: 86
            }
            Label {
                text: qsTr("All sheets")
                color: "#8a8a94"
                font.pixelSize: 11
                horizontalAlignment: Text.AlignRight
                Layout.preferredWidth: 86
            }
            // Over the rows' menu buttons.
            Item { Layout.preferredWidth: 26 }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#222228" }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: panel.items
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            delegate: Column {
                id: row
                required property var modelData
                required property int index
                readonly property bool active: modelData.id === panel.activeItem
                readonly property bool open: panel.expanded[modelData.id] === true
                readonly property int count: panel.revision >= 0 ? takeoff.measurementCount(modelData.id, -1) : 0
                readonly property int unscaled: panel.revision >= 0 ? takeoff.unscaledCount(modelData.id) : 0
                readonly property bool mirrored: Qt.application.layoutDirection === Qt.RightToLeft
                width: ListView.view.width

                Rectangle {
                    width: parent.width
                    height: 52
                    color: row.active ? Qt.alpha(theme.accent, 0.12) : hover.hovered ? "#1c1c20" : "transparent"

                    // The active item: the one clicks on the plan measure.
                    Rectangle {
                        visible: row.active
                        width: 3
                        height: parent.height
                        color: theme.accent
                    }

                    HoverHandler { id: hover }
                    // The arrow and the swatch at the start of the row
                    // handle their own taps; the rest picks the item.
                    TapHandler {
                        onTapped: (point) => {
                            if (row.mirrored ? point.position.x < row.width - 52 : point.position.x > 52)
                                panel.activated(row.modelData.id)
                        }
                        onDoubleTapped: (point) => {
                            if (row.mirrored ? point.position.x < row.width - 52 : point.position.x > 52)
                                panel.editRequested(row.modelData.id)
                        }
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 4
                        spacing: 8

                        // Opens the per-sheet breakdown.
                        Label {
                            text: row.open ? "▾" : "▸"
                            color: row.count > 0 ? "#b8b8c0" : "#3a3a42"
                            font.pixelSize: 12
                            Layout.preferredWidth: 10
                            TapHandler {
                                enabled: row.count > 0
                                onTapped: panel.toggleExpanded(row.modelData.id)
                            }
                        }

                        // The swatch shows and hides the item's marks.
                        KindGlyph {
                            kind: row.modelData.kind
                            color: row.modelData.color
                            opacity: row.modelData.visible ? 1 : 0.35
                            HoverHandler { id: glyphHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: takeoff.setItemVisible(row.modelData.id, !row.modelData.visible) }
                            ToolTip.visible: glyphHover.hovered
                            ToolTip.text: row.modelData.visible ? qsTr("Hide") : qsTr("Show")
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1

                            RowLayout {
                                spacing: 6
                                Layout.fillWidth: true
                                Label {
                                    text: row.modelData.name
                                    elide: Text.ElideRight
                                    font.pixelSize: 13
                                    color: row.modelData.visible ? "#ececf0" : "#8a8a94"
                                    Layout.fillWidth: true
                                    Layout.maximumWidth: implicitWidth
                                }
                                Label {
                                    visible: row.modelData.key !== ""
                                    text: row.modelData.key
                                    font.family: "monospace"
                                    font.pixelSize: 11
                                    color: theme.accent
                                }
                                Item { Layout.fillWidth: true }
                            }
                            Label {
                                text: row.unscaled > 0
                                    ? qsTr("%n not counted: no scale", "", row.unscaled)
                                    : row.modelData.visible
                                        ? qsTr("%n measurements", "", row.count)
                                        : qsTr("Hidden · %n measurements", "", row.count)
                                color: row.unscaled > 0 ? "#f0b45a" : "#8a8a94"
                                font.pixelSize: 11
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }

                        // This sheet: a dash when the item isn't on it, and no
                        // number when the sheet can't give one.
                        Label {
                            readonly property int here: panel.revision >= 0 ? takeoff.measurementCount(row.modelData.id, backend.sheet) : 0
                            readonly property bool unscaled: here > 0 && row.modelData.kind !== "count" && !takeoff.hasScale(backend.sheet)
                            text: here === 0 ? "—" : unscaled ? qsTr("No scale") : takeoff.quantityText(row.modelData.id, backend.sheet)
                            color: unscaled ? "#f0b45a" : here === 0 ? "#5a5a64" : "#d8d8de"
                            font.pixelSize: 12
                            font.features: { "tnum": 1 }
                            horizontalAlignment: Text.AlignRight
                            elide: Text.ElideLeft
                            Layout.preferredWidth: 86
                        }
                        Label {
                            text: panel.revision >= 0 ? takeoff.quantityText(row.modelData.id, -1) : ""
                            color: row.active ? theme.accent : "#ececf0"
                            font.pixelSize: 12
                            font.bold: true
                            font.features: { "tnum": 1 }
                            horizontalAlignment: Text.AlignRight
                            elide: Text.ElideLeft
                            Layout.preferredWidth: 86
                        }

                        ToolButton {
                            id: more
                            text: "⋯"
                            font.pixelSize: 14
                            implicitWidth: 26
                            implicitHeight: 26
                            padding: 0
                            topInset: 0
                            bottomInset: 0
                            Material.foreground: hover.hovered || menu.visible ? "#d8d8de" : "#5a5a64"
                            onClicked: menu.popup(more, 0, more.height)

                            Menu {
                                id: menu
                                font.pixelSize: 13
                                Material.roundedScale: theme.radius
                                background: Rectangle {
                                    implicitWidth: 180
                                    color: "#1c1c20"
                                    radius: Math.min(6, theme.radius)
                                    border.color: "#2a2a30"
                                }
                                MenuItem {
                                    implicitHeight: 34
                                    text: qsTr("Edit…")
                                    onTriggered: panel.editRequested(row.modelData.id)
                                }
                                MenuItem {
                                    implicitHeight: 34
                                    text: row.modelData.visible ? qsTr("Hide") : qsTr("Show")
                                    onTriggered: takeoff.setItemVisible(row.modelData.id, !row.modelData.visible)
                                }
                                MenuItem {
                                    implicitHeight: 34
                                    text: qsTr("Move up")
                                    enabled: row.index > 0
                                    onTriggered: takeoff.moveItem(row.modelData.id, row.index - 1)
                                }
                                MenuItem {
                                    implicitHeight: 34
                                    text: qsTr("Move down")
                                    enabled: row.index < panel.items.length - 1
                                    onTriggered: takeoff.moveItem(row.modelData.id, row.index + 1)
                                }
                                MenuItem {
                                    implicitHeight: 34
                                    text: qsTr("Delete…")
                                    Material.foreground: "#f08a84"
                                    onTriggered: panel.deleteRequested(row.modelData.id)
                                }
                            }
                        }
                    }
                }

                // Where the item is measured, sheet by sheet.
                Repeater {
                    model: row.open && panel.revision >= 0 ? takeoff.sheetBreakdown(row.modelData.id) : []
                    delegate: Rectangle {
                        required property var modelData
                        width: row.width
                        height: 26
                        color: sheetHover.hovered ? "#1c1c20" : "transparent"

                        HoverHandler { id: sheetHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: panel.sheetRequested(modelData.sheet) }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 50
                            anchors.rightMargin: 38
                            spacing: 8

                            Label {
                                text: backend.sheetLabel(modelData.sheet)
                                color: modelData.sheet === backend.sheet ? theme.accent : "#b8b8c0"
                                font.pixelSize: 12
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label {
                                text: qsTr("%n measurements", "", modelData.count)
                                color: "#8a8a94"
                                font.pixelSize: 11
                            }
                            Label {
                                text: modelData.text
                                color: modelData.scaled ? "#d8d8de" : "#f0b45a"
                                font.pixelSize: 12
                                font.features: { "tnum": 1 }
                                horizontalAlignment: Text.AlignRight
                                Layout.preferredWidth: 86
                            }
                        }
                    }
                }

                Rectangle { width: parent.width; height: 1; color: "#1c1c20" }
            }

            // Nothing to measure yet.
            ColumnLayout {
                visible: panel.items.length === 0
                anchors.centerIn: parent
                width: parent.width - 48
                spacing: 12

                Label {
                    text: qsTr("No items yet")
                    font.pixelSize: 15
                    font.bold: true
                    Layout.alignment: Qt.AlignHCenter
                }
                Label {
                    text: qsTr("Add an item for each thing you're counting up, like drywall, a slab, or doors. Then click on the plan to measure it.")
                    color: "#8a8a94"
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                    Layout.fillWidth: true
                }
                AccentButton {
                    text: qsTr("New item")
                    Layout.alignment: Qt.AlignHCenter
                    onClicked: panel.newRequested()
                }
            }
        }

        // Measurements that aren't in the totals, and how to fix it.
        Rectangle {
            visible: panel.unscaled > 0
            Layout.fillWidth: true
            Layout.margins: 12
            Layout.preferredHeight: warning.implicitHeight + 16
            radius: Math.min(4, theme.radius)
            color: "#2a2214"
            border.color: "#a8761e"

            RowLayout {
                id: warning
                anchors.fill: parent
                anchors.margins: 8
                anchors.leftMargin: 12
                spacing: 8

                Label {
                    text: qsTr("%n measurements sit on sheets with no scale and aren't in the totals.", "", panel.unscaled)
                    color: "#f4dfb8"
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                Button {
                    text: qsTr("Set scale")
                    flat: true
                    font.pixelSize: 12
                    onClicked: panel.scaleRequested()
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#222228" }

        // How quantities read, and where they go.
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            spacing: 8

            Row {
                spacing: 0
                Repeater {
                    model: [
                        { units: "imperial", text: qsTr("Feet") },
                        { units: "metric", text: qsTr("Meters") }
                    ]
                    delegate: Button {
                        required property var modelData
                        readonly property bool active: takeoff.units === modelData.units
                        text: modelData.text
                        font.pixelSize: 12
                        enabled: backend.sheetCount > 0
                        Material.foreground: active ? theme.accentForeground : "#d8d8de"
                        background: Rectangle {
                            implicitHeight: 30
                            radius: Math.min(4, theme.radius)
                            color: parent.active ? theme.accent : parent.hovered ? "#24242a" : "#1c1c20"
                        }
                        onClicked: takeoff.units = modelData.units
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Units for totals and the CSV (U)")
                    }
                }
            }

            Item { Layout.fillWidth: true }

            AccentButton {
                text: qsTr("Export CSV")
                enabled: takeoff.measurementTotal > 0
                onClicked: exportJob.start()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Export the quantities to a spreadsheet (E)")
            }
        }
    }
}
