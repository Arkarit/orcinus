/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAIMCPSERVER_H
#define WHALEAIMCPSERVER_H

#include "whaleacptypes.h"
#include "whaleaicontext.h"
#include "whaleaiuiactions.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>

#include <functional>
#include <memory>

/**
 * A tool of the back channel that the window implements (AI9: window, panels, messages, viewer).
 * Only the names in WhaleAiMcpServer::windowToolNames() are offered; texts for the agent are English.
 */
struct WhaleAiWindowTool {
    QString name;
    QString title;
    QString description;
    bool readOnly = false;
    /** The JSON schema properties of the arguments, and which are required. */
    QJsonObject properties;
    QStringList required;
    /**
     * Runs the tool. Sets @p error on wrong arguments or when it cannot be done, and @p notice to
     * a line for the transcript (translated) when it changed what the user sees.
     */
    std::function<QJsonObject(const QJsonObject &arguments, bool *error, QString *notice)> call;
};

/** What the back channel may do in the window (AI6c); set by the window, like the context. */
struct WhaleAiViewActions {
    /** Opens @p folder (absolute, an existing local folder) in the active view. */
    std::function<void(const QString &folder)> openFolder;
    /** Selects @p paths (in the folder of the active view) there, instead of what was selected. */
    std::function<void(const QStringList &paths)> select;
    /** The actions of the window (AI9b), visible ones only, without pure submenus. */
    std::function<QList<WhaleAiUiAction>()> actions;
    /** Triggers the action @p name of the window. */
    std::function<void(const QString &name)> trigger;
    /** Makes the left (0) or right (1) view of the split view active; false without split view. */
    std::function<bool(int pane)> activatePane;
    /** The active view: left (0) or right (1) in split view, -1 without split view. */
    std::function<int()> activePane;
    /** Tools of the window (AI9); see WhaleAiWindowTool. */
    QList<WhaleAiWindowTool> windowTools;
    /** The active view after a command: folder, mode, sorting (optional). */
    std::function<QJsonObject()> viewState;
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
    /** The window tools that may be offered (AI9); others in WhaleAiViewActions are ignored. */
    static const QStringList &windowToolNames();

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
    /** What set_location and select_items do; without them they answer with an error. */
    void setViewActions(const WhaleAiViewActions &actions);

    /**
     * @return The name of our tool @p toolCall calls, as the agents checked in AI6b say it, or an
     *         empty string if it is not one of ours (then the user answers the request).
     */
    static QString ownTool(const WhaleAcpToolCall &toolCall);

    /** Answers one JSON-RPC message; for the tests. @return The response, empty for a notification. */
    QJsonObject handle(const QJsonObject &message);

    /**
     * The key of a schema object that lists the order of its "properties". QJsonObject sorts keys,
     * but local models (llama.cpp grammars in LM Studio) only accept properties in the order the
     * schema lists them: with a sorted block schema, "title" left only "url" to follow (agent
     * tests, 2026-11-17). serialize() writes the properties in this order and drops the key.
     */
    static QString propertyOrderKey();

    /** @return @p message as one compact line of JSON, schema properties in their given order. */
    static QByteArray serialize(const QJsonObject &message);

Q_SIGNALS:
    /** A tool was called (the result is already sent). */
    void toolCalled(const QString &name, bool error);
    /** set_location opened @p folder. */
    void locationSet(const QString &folder);
    /** select_items selected @p count items. */
    void itemsSelected(int count);
    /** trigger_action triggered @p action; its state is the one afterwards. */
    void actionTriggered(const WhaleAiUiAction &action);
    /** A window tool changed what the user sees; @p notice says what, for the transcript. */
    void windowToolUsed(const QString &notice);

private:
    void newConnection();
    void readConnection(QLocalSocket *socket);
    QJsonObject callTool(const QString &name, const QJsonObject &arguments, bool *known);
    QJsonObject location();
    QJsonObject selection();
    QJsonObject setLocation(const QJsonObject &arguments, bool *error);
    QJsonObject selectItems(const QJsonObject &arguments, bool *error);
    QJsonObject listActions(const QJsonObject &arguments, bool *error);
    QJsonObject triggerAction(const QJsonObject &arguments, bool *error);
    /** The actions the agent may use. */
    QList<WhaleAiUiAction> allowedActions() const;
    /** The window tools set and allowed. */
    QList<WhaleAiWindowTool> windowTools() const;

    std::unique_ptr<QTemporaryDir> m_dir;
    QLocalServer *m_server = nullptr;
    QString m_token;
    QString m_error;
    QHash<QLocalSocket *, bool> m_authorized;
    std::function<WhaleAiContext()> m_contextProvider;
    WhaleAiViewActions m_viewActions;
};

#endif
