/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * A scripted ACP agent for the tests of WhaleAcpConnection and WhaleAcpSession. Its traffic is
 * modelled on the real agents of the AI0 spike (Claude Agent, Codex, OpenCode), see
 * .privatedoc/ai-support-plan.md. The first argument picks the scenario:
 *
 *   basic        configOptions; a prompt streams chunks, asks for a file the client does not
 *                offer (fs/read_text_file), starts a tool call and asks for permission
 *   modes        only the older `modes` field; set_mode answers with current_mode_update
 *   crash        writes to stderr and exits with code 3 when it gets a prompt
 *   garbage      writes a line that is no JSON before answering initialize
 *   oldprotocol  answers initialize with protocol version 99
 *   auth         session/new fails with "auth required"
 *   hang         never answers
 *
 * What the client sent comes back as text in agent_message_chunk, so the tests can check it.
 */

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <cstdio>
#include <iostream>
#include <string>

namespace
{
QString scenario;
QString sessionCwd;
QStringList mcpNames;
bool cancelSeen = false;
QString modelValue = QStringLiteral("small");
QString modeValue = QStringLiteral("auto");

void send(const QJsonObject &message)
{
    QJsonObject full = message;
    full.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    const QByteArray line = QJsonDocument(full).toJson(QJsonDocument::Compact);
    std::fwrite(line.constData(), 1, line.size(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

void respond(const QJsonValue &id, const QJsonObject &result)
{
    send({{QStringLiteral("id"), id}, {QStringLiteral("result"), result}});
}

void respondError(const QJsonValue &id, int code, const QString &message)
{
    send({{QStringLiteral("id"), id}, {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), code}, {QStringLiteral("message"), message}}}});
}

void update(const QString &sessionId, const QJsonObject &update)
{
    send({{QStringLiteral("method"), QStringLiteral("session/update")},
          {QStringLiteral("params"), QJsonObject{{QStringLiteral("sessionId"), sessionId}, {QStringLiteral("update"), update}}}});
}

void chunk(const QString &kind, const QString &text)
{
    update(QStringLiteral("s1"),
           {{QStringLiteral("sessionUpdate"), kind},
            {QStringLiteral("content"), QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), text}}}});
}

/** @return The next message; ends the agent on EOF, as real agents do. */
QJsonObject readMessage()
{
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            continue;
        }
        const QJsonObject message = QJsonDocument::fromJson(QByteArray::fromStdString(line)).object();
        if (message.value(QLatin1String("method")).toString() == QLatin1String("session/cancel")) {
            cancelSeen = true;
        }
        return message;
    }
    std::exit(0);
}

/** Reads until the response to @p id, noting a session/cancel on the way. */
QJsonObject waitForResponse(const QJsonValue &id)
{
    while (true) {
        const QJsonObject message = readMessage();
        if (!message.contains(QLatin1String("method")) && message.value(QLatin1String("id")) == id) {
            return message;
        }
    }
}

QJsonArray configOptions()
{
    return {
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("mode")},
            {QStringLiteral("name"), QStringLiteral("Mode")},
            {QStringLiteral("category"), QStringLiteral("mode")},
            {QStringLiteral("type"), QStringLiteral("select")},
            {QStringLiteral("currentValue"), modeValue},
            {QStringLiteral("options"),
             QJsonArray{
                 QJsonObject{{QStringLiteral("value"), QStringLiteral("ask")},
                             {QStringLiteral("name"), QStringLiteral("Ask")},
                             {QStringLiteral("description"), QStringLiteral("Always ask")}},
                 QJsonObject{{QStringLiteral("value"), QStringLiteral("auto")}, {QStringLiteral("name"), QStringLiteral("Auto")}},
             }},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("model")},
            {QStringLiteral("name"), QStringLiteral("Model")},
            {QStringLiteral("category"), QStringLiteral("model")},
            {QStringLiteral("type"), QStringLiteral("select")},
            {QStringLiteral("currentValue"), modelValue},
            {QStringLiteral("options"),
             QJsonArray{
                 QJsonObject{{QStringLiteral("group"), QStringLiteral("local")},
                             {QStringLiteral("name"), QStringLiteral("Local")},
                             {QStringLiteral("options"),
                              QJsonArray{QJsonObject{{QStringLiteral("value"), QStringLiteral("small")}, {QStringLiteral("name"), QStringLiteral("Small")}}}}},
                 QJsonObject{{QStringLiteral("group"), QStringLiteral("cloud")},
                             {QStringLiteral("name"), QStringLiteral("Cloud")},
                             {QStringLiteral("options"),
                              QJsonArray{QJsonObject{{QStringLiteral("value"), QStringLiteral("big")}, {QStringLiteral("name"), QStringLiteral("Big")}}}}},
             }},
        },
        // A type Orcinus does not advertise: must be left out.
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("fast")},
            {QStringLiteral("name"), QStringLiteral("Fast")},
            {QStringLiteral("type"), QStringLiteral("boolean")},
            {QStringLiteral("currentValue"), false},
        },
    };
}

