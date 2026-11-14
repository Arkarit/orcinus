/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaimcpserver.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <cstring>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

/** The back channel of the AI agents (AI6): WhaleAiMcpServer and the relay orcinus-mcp. */
class WhaleAiMcpServerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initialize();
    void toolsList();
    void errors();
    void location();
    void selection();
    void setLocation();
    void selectItems();
    void ownTool();
    void socketFolder();
    void relay();
    void wrongToken();
    void tooLong();
    void leftovers();

private:
    static QJsonObject request(WhaleAiMcpServer &server, const QString &method, const QJsonObject &params = {});
    static QJsonObject toolResult(WhaleAiMcpServer &server, const QString &tool, const QJsonObject &arguments = {}, bool *error = nullptr);
};

QJsonObject WhaleAiMcpServerTest::request(WhaleAiMcpServer &server, const QString &method, const QJsonObject &params)
{
    return server.handle({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                          {QStringLiteral("id"), 1},
                          {QStringLiteral("method"), method},
                          {QStringLiteral("params"), params}});
}

QJsonObject WhaleAiMcpServerTest::toolResult(WhaleAiMcpServer &server, const QString &tool, const QJsonObject &arguments, bool *error)
{
    const QJsonObject result = request(server, QStringLiteral("tools/call"), {{QStringLiteral("name"), tool}, {QStringLiteral("arguments"), arguments}})
                                   .value(QLatin1String("result"))
                                   .toObject();
    if (error) {
        *error = result.value(QLatin1String("isError")).toBool();
    }
    // The text says the same as the structured content.
    const QJsonObject structured = result.value(QLatin1String("structuredContent")).toObject();
    const QString text = result.value(QLatin1String("content")).toArray().first().toObject().value(QLatin1String("text")).toString();
    if (QJsonDocument::fromJson(text.toUtf8()).object() != structured) {
        return QJsonObject{{QStringLiteral("mismatch"), text}};
    }
    return structured;
}

void WhaleAiMcpServerTest::initialize()
{
    WhaleAiMcpServer server;
    QJsonObject result = request(server, QStringLiteral("initialize"), {{QStringLiteral("protocolVersion"), QStringLiteral("2025-03-26")}})
                             .value(QLatin1String("result"))
                             .toObject();
    QCOMPARE(result.value(QLatin1String("protocolVersion")).toString(), QStringLiteral("2025-03-26"));
    QVERIFY(result.value(QLatin1String("capabilities")).toObject().contains(QLatin1String("tools")));
    QCOMPARE(result.value(QLatin1String("serverInfo")).toObject().value(QLatin1String("name")).toString(), QStringLiteral("orcinus"));
    // One we do not know: the newest we speak.
    result = request(server, QStringLiteral("initialize"), {{QStringLiteral("protocolVersion"), QStringLiteral("2099-01-01")}})
                 .value(QLatin1String("result"))
                 .toObject();
    QCOMPARE(result.value(QLatin1String("protocolVersion")).toString(), QStringLiteral("2025-06-18"));
    // Notifications get no answer.
    QVERIFY(
        server.handle({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("method"), QStringLiteral("notifications/initialized")}}).isEmpty());
    QVERIFY(request(server, QStringLiteral("ping")).contains(QLatin1String("result")));
}

void WhaleAiMcpServerTest::toolsList()
{
    WhaleAiMcpServer server;
    const QJsonArray tools = request(server, QStringLiteral("tools/list")).value(QLatin1String("result")).toObject().value(QLatin1String("tools")).toArray();
    QStringList names;
    for (const QJsonValue &tool : tools) {
        names.append(tool.toObject().value(QLatin1String("name")).toString());
        QCOMPARE(tool.toObject().value(QLatin1String("inputSchema")).toObject().value(QLatin1String("type")).toString(), QStringLiteral("object"));
        const QJsonObject annotations = tool.toObject().value(QLatin1String("annotations")).toObject();
        // Only the get_ tools leave the view as it is; none of them changes a file.
        QCOMPARE(annotations.value(QLatin1String("readOnlyHint")).toBool(), names.last().startsWith(QLatin1String("get_")));
        QVERIFY(!annotations.value(QLatin1String("destructiveHint")).toBool());
        QVERIFY(!tool.toObject().value(QLatin1String("description")).toString().isEmpty());
    }
    QCOMPARE(names,
             (QStringList{QStringLiteral("get_location"), QStringLiteral("get_selection"), QStringLiteral("set_location"), QStringLiteral("select_items")}));
    QCOMPARE(tools.at(2).toObject().value(QLatin1String("inputSchema")).toObject().value(QLatin1String("required")).toArray(),
             QJsonArray{QStringLiteral("path")});
}

