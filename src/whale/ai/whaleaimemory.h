/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAIMEMORY_H
#define WHALEAIMEMORY_H

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

/** @brief A note the agents get in later conversations (AI16): global or for a folder and below. */
struct WhaleAiNote {
    QString id;
    QString text;
    QString folder; ///< absolute and clean; empty: global
    QDateTime created;
    QDateTime updated;
    QString conversationId; ///< the conversation it came from, if any
    bool byAgent = false; ///< written by an agent (remember), not by the user

    bool isGlobal() const
    {
        return folder.isEmpty();
    }
    /** @return Whether the note applies in @p folder (absolute): global, its folder or one above. */
    bool appliesTo(const QString &folder) const;

    QJsonObject toJson() const;
    static std::optional<WhaleAiNote> fromJson(const QJsonObject &object);

    bool operator==(const WhaleAiNote &other) const = default;
};

/**
 * @brief The notes of the AI panel's memory (AI16), in one JSON file (by default
 * `~/.local/share/orcinus/ai-memory.json`, readable by the user only).
 *
 * Read again on each call: the file is small, and several windows share it.
 */
class WhaleAiMemory
{
public:
    /** At most this many characters per note. */
    static constexpr int MaxNoteLength = 500;

    /** @p path empty: defaultPath(). */
    explicit WhaleAiMemory(const QString &path = QString());

    static QString defaultPath();
    QString path() const;

    /** @return All notes, oldest first. */
    QList<WhaleAiNote> notes() const;
    std::optional<WhaleAiNote> note(const QString &id) const;

    /**
     * Adds a note with @p text for @p folder (empty: global).
     * @return The note, or nothing with @p error (empty or too long text, a relative folder, the
     * file cannot be written).
     */
    std::optional<WhaleAiNote> add(const QString &text, const QString &folder, const QString &conversationId, bool byAgent, QString *error = nullptr) const;
    /** Changes the text and folder of the note @p id. */
    bool update(const QString &id, const QString &text, const QString &folder, QString *error = nullptr) const;
    bool remove(const QString &id) const;

    /**
     * @return The notes for @p folders (absolute; empty ones ignored): those of the folders and the
     * folders above, nearest first, then the global ones, newest first; as many as fit into
     * @p budget characters of text (a note that does not fit is left out, smaller ones after it
     * still come).
     */
    QList<WhaleAiNote> relevant(const QStringList &folders, int budget) const;

    /**
     * @return The text that gives @p notes to an agent (AI16b), in English; empty without notes.
     * They are marked as the user's notes, data and not instructions.
     */
    static QString promptText(const QList<WhaleAiNote> &notes);

    /** @return @p text checked and trimmed for a note; empty with @p error if it cannot be one. */
    static QString checkedText(const QString &text, QString *error = nullptr);
    /** @return @p folder cleaned; empty for global; nothing with @p error if it is not absolute. */
    static std::optional<QString> checkedFolder(const QString &folder, QString *error = nullptr);

private:
    bool write(const QList<WhaleAiNote> &notes, QString *error) const;

    QString m_path;
};

#endif
