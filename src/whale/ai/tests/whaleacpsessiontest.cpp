/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleacpsession.h"
#include "whaleacpconnection.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

/**
 * The ACP client core (AI1) against a scripted agent (whaleacpfakeagent, see there for the
 * scenarios): start, prompt turn with chunks, tool calls and permissions, cancellation, session
 * settings, and the ways an agent can fail.
 */
class WhaleAcpSessionTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void connectionFailsToStart();
    void startsSession();
    void promptTurn();
    void secondPrompt();
    void cancelAnswersPermission();
    void cancelWithoutPermission();
    void setsConfigOption();
    void modesWithoutConfigOptions();
    void agentCrashes();
    void ignoresGarbage();
    void startTimesOut();
    void rejectsOtherProtocol();
    void needsLogin();
    void closes();
    void needsAbsoluteDirectory();

private:
    /** @return A session of @p scenario, started and Ready (or what @p state says). */
    WhaleAcpSession *startSession(const QString &scenario, WhaleAcpSession::State state = WhaleAcpSession::State::Ready);
    static QString waitForMessage(WhaleAcpSession *session, const std::function<void()> &action);

    QTemporaryDir m_dir;
    WhaleAcpMcpServer m_mcp;
};

void WhaleAcpSessionTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    QVERIFY(QFile::exists(QStringLiteral(WHALE_ACP_FAKE_AGENT)));
    m_mcp.name = QStringLiteral("orcinus");
    m_mcp.command = QStringLiteral("/usr/lib/orcinus/orcinus-mcp");
    m_mcp.arguments = {QStringLiteral("--token"), QStringLiteral("abc")};
    m_mcp.environment = {{QStringLiteral("A"), QStringLiteral("b")}};
}

WhaleAcpSession *WhaleAcpSessionTest::startSession(const QString &scenario, WhaleAcpSession::State state)
{
    auto *session = new WhaleAcpSession(this);
    session->setStartTimeout(5000);
    session->start({QStringLiteral(WHALE_ACP_FAKE_AGENT), {scenario}, {}}, m_dir.path(), {m_mcp});
    if (!QTest::qWaitFor(
            [session, state] {
                return session->state() == state || session->state() == WhaleAcpSession::State::Failed;
            },
            10000)) {
        qWarning() << "session of" << scenario << "stuck in" << session->state();
    }
    return session;
}

void WhaleAcpSessionTest::connectionFailsToStart()
{
    WhaleAcpConnection connection;
    QSignalSpy finished(&connection, &WhaleAcpConnection::finished);
    connection.start(QStringLiteral("/nonexistent/agent"), {}, m_dir.path());
    QVERIFY(finished.wait(5000));
    QVERIFY(finished.first().first().toString().contains(QLatin1String("failed to start")));

    // A request to a dead agent is answered (later, not from inside the call).
    bool answered = false;
    WhaleAcpError error;
    connection.sendRequest(QStringLiteral("initialize"), {}, [&](const QJsonValue &, const WhaleAcpError &e) {
        answered = true;
        error = e;
    });
    QVERIFY(!answered);
    QTRY_VERIFY(answered);
    QCOMPARE(error.code, int(WhaleAcpError::ConnectionClosed));
}

void WhaleAcpSessionTest::startsSession()
{
    WhaleAcpSession *session = startSession(QStringLiteral("basic"));
    QCOMPARE(session->state(), WhaleAcpSession::State::Ready);
    QCOMPARE(session->sessionId(), QStringLiteral("s1"));

    const WhaleAcpAgentInfo info = session->agentInfo();
    QCOMPARE(info.displayName(), QStringLiteral("Fake Agent"));
    QCOMPARE(info.version, QStringLiteral("1.2.3"));
    QVERIFY(info.loadSession);
    QVERIFY(info.imagePrompts);
    QVERIFY(info.mcpHttp);
    QVERIFY(!info.mcpSse);
    QCOMPARE(info.authMethods.size(), 1);
    QCOMPARE(info.authMethods.first().description, QStringLiteral("Run `fake login` in a terminal"));

    // configOptions win over modes; the boolean option is left out.
    const QList<WhaleAcpConfigOption> options = session->configOptions();
    QCOMPARE(options.size(), 2);
    const WhaleAcpConfigOption *mode = session->configOption(QStringLiteral("mode"));
    QVERIFY(mode);
    QVERIFY(!mode->isFromModes());
    QCOMPARE(mode->currentValue, QStringLiteral("auto"));
    QCOMPARE(mode->choice(QStringLiteral("ask"))->description, QStringLiteral("Always ask"));
    const WhaleAcpConfigOption *model = session->configOption(QStringLiteral("model"));
    QVERIFY(model);
    QCOMPARE(model->choices.size(), 2);
    QCOMPARE(model->choices.at(0).group, QStringLiteral("Local"));
    QCOMPARE(model->choices.at(1).value, QStringLiteral("big"));
    QCOMPARE(model->choices.at(1).group, QStringLiteral("Cloud"));
    QVERIFY(session->agentLog().contains(QLatin1String("fake agent started")));
}