void WhaleAiMcpServerTest::errors()
{
    WhaleAiMcpServer server;
    QCOMPARE(request(server, QStringLiteral("resources/list")).value(QLatin1String("error")).toObject().value(QLatin1String("code")).toInt(), -32601);
    QCOMPARE(request(server, QStringLiteral("tools/call"), {{QStringLiteral("name"), QStringLiteral("rm_rf")}})
                 .value(QLatin1String("error"))
                 .toObject()
                 .value(QLatin1String("code"))
                 .toInt(),
             -32602);
    // A response to nothing we asked is ignored.
    QVERIFY(server.handle({{QStringLiteral("id"), 5}, {QStringLiteral("result"), QJsonObject()}}).isEmpty());
}

void WhaleAiMcpServerTest::location()
{
    WhaleAiMcpServer server;
    // No provider, or not a local folder.
    QJsonObject result = toolResult(server, QStringLiteral("get_location"));
    QVERIFY(result.value(QLatin1String("folder")).isNull());
    QVERIFY(result.contains(QLatin1String("note")));

    server.setContextProvider([] {
        return WhaleAiContext{QStringLiteral("/home/u/work"), {}, QStringLiteral("/home/u/private"), {}, {}};
    });
    QSignalSpy called(&server, &WhaleAiMcpServer::toolCalled);
    result = toolResult(server, QStringLiteral("get_location"));
    QCOMPARE(result,
             (QJsonObject{{QStringLiteral("folder"), QStringLiteral("/home/u/work")},
                          {QStringLiteral("access"), true},
                          {QStringLiteral("otherFolder"), QStringLiteral("/home/u/private")},
                          {QStringLiteral("otherAccess"), true}}));
    QCOMPARE(called.size(), 1);
    QCOMPARE(called.first().at(0).toString(), QStringLiteral("get_location"));

    // In the sandbox: what the agent does not reach is marked.
    server.setAccess([](const QString &path) {
        return path.startsWith(QLatin1String("/home/u/work"));
    });
    result = toolResult(server, QStringLiteral("get_location"));
    QVERIFY(result.value(QLatin1String("access")).toBool());
    QVERIFY(!result.value(QLatin1String("otherAccess")).toBool());
}

void WhaleAiMcpServerTest::selection()
{
    WhaleAiMcpServer server;
    QStringList many;
    for (int i = 0; i < WhaleAiMcpServer::MaxSelection + 20; ++i) {
        many.append(QStringLiteral("/w/f%1").arg(i));
    }
    QStringList selection{QStringLiteral("/w/a"), QStringLiteral("/elsewhere/b")};
    server.setContextProvider([&] {
        return WhaleAiContext{QStringLiteral("/w"), selection, {}, {}, {}};
    });
    server.setAccess([](const QString &path) {
        return path.startsWith(QLatin1String("/w/"));
    });
    QJsonObject result = toolResult(server, QStringLiteral("get_selection"));
    QCOMPARE(result.value(QLatin1String("count")).toInt(), 2);
    QVERIFY(!result.value(QLatin1String("truncated")).toBool());
    QCOMPARE(result.value(QLatin1String("items")).toArray(),
             (QJsonArray{QJsonObject{{QStringLiteral("path"), QStringLiteral("/w/a")}, {QStringLiteral("access"), true}},
                         QJsonObject{{QStringLiteral("path"), QStringLiteral("/elsewhere/b")}, {QStringLiteral("access"), false}}}));

    selection = many;
    result = toolResult(server, QStringLiteral("get_selection"));
    QCOMPARE(result.value(QLatin1String("count")).toInt(), many.size());
    QCOMPARE(result.value(QLatin1String("items")).toArray().size(), WhaleAiMcpServer::MaxSelection);
    QVERIFY(result.value(QLatin1String("truncated")).toBool());
}

