/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaimcpserver.h"

#include "whaleaidebug.h"

#include <KLocalizedString>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace
{
/** A message longer than this ends the connection: no tool call needs it. */
constexpr qsizetype MaxLine = 1024 * 1024;

const QStringList &protocolVersions()
{
    // Newest first; an agent asking for another one gets the newest.
    static const QStringList versions{QStringLiteral("2025-06-18"), QStringLiteral("2025-03-26"), QStringLiteral("2024-11-05")};
    return versions;
}

QString baseFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/orcinus-ai");
}

/** Removes the folders of conversations whose Orcinus is gone (a crash leaves them behind). */
void removeLeftovers(const QString &base)
{
    const QFileInfoList folders = QDir(base).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &folder : folders) {
        const QString socketPath = folder.absoluteFilePath() + QStringLiteral("/mcp");
        if (!QFileInfo::exists(socketPath)) {
            // Being set up, or not ours.
            continue;
        }
        QLocalSocket probe;
        probe.connectToServer(socketPath);
        if (!probe.waitForConnected(200) && probe.error() == QLocalSocket::ConnectionRefusedError) {
            QDir(folder.absoluteFilePath()).removeRecursively();
        }
    }
}

QJsonObject response(const QJsonValue &id, const QJsonObject &result)
{
    return {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), id}, {QStringLiteral("result"), result}};
}

QJsonObject errorResponse(const QJsonValue &id, int code, const QString &message)
{
    return {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
            {QStringLiteral("id"), id},
            {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), code}, {QStringLiteral("message"), message}}}};
}

QJsonObject
tool(const QString &name, const QString &title, const QString &description, bool readOnly, const QJsonObject &properties = {}, const QStringList &required = {})
{
    QJsonObject schema{{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), properties}};
    if (!required.isEmpty()) {
        schema.insert(QStringLiteral("required"), QJsonArray::fromStringList(required));
    }
    QJsonObject annotations{{QStringLiteral("readOnlyHint"), readOnly}, {QStringLiteral("openWorldHint"), false}};
    if (!readOnly) {
        // It changes what the user sees, never a file.
        annotations.insert(QStringLiteral("destructiveHint"), false);
        annotations.insert(QStringLiteral("idempotentHint"), true);
    }
    return {{QStringLiteral("name"), name},
            {QStringLiteral("title"), title},
            {QStringLiteral("description"), description},
            {QStringLiteral("inputSchema"), schema},
            {QStringLiteral("annotations"), annotations}};
}

const QStringList &toolNames()
{
    static const QStringList names{QStringLiteral("get_location"),
                                   QStringLiteral("get_selection"),
                                   QStringLiteral("set_location"),
                                   QStringLiteral("select_items"),
                                   QStringLiteral("remember_language")};
    return names;
}

QJsonObject toolError(const QString &message)
{
    return {{QStringLiteral("error"), message}};
}