void handleInitialize(const QJsonObject &message)
{
    const QJsonObject params = message.value(QLatin1String("params")).toObject();
    if (params.value(QLatin1String("protocolVersion")).toInt() != 1 || !params.value(QLatin1String("clientCapabilities")).isObject()) {
        respondError(message.value(QLatin1String("id")), -32602, QStringLiteral("bad initialize"));
        return;
    }
    if (scenario == QLatin1String("garbage")) {
        std::fputs("this is not json\n", stdout);
        std::fflush(stdout);
    }
    respond(message.value(QLatin1String("id")),
            {{QStringLiteral("protocolVersion"), scenario == QLatin1String("oldprotocol") ? 99 : 1},
             {QStringLiteral("agentInfo"),
              QJsonObject{{QStringLiteral("name"), QStringLiteral("fake-agent")},
                          {QStringLiteral("title"), QStringLiteral("Fake Agent")},
                          {QStringLiteral("version"), QStringLiteral("1.2.3")}}},
             {QStringLiteral("agentCapabilities"),
              QJsonObject{{QStringLiteral("loadSession"), true},
                          {QStringLiteral("promptCapabilities"), QJsonObject{{QStringLiteral("image"), true}, {QStringLiteral("embeddedContext"), true}}},
                          {QStringLiteral("mcpCapabilities"), QJsonObject{{QStringLiteral("http"), true}, {QStringLiteral("sse"), false}}}}},
             {QStringLiteral("authMethods"),
              QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("login")},
                                     {QStringLiteral("name"), QStringLiteral("Login")},
                                     {QStringLiteral("description"), QStringLiteral("Run `fake login` in a terminal")}}}}});
}

void handleNewSession(const QJsonObject &message)
{
    const QJsonValue id = message.value(QLatin1String("id"));
    if (scenario == QLatin1String("auth")) {
        respondError(id, -32000, QStringLiteral("Authentication required"));
        return;
    }
    const QJsonObject params = message.value(QLatin1String("params")).toObject();
    sessionCwd = params.value(QLatin1String("cwd")).toString();
    const QJsonArray servers = params.value(QLatin1String("mcpServers")).toArray();
    for (const QJsonValue &server : servers) {
        mcpNames.append(server.toObject().value(QLatin1String("name")).toString() + QLatin1Char(':')
                        + server.toObject().value(QLatin1String("command")).toString());
    }

    QJsonObject result{{QStringLiteral("sessionId"), QStringLiteral("s1")}};
    const QJsonObject modes{
        {QStringLiteral("currentModeId"), modeValue},
        {QStringLiteral("availableModes"),
         QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("ask")}, {QStringLiteral("name"), QStringLiteral("Ask")}},
                    QJsonObject{{QStringLiteral("id"), QStringLiteral("auto")}, {QStringLiteral("name"), QStringLiteral("Auto")}}}},
    };
    result.insert(QStringLiteral("modes"), modes);
    if (scenario != QLatin1String("modes")) {
        result.insert(QStringLiteral("configOptions"), configOptions());
    }
    respond(id, result);
}

