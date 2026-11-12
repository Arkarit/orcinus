/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEACPTYPES_H
#define WHALEACPTYPES_H

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QMetaType>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QUrl>

/**
 * @file
 * Values of the Agent Client Protocol (ACP, agentclientprotocol.com, protocol version 1) as the
 * AI panel uses them. Only what the panel needs is parsed; the JSON of each value is kept in
 * `raw` for everything else. See .privatedoc/ai-support-plan.md.
 */

/** The ACP protocol version Orcinus speaks. */
constexpr int WhaleAcpProtocolVersion = 1;

/** A JSON-RPC error, or none (code 0). */
struct WhaleAcpError {
    /** Codes of JSON-RPC and ACP, plus local ones below -40000 that never come over the wire. */
    enum Code {
        None = 0,
        ParseError = -32700,
        InvalidRequest = -32600,
        MethodNotFound = -32601,
        InvalidParams = -32602,
        InternalError = -32603,
        AuthRequired = -32000, ///< ACP: the agent needs a login first
        ConnectionClosed = -40001, ///< local: the agent ended before it answered
        Timeout = -40002, ///< local: no answer in time
    };

    int code = None;
    QString message;
    QJsonValue data;

    bool isError() const
    {
        return code != None;
    }
};

/** Who the agent is and what it can do, from the `initialize` response. */
struct WhaleAcpAgentInfo {
    QString name;
    QString title; ///< display name; falls back to name
    QString version;
    int protocolVersion = 0;
    bool loadSession = false;
    bool imagePrompts = false;
    bool embeddedContext = false;
    bool mcpHttp = false;
    bool mcpSse = false;

    struct AuthMethod {
        QString id;
        QString name;
        QString description;
    };
    QList<AuthMethod> authMethods;

    QJsonObject raw;

    QString displayName() const
    {
        return title.isEmpty() ? name : title;
    }

    static WhaleAcpAgentInfo fromInitializeResult(const QJsonObject &result);
};

/**
 * A session setting the agent offers (`configOptions`: mode, model, reasoning, …). Only the
 * `select` type exists for Orcinus: `boolean` is opt-in for clients, and Orcinus does not
 * advertise it. Agents that only send the older `modes` field get one option of category
 * "mode" with the id modesOptionId(), set through `session/set_mode`.
 */
struct WhaleAcpConfigOption {
    struct Choice {
        QString value;
        QString name;
        QString description;
        QString group; ///< name of the group, for grouped options; otherwise empty
    };

    QString id;
    QString name;
    QString description;
    QString category; ///< "mode", "model", "model_config", "thought_level", "_…" or empty
    QString currentValue;
    QList<Choice> choices;

    /** The id of the option made from the `modes` field. */
    static QString modesOptionId()
    {
        return QStringLiteral("_orcinus_modes");
    }

    bool isFromModes() const
    {
        return id == modesOptionId();
    }

    /** @return The choice with @p value, or nullptr. */
    const Choice *choice(const QString &value) const;

    /** Parses a `configOptions` array; options of an unknown type are left out. */
    static QList<WhaleAcpConfigOption> listFromJson(const QJsonArray &array);
    /** Turns a `modes` object (SessionModeState) into the option modesOptionId(). */
    static WhaleAcpConfigOption fromModes(const QJsonObject &modes);
};

/** A tool call of the agent, merged over all its `tool_call` / `tool_call_update` messages. */
struct WhaleAcpToolCall {
    enum class Status {
        Pending,
        InProgress,
        Completed,
        Failed,
        Cancelled, ///< local: the turn was cancelled before the call ended
    };

    QString id;
    QString title;
    QString kind; ///< read, edit, delete, move, search, execute, think, fetch, switch_mode, other
    Status status = Status::Pending;
    QStringList locations; ///< absolute paths
    QJsonArray content;
    QJsonValue rawInput;
    QJsonValue rawOutput;

    bool isFinished() const
    {
        return status == Status::Completed || status == Status::Failed || status == Status::Cancelled;
    }

    /** Applies the fields present in @p update (ACP sends only the changed ones). */
    void merge(const QJsonObject &update);

    static Status statusFromString(const QString &status);
};

/** A `session/request_permission` the user has to answer. */
struct WhaleAcpPermissionRequest {
    struct Option {
        QString id;
        QString name;
        QString kind; ///< allow_once, allow_always, reject_once, reject_always
    };

    QString key; ///< identifies the request for WhaleAcpSession::answerPermission()
    WhaleAcpToolCall toolCall;
    QList<Option> options;
};

/** A slash command the agent offers (`available_commands_update`). */
struct WhaleAcpCommand {
    QString name;
    QString description;
    QString inputHint;
};

/** An MCP server passed to the agent in `session/new` (stdio transport). */
struct WhaleAcpMcpServer {
    QString name;
    QString command; ///< absolute path
    QStringList arguments;
    QList<QPair<QString, QString>> environment;

    QJsonObject toJson() const;
};

/** Builders for the content blocks of a prompt. */
namespace WhaleAcpContent
{
QJsonObject text(const QString &text);
/** A link to a file or folder; @p mimeType may be empty. */
QJsonObject resourceLink(const QUrl &url, const QString &name, const QString &mimeType = QString());
}

Q_DECLARE_METATYPE(WhaleAcpToolCall)
Q_DECLARE_METATYPE(WhaleAcpPermissionRequest)

#endif
