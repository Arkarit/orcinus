/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// Sound Triage: an example tool of the AI canvas (CV2), written to the user's example prompt:
// collect the sounds of the current folder and its subfolders sorted by name, play the first,
// show name and folder, a Target folder, Prev / Next / Repeat / Keep (copy to Target, then next).
// Keys: Left / Right, Space (repeat), K (keep).

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

FocusScope {
    id: root
    focus: true

    property var sounds: []
    property int current: -1
    property string target: orcinus.setting("target", "")
    property string folder: orcinus.currentFolder()
    property bool loading: true
    property string status: ""
    readonly property var sound: current >= 0 && current < sounds.length ? sounds[current] : null

    function show(index) {
        if (index < 0 || index >= sounds.length) {
            return;
        }
        current = index;
        orcinus.player.play(sounds[index].url);
        orcinus.reveal(sounds[index].url);
    }
    function previous() { show(current - 1); }
    function next() { show(current + 1); }
    function repeat() { if (sound) orcinus.player.play(sound.url); }
    function keep() {
        if (!sound || target === "") {
            status = qsTr("Choose a Target folder first.");
            return;
        }
        const name = sound.name;
        orcinus.copy([sound.url], target, function (ok, error) {
            if (ok) {
                status = qsTr("Kept %1").arg(name);
                next();
            } else {
                status = error;
            }
        });
    }
    function chooseTarget() {
        orcinus.pickFolder(function (url) {
            if (url !== "") {
                target = url;
                orcinus.setSetting("target", url);
            }
        }, target);
    }
    function collect() {
        loading = true;
        current = -1;
        orcinus.list(folder, { recursive: true, mime: "audio/*", limit: 50000 }, function (items, error) {
            loading = false;
            if (error) {
                status = error;
                return;
            }
            items.sort((a, b) => a.name.localeCompare(b.name, undefined, { numeric: true, sensitivity: "base" }));
            sounds = items;
            if (items.length > 0) {
                show(0);
            } else {
                status = qsTr("No sounds here.");
            }
        });
    }

    Component.onCompleted: collect()

    Keys.onShortcutOverride: (event) => {
        event.accepted = [Qt.Key_Left, Qt.Key_Right, Qt.Key_Space, Qt.Key_K].includes(event.key) && event.modifiers === Qt.NoModifier;
    }
    Keys.onPressed: (event) => {
        switch (event.key) {
        case Qt.Key_Left: previous(); break;
        case Qt.Key_Right: next(); break;
        case Qt.Key_Space: repeat(); break;
        case Qt.Key_K: keep(); break;
        default: return;
        }
        event.accepted = true;
    }

    SystemPalette { id: palette }

    Rectangle {
        anchors.fill: parent
        color: palette.base
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        Label {
            objectName: "name"
            Layout.fillWidth: true
            text: root.loading ? qsTr("Collecting sounds…") : (root.sound ? root.sound.name : "")
            font.pointSize: Qt.application.font.pointSize * 1.4
            font.bold: true
            elide: Text.ElideMiddle
        }
        Label {
            objectName: "folder"
            Layout.fillWidth: true
            text: root.sound ? decodeURIComponent(root.sound.folder.replace("file://", "")) : ""
            elide: Text.ElideMiddle
            opacity: 0.7
        }
        Label {
            objectName: "position"
            text: root.sounds.length > 0 ? qsTr("%1 of %2").arg(root.current + 1).arg(root.sounds.length) : ""
            opacity: 0.7
        }

        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("Target:") }
            Label {
                objectName: "target"
                Layout.fillWidth: true
                text: root.target === "" ? qsTr("(none)") : decodeURIComponent(root.target.replace("file://", ""))
                elide: Text.ElideMiddle
            }
            Button {
                text: qsTr("Choose…")
                icon.name: "folder-open"
                onClicked: root.chooseTarget()
            }
        }

        RowLayout {
            Button {
                objectName: "prev"
                text: qsTr("Prev")
                icon.name: "go-previous"
                enabled: root.current > 0
                onClicked: root.previous()
            }
            Button {
                objectName: "next"
                text: qsTr("Next")
                icon.name: "go-next"
                enabled: root.current + 1 < root.sounds.length
                onClicked: root.next()
            }
            Button {
                objectName: "repeat"
                text: qsTr("Repeat")
                icon.name: "media-playlist-repeat"
                enabled: root.sound !== null
                onClicked: root.repeat()
            }
            Button {
                objectName: "keep"
                text: qsTr("Keep")
                icon.name: "document-save"
                enabled: root.sound !== null && root.target !== ""
                onClicked: root.keep()
            }
        }

        Label {
            objectName: "status"
            Layout.fillWidth: true
            text: root.status
            wrapMode: Text.Wrap
            opacity: 0.8
        }

        Item { Layout.fillHeight: true }
    }
}