QJsonArray tools()
{
    // Texts for the agent, not for the user: not translated.
    return {
        tool(QStringLiteral("get_location"),
             QStringLiteral("Get the folder shown in Orcinus"),
             QStringLiteral("Returns the folder the user has open in the active view of the Orcinus file manager, and the folder of the other "
                            "split view if there is one. \"access\" says whether you can reach the folder."),
             true),
        tool(QStringLiteral("get_selection"),
             QStringLiteral("Get the items selected in Orcinus"),
             QStringLiteral("Returns the paths of the files and folders the user has selected in the active view of the Orcinus file manager "
                            "(at most %1). \"access\" says whether you can reach each one.")
                 .arg(WhaleAiMcpServer::MaxSelection),
             true),
        tool(
            QStringLiteral("set_location"),
            QStringLiteral("Open a folder in Orcinus"),
            QStringLiteral("Opens a folder in the active view of the Orcinus file manager, to show the user a result. It changes only what the "
                           "user sees, no file. Only folders you can reach."),
            false,
            {{QStringLiteral("path"),
              QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Absolute path of the folder")}}}},
            {QStringLiteral("path")}),
        tool(QStringLiteral("select_items"),
             QStringLiteral("Select items in Orcinus"),
             QStringLiteral("Selects files and folders in the folder open in the active view of the Orcinus file manager (see get_location), "
                            "instead of what was selected, to show the user a result. It changes only what the user sees, no file. "
                            "Items hidden in the view cannot be selected. At most %1.")
                 .arg(WhaleAiMcpServer::MaxSelection),
             false,
             {{QStringLiteral("items"),
               QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                           {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                           {QStringLiteral("description"), QStringLiteral("Names in that folder, or absolute paths of items directly in it")}}}},
             {QStringLiteral("items")}),
        tool(QStringLiteral("remember_language"),
             QStringLiteral("Remember the user's language"),
             QStringLiteral("Tells Orcinus the language the user writes in, so later conversations greet the user in it. Call it once when the "
                            "user's language differs from the one you were asked to greet in. It changes nothing the user sees and no file."),
             false,
             {{QStringLiteral("language"),
               QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                           {QStringLiteral("description"), QStringLiteral("The language's name, e.g. \"German\" or \"Deutsch\"")}}}},
             {QStringLiteral("language")}),
    };
}

QJsonObject toolResult(const QJsonObject &structured, bool error = false)
{
    // Older clients read only the text: the same as JSON.
    return {{QStringLiteral("content"),
             QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                    {QStringLiteral("text"), QString::fromUtf8(QJsonDocument(structured).toJson(QJsonDocument::Compact))}}}},
            {QStringLiteral("structuredContent"), structured},
            {QStringLiteral("isError"), error}};
}
}

WhaleAiMcpServer::WhaleAiMcpServer(QObject *parent)
    : QObject(parent)
{
}

WhaleAiMcpServer::~WhaleAiMcpServer()
{
    if (m_server) {
        m_server->close();
    }
}

QString WhaleAiMcpServer::name()
{
    return QStringLiteral("orcinus");
}

QString WhaleAiMcpServer::ownTool(const WhaleAcpToolCall &toolCall)
{
    auto known = [](const QString &tool) {
        return toolNames().contains(tool) ? tool : QString();
    };
    // Claude Agent: the MCP server and the tool in its own _meta.
    const QJsonObject claude = toolCall.meta.value(QLatin1String("claudeCode")).toObject();
    if (claude.value(QLatin1String("mcpServer")).toObject().value(QLatin1String("name")).toString() == name()) {
        const QString prefix = QStringLiteral("mcp__%1__").arg(name());
        const QString tool = claude.value(QLatin1String("toolName")).toString();
        return tool.startsWith(prefix) ? known(tool.mid(prefix.size())) : QString();
    }
    // Codex: server and tool in rawInput.
    const QJsonObject input = toolCall.rawInput.toObject();
    if (input.value(QLatin1String("server")).toString() == name() && input.contains(QLatin1String("tool"))) {
        return known(input.value(QLatin1String("tool")).toString());
    }
    // OpenCode: only the title; so only exactly that, and nothing that names files.
    const QString prefix = name() + QLatin1Char('_');
    if (toolCall.title.startsWith(prefix) && (toolCall.kind.isEmpty() || toolCall.kind == QLatin1String("other")) && toolCall.locations.isEmpty()) {
        return known(toolCall.title.mid(prefix.size()));
    }
    return QString();
}

QString WhaleAiMcpServer::relay()
{
    const QString local = QCoreApplication::applicationDirPath() + QStringLiteral("/orcinus-mcp");
    if (QFileInfo(local).isExecutable()) {
        return local;
    }
    const QString installed = QStringLiteral(WHALE_MCP_INSTALLED);
    if (QFileInfo(installed).isExecutable()) {
        return installed;
    }
    return QString();
}

