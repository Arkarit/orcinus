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
#include <QJsonArray>
#include <QJsonDocument>
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
    void localAgent();
    void forTheAgent();
    void startMode();
    void systemPromptStorage();
    void restrictiveMode();
    void knownModes();
    void loginHints();
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
    // AI12: the local model Orcinus loads for it.
    local.localProvider = QStringLiteral("lmstudio");
    local.localModel = QStringLiteral("qwen3.8-27b");
    local.localContextLength = 65536;
    QVERIFY(local.usesLocalModel());
    QVERIFY(!claude.usesLocalModel());
    WhaleAiAgents::setAgents({local, claude});
    KSharedConfig::openConfig()->reparseConfiguration();
    QCOMPARE(WhaleAiAgents::agents(), (QList<WhaleAiAgent>{local, claude}));
    QCOMPARE(WhaleAiAgents::agent(QStringLiteral("claude")), claude);
    QVERIFY(WhaleAiAgents::agent(QStringLiteral("nope")).id.isEmpty());
    const QJsonObject described = WhaleAiAgents::describeForAgent(local).value(QLatin1String("localModel")).toObject();
    QCOMPARE(described.value(QLatin1String("model")).toString(), QStringLiteral("qwen3.8-27b"));
    QCOMPARE(described.value(QLatin1String("contextLength")).toInt(), 65536);
    QVERIFY(!WhaleAiAgents::describeForAgent(claude).contains(QLatin1String("localModel")));
    // Without a model, nothing of it is kept.
    local.localModel.clear();
    WhaleAiAgents::setAgents({local, claude});
    KSharedConfig::openConfig()->reparseConfiguration();
    QVERIFY(!KConfigGroup(KSharedConfig::openConfig(), QStringLiteral("AI")).group(QStringLiteral("local")).hasKey("LocalProvider"));
    QVERIFY(!KConfigGroup(KSharedConfig::openConfig(), QStringLiteral("AI")).group(QStringLiteral("local")).hasKey("LocalContextLength"));
    local.localProvider.clear();
    local.localContextLength = 0;
    QCOMPARE(WhaleAiAgents::agents(), (QList<WhaleAiAgent>{local, claude}));

    // An agent that is gone leaves no group behind.
    WhaleAiAgents::setAgents({claude});
    const KConfigGroup group(KSharedConfig::openConfig(), QStringLiteral("AI"));
    QCOMPARE(group.groupList(), QStringList{QStringLiteral("claude")});
    QCOMPARE(WhaleAiAgents::agents(), QList<WhaleAiAgent>{claude});
}

