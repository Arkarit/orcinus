/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaiagents.h"
#include "whaleaiagenttest.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

/**
 * The agents of the AI panel in orcinusrc, the known agents to suggest, and the agent test of
 * the settings page (AI2), against the scripted agent whaleacpfakeagent.
 */
class WhaleAiAgentsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();

    void command();
    void commandErrors();
    void storesAgents();
    void defaultAgent();
    void presetsFound();
    void presetsThroughNpx();
    void uniqueId();
    void startMode();
    void restrictiveMode();
    void testReady();
    void testNeedsLogin();
    void testMissingProgram();
    void testTimeout();

private:
    /** @return A folder with fake executables of @p names. */
    QString fakeBin(const QStringList &names);
    static WhaleAiAgent fakeAgent(const QString &scenario);

    QTemporaryDir m_home;
    QList<std::shared_ptr<QTemporaryDir>> m_bins;
};

void WhaleAiAgentsTest::initTestCase()
{
    // A configuration of its own: other tests change the shared one of the test mode.
    QVERIFY(m_home.isValid());
    for (const char *variable : {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME"}) {
        const QString path = m_home.filePath(QString::fromLatin1(variable).toLower());
        QVERIFY(QDir().mkpath(path));
        qputenv(variable, QFile::encodeName(path));
    }
    QCOMPARE(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation), m_home.filePath(QStringLiteral("xdg_config_home")));
}

void WhaleAiAgentsTest::init()
{
    KSharedConfig::openConfig()->deleteGroup(QStringLiteral("AI"));
    KSharedConfig::openConfig()->sync();
}

QString WhaleAiAgentsTest::fakeBin(const QStringList &names)
{
    auto dir = std::make_shared<QTemporaryDir>();
    m_bins.append(dir);
    for (const QString &name : names) {
        QFile file(dir->filePath(name));
        if (!file.open(QIODevice::WriteOnly)) {
            qFatal("cannot write %s", qPrintable(file.fileName()));
        }
        file.write("#!/bin/sh\n");
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    }
    return dir->path();
}

WhaleAiAgent WhaleAiAgentsTest::fakeAgent(const QString &scenario)
{
    WhaleAiAgent agent;
    agent.id = QStringLiteral("fake");
    agent.name = QStringLiteral("Fake");
    agent.commandLine = QStringLiteral("'%1' %2").arg(QStringLiteral(WHALE_ACP_FAKE_AGENT), scenario);
    return agent;
}

void WhaleAiAgentsTest::command()
{
    WhaleAiAgent agent;
    agent.commandLine = QStringLiteral("sh -c 'echo \"a b\"' second");
    agent.environment = {QStringLiteral("ORCINUS_TEST=x=y"), QStringLiteral(" SPACED = 1")};
    QString error = QStringLiteral("old");
    const WhaleAcpSession::AgentCommand command = agent.command(&error);
    QVERIFY(error.isEmpty());
    QCOMPARE(command.program, QStandardPaths::findExecutable(QStringLiteral("sh")));
    QCOMPARE(command.arguments, (QStringList{QStringLiteral("-c"), QStringLiteral("echo \"a b\""), QStringLiteral("second")}));
    // The environment of Orcinus plus the entries; the value may contain "=".
    QCOMPARE(command.environment.value(QStringLiteral("ORCINUS_TEST")), QStringLiteral("x=y"));
    QCOMPARE(command.environment.value(QStringLiteral("SPACED")), QStringLiteral(" 1"));
    QVERIFY(command.environment.contains(QStringLiteral("PATH")));

    // Without entries, the environment of Orcinus as it is.
    agent.environment.clear();
    QVERIFY(agent.command().environment.isEmpty());

    agent.commandLine = QStringLiteral(WHALE_ACP_FAKE_AGENT);
    QCOMPARE(agent.command().program, QStringLiteral(WHALE_ACP_FAKE_AGENT));
}

void WhaleAiAgentsTest::commandErrors()
{
    WhaleAiAgent agent;
    QString error;
    QVERIFY(agent.command(&error).program.isEmpty());
    QVERIFY(!error.isEmpty());

    agent.commandLine = QStringLiteral("sh 'unclosed");
    QVERIFY(agent.command(&error).program.isEmpty());
    QVERIFY(error.contains(QLatin1String("quotes")));

    agent.commandLine = QStringLiteral("orcinus-no-such-agent acp");
    QVERIFY(agent.command(&error).program.isEmpty());
    QVERIFY(error.contains(QLatin1String("orcinus-no-such-agent")));

    agent.commandLine = QStringLiteral("sh");
    agent.environment = {QStringLiteral("NOVALUE")};
    QVERIFY(agent.command(&error).program.isEmpty());
    QVERIFY(error.contains(QLatin1String("NOVALUE")));
}

