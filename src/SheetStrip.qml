import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// Every sheet in the drawing as a thumbnail, in order, with how many
// measurements it has and its scale.
ListView {
    id: strip
    clip: true
    model: backend.sheetCount
    currentIndex: backend.sheet
    spacing: 4
    topMargin: 8
    bottomMargin: 8
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar {}
    highlightMoveDuration: 120

    onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)

    delegate: Item {
        id: entry
        required property int index
        readonly property bool selected: index === backend.sheet
        readonly property int count: takeoff.revision >= 0 ? takeoff.countOnSheet(index) : 0
        readonly property string scale: takeoff.revision >= 0 ? takeoff.scaleLabel(index) : ""
        width: ListView.view.width
        height: thumb.height + label.implicitHeight + scaleLabel.implicitHeight + 24

        Rectangle {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            radius: Math.min(4, theme.radius)
            color: entry.selected ? Qt.alpha(theme.accent, 0.16) : hover.hovered ? "#1c1c20" : "transparent"
            border.color: entry.selected ? theme.accent : "transparent"
            border.width: 1
        }

        Rectangle {
            id: thumb
            anchors.horizontalCenter: parent.horizontalCenter
            y: 8
            color: "white"
            readonly property size points: backend.generation >= 0 ? backend.sizeOf(entry.index) : Qt.size(0, 0)
            width: entry.width - 40
            height: points.width > 0 ? width * points.height / points.width : width

            Image {
                anchors.fill: parent
                source: "image://sheets/" + backend.generation + "/" + entry.index
                sourceSize.width: Math.round(parent.width * Screen.devicePixelRatio)
                asynchronous: true
                smooth: true
                mipmap: true
            }
        }

        // How many measurements the sheet has.
        Rectangle {
            visible: entry.count > 0
            anchors.right: thumb.right
            anchors.top: thumb.top
            anchors.margins: 4
            width: Math.max(height, markCount.implicitWidth + 8)
            height: markCount.implicitHeight + 2
            radius: height / 2
            color: theme.accent

            Label {
                id: markCount
                anchors.centerIn: parent
                text: translator.language !== "" ? entry.count.toLocaleString(Qt.locale(), 'f', 0) : ""
                color: theme.accentForeground
                font.pixelSize: 10
                font.bold: true
            }
        }

        Label {
            id: label
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: thumb.bottom
            anchors.topMargin: 5
            text: backend.generation >= 0 ? backend.sheetLabel(entry.index) : ""
            color: entry.selected ? theme.accent : "#d8d8de"
            font.pixelSize: 12
            elide: Text.ElideMiddle
            width: Math.min(implicitWidth, entry.width - 32)
        }

        Label {
            id: scaleLabel
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: label.bottom
            text: entry.scale !== "" ? entry.scale : qsTr("No scale")
            color: entry.scale !== "" ? "#8a8a94" : "#f0b45a"
            font.pixelSize: 10
            elide: Text.ElideRight
            width: Math.min(implicitWidth, entry.width - 32)
        }

        HoverHandler { id: hover }
        TapHandler { onTapped: backend.sheet = entry.index }
    }
}
