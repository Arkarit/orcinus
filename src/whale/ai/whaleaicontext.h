/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAICONTEXT_H
#define WHALEAICONTEXT_H

#include <QJsonArray>
#include <QString>
#include <QStringList>

/**
 * @brief What the file manager shows, for a prompt to an AI agent (AI4).
 *
 * Absolute local paths only: the agent works on the local file system. Each part may be empty.
 * See .privatedoc/ai-support-plan.md.
 */
struct WhaleAiContext {
    QString folder; ///< the folder of the active view
    QStringList selection; ///< what is selected in it
    QString otherFolder; ///< the folder of the other split view
    QStringList added; ///< files and folders the user dropped onto the input

    bool isEmpty() const
    {
        return folder.isEmpty() && selection.isEmpty() && otherFolder.isEmpty() && added.isEmpty();
    }

    bool operator==(const WhaleAiContext &other) const = default;
};

namespace WhaleAiContextPrompt
{
/** At most this many selected or added items are sent; the rest is only counted. */
constexpr int MaxItems = 50;
/** At most this many subfolder names of the current folder are named. */
constexpr int MaxSubfolders = 30;

/**
 * @return The text that tells the agent about @p context, in English (it is for the agent, not
 *         the user); empty for an empty context. Names the subfolders of the current folder
 *         (AI0: an agent missed an empty one).
 */
QString text(const WhaleAiContext &context);

/**
 * @return The ACP content blocks of a prompt: the context text, a `resource_link` per folder
 *         and item (capped), then @p userText.
 */
QJsonArray blocks(const WhaleAiContext &context, const QString &userText);

/**
 * @return A short line for the transcript that says what was sent along, e.g. "With: folder
 *         “a”, 3 selected items" (translated); empty for an empty context.
 */
QString summary(const WhaleAiContext &context);
}

#endif
