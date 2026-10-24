import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import Plumb

// One sheet on paper, zoomed and panned, with its measurements over it. The
// image on screen scales at once and the sheet re-renders at the new
// resolution once the zoom settles, so zooming stays smooth on large sheets.
Item {
    id: viewer

    readonly property size points: backend.sheetSize
    readonly property real margin: 24
    readonly property real fitZoom: points.width > 0
        ? Math.max(0.01, Math.min((width - 2 * margin) / points.width, (height - 2 * margin) / points.height))
        : 1
    readonly property real minZoom: fitZoom / 2
    readonly property real maxZoom: Math.max(fitZoom * 2, 16)
    // The base image never renders bigger than this; past it, sharp tiles
    // of just the visible part are drawn over it.
    readonly property int baseEdge: 3072
    readonly property int tileEdge: 512
    readonly property size fullSize: Qt.size(Math.round(points.width * renderZoom * Screen.devicePixelRatio),
                                             Math.round(points.height * renderZoom * Screen.devicePixelRatio))
    readonly property bool tiled: Math.max(fullSize.width, fullSize.height) > baseEdge
    property var tiles: []
    // Logical pixels per PDF point.
    property real zoom: 1
    property real renderZoom: 1
    property bool fitted: true
    readonly property bool loading: sheetImage.status === Image.Loading

    // "select" to look around and pick measurements, "measure" to take them
    // for the active item, "calibrate" to draw along a known dimension.
    property string tool: "select"
    property int activeItem: -1
    property string activeKind: ""
    property color activeColor: theme.accent
    property bool labels: true
    property int selected: -1
    // The layer's own takeoff property hides the app's inside it.
    readonly property QtObject sheetTakeoff: takeoff
    // The corners of the measurement being taken, in sheet points.
    property var draft: []
    property point cursor: Qt.point(0, 0)
    property bool cursorOnSheet: false
    readonly property bool drafting: draft.length > 0
    readonly property real pixelsPerPoint: points.width > 0 ? paper.width / points.width : 1
    readonly property point origin: Qt.point(paper.x - flick.contentX, paper.y - flick.contentY)
    // A calibration line was drawn, this many points long.
    signal calibrated(real length)
    // Asks for an item to measure, when there is none to measure with.
    signal itemNeeded()

    function toSheet(x, y) {
        return Qt.point((x - origin.x) / pixelsPerPoint, (y - origin.y) / pixelsPerPoint)
    }
    function toView(p) {
        return Qt.point(origin.x + p.x * pixelsPerPoint, origin.y + p.y * pixelsPerPoint)
    }

    function clampZoom(value) { return Math.max(minZoom, Math.min(maxZoom, value)) }

    function updateTiles() {
        if (!tiled || paper.width <= 0) {
            tiles = []
            return
        }
        // The visible part of the sheet in full-size pixels, plus a tile of
        // margin so panning a little shows sharp tiles at once.
        const scale = fullSize.width / paper.width
        const left = Math.max(0, (flick.contentX - paper.x) * scale - tileEdge)
        const top = Math.max(0, (flick.contentY - paper.y) * scale - tileEdge)
        const right = Math.min(fullSize.width, (flick.contentX - paper.x + flick.width) * scale + tileEdge)
        const bottom = Math.min(fullSize.height, (flick.contentY - paper.y + flick.height) * scale + tileEdge)
        const list = []
        for (let y = Math.floor(top / tileEdge) * tileEdge; y < bottom; y += tileEdge)
            for (let x = Math.floor(left / tileEdge) * tileEdge; x < right; x += tileEdge)
                list.push({ x: x, y: y, w: Math.min(tileEdge, fullSize.width - x), h: Math.min(tileEdge, fullSize.height - y) })
        tiles = list
    }

    function fit() {
        zoom = fitZoom
        renderZoom = zoom
        tileTimer.restart()
        fitted = true
        flick.contentX = 0
        flick.contentY = 0
    }

    // Zooms keeping the sheet point under (vx, vy) in the viewport still.
    function zoomAt(value, vx, vy) {
        value = clampZoom(value)
        if (value === zoom)
            return
        const px = (flick.contentX + vx - paper.x) / zoom
        const py = (flick.contentY + vy - paper.y) / zoom
        zoom = value
        fitted = false
        flick.contentX = Math.max(0, Math.min(px * zoom + paper.x - vx, flick.contentWidth - flick.width))
        flick.contentY = Math.max(0, Math.min(py * zoom + paper.y - vy, flick.contentHeight - flick.height))
    }

    // Zooms at the pointer when it's over the sheet, else at the middle.
    function zoomBy(factor) {
        if (pointer.containsMouse)
            zoomAt(zoom * factor, pointer.mouseX, pointer.mouseY)
        else
            zoomAt(zoom * factor, width / 2, height / 2)
    }

    function pan(dx, dy) {
        flick.contentX = Math.max(0, Math.min(flick.contentX + dx, flick.contentWidth - flick.width))
        flick.contentY = Math.max(0, Math.min(flick.contentY + dy, flick.contentHeight - flick.height))
    }

    // Brings a measurement into view, zoomed so it fills about half of it.
    function show(id) {
        const m = takeoff.measurement(id)
        if (!m.points || points.width <= 0)
            return
        let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity
        for (const p of m.points) {
            x0 = Math.min(x0, p.x); y0 = Math.min(y0, p.y)
            x1 = Math.max(x1, p.x); y1 = Math.max(y1, p.y)
        }
        const w = Math.max(x1 - x0, 40), h = Math.max(y1 - y0, 40)
        zoom = clampZoom(Math.min(zoom, width * 0.55 / w, height * 0.55 / h))
        renderZoom = zoom
        fitted = false
        const cx = paper.x + (x0 + x1) / 2 * zoom
        const cy = paper.y + (y0 + y1) / 2 * zoom
        flick.contentX = Math.max(0, Math.min(cx - width / 2, flick.contentWidth - flick.width))
        flick.contentY = Math.max(0, Math.min(cy - height / 2, flick.contentHeight - flick.height))
        tileTimer.restart()
    }

    // Where a click at (x, y) lands: on a corner of another measurement when
    // it's close, and along 45° steps from the last corner with Shift held.
    function placeAt(x, y, modifiers) {
        let p = toSheet(x, y)
        if ((modifiers & Qt.ShiftModifier) && draft.length > 0) {
            const last = draft[draft.length - 1]
            const dx = p.x - last.x, dy = p.y - last.y
            const length = Math.hypot(dx, dy)
            const angle = Math.round(Math.atan2(dy, dx) / (Math.PI / 4)) * (Math.PI / 4)
            return Qt.point(last.x + Math.cos(angle) * length, last.y + Math.sin(angle) * length)
        }
        if (!(modifiers & Qt.AltModifier))
            p = takeoff.snap(backend.sheet, p, 8 / pixelsPerPoint, -1)
        return p
    }

    // Whether the pointer is on an area's first corner, so a click closes it.
    function closesArea(x, y) {
        if (activeKind !== "area" || draft.length < 3)
            return false
        const first = toView(draft[0])
        return Math.hypot(first.x - x, first.y - y) <= 10
    }

    function addCorner(x, y, modifiers) {
        if (tool === "calibrate") {
            const p = placeAt(x, y, modifiers)
            if (draft.length === 0) {
                draft = [p]
                return
            }
            const start = draft[0]
            const length = Math.hypot(p.x - start.x, p.y - start.y)
            cancelDraft()
            if (length > 1)
                calibrated(length)
            return
        }
        if (activeItem < 0) {
            itemNeeded()
            return
        }
        if (activeKind === "count") {
            takeoff.addMeasurement(activeItem, backend.sheet, [placeAt(x, y, modifiers)])
            return
        }
        if (closesArea(x, y)) {
            finish()
            return
        }
        const p = placeAt(x, y, modifiers)
        const last = draft.length > 0 ? toView(draft[draft.length - 1]) : null
        // A second click on the same spot is no new corner.
        if (last && Math.hypot(last.x - x, last.y - y) < 3)
            return
        draft = draft.concat([p])
    }

    // Places a corner, or counts one, under the pointer: Space.
    function placeAtPointer() {
        if (tool !== "select" && pointer.containsMouse)
            addCorner(pointer.mouseX, pointer.mouseY, 0)
    }

    function finish() {
        if (tool !== "measure" || activeItem < 0)
            return
        const needed = activeKind === "area" ? 3 : 2
        if (draft.length >= needed)
            takeoff.addMeasurement(activeItem, backend.sheet, draft)
        cancelDraft()
    }

    function removeLastCorner() {
        if (draft.length > 0)
            draft = draft.slice(0, draft.length - 1)
    }

    function cancelDraft() {
        draft = []
        layer.draft = ({})
    }

    function updateDraft() {
        if (draft.length === 0) {
            // Only when there was one, so moving the pointer doesn't repaint.
            if (layer.draft.kind !== undefined)
                layer.draft = ({})
            return
        }
        const state = { kind: tool === "calibrate" ? "calibrate" : activeKind, color: activeColor.toString(), points: draft }
        if (cursorOnSheet) {
            state.cursor = cursor
            state.closing = closesArea(pointer.mouseX, pointer.mouseY)
        }
        layer.draft = state
    }

    onDraftChanged: updateDraft()
    onCursorChanged: updateDraft()
    onCursorOnSheetChanged: updateDraft()
    onToolChanged: cancelDraft()
    onActiveItemChanged: cancelDraft()
    onZoomChanged: settle.restart()
    onFitZoomChanged: if (fitted) fit()

    Connections {
        target: backend
        function onSheetChanged() {
            viewer.cancelDraft()
            Qt.callLater(viewer.fit)
        }
        function onDocumentChanged() { Qt.callLater(viewer.fit) }
    }

    Timer {
        id: settle
        interval: 180
        onTriggered: {
            viewer.renderZoom = viewer.zoom
            viewer.updateTiles()
        }
    }

    Timer {
        id: tileTimer
        interval: 60
        onTriggered: viewer.updateTiles()
    }

    Flickable {
        id: flick
        anchors.fill: parent
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: Math.max(width, paper.width + 2 * viewer.margin)
        contentHeight: Math.max(height, paper.height + 2 * viewer.margin)
        onContentXChanged: tileTimer.restart()
        onContentYChanged: tileTimer.restart()
        onWidthChanged: tileTimer.restart()
        onHeightChanged: tileTimer.restart()
        ScrollBar.vertical: ScrollBar {}
        ScrollBar.horizontal: ScrollBar {}

        Rectangle {
            id: paper
            color: "white"
            width: viewer.points.width * viewer.zoom
            height: viewer.points.height * viewer.zoom
            x: Math.max(viewer.margin, (flick.contentWidth - width) / 2)
            y: Math.max(viewer.margin, (flick.contentHeight - height) / 2)

            Image {
                id: sheetImage
                anchors.fill: parent
                source: backend.sheetCount > 0 ? "image://sheets/" + backend.generation + "/" + backend.sheet : ""
                sourceSize: viewer.tiled
                    ? (viewer.fullSize.width >= viewer.fullSize.height
                        ? Qt.size(viewer.baseEdge, Math.round(viewer.baseEdge * viewer.fullSize.height / viewer.fullSize.width))
                        : Qt.size(Math.round(viewer.baseEdge * viewer.fullSize.width / viewer.fullSize.height), viewer.baseEdge))
                    : viewer.fullSize
                asynchronous: true
                retainWhileLoading: true
                // Full sheets are large; keep only the one on screen.
                cache: false
                smooth: true
                mipmap: true
            }

            Repeater {
                model: viewer.tiles
                delegate: Image {
                    required property var modelData
                    readonly property real scale: paper.width / viewer.fullSize.width
                    x: modelData.x * scale
                    y: modelData.y * scale
                    width: modelData.w * scale
                    height: modelData.h * scale
                    source: "image://sheets/" + backend.generation + "/" + backend.sheet + "/"
                        + viewer.fullSize.width + "x" + viewer.fullSize.height + "/"
                        + [modelData.x, modelData.y, modelData.w, modelData.h].join(",")
                    asynchronous: true
                    smooth: true
                }
            }
        }

        // A mouse wheel zooms at the pointer, as in every drawing viewer;
        // a touchpad's two-finger scroll pans, and zooms with Ctrl held.
        WheelHandler {
            target: null
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: (event) => {
                const p = viewer.mapFromItem(flick.contentItem, point.position.x, point.position.y)
                if (event.pixelDelta.x !== 0 || event.pixelDelta.y !== 0) {
                    if (event.modifiers & Qt.ControlModifier)
                        viewer.zoomAt(viewer.zoom * Math.pow(1.01, event.pixelDelta.y), p.x, p.y)
                    else
                        viewer.pan(-event.pixelDelta.x, -event.pixelDelta.y)
                } else if (event.modifiers & Qt.ShiftModifier) {
                    viewer.pan(-event.angleDelta.y, 0)
                } else {
                    viewer.zoomAt(viewer.zoom * Math.pow(1.0015, event.angleDelta.y), p.x, p.y)
                }
            }
        }

        PinchHandler {
            target: null
            property real startZoom: 1
            onActiveChanged: if (active) startZoom = viewer.zoom
            onActiveScaleChanged: {
                const p = viewer.mapFromItem(flick.contentItem, centroid.position.x, centroid.position.y)
                viewer.zoomAt(startZoom * activeScale, p.x, p.y)
            }
        }
    }

    TakeoffLayer {
        id: layer
        anchors.fill: flick
        takeoff: viewer.sheetTakeoff
        sheet: backend.sheet
        origin: viewer.origin
        pixelsPerPoint: viewer.pixelsPerPoint
        selected: viewer.selected
        activeItem: viewer.activeItem
        labels: viewer.labels
        accent: theme.accent
    }

    // Clicks place corners and pick measurements; a drag pans, whatever the
    // tool, so the sheet can be moved mid-measurement. Picked measurements
    // drag to move, and their corners drag to reshape them.
    MouseArea {
        id: pointer
        anchors.fill: flick
        hoverEnabled: true
        preventStealing: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        property point pressAt
        property point lastAt
        property bool dragging: false
        property bool panning: false
        property bool doubled: false
        // What a press in the select tool caught: a measurement, and which
        // of its corners.
        property int grabbed: -1
        property int grabbedVertex: -1
        property var grabbedPoints: []
        property var hover: ({ id: -1, vertex: -1 })

        cursorShape: panning ? Qt.ClosedHandCursor
            : viewer.tool !== "select" ? Qt.CrossCursor
            : hover.vertex >= 0 && hover.id === viewer.selected ? Qt.SizeAllCursor
            : hover.id >= 0 ? Qt.PointingHandCursor : Qt.OpenHandCursor

        function hitAt(x, y) {
            return takeoff.hitTest(backend.sheet, viewer.toSheet(x, y), 8 / viewer.pixelsPerPoint)
        }

        function moved(mouse) {
            return Math.hypot(mouse.x - pressAt.x, mouse.y - pressAt.y) > 5
        }

        onPressed: (mouse) => {
            pressAt = Qt.point(mouse.x, mouse.y)
            lastAt = pressAt
            dragging = false
            panning = mouse.button === Qt.MiddleButton
            doubled = false
            grabbed = -1
            if (viewer.tool === "select" && mouse.button === Qt.LeftButton) {
                const hit = hitAt(mouse.x, mouse.y)
                if (hit.id >= 0) {
                    grabbed = hit.id
                    // A corner reshapes only once its measurement is picked,
                    // so a first click never bends one by accident.
                    grabbedVertex = hit.id === viewer.selected ? hit.vertex : -1
                    grabbedPoints = takeoff.measurement(hit.id).points
                    viewer.selected = hit.id
                }
            }
        }

        onPositionChanged: (mouse) => {
            viewer.cursor = viewer.placeAt(mouse.x, mouse.y, mouse.modifiers)
            viewer.cursorOnSheet = true
            if (!pressed) {
                if (viewer.tool === "select")
                    hover = hitAt(mouse.x, mouse.y)
                return
            }
            if (!dragging && !panning && !moved(mouse))
                return
            if (grabbed >= 0) {
                dragging = true
                let p = viewer.toSheet(mouse.x, mouse.y)
                const start = viewer.toSheet(pressAt.x, pressAt.y)
                let next
                if (grabbedVertex >= 0) {
                    if (!(mouse.modifiers & Qt.AltModifier))
                        p = takeoff.snap(backend.sheet, p, 8 / viewer.pixelsPerPoint, grabbed)
                    next = grabbedPoints.slice()
                    next[grabbedVertex] = p
                } else {
                    next = grabbedPoints.map(q => Qt.point(q.x + p.x - start.x, q.y + p.y - start.y))
                }
                layer.preview = { id: grabbed, points: next }
                return
            }
            panning = true
            viewer.pan(lastAt.x - mouse.x, lastAt.y - mouse.y)
            lastAt = Qt.point(mouse.x, mouse.y)
        }

        onReleased: (mouse) => {
            if (dragging && layer.preview.points)
                takeoff.setPoints(grabbed, layer.preview.points)
            const wasDrag = dragging || panning
            layer.preview = ({})
            dragging = false
            panning = false
            if (wasDrag || doubled)
                return
            if (mouse.button === Qt.RightButton) {
                // Right click finishes a line or an area, as in CAD.
                if (viewer.tool === "measure" && viewer.drafting)
                    viewer.finish()
                else if (viewer.tool === "calibrate")
                    viewer.cancelDraft()
                return
            }
            if (mouse.button !== Qt.LeftButton)
                return
            if (viewer.tool === "select") {
                if (grabbed < 0)
                    viewer.selected = -1
                else if ((mouse.modifiers & Qt.ShiftModifier) && grabbedVertex >= 0)
                    removeCorner(grabbed, grabbedVertex)
                return
            }
            viewer.addCorner(mouse.x, mouse.y, mouse.modifiers)
        }

        onDoubleClicked: (mouse) => {
            doubled = true
            if (mouse.button !== Qt.LeftButton)
                return
            if (viewer.tool === "measure") {
                viewer.finish()
            } else if (viewer.tool === "select" && viewer.selected >= 0) {
                insertCorner(viewer.selected, viewer.toSheet(mouse.x, mouse.y))
            }
        }

        onExited: viewer.cursorOnSheet = false
        onCanceled: {
            layer.preview = ({})
            dragging = false
            panning = false
        }

        // Double-clicking an edge of the picked line or area adds a corner
        // there, to bend it around something.
        function insertCorner(id, p) {
            const m = takeoff.measurement(id)
            if (m.kind === "count")
                return
            const list = m.points
            const segments = m.kind === "area" ? list.length : list.length - 1
            let best = -1, nearest = 8 / viewer.pixelsPerPoint
            for (let i = 0; i < segments; ++i) {
                const a = list[i], b = list[(i + 1) % list.length]
                const abx = b.x - a.x, aby = b.y - a.y
                const length = abx * abx + aby * aby
                const t = length > 0 ? Math.max(0, Math.min(1, ((p.x - a.x) * abx + (p.y - a.y) * aby) / length)) : 0
                const d = Math.hypot(p.x - (a.x + t * abx), p.y - (a.y + t * aby))
                if (d <= nearest) {
                    nearest = d
                    best = i
                }
            }
            if (best < 0)
                return
            const next = list.slice()
            next.splice(best + 1, 0, p)
            takeoff.setPoints(id, next)
        }

        // Shift-clicking a corner of the picked measurement takes it out.
        function removeCorner(id, vertex) {
            const m = takeoff.measurement(id)
            const next = m.points.slice()
            next.splice(vertex, 1)
            takeoff.setPoints(id, next)
        }
    }

    BusyIndicator {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 12
        width: 36
        height: 36
        running: viewer.loading
    }
}