QString WhaleAcpSessionTest::waitForMessage(WhaleAcpSession *session, const std::function<void()> &action)
{
    QString text;
    const auto connection = connect(session, &WhaleAcpSession::agentMessageChunk, session, [&text](const QString &chunk) {
        text += chunk + QLatin1Char('|');
    });
    QSignalSpy finished(session, &WhaleAcpSession::promptFinished);
    action();
    if (!finished.wait(10000)) {
        qWarning() << "prompt did not finish";
    }
    disconnect(connection);
    return text;
}

void WhaleAcpSessionTest::promptTurn()
{
    WhaleAcpSession *session = startSession(QStringLiteral("basic"));
    QCOMPARE(session->state(), WhaleAcpSession::State::Ready);

    QSignalSpy thoughts(session, &WhaleAcpSession::agentThoughtChunk);
    QSignalSpy toolCalls(session, &WhaleAcpSession::toolCallChanged);
    QSignalSpy usage(session, &WhaleAcpSession::usageChanged);
    QSignalSpy commands(session, &WhaleAcpSession::availableCommandsChanged);
    QSignalSpy updates(session, &WhaleAcpSession::sessionUpdate);
    QSignalSpy finished(session, &WhaleAcpSession::promptFinished);

    // The user allows the write the agent asks for.
    WhaleAcpPermissionRequest asked;
    connect(session, &WhaleAcpSession::permissionRequested, this, [session, &asked](const WhaleAcpPermissionRequest &request) {
        asked = request;
        QCOMPARE(session->pendingPermissions().size(), 1);
        session->answerPermission(request.key, QStringLiteral("yes"));
    });

    const QString text = waitForMessage(session, [&] {
        QVERIFY(session->prompt({WhaleAcpContent::text(QStringLiteral("hello")),
                                 WhaleAcpContent::resourceLink(QUrl::fromLocalFile(m_dir.path()), QStringLiteral("dir"), QStringLiteral("inode/directory"))}));
        QCOMPARE(session->state(), WhaleAcpSession::State::Prompting);
        // One prompt at a time.
        QVERIFY(!session->prompt({WhaleAcpContent::text(QStringLiteral("again"))}));
    });

    // What the agent got: working directory, MCP server, text, link; not the other session's text.
    QCOMPARE(text,
             QStringLiteral("cwd=%1;mcp=orcinus:/usr/lib/orcinus/orcinus-mcp;text=hello;links=1|permission=yes;fs=%2|")
                 .arg(m_dir.path())
                 .arg(int(WhaleAcpError::MethodNotFound)));
    QCOMPARE(finished.size(), 1);
    QCOMPARE(finished.first().first().toString(), QStringLiteral("end_turn"));
    QCOMPARE(session->state(), WhaleAcpSession::State::Ready);
    QCOMPARE(thoughts.size(), 1);
    QCOMPARE(thoughts.first().first().toString(), QStringLiteral("thinking"));

    // The permission request carries what the tool call said before.
    QCOMPARE(asked.toolCall.id, QStringLiteral("t1"));
    QCOMPARE(asked.toolCall.title, QStringLiteral("Write note.txt"));
    QCOMPARE(asked.toolCall.kind, QStringLiteral("edit"));
    QCOMPARE(asked.toolCall.locations, QStringList{m_dir.path() + QStringLiteral("/note.txt")});
    QCOMPARE(asked.toolCall.rawInput.toObject().value(QLatin1String("content")).toString(), QStringLiteral("hi"));
    QCOMPARE(asked.options.size(), 3);
    QCOMPARE(asked.options.at(1).kind, QStringLiteral("allow_always"));
    QVERIFY(session->pendingPermissions().isEmpty());

    const WhaleAcpToolCall toolCall = session->toolCall(QStringLiteral("t1"));
    QCOMPARE(toolCall.status, WhaleAcpToolCall::Status::Completed);
    QCOMPARE(toolCall.title, QStringLiteral("Write note.txt"));
    QCOMPARE(toolCall.rawOutput.toString(), QStringLiteral("ok"));
    QCOMPARE(toolCalls.size(), 2);

    QCOMPARE(usage.size(), 1);
    QCOMPARE(usage.first().at(0).toLongLong(), 1500);
    QCOMPARE(usage.first().at(1).toLongLong(), 32000);
    QCOMPARE(commands.size(), 1);
    QCOMPARE(session->availableCommands().first().inputHint, QStringLiteral("what"));

    // Unknown kinds are passed on raw, but change nothing.
    bool sawFuture = false;
    for (const QList<QVariant> &args : std::as_const(updates)) {
        sawFuture |= args.first().toString() == QLatin1String("future_kind");
    }
    QVERIFY(sawFuture);
}