void WhaleAiAgentsTest::storesAgents()
{
    QVERIFY(WhaleAiAgents::agents().isEmpty());
    QVERIFY(WhaleAiAgents::defaultAgentId().isEmpty());

    WhaleAiAgent claude{QStringLiteral("claude"), QStringLiteral("Claude"), QStringLiteral("claude-agent-acp"), {}, QStringLiteral("default")};
    WhaleAiAgent local{QStringLiteral("local"),
                       QStringLiteral("Local Qwen"),
                       QStringLiteral("opencode acp"),
                       {QStringLiteral("A=1"), QStringLiteral("B=2")},
                       {}};
    WhaleAiAgents::setAgents({local, claude});
    KSharedConfig::openConfig()->reparseConfiguration();
    QCOMPARE(WhaleAiAgents::agents(), (QList<WhaleAiAgent>{local, claude}));
    QCOMPARE(WhaleAiAgents::agent(QStringLiteral("claude")), claude);
    QVERIFY(WhaleAiAgents::agent(QStringLiteral("nope")).id.isEmpty());

    // An agent that is gone leaves no group behind.
    WhaleAiAgents::setAgents({claude});
    const KConfigGroup group(KSharedConfig::openConfig(), QStringLiteral("AI"));
    QCOMPARE(group.groupList(), QStringList{QStringLiteral("claude")});
    QCOMPARE(WhaleAiAgents::agents(), QList<WhaleAiAgent>{claude});
}

void WhaleAiAgentsTest::defaultAgent()
{
    WhaleAiAgent a{QStringLiteral("a"), QStringLiteral("A"), QStringLiteral("sh"), {}, {}};
    WhaleAiAgent b{QStringLiteral("b"), QStringLiteral("B"), QStringLiteral("sh"), {}, {}};
    WhaleAiAgents::setAgents({a, b});
    // None set: the first one.
    QCOMPARE(WhaleAiAgents::defaultAgentId(), QStringLiteral("a"));
    WhaleAiAgents::setDefaultAgentId(QStringLiteral("b"));
    QCOMPARE(WhaleAiAgents::defaultAgentId(), QStringLiteral("b"));
    // The default goes with its agent.
    WhaleAiAgents::setAgents({a});
    QCOMPARE(WhaleAiAgents::defaultAgentId(), QStringLiteral("a"));
    WhaleAiAgents::setAgents({});
    QVERIFY(WhaleAiAgents::defaultAgentId().isEmpty());
}

void WhaleAiAgentsTest::presetsFound()
{
    const QString bin = fakeBin({QStringLiteral("claude-agent-acp"), QStringLiteral("opencode")});
    const QList<WhaleAiAgentPreset> presets = WhaleAiAgents::presets({bin});
    QCOMPARE(presets.size(), 3);

    QCOMPARE(presets.at(0).id, QStringLiteral("claude"));
    QCOMPARE(presets.at(0).commandLine, QStringLiteral("claude-agent-acp"));
    QVERIFY(!presets.at(0).viaNpx);

    QCOMPARE(presets.at(1).id, QStringLiteral("codex"));
    QVERIFY(!presets.at(1).isAvailable());
    QVERIFY(presets.at(1).installHint.contains(QLatin1String("@agentclientprotocol/codex-acp")));

    QCOMPARE(presets.at(2).id, QStringLiteral("opencode"));
    QCOMPARE(presets.at(2).commandLine, QStringLiteral("opencode acp"));
}

void WhaleAiAgentsTest::presetsThroughNpx()
{
    const QList<WhaleAiAgentPreset> presets = WhaleAiAgents::presets({fakeBin({QStringLiteral("npx")})});
    QCOMPARE(presets.at(0).commandLine, QStringLiteral("npx -y @agentclientprotocol/claude-agent-acp"));
    QVERIFY(presets.at(0).viaNpx);
    QCOMPARE(presets.at(1).commandLine, QStringLiteral("npx -y @agentclientprotocol/codex-acp"));
    QCOMPARE(presets.at(2).commandLine, QStringLiteral("npx -y opencode-ai acp"));
}

void WhaleAiAgentsTest::startMode()
{
    WhaleAiAgents::setAgents(
        {{QStringLiteral("a"), QStringLiteral("A"), QStringLiteral("a"), {}, {}}, {QStringLiteral("b"), QStringLiteral("B"), QStringLiteral("b"), {}, {}}});
    WhaleAiAgents::setStartMode(QStringLiteral("b"), QStringLiteral("ask"));
    QCOMPARE(WhaleAiAgents::agent(QStringLiteral("b")).startMode, QStringLiteral("ask"));
    QVERIFY(WhaleAiAgents::agent(QStringLiteral("a")).startMode.isEmpty());
    // An agent that is gone stays gone.
    WhaleAiAgents::setStartMode(QStringLiteral("c"), QStringLiteral("ask"));
    QCOMPARE(WhaleAiAgents::agents().size(), 2);
    WhaleAiAgents::setStartMode(QStringLiteral("b"), QString());
    QVERIFY(WhaleAiAgents::agent(QStringLiteral("b")).startMode.isEmpty());
}

