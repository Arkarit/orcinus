/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaicanvastools.h"

#include "whaleaicanvasmodel.h"
#include "whaleaicanvaspanel.h"

#include <KLocalizedString>

#include <QJsonArray>
#include <QPointer>

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

QJsonObject blockSchema()
{
    return {
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"),
         QJsonObject{
             {QStringLiteral("type"),
              QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                          {QStringLiteral("enum"),
                           QJsonArray{QStringLiteral("image"),
                                      QStringLiteral("video"),
                                      QStringLiteral("audio"),
                                      QStringLiteral("text"),
                                      QStringLiteral("files"),
                                      QStringLiteral("compare"),
                                      QStringLiteral("watch"),
                                      QStringLiteral("stream")}}}},
             {QStringLiteral("title"), property(QStringLiteral("string"), QStringLiteral("Optional heading above the block"))},
             {QStringLiteral("path"), property(QStringLiteral("string"), QStringLiteral("Absolute local path: image, video, audio; the folder of watch"))},
             {QStringLiteral("paths"),
              QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                          {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                          {QStringLiteral("description"), QStringLiteral("Absolute local paths: files (1-200 files or folders), compare (before, after)")}}},
             {QStringLiteral("text"), property(QStringLiteral("string"), QStringLiteral("Markdown for text; a caption under the content for the others"))},
             {QStringLiteral("url"), property(QStringLiteral("string"), QStringLiteral("stream: http, https, rtsp, rtmp, srt or udp URL"))},
         }},
        {QStringLiteral("required"), QJsonArray{QStringLiteral("type")}},
    };
}
}

QList<WhaleAiWindowTool> WhaleAiCanvasTools::create(WhaleAiCanvasPanel *panel, const std::function<void()> &showPanel)
{
    QPointer<WhaleAiCanvasPanel> guard(panel);
    return {
        {QStringLiteral("canvas_show"),
         QStringLiteral("Show something on the Orcinus canvas"),
         QStringLiteral("Shows your work to the user on the AI canvas, a panel of Orcinus next to the files: images, videos, sounds, Markdown texts, "
                        "clickable file lists, a before/after comparison of two images, a watched folder (always shows its newest image, e.g. while "
                        "something renders into it) and network streams. Blocks are added at the bottom; replace=true clears the canvas first. Files "
                        "are shown, never changed. Prefer this over describing images or listing many paths in the chat."),
         false,
         {{QStringLiteral("blocks"),
           QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                       {QStringLiteral("items"), blockSchema()},
                       {QStringLiteral("description"), QStringLiteral("What to show, in order")}}},
          {QStringLiteral("replace"), property(QStringLiteral("boolean"), QStringLiteral("Clear the canvas first (default false)"))}},
         {QStringLiteral("blocks")},
         [guard, showPanel](const QJsonObject &arguments, bool *error, QString *notice) -> QJsonObject {
             if (!guard) {
                 return failed(error, QStringLiteral("The canvas is gone."));
             }
             const QJsonArray given = arguments.value(QLatin1String("blocks")).toArray();
             if (given.isEmpty()) {
                 return failed(error, QStringLiteral("\"blocks\" must be a non-empty array."));
             }
             if (given.size() > WhaleAiCanvasModel::MaxBlocks) {
                 return failed(error, QStringLiteral("At most %1 blocks at once.").arg(WhaleAiCanvasModel::MaxBlocks));
             }
             // All or nothing: one wrong block shows none.
             QList<WhaleAiCanvasBlock> blocks;
             for (int i = 0; i < given.size(); ++i) {
                 QString message;
                 const auto block = WhaleAiCanvasModel::parse(given.at(i).toObject(), &message);
                 if (!block) {
                     return failed(error, QStringLiteral("Block %1: %2 Nothing was shown.").arg(i).arg(message));
                 }
                 blocks.append(*block);
             }
             const bool replace = arguments.value(QLatin1String("replace")).toBool();
             if (replace) {
                 guard->clear();
             }
             guard->model()->show(blocks, false);
             if (showPanel) {
                 showPanel();
             }
             *notice = i18ncp("@info", "The agent showed something on the canvas.", "The agent showed %1 things on the canvas.", blocks.size());
             return {{QStringLiteral("shown"), int(blocks.size())}, {QStringLiteral("total"), guard->model()->rowCount()}};
         }},
        {QStringLiteral("canvas_clear"),
         QStringLiteral("Clear the Orcinus canvas"),
         QStringLiteral("Removes everything from the AI canvas and stops what plays there."),
         false,
         {},
         {},
         [guard](const QJsonObject &, bool *error, QString *notice) -> QJsonObject {
             if (!guard) {
                 return failed(error, QStringLiteral("The canvas is gone."));
             }
             const bool hadContent = guard->model()->rowCount() > 0;
             guard->clear();
             if (hadContent) {
                 *notice = i18nc("@info", "The agent cleared the canvas.");
             }
             return {{QStringLiteral("cleared"), hadContent}};
         }},
        {QStringLiteral("get_canvas"),
         QStringLiteral("Read the Orcinus canvas"),
         QStringLiteral("Lists the blocks on the AI canvas, as canvas_show takes them, and whether the canvas is visible to the user."),
         true,
         {},
         {},
         [guard](const QJsonObject &, bool *error, QString *) -> QJsonObject {
             if (!guard) {
                 return failed(error, QStringLiteral("The canvas is gone."));
             }
             return {{QStringLiteral("blocks"), guard->model()->toJson()}, {QStringLiteral("visible"), guard->isVisible()}};
         }},
    };
}