void WhaleAiMcpServerTest::setLocation()
{
    QTemporaryDir shared;
    QTemporaryDir outside;
    QVERIFY(shared.isValid() && outside.isValid());
    QVERIFY(QDir(shared.path()).mkdir(QStringLiteral("sub")));
    QVERIFY(QFile::link(outside.path(), shared.filePath(QStringLiteral("link"))));
    QFile file(shared.filePath(QStringLiteral("file.txt")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();

    WhaleAiMcpServer server;
    QStringList opened;
    auto setLocation = [&server](const QString &path, bool *error) {
        return toolResult(server, QStringLiteral("set_location"), {{QStringLiteral("path"), path}}, error);
    };
    bool error = false;
    // Without the window: an error, nothing happens.
    setLocation(shared.path(), &error);
    QVERIFY(error);
    server.setViewActions({[&](const QString &folder) {
                               opened.append(folder);
                           },
                           {}});
    server.setAccess([&](const QString &path) {
        return path == shared.path() || path.startsWith(shared.path() + QLatin1Char('/'));
    });
    QSignalSpy set(&server, &WhaleAiMcpServer::locationSet);

    QJsonObject result = setLocation(shared.filePath(QStringLiteral("sub/../sub/")), &error);
    QVERIFY(!error);
    QCOMPARE(result.value(QLatin1String("folder")).toString(), shared.filePath(QStringLiteral("sub")));
    QCOMPARE(opened, QStringList{shared.filePath(QStringLiteral("sub"))});
    QCOMPARE(set.size(), 1);

    // Outside, existing or not, the same answer; a link inside to outside is outside.
    const QString outsideAnswer = setLocation(outside.path(), &error).value(QLatin1String("error")).toString();
    QVERIFY(error);
    QCOMPARE(setLocation(outside.filePath(QStringLiteral("nothing")), &error).value(QLatin1String("error")).toString(), outsideAnswer);
    QCOMPARE(setLocation(shared.filePath(QStringLiteral("link")), &error).value(QLatin1String("error")).toString(), outsideAnswer);
    QCOMPARE(setLocation(shared.filePath(QStringLiteral("../")), &error).value(QLatin1String("error")).toString(), outsideAnswer);
    // Inside: missing, a file, relative, no path.
    setLocation(shared.filePath(QStringLiteral("nothing")), &error);
    QVERIFY(error);
    setLocation(file.fileName(), &error);
    QVERIFY(error);
    setLocation(QStringLiteral("sub"), &error);
    QVERIFY(error);
    toolResult(server, QStringLiteral("set_location"), {}, &error);
    QVERIFY(error);
    QCOMPARE(opened.size(), 1);
    QCOMPARE(set.size(), 1);
}

void WhaleAiMcpServerTest::selectItems()
{
    QTemporaryDir shared;
    QVERIFY(shared.isValid());
    for (const char *name : {"a.txt", "b.txt", "c d.txt"}) {
        QFile file(shared.filePath(QString::fromLatin1(name)));
        QVERIFY(file.open(QIODevice::WriteOnly));
    }
    QVERIFY(QDir(shared.path()).mkpath(QStringLiteral("sub/deep")));
    QString folder = shared.path();
    WhaleAiMcpServer server;
    server.setContextProvider([&] {
        return WhaleAiContext{folder, {}, {}, {}, {}};
    });
    QStringList selected;
    server.setViewActions({{}, [&](const QStringList &paths) {
                               selected = paths;
                           }});
    server.setAccess([&](const QString &path) {
        return path == shared.path() || path.startsWith(shared.path() + QLatin1Char('/'));
    });
    QSignalSpy spy(&server, &WhaleAiMcpServer::itemsSelected);
    auto select = [&server](const QStringList &items, bool *error) {
        return toolResult(server, QStringLiteral("select_items"), {{QStringLiteral("items"), QJsonArray::fromStringList(items)}}, error);
    };
    bool error = false;
    QJsonObject result = select({QStringLiteral("a.txt"),
                                 shared.filePath(QStringLiteral("c d.txt")),
                                 QStringLiteral("sub"),
                                 QStringLiteral("a.txt"),
                                 QStringLiteral("missing"),
                                 QStringLiteral("sub/deep"),
                                 QStringLiteral(".."),
                                 shared.filePath(QStringLiteral("sub/deep")),
                                 QStringLiteral("/etc/passwd")},
                                &error);
    QVERIFY(!error);
    QCOMPARE(selected,
             (QStringList{shared.filePath(QStringLiteral("a.txt")), shared.filePath(QStringLiteral("c d.txt")), shared.filePath(QStringLiteral("sub"))}));
    QCOMPARE(result.value(QLatin1String("selected")).toInt(), 3);
    QCOMPARE(result.value(QLatin1String("notFound")).toArray(),
             (QJsonArray{QStringLiteral("missing"),
                         QStringLiteral("sub/deep"),
                         QStringLiteral(".."),
                         shared.filePath(QStringLiteral("sub/deep")),
                         QStringLiteral("/etc/passwd")}));
    QCOMPARE(spy.size(), 1);
    QCOMPARE(spy.first().at(0).toInt(), 3);

    // Nothing found: the selection stays.
    selected.clear();
    result = select({QStringLiteral("missing")}, &error);
    QVERIFY(!error);
    QVERIFY(selected.isEmpty());
    QCOMPARE(spy.size(), 1);

    // Too many, no list, or a folder the agent does not reach.
    QStringList many;
    for (int i = 0; i <= WhaleAiMcpServer::MaxSelection; ++i) {
        many.append(QStringLiteral("a.txt"));
    }
    select(many, &error);
    QVERIFY(error);
    toolResult(server, QStringLiteral("select_items"), {{QStringLiteral("items"), QStringLiteral("a.txt")}}, &error);
    QVERIFY(error);
    folder = QDir::tempPath();
    select({QStringLiteral("a.txt")}, &error);
    QVERIFY(error);
    QVERIFY(selected.isEmpty());
}

void WhaleAiMcpServerTest::ownTool()
{
    // As the agents of AI6b ask.
    WhaleAcpToolCall claude;
    claude.title = QStringLiteral("mcp__orcinus__set_location");
    claude.kind = QStringLiteral("other");
    claude.meta = QJsonObject{{QStringLiteral("claudeCode"),
                               QJsonObject{{QStringLiteral("toolName"), QStringLiteral("mcp__orcinus__set_location")},
                                           {QStringLiteral("mcpServer"), QJsonObject{{QStringLiteral("name"), QStringLiteral("orcinus")}}}}}};
    QCOMPARE(WhaleAiMcpServer::ownTool(claude), QStringLiteral("set_location"));
    WhaleAcpToolCall codex;
    codex.title = QStringLiteral("mcp.orcinus.get_selection");
    codex.kind = QStringLiteral("execute");
    codex.rawInput = QJsonObject{{QStringLiteral("server"), QStringLiteral("orcinus")}, {QStringLiteral("tool"), QStringLiteral("get_selection")}};
    QCOMPARE(WhaleAiMcpServer::ownTool(codex), QStringLiteral("get_selection"));
    WhaleAcpToolCall opencode;
    opencode.title = QStringLiteral("orcinus_select_items");
    opencode.kind = QStringLiteral("other");
    QCOMPARE(WhaleAiMcpServer::ownTool(opencode), QStringLiteral("select_items"));

    // Not ours: another server, an unknown tool, a command titled like ours, files named.
    WhaleAcpToolCall other = claude;
    other.meta = QJsonObject{{QStringLiteral("claudeCode"),
                              QJsonObject{{QStringLiteral("toolName"), QStringLiteral("mcp__orcinus__set_location")},
                                          {QStringLiteral("mcpServer"), QJsonObject{{QStringLiteral("name"), QStringLiteral("other")}}}}}};
    other.title = QStringLiteral("Bash");
    QVERIFY(WhaleAiMcpServer::ownTool(other).isEmpty());
    WhaleAcpToolCall unknown = codex;
    unknown.rawInput = QJsonObject{{QStringLiteral("server"), QStringLiteral("orcinus")}, {QStringLiteral("tool"), QStringLiteral("delete_all")}};
    QVERIFY(WhaleAiMcpServer::ownTool(unknown).isEmpty());
    WhaleAcpToolCall spoof = opencode;
    spoof.kind = QStringLiteral("execute");
    QVERIFY(WhaleAiMcpServer::ownTool(spoof).isEmpty());
    WhaleAcpToolCall files = opencode;
    files.locations = {QStringLiteral("/home/u/file")};
    QVERIFY(WhaleAiMcpServer::ownTool(files).isEmpty());
    WhaleAcpToolCall titleOnly;
    titleOnly.title = QStringLiteral("mcp__orcinus__get_location");
    QVERIFY(WhaleAiMcpServer::ownTool(titleOnly).isEmpty());
}

void WhaleAiMcpServerTest::socketFolder()
{
    QString socket;
    {
        WhaleAiMcpServer server;
        QVERIFY(server.socketPath().isEmpty());
        QVERIFY2(server.listen(), qPrintable(server.errorString()));
        socket = server.socketPath();
        QVERIFY(socket.startsWith(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/orcinus-ai/c-")));
        const QFileInfo folder(QFileInfo(socket).absolutePath());
        QCOMPARE(folder.permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther | QFileDevice::WriteGroup | QFileDevice::WriteOther),
                 QFileDevice::Permissions());
        QCOMPARE(QFileInfo(folder.absolutePath()).permissions() & (QFileDevice::ReadOther | QFileDevice::ExeOther), QFileDevice::Permissions());
        QVERIFY(server.token().size() >= 32);
        const WhaleAcpMcpServer mcp = server.mcpServer(QStringLiteral("/x/orcinus-mcp"));
        QCOMPARE(mcp.name, QStringLiteral("orcinus"));
        QCOMPARE(mcp.arguments, (QStringList{QStringLiteral("--socket"), socket, QStringLiteral("--token"), server.token()}));
    }
    // Gone with the server.
    QVERIFY(!QFileInfo::exists(QFileInfo(socket).absolutePath()));
}

void WhaleAiMcpServerTest::relay()
{
    const QString relay = QStringLiteral(WHALE_MCP_RELAY);
    WhaleAiMcpServer server;
    QVERIFY(server.listen());
    server.setContextProvider([] {
        return WhaleAiContext{QStringLiteral("/w"), {}, {}, {}, {}};
    });
    QProcess process;
    process.start(relay, server.mcpServer(relay).arguments);
    QVERIFY(process.waitForStarted());
    process.write(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18"}})"
                  "\n"
                  R"({"jsonrpc":"2.0","method":"notifications/initialized"})"
                  "\n"
                  R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"get_location"}})"
                  "\n");
    QStringList lines;
    QVERIFY(QTest::qWaitFor(
        [&] {
            while (process.canReadLine()) {
                lines.append(QString::fromUtf8(process.readLine()).trimmed());
            }
            return lines.size() >= 2;
        },
        5000));
    QCOMPARE(QJsonDocument::fromJson(lines.at(0).toUtf8()).object().value(QLatin1String("id")).toInt(), 1);
    const QJsonObject call = QJsonDocument::fromJson(lines.at(1).toUtf8()).object();
    QCOMPARE(call.value(QLatin1String("id")).toInt(), 2);
    QCOMPARE(call.value(QLatin1String("result")).toObject().value(QLatin1String("structuredContent")).toObject().value(QLatin1String("folder")).toString(),
             QStringLiteral("/w"));
    // The agent closes stdin: the relay ends.
    process.closeWriteChannel();
    QVERIFY(QTest::qWaitFor(
        [&] {
            return process.state() == QProcess::NotRunning;
        },
        5000));
    QCOMPARE(process.exitCode(), 0);

    // No Orcinus behind the socket: it says so and ends.
    QProcess lost;
    lost.start(relay, {QStringLiteral("--socket"), QStringLiteral("/nonexistent/mcp"), QStringLiteral("--token"), QStringLiteral("t")});
    QVERIFY(lost.waitForFinished(5000));
    QCOMPARE(lost.exitCode(), 1);
    QVERIFY(lost.readAllStandardError().contains("cannot reach Orcinus"));
}

