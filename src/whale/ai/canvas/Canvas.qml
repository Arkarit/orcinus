/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// The AI canvas (CV1): the blocks the agent shows, newest at the bottom.
// Context: canvasModel (WhaleAiCanvasModel), canvasPlayer, canvasPanel, canvasTexts.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.orcinus.canvas

Rectangle {
    id: root
    // The panel's colour, as around it.
    color: palette.window

    SystemPalette {
        id: palette
        colorGroup: SystemPalette.Active
    }

    function timeText(seconds) {
        const s = Math.max(0, Math.floor(seconds));
        const m = Math.floor(s / 60);
        return m + ":" + String(s % 60).padStart(2, "0");
    }

    Label {
        objectName: "placeholder"
        anchors.centerIn: parent
        width: parent.width - 32
        visible: canvasModel.count === 0
        text: canvasTexts.empty
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        color: palette.text
        opacity: 0.6
    }

    ListView {
        id: list
        objectName: "blocks"
        anchors.fill: parent
        anchors.margins: 8
        spacing: 16
        clip: true
        model: canvasModel
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {
            id: scrollBar
        }

        // What the agent just showed comes into view, also while its images load; scrolling up
        // by hand stops that until the next block or the end.
        property bool followEnd: true
        onContentHeightChanged: {
            if (followEnd) {
                Qt.callLater(list.positionViewAtEnd);
            }
        }
        onContentYChanged: followEnd = atYEnd
        Connections {
            target: canvasModel
            function onRowsInserted() {
                list.followEnd = true;
                Qt.callLater(list.positionViewAtEnd);
            }
        }

        delegate: ColumnLayout {
            id: block
            required property int index
            required property string blockType
            required property string title
            required property string text
            required property var urls
            required property var names
            required property var icons
            required property string current
            required property int revision

            width: list.width - scrollBar.width
            spacing: 4

            Label {
                Layout.fillWidth: true
                visible: block.title !== ""
                text: block.title
                font.bold: true
                elide: Text.ElideRight
                color: palette.text
            }

            Loader {
                Layout.fillWidth: true
                sourceComponent: {
                    switch (block.blockType) {
                    case "image": return imageBlock;
                    case "compare": return compareBlock;
                    case "files": return filesBlock;
                    case "text": return textBlock;
                    case "audio": return audioBlock;
                    case "video":
                    case "stream": return videoBlock;
                    case "watch": return watchBlock;
                    }
                    return null;
                }
            }

            Label {
                Layout.fillWidth: true
                visible: block.blockType !== "text" && block.text !== ""
                text: block.text
                wrapMode: Text.Wrap
                color: palette.text
                opacity: 0.8
            }

            // ---- the kinds of blocks ----

            Component {
                id: imageBlock
                Picture {
                    source: block.urls[0]
                    onClicked: canvasPanel.activate(block.urls[0])
                }
            }

            Component {
                id: compareBlock
                RowLayout {
                    spacing: 8
                    Repeater {
                        model: 2
                        ColumnLayout {
                            required property int index
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            Layout.alignment: Qt.AlignTop
                            Label {
                                text: index === 0 ? canvasTexts.before : canvasTexts.after
                                font.bold: true
                                color: palette.text
                            }
                            Picture {
                                Layout.fillWidth: true
                                source: block.urls[index]
                                onClicked: canvasPanel.activate(block.urls[index])
                            }
                            Label {
                                Layout.fillWidth: true
                                text: block.names[index]
                                elide: Text.ElideMiddle
                                color: palette.text
                                opacity: 0.8
                            }
                        }
                    }
                }
            }

            Component {
                id: filesBlock
                Column {
                    Repeater {
                        model: block.urls.length
                        ItemDelegate {
                            required property int index
                            width: parent.width
                            text: block.names[index]
                            icon.source: "image://icon/" + block.icons[index]
                            icon.color: "transparent"
                            ToolTip.visible: hovered
                            ToolTip.delay: 600
                            ToolTip.text: decodeURIComponent(block.urls[index].replace("file://", ""))
                            onClicked: canvasPanel.activate(block.urls[index])
                        }
                    }
                }
            }

            Component {
                id: textBlock
                TextEdit {
                    readOnly: true
                    selectByMouse: true
                    textFormat: TextEdit.MarkdownText
                    wrapMode: TextEdit.Wrap
                    text: block.text
                    color: palette.text
                    selectionColor: palette.highlight
                    selectedTextColor: palette.highlightedText
                    onLinkActivated: (link) => canvasPanel.openLink(link)
                    HoverHandler {
                        cursorShape: parent.hoveredLink !== "" ? Qt.PointingHandCursor : Qt.IBeamCursor
                    }
                }
            }

            Component {
                id: audioBlock
                PlayerControls {
                    url: block.urls[0]
                    name: block.names[0]
                }
            }

            Component {
                id: videoBlock
                ColumnLayout {
                    id: videoColumn
                    spacing: 4
                    readonly property bool active: canvasPlayer.source === block.urls[0]
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: width * 9 / 16
                        color: "black"
                        Loader {
                            id: videoLoader
                            anchors.fill: parent
                            active: videoColumn.active && canvasPlayer.available
                            sourceComponent: VideoOutput {
                                player: canvasPlayer
                            }
                        }
                        Label {
                            anchors.centerIn: parent
                            width: parent.width - 16
                            visible: videoLoader.item !== null && !videoLoader.item.supported
                            text: canvasTexts.noVideo
                            wrapMode: Text.Wrap
                            horizontalAlignment: Text.AlignHCenter
                            color: "white"
                        }
                        RoundButton {
                            anchors.centerIn: parent
                            visible: !videoColumn.active && canvasPlayer.available
                            icon.name: "media-playback-start"
                            onClicked: canvasPlayer.play(block.urls[0], true)
                        }
                    }
                    PlayerControls {
                        Layout.fillWidth: true
                        url: block.urls[0]
                        name: block.names[0]
                        video: true
                    }
                }
            }

            Component {
                id: watchBlock
                ColumnLayout {
                    spacing: 4
                    Label {
                        Layout.fillWidth: true
                        text: canvasTexts.newestIn.arg(block.names[0])
                        elide: Text.ElideMiddle
                        color: palette.text
                        opacity: 0.8
                    }
                    Picture {
                        Layout.fillWidth: true
                        visible: block.current !== ""
                        // The fragment makes Qt load a file again that was overwritten in place.
                        source: block.current === "" ? "" : block.current + "#" + block.revision
                        cache: false
                        onClicked: canvasPanel.activate(block.current)
                    }
                    Label {
                        visible: block.current === ""
                        text: canvasTexts.nothingYet
                        color: palette.text
                        opacity: 0.6
                    }
                }
            }
        }
    }

    // An image as wide as the canvas allows, at most its own size; click shows it in the view.
    component Picture: Item {
        id: picture
        objectName: "picture"
        property alias source: image.source
        property alias cache: image.cache
        signal clicked
        implicitWidth: 100
        implicitHeight: image.status === Image.Ready && image.implicitWidth > 0
                        ? Math.min(800, Math.min(image.implicitWidth, width) * image.implicitHeight / image.implicitWidth)
                        : 48
        Image {
            id: image
            anchors.fill: parent
            fillMode: Image.PreserveAspectFit
            horizontalAlignment: Image.AlignLeft
            asynchronous: true
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: picture.clicked()
        }
    }

    // Play / pause, position and time of one sound, video or stream.
    component PlayerControls: RowLayout {
        id: controls
        property string url
        property string name
        property bool video: false
        readonly property bool active: canvasPlayer.source === url
        spacing: 4

        ToolButton {
            visible: canvasPlayer.available
            icon.name: controls.active && canvasPlayer.playing ? "media-playback-pause" : "media-playback-start"
            text: controls.active && canvasPlayer.playing ? canvasTexts.pause : canvasTexts.play
            display: AbstractButton.IconOnly
            onClicked: canvasPlayer.toggle(controls.url, controls.video)
        }
        Label {
            Layout.fillWidth: !slider.visible
            text: controls.name
            elide: Text.ElideMiddle
            color: palette.text
        }
        Slider {
            id: slider
            Layout.fillWidth: true
            visible: canvasPlayer.available && controls.active && canvasPlayer.duration > 0
            from: 0
            to: Math.max(1, canvasPlayer.duration)
            value: controls.active ? canvasPlayer.position : 0
            onMoved: canvasPlayer.seek(value)
        }
        Label {
            visible: slider.visible
            text: root.timeText(canvasPlayer.position) + " / " + root.timeText(canvasPlayer.duration)
            color: palette.text
            opacity: 0.8
        }
        Label {
            visible: controls.active && canvasPlayer.failed
            text: canvasTexts.failed
            color: palette.text
        }
        Label {
            visible: !canvasPlayer.available
            text: canvasTexts.noPlayer
            color: palette.text
            opacity: 0.6
        }
        Button {
            visible: !canvasPlayer.available && controls.url.startsWith("file:")
            text: canvasTexts.open
            onClicked: canvasPanel.openExternally(controls.url)
        }
    }
}
