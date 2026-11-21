/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// Example tool "Photo Triage": rate the images of the current folder with 1-5 stars from the
// keyboard; follows the selection of the file view and shows the image there.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

FocusScope {
    id: root
    focus: true

    SystemPalette { id: pal }

    property var images: []
    property int index: -1
    property var ratings: ({})
    property int currentRating: 0
    property int ratedCount: 0
    property int version: 0
    property string folder: ""
    property bool autoNext: true
    property bool loading: false
    property int refreshGeneration: 0
    property int metaRequestId: 0
    property string pendingRevealPath: ""
    property bool scanInProgress: false
    property bool refreshQueued: false
    property string imageListFingerprint: ""
    // The last message (an error, the result of Write XMP); the key help without one.
    property string statusMessage: ""
    property int pointPage: 0
    property int pointRows: Math.max(1, Math.floor((pointsViewport.height - 14) / 16))
    property int pointColumns: Math.max(1, Math.floor(pointsViewport.width / 16))
    property int pointPageSize: Math.max(1, pointColumns * pointRows)
    property int pointPageCount: images.length > 0 ? Math.ceil(images.length / pointPageSize) : 0
    property int pointPageItemCount: images.length > 0
        ? Math.max(0, Math.min(pointPageSize, images.length - pointPage * pointPageSize))
        : 0

    onPointPageSizeChanged: ensureCurrentPage()

    function loadRatings() {
        try {
            ratings = JSON.parse(orcinus.setting("ratings", "{}"));
        } catch (e) {
            ratings = ({});
        }
        updateCounts();
    }

    function updateCounts() {
        var count = 0;
        for (var path in ratings) {
            if (ratings[path])
                count += 1;
        }
        ratedCount = count;
        version += 1;
    }

    function selectedImageIndex(items) {
        var selected = orcinus.selection();
        if (!selected || selected.length === 0)
            return -1;
        var selectedValue = String(selected[0]);
        for (var i = 0; i < items.length; ++i) {
            if (String(items[i].path) === selectedValue || String(items[i].url) === selectedValue)
                return i;
        }
        return -1;
    }

    function imageFingerprint(items) {
        var parts = [];
        for (var i = 0; i < items.length; ++i)
            parts.push(items[i].path + "\u001f" + items[i].size + "\u001f" + items[i].modified);
        return parts.join("\u001e");
    }

    function imageIndexByPath(items, path) {
        if (!path)
            return -1;
        for (var i = 0; i < items.length; ++i) {
            if (items[i].path === path)
                return i;
        }
        return -1;
    }

    function listContainsImage(items) {
        if (!items)
            return false;
        for (var i = 0; i < items.length; ++i) {
            if (items[i].mime && String(items[i].mime).indexOf("image/") === 0)
                return true;
        }
        return false;
    }

    function folderChangesAffectImages(changes) {
        return changes && (
            listContainsImage(changes.added)
            || listContainsImage(changes.removed)
            || listContainsImage(changes.changed)
        );
    }

    function finishRefresh() {
        scanInProgress = false;
        loading = false;
        if (refreshQueued) {
            refreshQueued = false;
            Qt.callLater(function() { root.refresh(false); });
        }
    }

    function refresh(fullReset) {
        if (scanInProgress && !fullReset) {
            refreshQueued = true;
            return;
        }

        var requestedFolder = orcinus.currentFolder();
        var folderChanged = requestedFolder !== folder;
        var previousPath = (!folderChanged && index >= 0 && index < images.length)
            ? images[index].path
            : "";
        var previousIndex = index;
        var generation = ++refreshGeneration;

        if (fullReset || folderChanged) {
            refreshQueued = false;
            folder = requestedFolder;
            loading = true;
            imageListFingerprint = "";
            index = -1;
            images = [];
            pointPage = 0;
            clearPreview();
        }

        scanInProgress = true;
        orcinus.list(requestedFolder, {mime: "image/*", hidden: false, limit: 10000}, function(items, error) {
            if (generation !== refreshGeneration || requestedFolder !== orcinus.currentFolder())
                return;

            if (error) {
                root.statusMessage = error;
                finishRefresh();
                return;
            }

            var fingerprint = imageFingerprint(items);
            if (fingerprint === imageListFingerprint) {
                syncFromFileView();
                finishRefresh();
                return;
            }

            imageListFingerprint = fingerprint;
            images = items;

            if (items.length === 0) {
                index = -1;
                pointPage = 0;
                clearPreview();
                finishRefresh();
                return;
            }

            var selected = selectedImageIndex(items);
            var preserved = imageIndexByPath(items, previousPath);
            var nextIndex = selected >= 0
                ? selected
                : (preserved >= 0
                    ? preserved
                    : Math.min(Math.max(previousIndex, 0), items.length - 1));
            var nextPath = items[nextIndex].path;
            index = nextIndex;

            if (nextPath !== previousPath || image.source === "")
                showCurrent();
            else {
                currentRating = ratings[nextPath] || 0;
                ensureCurrentPage();
            }
            finishRefresh();
        });
    }

    function clearPreview() {
        metaRequestId += 1;
        nameText.text = "";
        metaText.text = "";
        promptText.text = "";
        image.source = "";
        currentRating = 0;
    }

    function clampPointPage() {
        if (pointPageCount <= 0) {
            pointPage = 0;
            return;
        }
        pointPage = Math.max(0, Math.min(pointPage, pointPageCount - 1));
    }

    function ensureCurrentPage() {
        if (index >= 0 && pointPageSize > 0)
            pointPage = Math.floor(index / pointPageSize);
        clampPointPage();
    }

    function setPointPage(page) {
        pointPage = page;
        clampPointPage();
    }

    function showCurrent() {
        ensureCurrentPage();
        if (index < 0 || index >= images.length) {
            clearPreview();
            return;
        }

        var item = images[index];
        var requestId = ++metaRequestId;
        nameText.text = item.name;
        metaText.text = "";
        promptText.text = "";
        image.source = item.url;
        currentRating = ratings[item.path] || 0;
        orcinus.backend.send({cmd: "meta", id: requestId, path: item.path});
    }

    function syncFromFileView() {
        var selectedValues = orcinus.selection();
        if (images.length === 0) {
            if (selectedValues && selectedValues.length > 0)
                refresh(false);
            return;
        }

        var selected = selectedImageIndex(images);
        if (selected < 0) {
            if (selectedValues && selectedValues.length > 0)
                refresh(false);
            return;
        }

        if (pendingRevealPath !== "") {
            if (selected >= 0 && images[selected].path === pendingRevealPath) {
                pendingRevealPath = "";
                revealGuard.stop();
            } else {
                return;
            }
        }

        if (selected >= 0 && selected !== index) {
            index = selected;
            showCurrent();
        }
    }

    function go(to) {
        if (to < 0 || to >= images.length)
            return;
        index = to;
        showCurrent();
        pendingRevealPath = images[index].path;
        revealGuard.restart();
        orcinus.reveal(images[index].url);
    }

    Timer {
        id: revealGuard
        interval: 1000
        repeat: false
        onTriggered: root.pendingRevealPath = ""
    }

    function rate(stars) {
        if (index < 0 || index >= images.length)
            return;
        var item = images[index];
        if (stars === 0)
            delete ratings[item.path];
        else
            ratings[item.path] = stars;
        currentRating = stars;
        updateCounts();
        orcinus.setSetting("ratings", JSON.stringify(ratings));
        if (stars > 0 && autoNext)
            go(index + 1);
    }

    function writeXmp() {
        orcinus.backend.send({cmd: "export", ratings: ratings});
    }

    // Only the keys of the tool; shortcuts such as Ctrl+Q stay the window's.
    function handlesKey(event) {
        if (event.modifiers !== Qt.NoModifier && event.modifiers !== Qt.KeypadModifier)
            return false;
        return (event.key >= Qt.Key_0 && event.key <= Qt.Key_5) || event.key === Qt.Key_Space
            || event.key === Qt.Key_Right || event.key === Qt.Key_Left || event.key === Qt.Key_Backspace;
    }

    Keys.onShortcutOverride: event.accepted = handlesKey(event)
    Keys.onPressed: {
        if (!handlesKey(event))
            return;
        if (event.key >= Qt.Key_1 && event.key <= Qt.Key_5) {
            rate(event.key - Qt.Key_1 + 1);
            event.accepted = true;
        } else if (event.key === Qt.Key_Space || event.key === Qt.Key_Right) {
            go(index + 1);
            event.accepted = true;
        } else if (event.key === Qt.Key_Left) {
            go(index - 1);
            event.accepted = true;
        } else if (event.key === Qt.Key_Backspace || event.key === Qt.Key_0) {
            rate(0);
            event.accepted = true;
        }
    }

    Connections {
        target: orcinus

        function onContextChanged() {
            if (orcinus.currentFolder() !== root.folder)
                root.refresh(true);
            else
                root.syncFromFileView();
        }

        function onFolderContentsChanged(changes) {
            if (root.folderChangesAffectImages(changes))
                root.refresh(false);
        }
    }

    Connections {
        target: orcinus.backend
        function onMessage(value) {
            if (!value)
                return;
            if (value.cmd === "export") {
                root.statusMessage = value.written + " XMP sidecar" + (value.written === 1 ? "" : "s") + " written"
                    + (value.skipped ? ", " + value.skipped + " kept (written by another program)" : "");
                return;
            }
            if (value.cmd !== "meta" || value.id !== root.metaRequestId)
                return;

            var line = "";
            if (value.dims)
                line += value.dims[0] + "×" + value.dims[1];
            if (value.info && value.info.seed) {
                if (line)
                    line += " · ";
                line += "seed " + value.info.seed;
                if (value.info.steps)
                    line += " · " + value.info.steps + " steps";
                if (value.info.sampler)
                    line += " · " + value.info.sampler;
            }
            metaText.text = line;
            promptText.text = value.prompt || "";
            if (value.error)
                root.statusMessage = value.error;
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        RowLayout {
            Text {
                text: root.folder
                font.pixelSize: 12
                color: pal.mid
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Text {
                text: root.ratedCount + " rated"
                font.pixelSize: 12
                color: pal.mid
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 150

            Rectangle {
                anchors.fill: parent
                radius: 6
                color: Qt.rgba(0, 0, 0, 0.06)
                border.color: pal.mid
                border.width: 1
            }

            Image {
                id: image
                anchors.fill: parent
                anchors.margins: 6
                visible: root.images.length > 0
                fillMode: Image.PreserveAspectFit
                smooth: true
                sourceSize.width: 900
            }

            BusyIndicator {
                anchors.centerIn: parent
                running: root.loading
                visible: running
            }

            Text {
                objectName: "emptyMessage"
                anchors.centerIn: parent
                visible: !root.loading && root.images.length === 0
                text: "Folder doesn't contain images"
                color: pal.mid
                font.pixelSize: 14
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 72

            ColumnLayout {
                anchors.fill: parent
                spacing: 2

                Text {
                    id: nameText
                    font.pixelSize: 14
                    font.bold: true
                    color: pal.text
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    Layout.preferredHeight: 20
                }
                Text {
                    id: metaText
                    font.pixelSize: 11
                    color: pal.mid
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    Layout.preferredHeight: 16
                }
                Text {
                    id: promptText
                    font.pixelSize: 11
                    color: pal.text
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }
            }
        }

        RowLayout {
            Repeater {
                model: 5
                Text {
                    objectName: "star" + (index + 1)
                    text: root.currentRating >= index + 1 ? "★" : "☆"
                    font.pixelSize: 20
                    color: root.currentRating >= index + 1 ? "#e0a030" : pal.mid
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.rate(index + 1)
                    }
                }
            }
            Item { Layout.fillWidth: true }
            Text {
                text: root.images.length > 0 ? (root.index + 1) + " / " + root.images.length : "0 / 0"
                font.pixelSize: 12
                color: pal.mid
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 62
            spacing: 4

            ToolButton {
                objectName: "pointsPrevBtn"
                text: "‹"
                font.pixelSize: 22
                Layout.preferredWidth: 30
                Layout.fillHeight: true
                enabled: root.pointPage > 0
                onClicked: root.setPointPage(root.pointPage - 1)
            }

            Item {
                id: pointsViewport
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                Grid {
                    anchors.top: parent.top
                    anchors.horizontalCenter: parent.horizontalCenter
                    columns: root.pointColumns
                    spacing: 4

                    Repeater {
                        model: root.pointPageItemCount

                        Rectangle {
                            property int imageIndex: root.pointPage * root.pointPageSize + index
                            width: 12
                            height: 12
                            radius: 6
                            color: root.index === imageIndex ? pal.highlight
                                 : (imageIndex < root.images.length
                                    && root.ratings[root.images[imageIndex].path] ? "#e0a030" : pal.mid)

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.go(parent.imageIndex)
                            }
                        }
                    }
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    text: root.pointPageCount > 0
                        ? "Page " + (root.pointPage + 1) + " / " + root.pointPageCount
                        : "Page 0 / 0"
                    font.pixelSize: 10
                    color: pal.mid
                }
            }

            ToolButton {
                objectName: "pointsNextBtn"
                text: "›"
                font.pixelSize: 22
                Layout.preferredWidth: 30
                Layout.fillHeight: true
                enabled: root.pointPage + 1 < root.pointPageCount
                onClicked: root.setPointPage(root.pointPage + 1)
            }
        }

        RowLayout {
            spacing: 6

            Button {
                objectName: "prevBtn"
                text: "◀"
                padding: 6
                enabled: root.index > 0
                onClicked: root.go(root.index - 1)
            }
            Button {
                objectName: "nextBtn"
                text: "▶"
                padding: 6
                enabled: root.index >= 0 && root.index < root.images.length - 1
                onClicked: root.go(root.index + 1)
            }
            Button {
                objectName: "clearBtn"
                text: "Clear rating"
                padding: 6
                enabled: root.currentRating > 0
                onClicked: root.rate(0)
            }
            CheckBox {
                objectName: "autoNextBox"
                text: "Auto-advance"
                checked: root.autoNext
                onToggled: {
                    root.autoNext = checked;
                    orcinus.setSetting("autoNext", checked);
                }
            }
            Item { Layout.fillWidth: true }
        }

        RowLayout {
            Text {
                id: status
                objectName: "status"
                font.pixelSize: 11
                color: pal.mid
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                text: orcinus.backend.state === "waiting"
                    ? "Click Start in the tab bar first."
                    : (root.statusMessage || "1–5 rate · → next · ← previous · 0 clear rating")
            }

            Button {
                objectName: "exportBtn"
                text: "Write XMP"
                padding: 6
                onClicked: root.writeXmp()
            }
        }
    }

    Component.onCompleted: {
        autoNext = orcinus.setting("autoNext", true);
        loadRatings();
        refresh(true);
    }
}
