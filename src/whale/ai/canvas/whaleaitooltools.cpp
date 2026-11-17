/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaitooltools.h"

#include "whaleaicanvaspanel.h"
#include "whaleaitoolguide.h"
#include "whaleaitoolstore.h"
#include "whaleaitooltab.h"

#include <KLocalizedString>

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QPointer>
#include <QSaveFile>

namespace
{
QJsonObject failed(bool *error, const QString &message)
{
    *error = true;
    return {{QStringLiteral("error"), message}};
}

QJsonObject property(const QString &type, const QString &description)
{
    return {{QStringLiteral("type"), type}, {QStringLiteral("description"), description}};
}

const QJsonObject toolProperty = property(QStringLiteral("string"), QStringLiteral("The tool's id: its folder name (lower case letters, digits, - and _)"));

/** The file @p relative of the tool @p id as an absolute path, or empty with @p error. */
QString toolFile(const WhaleAiToolStore &store, const QString &id, const QString &relative, QString *error)
{
    if (!WhaleAiToolStore::isValidId(id)) {
        *error = QStringLiteral("\"tool\" must be lower case letters, digits, - and _ (it names the folder).");
        return QString();
    }
    const QString clean = QDir::cleanPath(relative);
    if (relative.isEmpty() || QDir::isAbsolutePath(relative) || clean.startsWith(QLatin1String("..")) || clean.startsWith(QLatin1Char('.'))
        || clean.contains(QLatin1String("/."))) {
        *error = QStringLiteral("\"path\" must be a relative path inside the tool's folder, without hidden parts.");
        return QString();
    }
    static const QStringList extensions{QStringLiteral("qml"),
                                        QStringLiteral("js"),
                                        QStringLiteral("mjs"),
                                        QStringLiteral("json"),
                                        QStringLiteral("md"),
                                        QStringLiteral("txt"),
                                        QStringLiteral("svg"),
                                        QStringLiteral("py")};
    if (!extensions.contains(QFileInfo(clean).suffix().toLower())) {
        *error = QStringLiteral("Tool files are text: .qml, .js, .mjs, .json, .md, .txt, .svg or .py.");
        return QString();
    }
    return QDir(store.root()).filePath(id + QLatin1Char('/') + clean);
}

QJsonObject toolStatus(WhaleAiCanvasPanel *panel, const QString &id)
{
    WhaleAiToolTab *tab = panel->toolTab(id);
    QJsonObject status{{QStringLiteral("open"), tab != nullptr}};
    if (tab) {
        status.insert(QStringLiteral("loaded"), tab->isLoaded());
        status.insert(QStringLiteral("blocked"), tab->isBlocked());
        QJsonArray log;
        const QStringList lines = tab->log();
        for (qsizetype i = std::max<qsizetype>(0, lines.size() - 20); i < lines.size(); ++i) {
            log.append(lines.at(i));
        }
        status.insert(QStringLiteral("log"), log);
    }
    return status;
}
}

const QStringList &WhaleAiToolTools::changingTools()
{
    static const QStringList names{QStringLiteral("tool_write_file"),
                                   QStringLiteral("tool_delete_file"),
                                   QStringLiteral("tool_open"),
                                   QStringLiteral("tool_invoke")};
    return names;
}