bool WhaleAiMcpServer::listen()
{
    const QString base = baseFolder();
    if (!QDir().mkpath(base)) {
        m_error = i18nc("@info %1 is a folder", "Cannot create %1.", base);
        return false;
    }
    QFile::setPermissions(base, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    removeLeftovers(base);

    // Made with 0700.
    m_dir = std::make_unique<QTemporaryDir>(base + QStringLiteral("/c-XXXXXX"));
    if (!m_dir->isValid()) {
        m_error = m_dir->errorString();
        m_dir.reset();
        return false;
    }
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(m_dir->filePath(QStringLiteral("mcp")))) {
        m_error = m_server->errorString();
        delete m_server;
        m_server = nullptr;
        m_dir.reset();
        return false;
    }
    connect(m_server, &QLocalServer::newConnection, this, &WhaleAiMcpServer::newConnection);

    QByteArray random(24, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(random.data()), random.size() / sizeof(quint32));
    m_token = QString::fromLatin1(random.toHex());
    return true;
}

QString WhaleAiMcpServer::errorString() const
{
    return m_error;
}

QString WhaleAiMcpServer::socketPath() const
{
    return m_server ? m_server->fullServerName() : QString();
}

QString WhaleAiMcpServer::token() const
{
    return m_token;
}

WhaleAcpMcpServer WhaleAiMcpServer::mcpServer(const QString &program) const
{
    WhaleAcpMcpServer server;
    server.name = name();
    server.command = program;
    server.arguments = {QStringLiteral("--socket"), socketPath(), QStringLiteral("--token"), m_token};
    return server;
}

void WhaleAiMcpServer::setContextProvider(const std::function<WhaleAiContext()> &provider)
{
    m_contextProvider = provider;
}

void WhaleAiMcpServer::setAccess(const std::function<bool(const QString &)> &access)
{
    m_access = access;
}

void WhaleAiMcpServer::setViewActions(const WhaleAiViewActions &actions)
{
    m_viewActions = actions;
}

void WhaleAiMcpServer::setRememberLanguage(const std::function<void(const QString &)> &remember)
{
    m_rememberLanguage = remember;
}

void WhaleAiMcpServer::newConnection()
{
    while (QLocalSocket *socket = m_server->nextPendingConnection()) {
        socket->setParent(this);
        m_authorized.insert(socket, false);
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
            readConnection(socket);
        });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
            m_authorized.remove(socket);
            socket->deleteLater();
        });
    }
}

void WhaleAiMcpServer::readConnection(QLocalSocket *socket)
{
    while (socket->canReadLine()) {
        const QByteArray line = socket->readLine(MaxLine + 1).trimmed();
        if (!m_authorized.value(socket)) {
            if (m_token.isEmpty() || line != m_token.toLatin1()) {
                qCWarning(WhaleAiDebug) << "MCP connection with a wrong token closed";
                m_authorized.remove(socket);
                socket->abort();
                socket->deleteLater();
                return;
            }
            m_authorized.insert(socket, true);
            continue;
        }
        if (line.isEmpty()) {
            continue;
        }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
        QJsonObject answer;
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            answer = errorResponse(QJsonValue::Null, -32700, QStringLiteral("Parse error"));
        } else {
            answer = handle(document.object());
        }
        if (!answer.isEmpty()) {
            socket->write(QJsonDocument(answer).toJson(QJsonDocument::Compact) + '\n');
        }
    }
    if (socket->bytesAvailable() > MaxLine) {
        qCWarning(WhaleAiDebug) << "MCP connection closed: message too long";
        socket->abort();
    }
}