void WhaleAiAgentsTest::localAgent()
{
    // AI12d: an agent for a model of LM Studio, run by OpenCode.
    WhaleAiLocalProvider lmStudio;
    lmStudio.id = QStringLiteral("lmstudio");
    lmStudio.name = QStringLiteral("LM Studio");
    lmStudio.port = 4321;
    WhaleAiLocalModel qwen{QStringLiteral("qwen3.8-27b"), QStringLiteral("Qwen3.8 27B"), {}, {}, 0, 262144, true, true, false};
    WhaleAiLocalModel small{QStringLiteral("tiny"), QStringLiteral("Tiny"), {}, {}, 0, 8192, false, false, false};
    WhaleAiLocalModel unknown{QStringLiteral("x"), QStringLiteral("X"), {}, {}, 0, 0, true, false, false};
    QCOMPARE(WhaleAiAgents::defaultContextLength(qwen), 65536);
    QCOMPARE(WhaleAiAgents::defaultContextLength(small), 8192);
    QCOMPARE(WhaleAiAgents::defaultContextLength(unknown), 65536);

    const QJsonObject config = QJsonDocument::fromJson(WhaleAiAgents::openCodeConfig(lmStudio, qwen, 40960).toUtf8()).object();
    QCOMPARE(config.value(QLatin1String("model")).toString(), QStringLiteral("lmstudio/qwen3.8-27b"));
    QCOMPARE(config.value(QLatin1String("share")).toString(), QStringLiteral("disabled"));
    QCOMPARE(config.value(QLatin1String("autoupdate")).toBool(true), false);
    const QJsonObject provider = config.value(QLatin1String("provider")).toObject().value(QLatin1String("lmstudio")).toObject();
    QCOMPARE(provider.value(QLatin1String("npm")).toString(), QStringLiteral("@ai-sdk/openai-compatible"));
    QCOMPARE(provider.value(QLatin1String("options")).toObject().value(QLatin1String("baseURL")).toString(), QStringLiteral("http://127.0.0.1:4321/v1"));
    const QJsonObject entry = provider.value(QLatin1String("models")).toObject().value(QLatin1String("qwen3.8-27b")).toObject();
    QCOMPARE(entry.value(QLatin1String("limit")).toObject().value(QLatin1String("context")).toInt(), 40960);
    QCOMPARE(entry.value(QLatin1String("limit")).toObject().value(QLatin1String("output")).toInt(), 10240);
    QVERIFY(entry.value(QLatin1String("tool_call")).toBool());
    QCOMPARE(entry.value(QLatin1String("modalities")).toObject().value(QLatin1String("input")).toArray().size(), 2);
    // LM Studio's default length: the model's maximum is assumed; no images, no modalities.
    const QJsonObject tiny = QJsonDocument::fromJson(WhaleAiAgents::openCodeConfig(lmStudio, small, 0).toUtf8())
                                 .object()
                                 .value(QLatin1String("provider"))
                                 .toObject()
                                 .value(QLatin1String("lmstudio"))
                                 .toObject()
                                 .value(QLatin1String("models"))
                                 .toObject()
                                 .value(QLatin1String("tiny"))
                                 .toObject();
    QCOMPARE(tiny.value(QLatin1String("limit")).toObject().value(QLatin1String("context")).toInt(), 8192);
    QVERIFY(!tiny.contains(QLatin1String("modalities")));

    // The configuration replaces one for LM Studio, is added if there is none, and leaves one of the user's own.
    const QString line = QStringLiteral("OPENCODE_CONFIG_CONTENT=") + WhaleAiAgents::openCodeConfig(lmStudio, qwen, 65536);
    QCOMPARE(WhaleAiAgents::withOpenCodeConfig({QStringLiteral("A=1")}, lmStudio, qwen, 65536), (QStringList{QStringLiteral("A=1"), line}));
    const QStringList old{QStringLiteral("OPENCODE_CONFIG_CONTENT=") + WhaleAiAgents::openCodeConfig(lmStudio, small, 0), QStringLiteral("B=2")};
    QCOMPARE(WhaleAiAgents::withOpenCodeConfig(old, lmStudio, qwen, 65536), (QStringList{line, QStringLiteral("B=2")}));
    const QStringList own{QStringLiteral("OPENCODE_CONFIG_CONTENT={\"model\":\"anthropic/x\"}")};
    QCOMPARE(WhaleAiAgents::withOpenCodeConfig(own, lmStudio, qwen, 65536), own);

    WhaleAiLocalClient openCode{QStringLiteral("opencode"), QStringLiteral("OpenCode"), QStringLiteral("opencode acp"), true, QString()};
    const WhaleAiAgent existing{QStringLiteral("qwen3-8-27b-local"), QStringLiteral("x"), QStringLiteral("x"), {}, {}};
    const WhaleAiAgent agent = WhaleAiAgents::localAgent(lmStudio, qwen, openCode, {existing});
    QCOMPARE(agent.name, QStringLiteral("Qwen3.8 27B (local)"));
    QCOMPARE(agent.id, QStringLiteral("qwen3-8-27b-local-2"));
    QCOMPARE(agent.commandLine, QStringLiteral("opencode acp"));
    QCOMPARE(agent.localProvider, QStringLiteral("lmstudio"));
    QCOMPARE(agent.localModel, QStringLiteral("qwen3.8-27b"));
    QCOMPARE(agent.localContextLength, 65536);
    QCOMPARE(agent.environment, QStringList{line});
    QCOMPARE(WhaleAiAgents::kind(agent), WhaleAiAgentKind::OpenCode);
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

void WhaleAiAgentsTest::forTheAgent()
{
    // AI11: what the agent sees of an agent, and what it may propose.
    QCOMPARE(WhaleAiAgents::maskSecrets(QStringLiteral("x --api-key abc --token=def -password 'g h' --model qwen")),
             QStringLiteral("x --api-key … --token=… -password … --model qwen"));
    const WhaleAiAgent agent{QStringLiteral("o"),
                             QStringLiteral("OpenCode"),
                             QStringLiteral("opencode acp --auth-token secret1"),
                             {QStringLiteral("OPENAI_API_KEY=secret2"), QStringLiteral("DEBUG=1")},
                             QStringLiteral("plan")};
    const QJsonObject described = WhaleAiAgents::describeForAgent(agent);
    const QByteArray json = QJsonDocument(described).toJson();
    QVERIFY(!json.contains("secret1"));
    QVERIFY(!json.contains("secret2"));
    QCOMPARE(described.value(QLatin1String("environmentVariables")).toArray(), (QJsonArray{QStringLiteral("OPENAI_API_KEY"), QStringLiteral("DEBUG")}));
    QCOMPARE(described.value(QLatin1String("startMode")).toString(), QStringLiteral("plan"));

    QString error;
    const auto proposed =
        WhaleAiAgents::proposal({{QStringLiteral("name"), QStringLiteral("OpenCode")},
                                 {QStringLiteral("command"), QStringLiteral("opencode acp")},
                                 {QStringLiteral("environment"), QJsonArray{QStringLiteral("LMSTUDIO_URL=http://localhost:1234"), QStringLiteral("API_KEY")}}},
                                {agent},
                                &error);
    QVERIFY2(proposed, qPrintable(error));
    QCOMPARE(proposed->id, QStringLiteral("opencode"));
    QCOMPARE(proposed->environment, QStringList({QStringLiteral("LMSTUDIO_URL=http://localhost:1234"), QStringLiteral("API_KEY=")}));
    QVERIFY(!WhaleAiAgents::proposal({{QStringLiteral("command"), QStringLiteral("x")}}, {}, &error));
    QVERIFY(!WhaleAiAgents::proposal({{QStringLiteral("name"), QStringLiteral("x")}, {QStringLiteral("command"), QStringLiteral("x 'open")}}, {}, &error));
    QVERIFY(error.contains(QLatin1String("quoted")));
    QVERIFY(!WhaleAiAgents::proposal({{QStringLiteral("name"), QStringLiteral("x")},
                                      {QStringLiteral("command"), QStringLiteral("x")},
                                      {QStringLiteral("environment"), QJsonArray{QStringLiteral("1BAD=x")}}},
                                     {},
                                     &error));
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

void WhaleAiAgentsTest::systemPromptStorage()
{
    // Default until the user takes over; the built-in text mentions the tools.
    QVERIFY(!WhaleAiAgents::customSystemPrompt());
    QCOMPARE(WhaleAiAgents::systemPrompt(), WhaleAiAgents::defaultSystemPrompt());
    QVERIFY(WhaleAiAgents::defaultSystemPrompt().contains(QLatin1String("set_location")));

    // A stored custom text is only used while editing is on.
    WhaleAiAgents::setSystemPrompt(QStringLiteral("Be brief."));
    QCOMPARE(WhaleAiAgents::systemPrompt(), WhaleAiAgents::defaultSystemPrompt());
    WhaleAiAgents::setCustomSystemPrompt(true);
    QVERIFY(WhaleAiAgents::customSystemPrompt());
    QCOMPARE(WhaleAiAgents::systemPrompt(), QStringLiteral("Be brief."));
    WhaleAiAgents::setCustomSystemPrompt(false);
    QCOMPARE(WhaleAiAgents::systemPrompt(), WhaleAiAgents::defaultSystemPrompt());
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

void WhaleAiAgentsTest::knownModes()
{
    WhaleAiAgents::setAgents({{QStringLiteral("a"), QStringLiteral("A"), QStringLiteral("a"), {}, {}}});
    QVERIFY(WhaleAiAgents::knownModes(QStringLiteral("a")).choices.isEmpty());

    WhaleAcpConfigOption modes;
    modes.id = QStringLiteral("mode");
    modes.category = QStringLiteral("mode");
    modes.currentValue = QStringLiteral("auto");
    modes.choices = {{QStringLiteral("plan"), QStringLiteral("Plan"), QStringLiteral("Only reads"), {}},
                     {QStringLiteral("auto"), QStringLiteral("Auto"), {}, {}}};
    WhaleAiAgents::setKnownModes(QStringLiteral("a"), modes);
    WhaleAiAgents::setKnownModes(QStringLiteral("gone"), modes);

    const WhaleAcpConfigOption known = WhaleAiAgents::knownModes(QStringLiteral("a"));
    QCOMPARE(known.category, QStringLiteral("mode"));
    QVERIFY(known.currentValue.isEmpty());
    QCOMPARE(known.choices.size(), 2);
    QCOMPARE(known.choices.at(0).value, QStringLiteral("plan"));
    QCOMPARE(known.choices.at(0).name, QStringLiteral("Plan"));
    QCOMPARE(known.choices.at(0).description, QStringLiteral("Only reads"));
    QCOMPARE(known.choices.at(1).value, QStringLiteral("auto"));
    QVERIFY(WhaleAiAgents::knownModes(QStringLiteral("gone")).choices.isEmpty());

    // Saving the agents (the settings page) keeps them.
    WhaleAiAgents::setAgents(WhaleAiAgents::agents());
    QCOMPARE(WhaleAiAgents::knownModes(QStringLiteral("a")).choices.size(), 2);
}

void WhaleAiAgentsTest::loginHints()
{
    auto agentWith = [](const QString &commandLine) {
        WhaleAiAgent agent;
        agent.commandLine = commandLine;
        return agent;
    };
    const QString bin = fakeBin({QStringLiteral("codex"), QStringLiteral("opencode")});

    // Codex: its own "ChatGPT" method needs a client that runs the login; the CLI does it.
    WhaleAiLoginHint hint = WhaleAiAgents::loginHint(agentWith(QStringLiteral("codex-acp")), {bin});
    QVERIFY(hint.isValid());
    QVERIFY(hint.text.contains(QLatin1String("codex login")));
    QVERIFY(hint.text.contains(QLatin1String("OPENAI_API_KEY")));
    QCOMPARE(hint.command, QStringLiteral("codex login"));
    QCOMPARE(hint.url, QUrl(QStringLiteral("https://github.com/openai/codex")));
    // Through npx, and without the CLI installed: the CLI through npx too.
    hint = WhaleAiAgents::loginHint(agentWith(QStringLiteral("npx -y @agentclientprotocol/codex-acp")), {m_home.path()});
    QCOMPARE(hint.command, QStringLiteral("npx -y @openai/codex login"));
    hint = WhaleAiAgents::loginHint(agentWith(QStringLiteral("/opt/x/bin/codex-acp --flag")), {bin});
    QCOMPARE(hint.command, QStringLiteral("codex login"));

    hint = WhaleAiAgents::loginHint(agentWith(QStringLiteral("npx -y @agentclientprotocol/claude-agent-acp")), {m_home.path()});
    QVERIFY(hint.text.contains(QLatin1String("Claude Code")));
    QCOMPARE(hint.command, QStringLiteral("npx -y @anthropic-ai/claude-code"));

    hint = WhaleAiAgents::loginHint(agentWith(QStringLiteral("opencode acp")), {bin});
    QCOMPARE(hint.command, QStringLiteral("opencode auth login"));

    QVERIFY(!WhaleAiAgents::loginHint(agentWith(QStringLiteral("my-agent --acp")), {bin}).isValid());
    QVERIFY(!WhaleAiAgents::loginHint(agentWith(QStringLiteral("codex-acp-fork")), {bin}).isValid());
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
