/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAICONVERSATIONSTORE_H
#define WHALEAICONVERSATIONSTORE_H

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <optional>

/**
 * @brief A saved conversation of the AI panel (AI14): what the user saw and what is needed to let
 * the agent continue it (its ACP session id, working folder, mode).
 */
struct WhaleAiConversation {
    QString id; ///< a UUID, also the file name
    QString title; ///< the start of the first user message
    QDateTime created;
    QDateTime updated;
    QString agentId;
    QString agentName;
    QString commandLine;
    QString localProvider;
    QString localModel;
    QString folder; ///< the agent's working folder
    QString sessionId; ///< the agent's ACP session; empty if it never got one
    QString mode;
    QJsonArray entries; ///< WhaleAiTranscript::record()

    /** @return A new conversation with a fresh id, created now. */
    static WhaleAiConversation create();
    /** @return The title for @p firstMessage: its first line, at most 80 characters. */
    static QString titleFor(const QString &firstMessage);

    QJsonObject toJson() const;
    /** @return Nothing if @p object is no conversation (or a newer format). */
    static std::optional<WhaleAiConversation> fromJson(const QJsonObject &object);

    bool operator==(const WhaleAiConversation &other) const = default;
};

/**
 * @brief The saved conversations: one JSON file each in a folder (by default
 * `~/.local/state/orcinus/conversations`, beside the other AI state).
 */
class WhaleAiConversationStore
{
public:
    /** @p folder empty: defaultFolder(). */
    explicit WhaleAiConversationStore(const QString &folder = QString());

    static QString defaultFolder();
    QString folder() const;

    /** Writes @p conversation (atomically). */
    bool save(const WhaleAiConversation &conversation) const;
    std::optional<WhaleAiConversation> load(const QString &id) const;
    /** @return All saved conversations, newest first; with @p withEntries false without their entries. */
    QList<WhaleAiConversation> list(bool withEntries = false) const;
    bool remove(const QString &id) const;
    /** Deletes those not updated within @p days (0: keeps all). @return How many went. */
    int prune(int days) const;

private:
    QString fileOf(const QString &id) const;

    QString m_folder;
};

#endif