void WhaleAcpSessionTest::secondPrompt()
{
    WhaleAcpSession *session = startSession(QStringLiteral("basic"));
    for (int i = 0; i < 2; ++i) {
        const QString text = waitForMessage(session, [&] {
            QVERIFY(session->prompt({WhaleAcpContent::text(QStringLiteral("nopermission %1").arg(i))}));
        });
        QVERIFY2(text.contains(QStringLiteral("text=nopermission %1;").arg(i)), qPrintable(text));
        QVERIFY(text.contains(QLatin1String("permission=none")));
        QCOMPARE(session->state(), WhaleAcpSession::State::Ready);
    }
}

void WhaleAcpSessionTest::cancelAnswersPermission()
{
    WhaleAcpSession *session = startSession(QStringLiteral("basic"));
    QSignalSpy withdrawn(session, &WhaleAcpSession::permissionWithdrawn);
    QString key;
    connect(session, &WhaleAcpSession::permissionRequested, this, [session, &key](const WhaleAcpPermissionRequest &request) {
        key = request.key;
        session->cancel();
    });
    QSignalSpy finished(session, &WhaleAcpSession::promptFinished);

    const QString text = waitForMessage(session, [&] {
        QVERIFY(session->prompt({WhaleAcpContent::text(QStringLiteral("write"))}));
    });

    QCOMPARE(finished.first().first().toString(), QStringLiteral("cancelled"));
    QVERIFY2(text.contains(QLatin1String("permission=cancelled")), qPrintable(text));
    QCOMPARE(withdrawn.size(), 1);
    QCOMPARE(withdrawn.first().first().toString(), key);
    QCOMPARE(session->toolCall(QStringLiteral("t1")).status, WhaleAcpToolCall::Status::Cancelled);
    QCOMPARE(session->state(), WhaleAcpSession::State::Ready);
    // A late answer is ignored.
    session->answerPermission(key, QStringLiteral("yes"));
}

void WhaleAcpSessionTest::cancelWithoutPermission()
{
    WhaleAcpSession *session = startSession(QStringLiteral("basic"));
    connect(session, &WhaleAcpSession::toolCallChanged, this, [session](const WhaleAcpToolCall &toolCall) {
        if (toolCall.status == WhaleAcpToolCall::Status::Pending) {
            session->cancel();
        }
    });
    QSignalSpy finished(session, &WhaleAcpSession::promptFinished);
    waitForMessage(session, [&] {
        QVERIFY(session->prompt({WhaleAcpContent::text(QStringLiteral("nopermission waitforcancel"))}));
    });
    QCOMPARE(finished.first().first().toString(), QStringLiteral("cancelled"));
    QCOMPARE(session->toolCall(QStringLiteral("t1")).status, WhaleAcpToolCall::Status::Cancelled);
}

void WhaleAcpSessionTest::setsConfigOption()
{
    WhaleAcpSession *session = startSession(QStringLiteral("basic"));
    QSignalSpy changed(session, &WhaleAcpSession::configOptionsChanged);
    session->setConfigOption(QStringLiteral("model"), QStringLiteral("big"));
    QVERIFY(changed.wait(5000));
    QCOMPARE(session->configOption(QStringLiteral("model"))->currentValue, QStringLiteral("big"));
    // The agent's answer holds all options: the mode it moved along is there too.
    QCOMPARE(session->configOption(QStringLiteral("mode"))->currentValue, QStringLiteral("ask"));

    QSignalSpy warnings(session, &WhaleAcpSession::protocolWarning);
    session->setConfigOption(QStringLiteral("nonsense"), QStringLiteral("x"));
    QVERIFY(warnings.wait(5000));
    QCOMPARE(session->state(), WhaleAcpSession::State::Ready);
}