QJsonObject WhaleAiMcpServer::handle(const QJsonObject &message)
{
    const QString method = message.value(QLatin1String("method")).toString();
    const bool isRequest = message.contains(QLatin1String("id"));
    const QJsonValue id = message.value(QLatin1String("id"));
    if (method.isEmpty()) {
        // A response: we send no requests.
        return isRequest && !message.contains(QLatin1String("result")) && !message.contains(QLatin1String("error"))
            ? errorResponse(id, -32600, QStringLiteral("Invalid request"))
            : QJsonObject();
    }
    if (!isRequest) {
        // notifications/initialized, notifications/cancelled: nothing to do.
        return QJsonObject();
    }
    const QJsonObject params = message.value(QLatin1String("params")).toObject();
    if (method == QLatin1String("initialize")) {
        const QString asked = params.value(QLatin1String("protocolVersion")).toString();
        return response(
            id,
            {{QStringLiteral("protocolVersion"), protocolVersions().contains(asked) ? asked : protocolVersions().constFirst()},
             {QStringLiteral("capabilities"), QJsonObject{{QStringLiteral("tools"), QJsonObject()}}},
             {QStringLiteral("serverInfo"),
              QJsonObject{{QStringLiteral("name"), QStringLiteral("orcinus")}, {QStringLiteral("version"), QCoreApplication::applicationVersion()}}},
             {QStringLiteral("instructions"),
              QStringLiteral("Tools of the Orcinus file manager the user is chatting with you from. They show where the user is.")}});
    }
    if (method == QLatin1String("ping")) {
        return response(id, QJsonObject());
    }
    if (method == QLatin1String("tools/list")) {
        return response(id, {{QStringLiteral("tools"), tools()}});
    }
    if (method == QLatin1String("tools/call")) {
        const QString name = params.value(QLatin1String("name")).toString();
        bool known = false;
        const QJsonObject result = callTool(name, params.value(QLatin1String("arguments")).toObject(), &known);
        if (!known) {
            return errorResponse(id, -32602, QStringLiteral("Unknown tool: %1").arg(name));
        }
        Q_EMIT toolCalled(name, result.value(QLatin1String("isError")).toBool());
        return response(id, result);
    }
    return errorResponse(id, -32601, QStringLiteral("Method not found: %1").arg(method));
}

QJsonObject WhaleAiMcpServer::callTool(const QString &name, const QJsonObject &arguments, bool *known)
{
    *known = true;
    bool error = false;
    if (name == QLatin1String("get_location")) {
        return toolResult(location());
    }
    if (name == QLatin1String("get_selection")) {
        return toolResult(selection());
    }
    if (name == QLatin1String("set_location")) {
        const QJsonObject result = setLocation(arguments, &error);
        return toolResult(result, error);
    }
    if (name == QLatin1String("select_items")) {
        const QJsonObject result = selectItems(arguments, &error);
        return toolResult(result, error);
    }
    if (name == QLatin1String("remember_language")) {
        const QJsonObject result = rememberLanguage(arguments, &error);
        return toolResult(result, error);
    }
    *known = false;
    return QJsonObject();
}

QJsonObject WhaleAiMcpServer::setLocation(const QJsonObject &arguments, bool *error)
{
    *error = true;
    const QString path = arguments.value(QLatin1String("path")).toString();
    if (path.isEmpty() || !QDir::isAbsolutePath(path)) {
        return toolError(QStringLiteral("\"path\" must be an absolute path."));
    }
    const QString clean = QDir::cleanPath(path);
    // Before anything that tells whether it exists. A link inside that leads outside counts as outside.
    const QString canonical = QFileInfo(clean).canonicalFilePath();
    if (!reaches(clean) || (!canonical.isEmpty() && !reaches(canonical))) {
        return toolError(QStringLiteral("You cannot reach this folder; the user can give you access in Orcinus."));
    }
    if (!QFileInfo(clean).isDir()) {
        return toolError(QStringLiteral("Not an existing folder: %1").arg(clean));
    }
    if (!m_viewActions.openFolder) {
        return toolError(QStringLiteral("Orcinus cannot change its view here."));
    }
    m_viewActions.openFolder(clean);
    *error = false;
    Q_EMIT locationSet(clean);
    return {{QStringLiteral("folder"), clean}};
}

