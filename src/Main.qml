import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1440
    height: 880
    minimumWidth: 900
    minimumHeight: 520
    visible: true
    title: backend.fileName ? "Plumb — " + backend.fileName : "Plumb"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false
    readonly property bool hasDrawing: backend.sheetCount > 0
    // Letters type into fields while a dialog is up, rather than act.
    readonly property bool keysEnabled: hasDrawing && !itemDialog.visible && !scaleDialog.visible && !quitDialog.visible

    // "select", "measure" or "calibrate", and the item clicks measure.
    property string tool: "select"
    property int activeItem: -1
    readonly property var activeInfo: takeoff.revision >= 0 && activeItem >= 0 ? takeoff.itemInfo(activeItem) : ({})
    // The tool to go back to once calibrating is done.
    property string toolBeforeCalibrating: "select"
    property bool labels: true
    // Says how an export went, for a few seconds.
    property string notice: ""
    // Set once quitting is confirmed, since Qt.quit() closes the window and
    // would ask again.
    property bool quitting: false

    // Reads the language so bindings that format numbers re-render when
    // Omarchy's language changes.
    function number(value) {
        return translator.language !== "" ? value.toLocaleString(Qt.locale(), 'f', 0) : ""
    }

    function pickTool(next) {
        if (next === "measure" && activeItem < 0) {
            const items = takeoff.items()
            if (items.length === 0) {
                itemDialog.create()
                return
            }
            activeItem = items[0].id
        }
        if (next === "calibrate" && tool !== "calibrate")
            toolBeforeCalibrating = tool
        tool = next
        if (next !== "select")
            viewer.selected = -1
    }

    // Number keys and clicks in the list both land here.
    function activate(id) {
        if (activeItem === id && tool === "measure") {
            pickTool("select")
            return
        }
        activeItem = id
        pickTool("measure")
    }

    function activateIndex(index) {
        const items = takeoff.items()
        if (index < items.length)
            activate(items[index].id)
    }

    // Moves the active item up or down the list, and so to another number key.
    function moveActive(step) {
        const items = takeoff.items()
        const index = items.findIndex(item => item.id === activeItem)
        if (index >= 0)
            takeoff.moveItem(activeItem, index + step)
    }

    function deleteSelected() {
        if (viewer.selected >= 0) {
            takeoff.removeMeasurement(viewer.selected)
            viewer.selected = -1
        }
    }

    function quit() {
        if (takeoff.unexported && takeoff.measurementTotal > 0 && !quitting)
            quitDialog.open()
        else
            Qt.quit()
    }

    function showNotice(text) {
        notice = text
        noticeTimer.restart()
    }

    onClosing: (close) => {
        if (takeoff.unexported && takeoff.measurementTotal > 0 && !quitting) {
            close.accepted = false
            quitDialog.open()
        }
    }

    // Keeps the active item and the selection pointing at things that exist,
    // after undo, a delete, or another drawing.
    Connections {
        target: takeoff
        function onChanged() {
            if (win.activeItem >= 0 && !takeoff.itemInfo(win.activeItem).id) {
                const items = takeoff.items()
                win.activeItem = items.length > 0 ? items[0].id : -1
                if (win.activeItem < 0 && win.tool === "measure")
                    win.tool = "select"
            }
            if (win.activeItem < 0 && takeoff.itemCount > 0)
                win.activeItem = takeoff.items()[0].id
            const picked = takeoff.measurement(viewer.selected)
            if (viewer.selected >= 0 && (picked.id === undefined || picked.sheet !== backend.sheet))
                viewer.selected = -1
        }
    }
    Connections {
        target: backend
        function onSheetChanged() { viewer.selected = -1 }
        function onDocumentChanged() {
            const items = takeoff.items()
            win.activeItem = items.length > 0 ? items[0].id : -1
            win.tool = "select"
        }
    }
    Connections {
        target: exportJob
        function onFinished(path) {
            win.showNotice(qsTr("Exported %1").arg(path.substring(path.lastIndexOf("/") + 1)))
        }
    }

    Timer {
        id: noticeTimer
        interval: 4000
        onTriggered: win.notice = ""
    }

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; enabled: !itemDialog.visible && !scaleDialog.visible; onActivated: win.quit() }
    Shortcut { sequences: ["O", StandardKey.Open]; context: Qt.ApplicationShortcut; enabled: !itemDialog.visible && !scaleDialog.visible; onActivated: backend.openDrawing() }
    Shortcut { sequence: "S"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: scaleDialog.pick(0) }
    Shortcut { sequence: "Shift+S"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: win.pickTool("calibrate") }
    Shortcut { sequence: "N"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: itemDialog.create() }
    Shortcut { sequence: "M"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: win.pickTool("measure") }
    Shortcut { sequence: "V"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: win.pickTool("select") }
    Shortcut { sequence: "R"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled && win.activeItem >= 0; onActivated: itemDialog.edit(win.activeItem) }
    Shortcut {
        sequence: "H"
        context: Qt.ApplicationShortcut
        enabled: win.keysEnabled && win.activeItem >= 0
        onActivated: takeoff.setItemVisible(win.activeItem, !win.activeInfo.visible)
    }
    Shortcut { sequence: "Alt+Up"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: win.moveActive(-1) }
    Shortcut { sequence: "Alt+Down"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: win.moveActive(1) }
    Shortcut { sequence: "E"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: exportJob.start() }
    Shortcut { sequence: "U"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: takeoff.units = takeoff.units === "metric" ? "imperial" : "metric" }
    Shortcut { sequence: "L"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: win.labels = !win.labels }
    Repeater {
        model: 9
        delegate: Item {
            required property int index
            Shortcut {
                sequence: String(index + 1)
                context: Qt.ApplicationShortcut
                enabled: win.keysEnabled
                onActivated: win.activateIndex(index)
            }
        }
    }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: viewer.placeAtPointer() }
    Shortcut { sequences: ["Return", "Enter"]; context: Qt.ApplicationShortcut; enabled: win.keysEnabled && viewer.drafting; onActivated: viewer.finish() }
    Shortcut {
        sequence: "Backspace"
        context: Qt.ApplicationShortcut
        enabled: win.keysEnabled
        onActivated: viewer.drafting ? viewer.removeLastCorner() : win.deleteSelected()
    }
    Shortcut { sequences: [StandardKey.Delete]; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: win.deleteSelected() }
    Shortcut { sequences: ["J", "PgDown"]; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: backend.nextSheet() }
    Shortcut { sequences: ["K", "PgUp"]; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: backend.previousSheet() }
    Shortcut { sequence: "Home"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: backend.sheet = 0 }
    Shortcut { sequence: "End"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: backend.sheet = backend.sheetCount - 1 }
    Shortcut { sequences: ["+", "=", StandardKey.ZoomIn]; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: viewer.zoomBy(1.25) }
    Shortcut { sequences: ["-", StandardKey.ZoomOut]; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: viewer.zoomBy(0.8) }
    Shortcut { sequences: ["0", "F"]; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: viewer.fit() }
    Shortcut { sequence: "Left"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: viewer.pan(-80, 0) }
    Shortcut { sequence: "Right"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: viewer.pan(80, 0) }
    Shortcut { sequence: "Up"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: viewer.pan(0, -80) }
    Shortcut { sequence: "Down"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: viewer.pan(0, 80) }
    Shortcut {
        sequences: [StandardKey.Undo]
        context: Qt.ApplicationShortcut
        enabled: win.keysEnabled
        onActivated: viewer.drafting ? viewer.removeLastCorner() : takeoff.undo()
    }
    Shortcut { sequences: ["Ctrl+Shift+Z", "Ctrl+Y"]; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: takeoff.redo() }
    // Escape steps back one thing at a time: help, the measurement being
    // taken, the tool, the selection, then the error.
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        // Open dialogs close themselves on Escape.
        enabled: !itemDialog.visible && !scaleDialog.visible && !quitDialog.visible
        onActivated: {
            if (win.helpVisible)
                win.helpVisible = false
            else if (viewer.drafting)
                viewer.cancelDraft()
            else if (win.tool === "calibrate")
                win.pickTool(win.toolBeforeCalibrating)
            else if (win.tool !== "select")
                win.pickTool("select")
            else if (viewer.selected >= 0)
                viewer.selected = -1
            else
                backend.clearError()
        }
    }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // The bar: what's open, which sheet, its scale, and the tools.
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 52
                color: "#131316"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 8
                    spacing: 12

                    Label {
                        text: "Plumb"
                        font.pixelSize: 16
                        font.bold: true
                        color: theme.accent
                    }

                    Label {
                        visible: win.hasDrawing
                        text: backend.fileName
                        elide: Text.ElideMiddle
                        font.pixelSize: 13
                        Layout.fillWidth: true
                        Layout.minimumWidth: 60
                        Layout.maximumWidth: Math.max(60, implicitWidth)
                    }

                    Label {
                        visible: win.hasDrawing
                        text: qsTr("Sheet %1 · %2 of %3").arg(backend.sheetLabel(backend.sheet))
                            .arg(win.number(backend.sheet + 1))
                            .arg(win.number(backend.sheetCount))
                        color: "#b8b8c0"
                        font.pixelSize: 13
                    }

                    // The sheet's scale; click to change it.
                    Button {
                        id: scaleButton
                        visible: win.hasDrawing
                        readonly property string label: takeoff.revision >= 0 ? takeoff.scaleLabel(backend.sheet) : ""
                        text: label !== "" ? qsTr("Scale %1").arg(label) : qsTr("No scale: press S")
                        font.pixelSize: 12
                        leftPadding: 10
                        rightPadding: 10
                        topInset: 0
                        bottomInset: 0
                        topPadding: 0
                        bottomPadding: 0
                        implicitHeight: 28
                        Material.foreground: label !== "" ? "#d8d8de" : "#f0b45a"
                        background: Rectangle {
                            implicitHeight: 28
                            radius: Math.min(4, theme.radius)
                            color: scaleButton.hovered ? "#24242a" : "transparent"
                            border.color: scaleButton.label !== "" ? "#2a2a30" : "#a8761e"
                        }
                        onClicked: scaleDialog.pick(0)
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Set the sheet's scale (S)")
                    }

                    Item { Layout.fillWidth: true }

                    // Selecting, or measuring the active item.
                    Row {
                        visible: win.hasDrawing
                        spacing: 0

                        Repeater {
                            model: [
                                { tool: "select", text: qsTr("Select"), key: "V" },
                                { tool: "measure", text: qsTr("Measure"), key: "M" }
                            ]
                            delegate: Button {
                                id: toolButton
                                required property var modelData
                                readonly property bool active: win.tool === modelData.tool
                                text: modelData.text
                                font.pixelSize: 13
                                topInset: 0
                                bottomInset: 0
                                topPadding: 0
                                bottomPadding: 0
                                leftPadding: 14
                                rightPadding: 14
                                implicitHeight: 32
                                Material.foreground: active ? theme.accentForeground : "#d8d8de"
                                background: Rectangle {
                                    implicitHeight: 32
                                    radius: Math.min(4, theme.radius)
                                    color: toolButton.active ? theme.accent : toolButton.hovered ? "#24242a" : "transparent"
                                }
                                onClicked: win.pickTool(modelData.tool)
                                ToolTip.visible: hovered
                                ToolTip.text: "%1 (%2)".arg(modelData.text).arg(modelData.key)
                            }
                        }
                    }

                    ToolButton {
                        text: qsTr("Open")
                        onClicked: backend.openDrawing()
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Open a drawing (O)")
                    }
                    ToolButton {
                        text: "?"
                        onClicked: win.helpVisible = !win.helpVisible
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Keyboard shortcuts (?)")
                    }
                }

                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#222228" }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                SheetStrip {
                    visible: win.hasDrawing
                    Layout.preferredWidth: 180
                    Layout.fillHeight: true
                }

                Rectangle {
                    visible: win.hasDrawing
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    color: "#222228"
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    SheetViewer {
                        id: viewer
                        objectName: "viewer"
                        anchors.fill: parent
                        visible: win.hasDrawing
                        tool: win.tool
                        activeItem: win.activeItem
                        activeKind: win.activeInfo.kind || ""
                        activeColor: win.activeInfo.color || theme.accent
                        labels: win.labels
                        onCalibrated: (length) => {
                            win.pickTool(win.toolBeforeCalibrating)
                            scaleDialog.pick(length)
                        }
                        onItemNeeded: itemDialog.create()
                    }

                    // The measurement picked: what it is, how much, and
                    // what can be done with it.
                    Rectangle {
                        id: selection
                        readonly property var info: takeoff.revision >= 0 && viewer.selected >= 0 ? takeoff.measurement(viewer.selected) : ({})
                        readonly property var owner: info.item !== undefined ? takeoff.itemInfo(info.item) : ({})
                        readonly property var siblings: info.kind
                            ? takeoff.items().filter(item => item.kind === info.kind) : []
                        visible: win.hasDrawing && info.id !== undefined
                        anchors.top: parent.top
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.topMargin: 12
                        width: selectionRow.implicitWidth + 20
                        height: 44
                        radius: Math.min(6, theme.radius)
                        color: "#131316"
                        border.color: selection.owner.color || "#2a2a30"

                        RowLayout {
                            id: selectionRow
                            anchors.centerIn: parent
                            spacing: 10

                            KindGlyph {
                                kind: selection.info.kind || "length"
                                color: selection.owner.color || "white"
                            }
                            ComboBox {
                                id: itemBox
                                Layout.preferredWidth: Math.min(220, Math.max(140, implicitContentWidth + 50))
                                Layout.preferredHeight: 34
                                topInset: 0
                                bottomInset: 0
                                font.pixelSize: 13
                                model: selection.siblings
                                textRole: "name"
                                currentIndex: selection.siblings.findIndex(item => item.id === selection.info.item)
                                onActivated: (index) => takeoff.setMeasurementItem(viewer.selected, selection.siblings[index].id)
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Move to another item")
                            }
                            Label {
                                visible: selection.info.kind !== "count"
                                text: selection.info.text || ""
                                font.pixelSize: 13
                                font.bold: true
                                font.features: { "tnum": 1 }
                            }
                            RowButton {
                                text: qsTr("Delete")
                                implicitHeight: 28
                                font.pixelSize: 12
                                Material.foreground: "#f08a84"
                                onClicked: win.deleteSelected()
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Delete the measurement (Delete)")
                            }
                        }
                    }

                    // An export that finished.
                    Rectangle {
                        visible: win.notice !== "" && !selection.visible
                        anchors.top: parent.top
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.topMargin: 12
                        width: noticeLabel.implicitWidth + 24
                        height: noticeLabel.implicitHeight + 12
                        radius: Math.min(4, theme.radius)
                        color: "#131316"
                        border.color: theme.accent

                        Label {
                            id: noticeLabel
                            anchors.centerIn: parent
                            font.pixelSize: 13
                            text: win.notice
                        }
                    }

                    // What's being measured, and how.
                    Rectangle {
                        visible: win.hasDrawing && win.tool !== "select"
                        anchors.bottom: parent.bottom
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottomMargin: 12
                        width: Math.min(parent.width - 140, hintRow.implicitWidth + 28)
                        height: hintRow.implicitHeight + 14
                        radius: Math.min(6, theme.radius)
                        color: "#131316"
                        border.color: win.tool === "calibrate" ? theme.accent : (win.activeInfo.color || theme.accent)

                        RowLayout {
                            id: hintRow
                            anchors.fill: parent
                            anchors.leftMargin: 14
                            anchors.rightMargin: 14
                            spacing: 10

                            KindGlyph {
                                visible: win.tool === "measure"
                                kind: win.activeInfo.kind || "length"
                                color: win.activeInfo.color || theme.accent
                            }
                            Label {
                                visible: win.tool === "measure"
                                text: win.activeInfo.name || ""
                                font.pixelSize: 13
                                font.bold: true
                            }
                            Label {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                font.pixelSize: 12
                                color: "#b8b8c0"
                                text: {
                                    if (win.tool === "calibrate")
                                        return qsTr("Click both ends of a dimension you know the length of.")
                                    switch (win.activeInfo.kind) {
                                    case "length": return viewer.drafting
                                        ? qsTr("Keep clicking corners. Double-click, Enter, or right-click to finish.")
                                        : qsTr("Click where the line starts.")
                                    case "area": return viewer.drafting
                                        ? qsTr("Keep clicking corners. Click the first one, or press Enter, to close it.")
                                        : qsTr("Click the first corner of the area.")
                                    case "count": return qsTr("Click each one to count it.")
                                    }
                                    return ""
                                }
                            }
                        }
                    }

                    // The zoom level.
                    Rectangle {
                        visible: win.hasDrawing
                        anchors.left: parent.left
                        anchors.bottom: parent.bottom
                        anchors.margins: 12
                        width: zoomLabel.implicitWidth + 20
                        height: zoomLabel.implicitHeight + 12
                        radius: Math.min(4, theme.radius)
                        color: "#131316"
                        border.color: "#2a2a30"

                        Label {
                            id: zoomLabel
                            anchors.centerIn: parent
                            text: qsTr("%1%").arg(win.number(Math.round(viewer.zoom * 100 / viewer.fitZoom)))
                            color: "#b8b8c0"
                            font.pixelSize: 12
                        }
                    }

                    ColumnLayout {
                        visible: !win.hasDrawing
                        anchors.centerIn: parent
                        spacing: 14

                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("Open a drawing")
                            font.pixelSize: 22
                            font.bold: true
                        }
                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("Press O, or drop a PDF here.")
                            color: "#8a8a94"
                        }
                        AccentButton {
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("Open drawing")
                            onClicked: backend.openDrawing()
                        }
                    }
                }

                Rectangle {
                    visible: win.hasDrawing
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    color: "#222228"
                }

                ItemPanel {
                    visible: win.hasDrawing
                    Layout.preferredWidth: 400
                    Layout.fillHeight: true
                    activeItem: win.activeItem
                    onActivated: (id) => win.activate(id)
                    onEditRequested: (id) => itemDialog.edit(id)
                    onDeleteRequested: (id) => itemDialog.remove(id)
                    onNewRequested: itemDialog.create()
                    onSheetRequested: (sheet) => backend.sheet = sheet
                    onScaleRequested: scaleDialog.pick(0)
                }
            }
        }

        // What went wrong, until it's dismissed or the next thing works.
        Rectangle {
            visible: backend.error !== ""
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottomMargin: 64
            width: Math.min(parent.width - 40, errorRow.implicitWidth + 24)
            height: errorRow.implicitHeight + 16
            radius: Math.min(6, theme.radius)
            color: "#2a1414"
            border.color: "#E0302A"

            RowLayout {
                id: errorRow
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 6
                spacing: 8

                Label {
                    Layout.fillWidth: true
                    text: backend.error
                    color: "#f4d0ce"
                    wrapMode: Text.Wrap
                }
                ToolButton {
                    text: "✕"
                    onClicked: backend.clearError()
                }
            }
        }

        DropArea {
            id: drop
            anchors.fill: parent
            keys: ["text/uri-list"]
            onDropped: (event) => {
                if (event.hasUrls)
                    backend.load(event.urls[0])
            }
        }

        Rectangle {
            anchors.fill: parent
            visible: drop.containsDrag
            color: "#dd0b0b0d"

            Rectangle {
                anchors.fill: parent
                anchors.margins: 24
                radius: theme.radius
                color: Qt.alpha(theme.accent, 0.12)
                border.color: theme.accent
                border.width: 2

                Label {
                    anchors.centerIn: parent
                    text: qsTr("Open this drawing")
                    font.pixelSize: 18
                    color: theme.accent
                }
            }
        }

        ScaleDialog {
            id: scaleDialog
            objectName: "scaleDialog"
            sheet: backend.sheet
            onCalibrateRequested: win.pickTool("calibrate")
        }

        ItemDialog {
            id: itemDialog
            objectName: "itemDialog"
            onCreated: (id) => {
                win.activeItem = id
                win.pickTool("measure")
            }
        }

        // Quitting with quantities that haven't been exported since they
        // changed. The takeoff itself is saved; the CSV may be behind.
        Dialog {
            id: quitDialog
            objectName: "quitDialog"
            title: qsTr("Export the quantities first?")
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

            ColumnLayout {
                anchors.fill: parent

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Your takeoff is saved, but the quantities changed since they were last exported to CSV.")
                    color: "#b8b8c0"
                    wrapMode: Text.Wrap
                }
            }

            footer: DialogButtonBox {
                background: Item {}
                Button {
                    text: qsTr("Export…")
                    flat: true
                    onClicked: {
                        quitDialog.close()
                        exportJob.start()
                    }
                }
                Button {
                    text: qsTr("Quit")
                    flat: true
                    Material.foreground: "#f08a84"
                    onClicked: {
                        win.quitting = true
                        Qt.quit()
                    }
                }
                Button {
                    text: qsTr("Cancel")
                    flat: true
                    onClicked: quitDialog.close()
                }
            }
        }

        ShortcutHelp {
            anchors.fill: parent
            visible: win.helpVisible
            onVisibleChanged: win.helpVisible = visible
        }
    }
}