void WhaleAiMcpServerTest::wrongToken()
{
    WhaleAiMcpServer server;
    QVERIFY(server.listen());
    QLocalSocket socket;
    socket.connectToServer(server.socketPath());
    QVERIFY(socket.waitForConnected(2000));
    socket.write(
        "not-the-token\n"
        R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"get_location"}})"
        "\n");
    QTRY_COMPARE(socket.state(), QLocalSocket::UnconnectedState);
    QVERIFY(socket.readAll().isEmpty());

    // The right one, then a broken message: an error, and the connection stays.
    QLocalSocket good;
    good.connectToServer(server.socketPath());
    QVERIFY(good.waitForConnected(2000));
    good.write(server.token().toLatin1() + "\n{broken\n" R"({"jsonrpc":"2.0","id":2,"method":"ping"})" "\n");
    QStringList lines;
    QVERIFY(QTest::qWaitFor(
        [&] {
            while (good.canReadLine()) {
                lines.append(QString::fromUtf8(good.readLine()).trimmed());
            }
            return lines.size() >= 2;
        },
        5000));
    QCOMPARE(QJsonDocument::fromJson(lines.at(0).toUtf8()).object().value(QLatin1String("error")).toObject().value(QLatin1String("code")).toInt(), -32700);
    QCOMPARE(QJsonDocument::fromJson(lines.at(1).toUtf8()).object().value(QLatin1String("id")).toInt(), 2);
}