void handlePrompt(const QJsonObject &message)
{
    const QJsonValue promptId = message.value(QLatin1String("id"));
    if (scenario == QLatin1String("crash")) {
        std::fputs("fatal: the model exploded\n", stderr);
        std::fflush(stderr);
        std::exit(3);
    }

    QStringList texts;
    int links = 0;
    const QJsonArray prompt = message.value(QLatin1String("params")).toObject().value(QLatin1String("prompt")).toArray();
    for (const QJsonValue &value : prompt) {
        const QJsonObject block = value.toObject();
        if (block.value(QLatin1String("type")).toString() == QLatin1String("text")) {
            texts.append(block.value(QLatin1String("text")).toString());
        } else if (block.value(QLatin1String("type")).toString() == QLatin1String("resource_link")) {
            ++links;
        }
    }
    const QString text = texts.join(QLatin1Char(' '));

    // Updates the client must ignore.
    update(QStringLiteral("other"),
           {{QStringLiteral("sessionUpdate"), QStringLiteral("agent_message_chunk")},
            {QStringLiteral("content"),
             QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), QStringLiteral("WRONG SESSION")}}}});
    update(QStringLiteral("s1"), {{QStringLiteral("sessionUpdate"), QStringLiteral("future_kind")}, {QStringLiteral("whatever"), 1}});

    chunk(QStringLiteral("agent_thought_chunk"), QStringLiteral("thinking"));
    chunk(QStringLiteral("agent_message_chunk"),
          QStringLiteral("cwd=%1;mcp=%2;text=%3;links=%4").arg(sessionCwd, mcpNames.join(QLatin1Char(',')), text).arg(links));

    // The client offers no fs/*: it must answer with an error.
    send({{QStringLiteral("id"), QStringLiteral("fs1")},
          {QStringLiteral("method"), QStringLiteral("fs/read_text_file")},
          {QStringLiteral("params"),
           QJsonObject{{QStringLiteral("sessionId"), QStringLiteral("s1")}, {QStringLiteral("path"), QStringLiteral("/etc/hostname")}}}});
    const QJsonObject fsAnswer = waitForResponse(QStringLiteral("fs1"));
    const int fsCode = fsAnswer.value(QLatin1String("error")).toObject().value(QLatin1String("code")).toInt();

    update(QStringLiteral("s1"),
           {{QStringLiteral("sessionUpdate"), QStringLiteral("tool_call")},
            {QStringLiteral("toolCallId"), QStringLiteral("t1")},
            {QStringLiteral("title"), QStringLiteral("Write note.txt")},
            {QStringLiteral("kind"), QStringLiteral("edit")},
            {QStringLiteral("status"), QStringLiteral("pending")},
            {QStringLiteral("locations"), QJsonArray{QJsonObject{{QStringLiteral("path"), QString(sessionCwd + QStringLiteral("/note.txt"))}}}}});

    QString permission = QStringLiteral("none");
    if (!text.contains(QLatin1String("nopermission"))) {
        send({{QStringLiteral("id"), 7},
              {QStringLiteral("method"), QStringLiteral("session/request_permission")},
              {QStringLiteral("params"),
               QJsonObject{{QStringLiteral("sessionId"), QStringLiteral("s1")},
                           {QStringLiteral("toolCall"),
                            QJsonObject{{QStringLiteral("toolCallId"), QStringLiteral("t1")},
                                        {QStringLiteral("rawInput"), QJsonObject{{QStringLiteral("content"), QStringLiteral("hi")}}}}},
                           {QStringLiteral("options"),
                            QJsonArray{QJsonObject{{QStringLiteral("optionId"), QStringLiteral("yes")},
                                                   {QStringLiteral("name"), QStringLiteral("Yes")},
                                                   {QStringLiteral("kind"), QStringLiteral("allow_once")}},
                                       QJsonObject{{QStringLiteral("optionId"), QStringLiteral("always")},
                                                   {QStringLiteral("name"), QStringLiteral("Always")},
                                                   {QStringLiteral("kind"), QStringLiteral("allow_always")}},
                                       QJsonObject{{QStringLiteral("optionId"), QStringLiteral("no")},
                                                   {QStringLiteral("name"), QStringLiteral("No")},
                                                   {QStringLiteral("kind"), QStringLiteral("reject_once")}}}}}}});
        const QJsonObject outcome = waitForResponse(7).value(QLatin1String("result")).toObject().value(QLatin1String("outcome")).toObject();
        permission = outcome.value(QLatin1String("outcome")).toString() == QLatin1String("selected") ? outcome.value(QLatin1String("optionId")).toString()
                                                                                                     : QStringLiteral("cancelled");
    }

    if (cancelSeen || text.contains(QLatin1String("waitforcancel"))) {
        while (!cancelSeen) {
            readMessage();
        }
        chunk(QStringLiteral("agent_message_chunk"), QStringLiteral("permission=%1;fs=%2").arg(permission).arg(fsCode));
        respond(promptId, {{QStringLiteral("stopReason"), QStringLiteral("cancelled")}});
        cancelSeen = false;
        return;
    }

    chunk(QStringLiteral("agent_message_chunk"), QStringLiteral("permission=%1;fs=%2").arg(permission).arg(fsCode));
    update(QStringLiteral("s1"),
           {{QStringLiteral("sessionUpdate"), QStringLiteral("tool_call_update")},
            {QStringLiteral("toolCallId"), QStringLiteral("t1")},
            {QStringLiteral("status"), QStringLiteral("completed")},
            {QStringLiteral("rawOutput"), QStringLiteral("ok")}});
    update(QStringLiteral("s1"),
           {{QStringLiteral("sessionUpdate"), QStringLiteral("usage_update")}, {QStringLiteral("used"), 1500}, {QStringLiteral("size"), 32000}});
    update(QStringLiteral("s1"),
           {{QStringLiteral("sessionUpdate"), QStringLiteral("available_commands_update")},
            {QStringLiteral("availableCommands"),
             QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("review")},
                                    {QStringLiteral("description"), QStringLiteral("Review")},
                                    {QStringLiteral("input"), QJsonObject{{QStringLiteral("hint"), QStringLiteral("what")}}}}}}});
    respond(promptId, {{QStringLiteral("stopReason"), QStringLiteral("end_turn")}});
}