void WhaleAcpSessionTest::modesWithoutConfigOptions()
{
    WhaleAcpSession *session = startSession(QStringLiteral("modes"));
    QCOMPARE(session->configOptions().size(), 1);
    const WhaleAcpConfigOption *mode = session->configOption(QStringLiteral("mode"));
    QVERIFY(mode);
    QVERIFY(mode->isFromModes());
    QCOMPARE(mode->currentValue, QStringLiteral("auto"));
    QCOMPARE(mode->choices.size(), 2);

    QSignalSpy changed(session, &WhaleAcpSession::configOptionsChanged);
    session->setConfigOption(WhaleAcpConfigOption::modesOptionId(), QStringLiteral("ask"));
    QTRY_COMPARE(session->configOption(QStringLiteral("mode"))->currentValue, QStringLiteral("ask"));
    QVERIFY(!changed.isEmpty());
}

void WhaleAcpSessionTest::agentCrashes()
{
    WhaleAcpSession *session = startSession(QStringLiteral("crash"));
    QSignalSpy finished(session, &WhaleAcpSession::promptFinished);
    QVERIFY(session->prompt({WhaleAcpContent::text(QStringLiteral("boom"))}));
    QTRY_COMPARE_WITH_TIMEOUT(session->state(), WhaleAcpSession::State::Failed, 10000);
    QCOMPARE(finished.size(), 1);
    QCOMPARE(finished.first().first().toString(), QStringLiteral("error"));
    QCOMPARE(session->errorCode(), int(WhaleAcpError::ConnectionClosed));
    QVERIFY2(session->errorString().contains(QLatin1String("code 3")), qPrintable(session->errorString()));
    QVERIFY(session->agentLog().contains(QLatin1String("the model exploded")));
}

void WhaleAcpSessionTest::ignoresGarbage()
{
    auto *session = new WhaleAcpSession(this);
    QSignalSpy warnings(session, &WhaleAcpSession::protocolWarning);
    session->start({QStringLiteral(WHALE_ACP_FAKE_AGENT), {QStringLiteral("garbage")}, {}}, m_dir.path());
    QTRY_COMPARE_WITH_TIMEOUT(session->state(), WhaleAcpSession::State::Ready, 10000);
    QCOMPARE(warnings.size(), 1);
    QVERIFY(warnings.first().first().toString().contains(QLatin1String("this is not json")));
}

void WhaleAcpSessionTest::startTimesOut()
{
    auto *session = new WhaleAcpSession(this);
    session->setStartTimeout(300);
    session->start({QStringLiteral(WHALE_ACP_FAKE_AGENT), {QStringLiteral("hang")}, {}}, m_dir.path());
    QTRY_COMPARE_WITH_TIMEOUT(session->state(), WhaleAcpSession::State::Failed, 5000);
    QCOMPARE(session->errorCode(), int(WhaleAcpError::Timeout));
}

void WhaleAcpSessionTest::rejectsOtherProtocol()
{
    WhaleAcpSession *session = startSession(QStringLiteral("oldprotocol"));
    QCOMPARE(session->state(), WhaleAcpSession::State::Failed);
    QVERIFY2(session->errorString().contains(QLatin1String("99")), qPrintable(session->errorString()));
}

void WhaleAcpSessionTest::needsLogin()
{
    WhaleAcpSession *session = startSession(QStringLiteral("auth"));
    QCOMPARE(session->state(), WhaleAcpSession::State::Failed);
    QCOMPARE(session->errorCode(), int(WhaleAcpError::AuthRequired));
    // The login hints of initialize stay available for the message.
    QCOMPARE(session->agentInfo().authMethods.size(), 1);
}

void WhaleAcpSessionTest::closes()
{
    WhaleAcpSession *session = startSession(QStringLiteral("basic"));
    QSignalSpy states(session, &WhaleAcpSession::stateChanged);
    session->close();
    QCOMPARE(session->state(), WhaleAcpSession::State::Closed);
    // The agent ends on EOF; that is no failure.
    QTest::qWait(300);
    QCOMPARE(session->state(), WhaleAcpSession::State::Closed);
    QCOMPARE(states.size(), 1);
    QVERIFY(!session->prompt({WhaleAcpContent::text(QStringLiteral("late"))}));
}

void WhaleAcpSessionTest::needsAbsoluteDirectory()
{
    WhaleAcpSession session;
    session.start({QStringLiteral(WHALE_ACP_FAKE_AGENT), {}, {}}, QStringLiteral("relative/dir"));
    QCOMPARE(session.state(), WhaleAcpSession::State::Failed);
}

QTEST_GUILESS_MAIN(WhaleAcpSessionTest)

#include "whaleacpsessiontest.moc"