QJsonObject WhaleAiMcpServer::selectItems(const QJsonObject &arguments, bool *error)
{
    *error = true;
    const QJsonValue value = arguments.value(QLatin1String("items"));
    if (!value.isArray()) {
        return toolError(QStringLiteral("\"items\" must be a list of names or paths."));
    }
    const QJsonArray items = value.toArray();
    if (items.size() > MaxSelection) {
        return toolError(QStringLiteral("At most %1 items.").arg(MaxSelection));
    }
    const WhaleAiContext context = m_contextProvider ? m_contextProvider() : WhaleAiContext();
    if (context.folder.isEmpty()) {
        return toolError(QStringLiteral("The active view does not show a local folder."));
    }
    const QString folder = QDir::cleanPath(context.folder);
    if (!reaches(folder)) {
        return toolError(QStringLiteral("You cannot reach the folder open in Orcinus; the user can give you access."));
    }
    if (!m_viewActions.select) {
        return toolError(QStringLiteral("Orcinus cannot change its view here."));
    }
    QStringList found;
    QJsonArray notFound;
    for (const QJsonValue &item : items) {
        const QString text = item.toString();
        QString path;
        if (QDir::isAbsolutePath(text)) {
            path = QDir::cleanPath(text);
        } else if (!text.isEmpty() && !text.contains(QLatin1Char('/')) && text != QLatin1String(".") && text != QLatin1String("..")) {
            path = QDir::cleanPath(QDir(folder).filePath(text));
        }
        const QFileInfo info(path);
        // Directly in the folder: what the view lists.
        if (path.isEmpty() || info.absolutePath() != folder || !(info.exists() || info.isSymLink())) {
            notFound.append(text);
        } else if (!found.contains(path)) {
            found.append(path);
        }
    }
    *error = false;
    if (!found.isEmpty()) {
        m_viewActions.select(found);
        Q_EMIT itemsSelected(found.size());
    }
    return {{QStringLiteral("folder"), folder}, {QStringLiteral("selected"), int(found.size())}, {QStringLiteral("notFound"), notFound}};
}

QJsonObject WhaleAiMcpServer::location()
{
    const WhaleAiContext context = m_contextProvider ? m_contextProvider() : WhaleAiContext();
    QJsonObject result;
    if (context.folder.isEmpty()) {
        result.insert(QStringLiteral("folder"), QJsonValue::Null);
        result.insert(QStringLiteral("note"), QStringLiteral("The active view does not show a local folder."));
    } else {
        result.insert(QStringLiteral("folder"), context.folder);
        result.insert(QStringLiteral("access"), reaches(context.folder));
    }
    if (!context.otherFolder.isEmpty()) {
        result.insert(QStringLiteral("otherFolder"), context.otherFolder);
        result.insert(QStringLiteral("otherAccess"), reaches(context.otherFolder));
    }
    return result;
}

QJsonObject WhaleAiMcpServer::selection()
{
    const WhaleAiContext context = m_contextProvider ? m_contextProvider() : WhaleAiContext();
    QJsonArray items;
    for (const QString &path : context.selection) {
        if (items.size() == MaxSelection) {
            break;
        }
        items.append(QJsonObject{{QStringLiteral("path"), path}, {QStringLiteral("access"), reaches(path)}});
    }
    QJsonObject result{{QStringLiteral("count"), int(context.selection.size())},
                       {QStringLiteral("items"), items},
                       {QStringLiteral("truncated"), context.selection.size() > MaxSelection}};
    result.insert(QStringLiteral("folder"), context.folder.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(context.folder));
    return result;
}

QJsonObject WhaleAiMcpServer::rememberLanguage(const QJsonObject &arguments, bool *error)
{
    *error = true;
    QString language = arguments.value(QLatin1String("language")).toString().simplified();
    if (language.isEmpty()) {
        return toolError(QStringLiteral("\"language\" must be the name of a language."));
    }
    // A name, not a sentence: keep it short and on one line.
    language.truncate(40);
    if (!m_rememberLanguage) {
        return toolError(QStringLiteral("Orcinus cannot remember a language here."));
    }
    m_rememberLanguage(language);
    *error = false;
    return {{QStringLiteral("language"), language}};
}

bool WhaleAiMcpServer::reaches(const QString &path) const
{
    return !m_access || m_access(path);
}
