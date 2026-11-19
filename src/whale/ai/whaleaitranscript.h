/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAITRANSCRIPT_H
#define WHALEAITRANSCRIPT_H

#include "whaleacptypes.h"

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QScrollArea>

class QLabel;
class QVBoxLayout;

/**
 * @brief The conversation of the AI panel (AI3): one widget per entry, newest at the bottom.
 *
 * Entries: the user's messages, the agent's answers (Markdown, streamed), its thoughts
 * (collapsed), its tool calls (a row with status, details on click), permission requests (what
 * kind of thing the agent wants to do, the command or the paths, the changes or the tool input
 * to unfold, one button per option) and notices of the panel. Text the agent streams after a tool call starts
 * a new answer entry, so the entries are in the order things happened.
 *
 * The view follows new entries while it is scrolled to the bottom; scrolled up, it stays.
 *
 * Each entry widget has the property "entryKind" (see Kind) for tests and styles. It sits in a row
 * of its own whose start shows the time it was added ("timestamp" label) if timestamps are shown.
 *
 * Each entry also keeps what it shows as JSON (AI14): record() is the whole transcript, restore()
 * builds it again (with the old times; answered permission requests without their buttons).
 */
class WhaleAiTranscript : public QScrollArea
{
    Q_OBJECT

public:
    enum class Kind {
        UserMessage,
        AgentMessage,
        Thought,
        ToolCall,
        Permission,
        Notice,
        /** What the agent did in the window ("The agent opened …"). */
        Action,
        Error,
        /** A note of the memory the agent kept or proposes, with its buttons (AI16c). */
        Memory,
    };
    Q_ENUM(Kind)

    explicit WhaleAiTranscript(QWidget *parent = nullptr);

    /** @p context says what was sent along (folder, selection, …); shown small below. */
    void addUserMessage(const QString &text, const QString &context = QString());
    /** Appends to the agent's current answer, or starts one. */
    void appendAgentText(const QString &text);
    /** Appends to the agent's current thought, or starts one. */
    void appendThought(const QString &text);
    /** Adds the row of @p toolCall, or updates it. */
    void updateToolCall(const WhaleAcpToolCall &toolCall);
    /** Adds the request; permissionAnswered() follows when the user picks an option. */
    void addPermissionRequest(const WhaleAcpPermissionRequest &request);
    /**
     * Gives the keyboard focus to the first "allow" button of the request @p key.
     * @return False if there is no such request.
     */
    bool focusPermissionRequest(const QString &key);
    /** Where the focus goes when a focused permission request is answered or withdrawn (the input). */
    void setFocusReturn(QWidget *widget);
    /** The request @p key needs no answer any more: its buttons go. */
    void withdrawPermissionRequest(const QString &key);
    /** Shows or hides what the agent did: tool calls and actions (the "Actions" filter). */
    void setShowActions(bool show);
    bool showActions() const;
    /** Shows or hides the time at the start of each entry (the "Times" filter); off at first. */
    void setShowTimestamps(bool show);
    bool showTimestamps() const;
    /** @return Whether a permission request waits for the user. */
    bool hasOpenPermissionRequest() const;
    /** A message of the panel; @p details (plain text) can be unfolded. */
    void addNotice(const QString &text, Kind kind = Kind::Notice, const QString &details = QString(), const QString &toolTip = QString());
    /**
     * A note of the memory (AI16c): @p text, and a button per entry of @p buttons (id, label);
     * a click emits memoryButtonClicked(@p key, id). settleMemoryNote() ends it.
     */
    void addMemoryNote(const QString &key, const QString &text, const QList<QPair<QString, QString>> &buttons);
    /** The note @p key is decided: its buttons go, @p status (e.g. "Kept.") shows. */
    void settleMemoryNote(const QString &key, const QString &status);
    /** Ends the current answer and thought: what the agent sends next starts new entries. */
    void endTurn();
    void clear();

    /** @return The entry widgets, oldest first. */
    QList<QWidget *> entries() const;
    int entryCount() const;
    /** @return What @p count entries (-1: all) from @p from on show, as JSON objects (kind, time, text, …). */
    QJsonArray record(int from = 0, int count = -1) const;
    /** Appends the entries of @p record (from record()); unanswered requests come back answered "no longer needed". */
    void restore(const QJsonArray &record);
    static Kind kindOf(const QWidget *entry);
    /** @return When @p entry was added (always kept, shown or not). */
    static QDateTime timeOf(const QWidget *entry);

Q_SIGNALS:
    void permissionAnswered(const QString &key, const QString &optionId);
    /** A permission request came, was answered or withdrawn (see hasOpenPermissionRequest()). */
    void openPermissionsChanged();
    /** A link in an answer was clicked. */
    void linkActivated(const QString &link);
    /** record() changed: an entry came or changed, or all went. */
    void changed();
    /** A button of a memory note was clicked (addMemoryNote()). */
    void memoryButtonClicked(const QString &key, const QString &buttonId);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *addEntry(Kind kind, QWidget *entry);
    static bool isAction(Kind kind);
    void setEntryVisible(QWidget *entry, bool visible);
    /** Merges @p fields into the record of the entry that holds @p widget. */
    void updateRecord(QWidget *widget, const QJsonObject &fields);
    QLabel *createTextLabel(Qt::TextFormat format);
    QWidget *createFoldable(const QString &title, QLabel *body, bool unfolded);
    void scrollToBottomLater();

    QWidget *m_content = nullptr;
    QVBoxLayout *m_layout = nullptr;
    QLabel *m_currentAnswer = nullptr;
    QString m_currentAnswerText;
    QLabel *m_currentThought = nullptr;
    QString m_currentThoughtText;
    struct ToolCallEntry {
        QWidget *row = nullptr;
        QLabel *icon = nullptr;
        QLabel *title = nullptr;
        QLabel *details = nullptr;
    };
    QHash<QString, ToolCallEntry> m_toolCalls;
    QHash<QString, QWidget *> m_permissions;
    QHash<QString, QWidget *> m_memoryNotes; ///< open ones, by key
    bool m_followBottom = true;
    QPointer<QWidget> m_focusReturn;
    bool m_showActions = true;
    bool m_showTimestamps = false;
    QDateTime m_restoreTime; ///< while restoring: the time of the entry that comes
};

#endif
