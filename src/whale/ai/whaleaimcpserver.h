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
    /** At most this many items in get_selection. */
    static constexpr int MaxSelection = 500;

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

    /** Answers one JSON-RPC message; for the tests. @return The response, empty for a notification. */
    QJsonObject handle(const QJsonObject &message);

Q_SIGNALS:
    /** A tool was called (the result is already sent). */
    void toolCalled(const QString &name, bool error);

private:
    void newConnection();
    void readConnection(QLocalSocket *socket);
    QJsonObject callTool(const QString &name, const QJsonObject &arguments, bool *known);
    QJsonObject location();
    QJsonObject selection();
    bool reaches(const QString &path) const;

    std::unique_ptr<QTemporaryDir> m_dir;
    QLocalServer *m_server = nullptr;
    QString m_token;
    QString m_error;
    QHash<QLocalSocket *, bool> m_authorized;
    std::function<WhaleAiContext()> m_contextProvider;
    std::function<bool(const QString &)> m_access;
};

#endif