QList<WhaleAiWindowTool> WhaleAiToolTools::create(WhaleAiCanvasPanel *panel, const std::function<void()> &showPanel)
{
    QPointer<WhaleAiCanvasPanel> guard(panel);
    auto bind = [guard](auto function) {
        return [guard, function](const QJsonObject &arguments, bool *error, QString *notice) -> QJsonObject {
            if (!guard) {
                return failed(error, QStringLiteral("The canvas is gone."));
            }
            return function(guard.data(), arguments, error, notice);
        };
    };
    return {
        {QStringLiteral("tool_guide"),
         QStringLiteral("How to write an Orcinus canvas tool"),
         QStringLiteral("Explains canvas tools: small QML apps the user keeps next to the files (files, QML rules, the orcinus API, the "
                        "development loop). Read it before writing a tool."),
         true,
         {},
         {},
         bind([](WhaleAiCanvasPanel *, const QJsonObject &, bool *, QString *) -> QJsonObject {
             return {{QStringLiteral("guide"), WhaleAiToolGuide::text()}};
         })},

        {QStringLiteral("tool_list"),
         QStringLiteral("List the Orcinus canvas tools"),
         QStringLiteral("Lists the user's canvas tools: id, name, description, files, whether open, loaded and kept by the user."),
         true,
         {},
         {},
         bind([](WhaleAiCanvasPanel *panel, const QJsonObject &, bool *, QString *) -> QJsonObject {
             const WhaleAiToolStore &store = panel->toolStore();
             QJsonArray tools;
             const QStringList ids = store.ids();
             for (const QString &id : ids) {
                 const auto tool = store.tool(id);
                 QJsonObject entry = toolStatus(panel, id);
                 entry.insert(QStringLiteral("id"), id);
                 entry.insert(QStringLiteral("name"), tool->name);
                 entry.insert(QStringLiteral("description"), tool->description);
                 entry.insert(QStringLiteral("files"), QJsonArray::fromStringList(store.files(id)));
                 entry.insert(QStringLiteral("kept"), store.pinned().contains(id));
                 entry.remove(QStringLiteral("log"));
                 tools.append(entry);
             }
             // Folders whose manifest is wrong: the agent may be writing one.
             QJsonArray invalid;
             const QStringList folders = QDir(store.root()).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
             for (const QString &folder : folders) {
                 QString problem;
                 if (!ids.contains(folder) && !store.tool(folder, &problem)) {
                     invalid.append(QJsonObject{{QStringLiteral("id"), folder}, {QStringLiteral("error"), problem}});
                 }
             }
             return {{QStringLiteral("folder"), store.root()}, {QStringLiteral("tools"), tools}, {QStringLiteral("invalid"), invalid}};
         })},

        {QStringLiteral("tool_read_file"),
         QStringLiteral("Read a file of an Orcinus canvas tool"),
         QStringLiteral("Reads a file of a canvas tool, e.g. its main.qml, before changing it."),
         true,
         {{QStringLiteral("tool"), toolProperty},
          {QStringLiteral("path"), property(QStringLiteral("string"), QStringLiteral("Relative path in the tool's folder, e.g. main.qml"))}},
         {QStringLiteral("tool"), QStringLiteral("path")},
         bind([](WhaleAiCanvasPanel *panel, const QJsonObject &arguments, bool *error, QString *) -> QJsonObject {
             QString problem;
             const QString path =
                 toolFile(panel->toolStore(), arguments.value(QLatin1String("tool")).toString(), arguments.value(QLatin1String("path")).toString(), &problem);
             if (path.isEmpty()) {
                 return failed(error, problem);
             }
             QFile file(path);
             if (!file.exists()) {
                 return failed(error, QStringLiteral("No such file."));
             }
             if (file.size() > MaxFileSize || !file.open(QIODevice::ReadOnly)) {
                 return failed(error, QStringLiteral("The file cannot be read or is too large."));
             }
             return {{QStringLiteral("content"), QString::fromUtf8(file.readAll())}};
         })},

        {QStringLiteral("tool_write_file"),
         QStringLiteral("Write a file of an Orcinus canvas tool"),
         QStringLiteral("Creates or replaces a text file of a canvas tool (tool.json, main.qml, ...); the tool's folder is created with "
                        "its first file. The only way to write tool files. Then call tool_open to load the tool and see its errors. "
                        "Refused in a read-only mode."),
         false,
         {{QStringLiteral("tool"), toolProperty},
          {QStringLiteral("path"), property(QStringLiteral("string"), QStringLiteral("Relative path in the tool's folder, e.g. main.qml"))},
          {QStringLiteral("content"), property(QStringLiteral("string"), QStringLiteral("The whole file"))}},
         {QStringLiteral("tool"), QStringLiteral("path"), QStringLiteral("content")},
         bind([](WhaleAiCanvasPanel *panel, const QJsonObject &arguments, bool *error, QString *notice) -> QJsonObject {
             const QString id = arguments.value(QLatin1String("tool")).toString();
             const QString relative = arguments.value(QLatin1String("path")).toString();
             const QByteArray content = arguments.value(QLatin1String("content")).toString().toUtf8();
             WhaleAiToolStore store = panel->toolStore();
             QString problem;
             const QString path = toolFile(store, id, relative, &problem);
             if (path.isEmpty()) {
                 return failed(error, problem);
             }
             if (content.size() > MaxFileSize) {
                 return failed(error, QStringLiteral("A tool file has at most %1 bytes.").arg(MaxFileSize));
             }
             if (!QFile::exists(path) && store.files(id).size() >= MaxFiles) {
                 return failed(error, QStringLiteral("A tool has at most %1 files.").arg(MaxFiles));
             }
             QDir().mkpath(QFileInfo(path).absolutePath());
             QSaveFile file(path);
             if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size() || !file.commit()) {
                 return failed(error, QStringLiteral("Could not write %1.").arg(path));
             }
             QJsonObject result{{QStringLiteral("written"), QDir::cleanPath(relative)}, {QStringLiteral("bytes"), content.size()}};
             // A kept tool that the agent changes is no longer the one the user trusted: it is
             // not kept any more until the user keeps it again.
             if (store.pinned().contains(id)) {
                 panel->unkeepTool(id);
                 result.insert(QStringLiteral("note"),
                               QStringLiteral("The user had kept this tool; it is no longer kept until they keep it again (tab menu, Keep on Canvas)."));
             }
             QString manifestError;
             const auto tool = store.tool(id, &manifestError);
             result.insert(QStringLiteral("toolValid"), bool(tool));
             if (!tool) {
                 result.insert(QStringLiteral("toolProblem"), manifestError);
             }
             *notice =
                 i18nc("@info %1 is a file name, %2 a tool's id", "The agent wrote “%1” of the tool “%2”.", QDir::cleanPath(relative), tool ? tool->name : id);
             return result;
         })},

        {QStringLiteral("tool_delete_file"),
         QStringLiteral("Delete a file of an Orcinus canvas tool"),
         QStringLiteral("Deletes a file of a canvas tool that is no longer needed. Refused in a read-only mode."),
         false,
         {{QStringLiteral("tool"), toolProperty},
          {QStringLiteral("path"), property(QStringLiteral("string"), QStringLiteral("Relative path in the tool's folder"))}},
         {QStringLiteral("tool"), QStringLiteral("path")},
         bind([](WhaleAiCanvasPanel *panel, const QJsonObject &arguments, bool *error, QString *notice) -> QJsonObject {
             const QString id = arguments.value(QLatin1String("tool")).toString();
             QString problem;
             const QString path = toolFile(panel->toolStore(), id, arguments.value(QLatin1String("path")).toString(), &problem);
             if (path.isEmpty()) {
                 return failed(error, problem);
             }
             if (!QFile::exists(path) || !QFile::remove(path)) {
                 return failed(error, QStringLiteral("No such file, or it cannot be deleted."));
             }
             if (panel->toolStore().pinned().contains(id)) {
                 panel->unkeepTool(id);
             }
             *notice = i18nc("@info %1 is a file name, %2 a tool's id", "The agent deleted “%1” of the tool “%2”.", QFileInfo(path).fileName(), id);
             return {{QStringLiteral("deleted"), arguments.value(QLatin1String("path")).toString()}};
         })},

        {QStringLiteral("tool_open"),
         QStringLiteral("Open (or reload) an Orcinus canvas tool"),
         QStringLiteral("Opens a canvas tool in its tab on the AI canvas, or loads it again if it is open, and shows the canvas. Answers "
                        "whether it loaded and the first lines of its log (errors). Refused in a read-only mode: the tool's code runs."),
         false,
         {{QStringLiteral("tool"), toolProperty}},
         {QStringLiteral("tool")},
         bind([showPanel](WhaleAiCanvasPanel *panel, const QJsonObject &arguments, bool *error, QString *notice) -> QJsonObject {
             const QString id = arguments.value(QLatin1String("tool")).toString();
             const bool wasOpen = panel->toolTab(id) != nullptr;
             if (showPanel) {
                 showPanel();
             }
             const QString problem = panel->openTool(id);
             if (!problem.isEmpty()) {
                 return failed(error, problem);
             }
             WhaleAiToolTab *tab = panel->toolTab(id);
             // Loaded when shown; again when it was open (new files), and also while the panel is not on screen.
             if (!tab->isBlocked() && (wasOpen || !tab->isLoaded())) {
                 tab->load();
             }
             *notice = i18nc("@info %1 is a tool's name", "The agent opened the tool “%1”.", tab->tool().name);
             QJsonObject result = toolStatus(panel, id);
             if (tab->isBlocked()) {
                 result.insert(QStringLiteral("note"),
                               QStringLiteral("The user kept this tool and its files changed outside Orcinus; it runs only after they keep it again."));
             }
             return result;
         })},

        {QStringLiteral("tool_close"),
         QStringLiteral("Close an Orcinus canvas tool"),
         QStringLiteral("Closes the tab of a canvas tool (a kept one comes back with the next start)."),
         false,
         {{QStringLiteral("tool"), toolProperty}},
         {QStringLiteral("tool")},
         bind([](WhaleAiCanvasPanel *panel, const QJsonObject &arguments, bool *, QString *) -> QJsonObject {
             return {{QStringLiteral("closed"), panel->closeTool(arguments.value(QLatin1String("tool")).toString())}};
         })},

        {QStringLiteral("tool_log"),
         QStringLiteral("Read the log of an Orcinus canvas tool"),
         QStringLiteral("The log of the last load of an open canvas tool: QML errors and warnings, console.log output, errors thrown in "
                        "callbacks."),
         true,
         {{QStringLiteral("tool"), toolProperty}},
         {QStringLiteral("tool")},
         bind([](WhaleAiCanvasPanel *panel, const QJsonObject &arguments, bool *error, QString *) -> QJsonObject {
             const QString id = arguments.value(QLatin1String("tool")).toString();
             WhaleAiToolTab *tab = panel->toolTab(id);
             if (!tab) {
                 return failed(error, QStringLiteral("The tool is not open (tool_open)."));
             }
             const QStringList lines = tab->log();
             return {{QStringLiteral("loaded"), tab->isLoaded()},
                     {QStringLiteral("blocked"), tab->isBlocked()},
                     {QStringLiteral("log"), QJsonArray::fromStringList(lines.mid(std::max<qsizetype>(0, lines.size() - 100)))}};
         })},

        {QStringLiteral("tool_inspect"),
         QStringLiteral("Inspect an open Orcinus canvas tool"),
         QStringLiteral("Shows the state of an open canvas tool: the properties its root declares and every item with an objectName "
                        "(type, visible, enabled, text, checked, value, ...)."),
         true,
         {{QStringLiteral("tool"), toolProperty}},
         {QStringLiteral("tool")},
         bind([](WhaleAiCanvasPanel *panel, const QJsonObject &arguments, bool *error, QString *) -> QJsonObject {
             WhaleAiToolTab *tab = panel->toolTab(arguments.value(QLatin1String("tool")).toString());
             if (!tab) {
                 return failed(error, QStringLiteral("The tool is not open (tool_open)."));
             }
             return tab->inspect();
         })},

        {QStringLiteral("tool_invoke"),
         QStringLiteral("Act in an open Orcinus canvas tool"),
         QStringLiteral("Acts in an open canvas tool as the user would, to test it: click an item by its objectName, press keys (a key "
                        "sequence such as \"Right\" or \"Ctrl+S\"), or call a function of the root with arguments. Give one of click, "
                        "keys, function. What the tool does is real (e.g. copying files). Refused in a read-only mode."),
         false,
         {{QStringLiteral("tool"), toolProperty},
          {QStringLiteral("click"), property(QStringLiteral("string"), QStringLiteral("objectName of the item to click"))},
          {QStringLiteral("keys"), property(QStringLiteral("string"), QStringLiteral("Key sequence to press"))},
          {QStringLiteral("function"), property(QStringLiteral("string"), QStringLiteral("Name of a function of the root"))},
          {QStringLiteral("arguments"),
           QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("description"), QStringLiteral("Arguments of the function")}}}},
         {QStringLiteral("tool")},
         bind([](WhaleAiCanvasPanel *panel, const QJsonObject &arguments, bool *error, QString *notice) -> QJsonObject {
             WhaleAiToolTab *tab = panel->toolTab(arguments.value(QLatin1String("tool")).toString());
             if (!tab) {
                 return failed(error, QStringLiteral("The tool is not open (tool_open)."));
             }
             QString problem;
             const QJsonObject result = tab->invoke(arguments, &problem);
             if (!problem.isEmpty()) {
                 return failed(error, problem);
             }
             const QString what = arguments.contains(QLatin1String("click")) ? arguments.value(QLatin1String("click")).toString()
                 : arguments.contains(QLatin1String("keys"))                 ? arguments.value(QLatin1String("keys")).toString()
                                                                             : arguments.value(QLatin1String("function")).toString();
             *notice = i18nc("@info %1 is a button, key or function, %2 a tool's name", "The agent used “%1” in the tool “%2”.", what, tab->tool().name);
             return result;
         }),
         {QStringLiteral("tool"), QStringLiteral("click"), QStringLiteral("keys"), QStringLiteral("function"), QStringLiteral("arguments")}},

        {QStringLiteral("tool_screenshot"),
         QStringLiteral("Screenshot of an open Orcinus canvas tool"),
         QStringLiteral("An image of what an open canvas tool shows now, to check its layout."),
         true,
         {{QStringLiteral("tool"), toolProperty}},
         {QStringLiteral("tool")},
         bind([](WhaleAiCanvasPanel *panel, const QJsonObject &arguments, bool *error, QString *) -> QJsonObject {
             WhaleAiToolTab *tab = panel->toolTab(arguments.value(QLatin1String("tool")).toString());
             if (!tab) {
                 return failed(error, QStringLiteral("The tool is not open (tool_open)."));
             }
             QImage image = tab->screenshot();
             if (image.isNull()) {
                 return failed(error, QStringLiteral("The tool shows nothing (not loaded, see tool_log)."));
             }
             if (image.width() > 1024) {
                 image = image.scaledToWidth(1024, Qt::SmoothTransformation);
             }
             QByteArray png;
             QBuffer buffer(&png);
             buffer.open(QIODevice::WriteOnly);
             image.save(&buffer, "PNG");
             return {{QStringLiteral("width"), image.width()},
                     {QStringLiteral("height"), image.height()},
                     // The MCP server turns this into an image content block.
                     {QStringLiteral("_mcpImage"),
                      QJsonObject{{QStringLiteral("data"), QString::fromLatin1(png.toBase64())}, {QStringLiteral("mimeType"), QStringLiteral("image/png")}}}};
         })},
    };
}
