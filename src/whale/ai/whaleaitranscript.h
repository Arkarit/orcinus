/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAITRANSCRIPT_H
#define WHALEAITRANSCRIPT_H

#include "whaleacptypes.h"

#include <QHash>
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
 * Each entry widget has the property "entryKind" (see Kind) for tests and styles.
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
        Error,
    };
    Q_ENUM(Kind)

    explicit WhaleAiTranscript(QWidget *parent = nullptr);

    void addUserMessage(const QString &text);
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
    /** The request @p key needs no answer any more: its buttons go. */
    void withdrawPermissionRequest(const QString &key);
    /** A message of the panel; @p details (plain text) can be unfolded. */
    void addNotice(const QString &text, Kind kind = Kind::Notice, const QString &details = QString());
    /** Ends the current answer and thought: what the agent sends next starts new entries. */
    void endTurn();
    void clear();

    /** @return The entry widgets, oldest first. */
    QList<QWidget *> entries() const;
    static Kind kindOf(const QWidget *entry);

Q_SIGNALS:
    void permissionAnswered(const QString &key, const QString &optionId);
    /** A link in an answer was clicked. */
    void linkActivated(const QString &link);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *addEntry(Kind kind, QWidget *entry);
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
    bool m_followBottom = true;
};

#endif