void WhaleAiMcpServerTest::tooLong()
{
    WhaleAiMcpServer server;
    QVERIFY(server.listen());
    QLocalSocket socket;
    socket.connectToServer(server.socketPath());
    QVERIFY(socket.waitForConnected(2000));
    socket.write(server.token().toLatin1() + "\n");
    // A line without end, longer than any message: the connection ends.
    socket.write(QByteArray(2 * 1024 * 1024, 'x'));
    QTRY_COMPARE_WITH_TIMEOUT(socket.state(), QLocalSocket::UnconnectedState, 10000);
}

void WhaleAiMcpServerTest::leftovers()
{
    // The folder of an Orcinus that crashed: a socket nobody listens on.
    const QString base = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/orcinus-ai");
    QVERIFY(QDir().mkpath(base));
    const QString dead = base + QStringLiteral("/c-testdead");
    QVERIFY(QDir().mkpath(dead));
    const QByteArray path = QFile::encodeName(dead + QStringLiteral("/mcp"));
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.constData(), path.size() + 1);
    QCOMPARE(::bind(fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)), 0);
    ::close(fd);
    QVERIFY(QFileInfo::exists(dead + QStringLiteral("/mcp")));

    // A living one stays.
    WhaleAiMcpServer living;
    QVERIFY(living.listen());
    QVERIFY(!QFileInfo::exists(dead));

    WhaleAiMcpServer another;
    QVERIFY(another.listen());
    QVERIFY(QFileInfo::exists(living.socketPath()));
}

QTEST_GUILESS_MAIN(WhaleAiMcpServerTest)

#include "whaleaimcpservertest.moc"