void handleSetConfigOption(const QJsonObject &message)
{
    const QJsonObject params = message.value(QLatin1String("params")).toObject();
    const QString configId = params.value(QLatin1String("configId")).toString();
    const QString value = params.value(QLatin1String("value")).toString();
    if (configId == QLatin1String("model")) {
        modelValue = value;
        // Like a real agent: the model change also moves the mode.
        modeValue = QStringLiteral("ask");
    } else if (configId == QLatin1String("mode")) {
        modeValue = value;
    } else {
        respondError(message.value(QLatin1String("id")), -32602, QStringLiteral("unknown option"));
        return;
    }
    respond(message.value(QLatin1String("id")), {{QStringLiteral("configOptions"), configOptions()}});
}

void handleSetMode(const QJsonObject &message)
{
    modeValue = message.value(QLatin1String("params")).toObject().value(QLatin1String("modeId")).toString();
    respond(message.value(QLatin1String("id")), {});
    update(QStringLiteral("s1"), {{QStringLiteral("sessionUpdate"), QStringLiteral("current_mode_update")}, {QStringLiteral("currentModeId"), modeValue}});
}
}

int main(int argc, char **argv)
{
    scenario = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("basic");
    std::fputs("fake agent started\n", stderr);
    std::fflush(stderr);

    while (true) {
        const QJsonObject message = readMessage();
        if (scenario == QLatin1String("hang")) {
            continue;
        }
        const QString method = message.value(QLatin1String("method")).toString();
        if (method == QLatin1String("initialize")) {
            handleInitialize(message);
        } else if (method == QLatin1String("session/new")) {
            handleNewSession(message);
        } else if (method == QLatin1String("session/prompt")) {
            handlePrompt(message);
        } else if (method == QLatin1String("session/set_config_option")) {
            handleSetConfigOption(message);
        } else if (method == QLatin1String("session/set_mode")) {
            handleSetMode(message);
        } else if (message.contains(QLatin1String("id")) && !method.isEmpty()) {
            respondError(message.value(QLatin1String("id")), -32601, QStringLiteral("unknown method"));
        }
    }
}
