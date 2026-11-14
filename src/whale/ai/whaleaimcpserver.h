/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAIMCPSERVER_H
#define WHALEAIMCPSERVER_H

#include "whaleacptypes.h"
#include "whaleaicontext.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>

#include <functional>
#include <memory>

/** What the back channel may do in the window (AI6c); set by the window, like the context. */
struct WhaleAiViewActions {
    /** Opens @p folder (absolute, an existing local folder) in the active view. */
    std::function<void(const QString &folder)> openFolder;
    /** Selects @p paths (in the folder of the active view) there, instead of what was selected. */
    std::function<void(const QStringList &paths)> select;
};

class QLocalServer;
class QLocalSocket;
class QTemporaryDir;

/**
 * @brief The back channel of an AI agent (AI6): the MCP server of one conversation.
 *
 * Listens on a private Unix socket ($XDG_RUNTIME_DIR/orcinus-ai/<random>/mcp). The agent starts
 * orcinus-mcp (mcpServer()), which relays its stdio here after the token. Nothing that arrives
 * is trusted: the agent's own commands can open the socket too, so every check is here. See
 * .privatedoc/ai-support-plan.md (AI6).
 */
class WhaleAiMcpServer : public QObject
{
    Q_OBJECT

public:
    /** At most this many items in get_selection and select_items. */
    static constexpr int MaxSelection = 500;
    /** The name of the server in session/new. */
    static QString name();

    explicit WhaleAiMcpServer(QObject *parent = nullptr);
    ~WhaleAiMcpServer() override;

    /** @return The relay: next to the program (build tree), else the installed one; empty if none. */
    static QString relay();

    /** Starts listening; removes the folders of conversations whose socket is gone. */
    bool listen();
    QString errorString() const;
    /** The socket; empty before listen(). */
    QString socketPath() const;
    QString token() const;

    /** For session/new: @p program (relay()) with the socket and the token. */
    WhaleAcpMcpServer mcpServer(const QString &program) const;

    /** Where the user is, as the panel sends it (folder, selection, other split view). */
    void setContextProvider(const std::function<WhaleAiContext()> &provider);
    /** Whether the agent reaches a path (its sandbox); none: all of them. */
    void setAccess(const std::function<bool(const QString &)> &access);
    /** What set_location and select_items do; without them they answer with an error. */
    void setViewActions(const WhaleAiViewActions &actions);
    /** Called when the agent reports the user's language (remember_language); none: the tool is not offered. */
    void setRememberLanguage(const std::function<void(const QString &)> &remember);

    /**
     * @return The name of our tool @p toolCall calls, as the agents checked in AI6b say it, or an
     *         empty string if it is not one of ours (then the user answers the request).
     */
    static QString ownTool(const WhaleAcpToolCall &toolCall);

    /** Answers one JSON-RPC message; for the tests. @return The response, empty for a notification. */
    QJsonObject handle(const QJsonObject &message);

Q_SIGNALS:
    /** A tool was called (the result is already sent). */
    void toolCalled(const QString &name, bool error);
    /** set_location opened @p folder. */
    void locationSet(const QString &folder);
    /** select_items selected @p count items. */
    void itemsSelected(int count);

private:
    void newConnection();
    void readConnection(QLocalSocket *socket);
    QJsonObject callTool(const QString &name, const QJsonObject &arguments, bool *known);
    QJsonObject location();
    QJsonObject selection();
    QJsonObject setLocation(const QJsonObject &arguments, bool *error);
    QJsonObject selectItems(const QJsonObject &arguments, bool *error);
    QJsonObject rememberLanguage(const QJsonObject &arguments, bool *error);
    bool reaches(const QString &path) const;

    std::unique_ptr<QTemporaryDir> m_dir;
    QLocalServer *m_server = nullptr;
    QString m_token;
    QString m_error;
    QHash<QLocalSocket *, bool> m_authorized;
    std::function<WhaleAiContext()> m_contextProvider;
    std::function<bool(const QString &)> m_access;
    WhaleAiViewActions m_viewActions;
    std::function<void(const QString &)> m_rememberLanguage;
};

#endif