void WhaleAiAgentsTest::restrictiveMode()
{
    auto modes = [](const QStringList &values) {
        WhaleAcpConfigOption option;
        option.category = QStringLiteral("mode");
        for (const QString &value : values) {
            option.choices.append({value, value.toUpper(), {}, {}});
        }
        return option;
    };
    // Claude Agent, Codex, OpenCode as seen in AI0.
    QCOMPARE(
        WhaleAiAgents::restrictiveMode(modes(
            {QStringLiteral("default"), QStringLiteral("acceptEdits"), QStringLiteral("plan"), QStringLiteral("auto"), QStringLiteral("bypassPermissions")})),
        QStringLiteral("plan"));
    QCOMPARE(WhaleAiAgents::restrictiveMode(
                 modes({QStringLiteral("read-only"), QStringLiteral("workspace-write"), QStringLiteral("agent"), QStringLiteral("agent-full-access")})),
             QStringLiteral("read-only"));
    QCOMPARE(WhaleAiAgents::restrictiveMode(modes({QStringLiteral("build"), QStringLiteral("plan")})), QStringLiteral("plan"));
    QCOMPARE(WhaleAiAgents::restrictiveMode(modes({QStringLiteral("ask"), QStringLiteral("ReadOnly")})), QStringLiteral("ReadOnly"));
    QVERIFY(WhaleAiAgents::restrictiveMode(modes({QStringLiteral("ask"), QStringLiteral("auto")})).isEmpty());
    QVERIFY(WhaleAiAgents::restrictiveMode(modes({})).isEmpty());
}

void WhaleAiAgentsTest::uniqueId()
{
    const QList<WhaleAiAgent> agents{{QStringLiteral("claude"), {}, {}, {}, {}}, {QStringLiteral("claude-2"), {}, {}, {}, {}}};
    QCOMPARE(WhaleAiAgents::uniqueId(QStringLiteral("claude"), agents), QStringLiteral("claude-3"));
    QCOMPARE(WhaleAiAgents::uniqueId(QStringLiteral("Local Qwen 3.8!"), agents), QStringLiteral("local-qwen-3-8"));
    QCOMPARE(WhaleAiAgents::uniqueId(QStringLiteral("…"), agents), QStringLiteral("agent"));
    QCOMPARE(WhaleAiAgents::uniqueId(QString(), agents), QStringLiteral("agent"));
}

void WhaleAiAgentsTest::testReady()
{
    WhaleAiAgentTest test;
    QSignalSpy finished(&test, &WhaleAiAgentTest::finished);
    test.run(fakeAgent(QStringLiteral("basic")), 5000);
    QVERIFY(test.isRunning());
    QVERIFY(finished.wait(10000));
    QVERIFY(!test.isRunning());
    const auto result = finished.first().first().value<WhaleAiAgentTest::Result>();
    QVERIFY(result.ok);
    QCOMPARE(result.agentInfo.displayName(), QStringLiteral("Fake Agent"));
    QCOMPARE(result.configOptions.size(), 2);
    QVERIFY(result.error.isEmpty());
}

void WhaleAiAgentsTest::testNeedsLogin()
{
    WhaleAiAgentTest test;
    QSignalSpy finished(&test, &WhaleAiAgentTest::finished);
    test.run(fakeAgent(QStringLiteral("auth")), 5000);
    QVERIFY(finished.wait(10000));
    const auto result = finished.first().first().value<WhaleAiAgentTest::Result>();
    QVERIFY(!result.ok);
    QVERIFY(result.needsLogin);
    QCOMPARE(result.agentInfo.authMethods.size(), 1);
    QVERIFY(!result.error.isEmpty());
    QVERIFY(result.log.contains(QLatin1String("fake agent started")));
}

void WhaleAiAgentsTest::testMissingProgram()
{
    WhaleAiAgentTest test;
    QSignalSpy finished(&test, &WhaleAiAgentTest::finished);
    WhaleAiAgent agent;
    agent.commandLine = QStringLiteral("orcinus-no-such-agent");
    test.run(agent);
    QVERIFY(finished.isEmpty());
    QVERIFY(finished.wait(5000));
    const auto result = finished.first().first().value<WhaleAiAgentTest::Result>();
    QVERIFY(!result.ok);
    QVERIFY(result.error.contains(QLatin1String("orcinus-no-such-agent")));
}

void WhaleAiAgentsTest::testTimeout()
{
    WhaleAiAgentTest test;
    QSignalSpy finished(&test, &WhaleAiAgentTest::finished);
    test.run(fakeAgent(QStringLiteral("hang")), 300);
    QVERIFY(finished.wait(5000));
    const auto result = finished.first().first().value<WhaleAiAgentTest::Result>();
    QVERIFY(!result.ok);
    QVERIFY(!result.needsLogin);
    QVERIFY(result.detail.contains(QLatin1String("in time")));
}

QTEST_GUILESS_MAIN(WhaleAiAgentsTest)

#include "whaleaiagentstest.moc"
