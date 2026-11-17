/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// Sound Check: an example tool of the AI canvas with a backend (CV4). The Python backend
// measures the WAV files of the current folder (length, peak, loudness, clipping, silence at
// start and end, a waveform); Trim writes a copy without the silence into the subfolder trimmed.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

FocusScope {
    id: root
    focus: true

    property var files: []
    property int selected: -1
    property string folder: ""
    property bool scanning: false
    property string status: ""
    readonly property var file: selected >= 0 && selected < files.length ? files[selected] : null

    function localPath(url) {
        return url.startsWith("file://") ? decodeURIComponent(url.substring(7)) : url;
    }
    function scan(url) {
        folder = localPath(url || orcinus.currentFolder());
        files = [];
        selected = -1;
        scanning = true;
        status = qsTr("Measuring…");
        orcinus.backend.send({cmd: "scan", folder: folder});
    }
    function select(index) {
        if (index >= 0 && index < files.length) {
            selected = index;
            orcinus.reveal(files[index].path);
        }
    }
    function trimSelected() {
        if (file && file.longSilence) {
            status = qsTr("Trimming %1…").arg(file.name);
            orcinus.backend.send({cmd: "trim", path: file.path});
        }
    }
    function play() {
        if (file) {
            orcinus.player.play(file.path);
        }
    }

    Connections {
        target: orcinus.backend
        function onMessage(value) {
            if (value.type === "file") {
                root.files = root.files.concat([value]);
                if (root.selected < 0) {
                    root.selected = 0;
                }
            } else if (value.type === "done") {
                root.scanning = false;
                root.status = value.count === 0 ? qsTr("No WAV files in this folder.") : qsTr("%1 files measured.").arg(value.count);
            } else if (value.type === "trimmed") {
                root.status = qsTr("Trimmed: %1 (%2 s less)").arg(value.target).arg(value.removed);
                orcinus.reveal(value.target);
            } else if (value.type === "error") {
                root.scanning = false;
                root.status = value.error;
            }
        }
    }
    Component.onCompleted: scan()

    SystemPalette { id: palette }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        RowLayout {
            Label {
                objectName: "folder"
                Layout.fillWidth: true
                elide: Text.ElideMiddle
                text: root.folder
                color: palette.windowText
            }
            Button {
                objectName: "rescan"
                text: qsTr("Measure Current Folder")
                enabled: !root.scanning && orcinus.backend.state === "running"
                onClicked: root.scan()
            }
        }

        ListView {
            id: list
            objectName: "files"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.files
            currentIndex: root.selected
            delegate: ItemDelegate {
                required property var modelData
                required property int index
                width: list.width
                highlighted: index === root.selected
                onClicked: root.select(index)
                contentItem: RowLayout {
                    Label {
                        Layout.fillWidth: true
                        text: modelData.name
                        elide: Text.ElideRight
                        color: highlighted ? palette.highlightedText : palette.windowText
                    }
                    Label {
                        text: modelData.error ? modelData.error
                            : qsTr("%1 s · peak %2 dB · %3 dB").arg(modelData.duration).arg(modelData.peakDb).arg(modelData.loudnessDb)
                        color: highlighted ? palette.highlightedText : palette.windowText
                        opacity: 0.7
                    }
                    Label {
                        visible: !!modelData.clipped
                        text: qsTr("clipped")
                        font.bold: true
                        color: highlighted ? palette.highlightedText : "#c0392b"
                    }
                    Label {
                        visible: !!modelData.longSilence
                        text: qsTr("silence")
                        font.bold: true
                        color: highlighted ? palette.highlightedText : "#b9770e"
                    }
                }
            }
        }

        Canvas {
            id: waveform
            objectName: "waveform"
            Layout.fillWidth: true
            Layout.preferredHeight: 80
            property var points: root.file && root.file.waveform ? root.file.waveform : []
            onPointsChanged: requestPaint()
            onPaint: {
                const context = getContext("2d");
                context.reset();
                context.fillStyle = palette.base;
                context.fillRect(0, 0, width, height);
                if (points.length === 0) {
                    return;
                }
                context.fillStyle = palette.highlight;
                const step = width / points.length;
                for (let i = 0; i < points.length; ++i) {
                    const h = Math.max(1, points[i] * height);
                    context.fillRect(i * step, (height - h) / 2, Math.max(1, step - 1), h);
                }
            }
        }

        RowLayout {
            Button {
                objectName: "play"
                text: qsTr("Play")
                enabled: root.file !== null
                onClicked: root.play()
            }
            Button {
                objectName: "trim"
                text: qsTr("Trim Silence")
                enabled: root.file !== null && !!root.file.longSilence
                onClicked: root.trimSelected()
            }
            Label {
                objectName: "status"
                Layout.fillWidth: true
                elide: Text.ElideRight
                text: orcinus.backend.state === "waiting" ? qsTr("Waiting for Start above…") : root.status
                color: palette.windowText
            }
        }
    }
}
