/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaitoolguide.h"

QString WhaleAiToolGuide::text()
{
    // For the agent, in English; kept short enough for small local models.
    return QStringLiteral(R"(# Writing a tool for the Orcinus canvas

A tool is a small QML app that the user keeps in a tab of the AI Canvas panel, next to the files.
Write it only when the user asks for a tool; to just show something, use canvas_show.

## Files
A folder per tool, named by its id (lower case letters, digits, - and _). Write the files ONLY with
tool_write_file (never with a shell or editor), so Orcinus sees every change.
- tool.json: {"name": "Sound Triage", "icon": "<freedesktop icon name>", "description": "...",
  "entry": "main.qml", "writes": ["where the tool writes, in words"], "settings": {"key": default}}
- main.qml (the entry) and more .qml/.js files if needed.

## QML
Import only QtQuick, QtQuick.Controls, QtQuick.Layouts (and org.kde.orcinus.canvas for VideoOutput).
Modules with their own file, network or storage access (Qt.labs.*, QtCore, LocalStorage, QtNetwork,
QtMultimedia, ...) are refused: use the `orcinus` object. Root: an Item or FocusScope with
`focus: true`. Use the palette (SystemPalette) so it fits light and dark themes.
Give every control you want to test an objectName.
Keys: plain keys (letters, arrows, Space, Delete) go to the tool while it has the focus. To take a
key, accept it in Keys.onShortcutOverride and handle it in Keys.onPressed.

## The orcinus object
Paths may be absolute paths or URLs; URLs come back. Asynchronous calls take a callback last and call
it later: reads get (value, error), changes get (ok, error). error is null when it worked.
Context: orcinus.currentFolder(), orcinus.selection() (list), orcinus.otherFolder() ("" without split
  view); signal orcinus.contextChanged when they change.
Reading: orcinus.list(url, {recursive: false, mime: "audio/*" or a list, hidden: false, limit: 10000,
  progress: function(count)}, function(items, error)) returns an id for orcinus.cancel(id);
  an item is {url, path, name, folder, folderName, isDir, size, modified, mime}.
  orcinus.item(url, function(item, error)); orcinus.readText(url, function(text, error), maxBytes).
Changing (all can be undone with Orcinus' Undo): orcinus.copy(urls, folder, cb),
  orcinus.move(urls, folder, cb), orcinus.rename(url, newName, cb), orcinus.trash(urls, cb),
  orcinus.mkdir(url, cb), orcinus.writeText(url, text, overwrite, cb) (overwrite moves the old file to
  the trash first).
View: orcinus.reveal(url) shows a file selected in the file view (a folder: opens it).
User: orcinus.pickFolder(function(url), start) ("" if cancelled), orcinus.pickFiles(function(urls),
  {multiple: true, mime: "image/png"}), orcinus.notify(text) (a short message above the tool).
Player (sounds, videos, streams): orcinus.player.play(url), .toggle(url), .seek(seconds), .stop();
  properties source, playing, loaded, failed, position, duration.
Settings kept across starts: orcinus.setting(key, fallback) (the manifest's default first),
  orcinus.setSetting(key, value).

## Loop
1. tool_write_file tool.json and main.qml.
2. tool_open (loads it again if open) and read the errors it returns; tool_log shows load errors,
   warnings, console.log output and errors in callbacks.
3. Check it: tool_inspect (named items and the root's properties), tool_invoke (click an item by
   objectName, press keys, or call a root function), tool_screenshot.
4. Fix and repeat until it loads without errors and does what the user asked.
5. Tell the user it is ready and that they can keep it with "Keep on Canvas" in the tab's context
   menu. Tools that are not kept are gone after a restart.
)");
}
