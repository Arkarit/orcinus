/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaiagents.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>
#include <KShell>

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStandardPaths>

namespace
{
const QString GroupName = QStringLiteral("AI");
const char AgentsKey[] = "Agents";
const char DefaultAgentKey[] = "DefaultAgent";
const char NameKey[] = "Name";
const char CommandKey[] = "Command";
const char EnvironmentKey[] = "Environment";
const char StartModeKey[] = "StartMode";
const char LocalProviderKey[] = "LocalProvider";
const char LocalModelKey[] = "LocalModel";
const char LocalContextLengthKey[] = "LocalContextLength";
const char DisclaimerAcceptedKey[] = "DisclaimerAccepted";
const char ShowActionsKey[] = "ShowActions";
// Of the sandbox of AI5d/AI5e, removed again: no longer read, deleted when the agents are stored.
const char AccessKey[] = "Access";
const char SandboxPathsKey[] = "SandboxPaths";
const char ModeIdsKey[] = "ModeIds";
const char ModeNamesKey[] = "ModeNames";
const char ModeDescriptionsKey[] = "ModeDescriptions";
const char SystemPromptKey[] = "SystemPrompt";
const char CustomSystemPromptKey[] = "CustomSystemPrompt";

KConfigGroup aiGroup()
{
    return KConfigGroup(KSharedConfig::openConfig(), GroupName);
}

QString findExecutable(const QString &name, const QStringList &searchPaths)
{
    return QStandardPaths::findExecutable(name, searchPaths);
}
}

WhaleAcpSession::AgentCommand WhaleAiAgent::command(QString *error) const
{
    auto fail = [error](const QString &message) {
        if (error) {
            *error = message;
        }
        return WhaleAcpSession::AgentCommand();
    };

    KShell::Errors splitError = KShell::NoError;
    QStringList words = KShell::splitArgs(commandLine.trimmed(), KShell::TildeExpand, &splitError);
    if (splitError != KShell::NoError) {
        return fail(i18nc("@info", "The command cannot be read: check the quotes."));
    }
    if (words.isEmpty()) {
        return fail(i18nc("@info", "No command is set."));
    }

    WhaleAcpSession::AgentCommand command;
    const QString program = words.takeFirst();
    command.program = QDir::isAbsolutePath(program) ? program : QStandardPaths::findExecutable(program);
    if (command.program.isEmpty() || !QFileInfo(command.program).isExecutable()) {
        return fail(i18nc("@info", "The program “%1” was not found.", program));
    }
    command.arguments = words;

    if (!environment.isEmpty()) {
        command.environment = QProcessEnvironment::systemEnvironment();
        for (const QString &entry : environment) {
            const qsizetype equals = entry.indexOf(QLatin1Char('='));
            if (equals <= 0) {
                return fail(i18nc("@info", "The environment entry “%1” is not of the form NAME=value.", entry));
            }
            command.environment.insert(entry.left(equals).trimmed(), entry.mid(equals + 1));
        }
    }
    if (error) {
        error->clear();
    }
    return command;
}

namespace WhaleAiAgents
{
QList<WhaleAiAgent> agents()
{
    const KConfigGroup group = aiGroup();
    QList<WhaleAiAgent> list;
    const QStringList ids = group.readEntry(AgentsKey, QStringList());
    for (const QString &id : ids) {
        const KConfigGroup agentGroup = group.group(id);
        WhaleAiAgent agent;
        agent.id = id;
        agent.name = agentGroup.readEntry(NameKey, id);
        agent.commandLine = agentGroup.readEntry(CommandKey, QString());
        agent.environment = agentGroup.readEntry(EnvironmentKey, QStringList());
        agent.startMode = agentGroup.readEntry(StartModeKey, QString());
        agent.localProvider = agentGroup.readEntry(LocalProviderKey, QString());
        agent.localModel = agentGroup.readEntry(LocalModelKey, QString());
        agent.localContextLength = agentGroup.readEntry(LocalContextLengthKey, 0);
        list.append(agent);
    }
    return list;
}

void setAgents(const QList<WhaleAiAgent> &agents)
{
    KConfigGroup group = aiGroup();
    QStringList ids;
    for (const WhaleAiAgent &agent : agents) {
        ids.append(agent.id);
        KConfigGroup agentGroup = group.group(agent.id);
        agentGroup.writeEntry(NameKey, agent.name);
        agentGroup.writeEntry(CommandKey, agent.commandLine);
        if (agent.environment.isEmpty()) {
            agentGroup.deleteEntry(EnvironmentKey);
        } else {
            agentGroup.writeEntry(EnvironmentKey, agent.environment);
        }
        if (agent.startMode.isEmpty()) {
            agentGroup.deleteEntry(StartModeKey);
        } else {
            agentGroup.writeEntry(StartModeKey, agent.startMode);
        }
        if (agent.usesLocalModel()) {
            agentGroup.writeEntry(LocalProviderKey, agent.localProvider);
            agentGroup.writeEntry(LocalModelKey, agent.localModel);
        } else {
            agentGroup.deleteEntry(LocalProviderKey);
            agentGroup.deleteEntry(LocalModelKey);
        }
        if (agent.usesLocalModel() && agent.localContextLength > 0) {
            agentGroup.writeEntry(LocalContextLengthKey, agent.localContextLength);
        } else {
            agentGroup.deleteEntry(LocalContextLengthKey);
        }
        agentGroup.deleteEntry(AccessKey);
        agentGroup.deleteEntry(SandboxPathsKey);
    }
    const QStringList groups = group.groupList();
    for (const QString &name : groups) {
        if (!ids.contains(name)) {
            group.deleteGroup(name);
        }
    }
    group.writeEntry(AgentsKey, ids);
    if (!ids.contains(group.readEntry(DefaultAgentKey, QString()))) {
        group.deleteEntry(DefaultAgentKey);
    }
    group.sync();
}

QString defaultAgentId()
{
    const KConfigGroup group = aiGroup();
    const QStringList ids = group.readEntry(AgentsKey, QStringList());
    const QString id = group.readEntry(DefaultAgentKey, QString());
    if (ids.contains(id)) {
        return id;
    }
    return ids.value(0);
}

void setDefaultAgentId(const QString &id)
{
    KConfigGroup group = aiGroup();
    if (id.isEmpty()) {
        group.deleteEntry(DefaultAgentKey);
    } else {
        group.writeEntry(DefaultAgentKey, id);
    }
    group.sync();
}

WhaleAiAgent agent(const QString &id)
{
    const QList<WhaleAiAgent> list = agents();
    for (const WhaleAiAgent &agent : list) {
        if (agent.id == id) {
            return agent;
        }
    }
    return {};
}

void setStartMode(const QString &id, const QString &mode)
{
    QList<WhaleAiAgent> list = agents();
    for (WhaleAiAgent &agent : list) {
        if (agent.id == id) {
            agent.startMode = mode;
            setAgents(list);
            return;
        }
    }
}

QString restrictiveMode(const WhaleAcpConfigOption &modes)
{
    // Ids of the agents checked in AI0, plus spellings other agents may use.
    static const QStringList readOnly{
        QStringLiteral("plan"),
        QStringLiteral("read-only"),
        QStringLiteral("read_only"),
        QStringLiteral("readonly"),
    };
    for (const WhaleAcpConfigOption::Choice &choice : modes.choices) {
        if (readOnly.contains(choice.value, Qt::CaseInsensitive)) {
            return choice.value;
        }
    }
    return QString();
}

WhaleAcpConfigOption knownModes(const QString &id)
{
    WhaleAcpConfigOption modes;
    modes.category = QStringLiteral("mode");
    const KConfigGroup group = aiGroup();
    if (id.isEmpty() || !group.readEntry(AgentsKey, QStringList()).contains(id)) {
        return modes;
    }
    const KConfigGroup agentGroup = group.group(id);
    const QStringList ids = agentGroup.readEntry(ModeIdsKey, QStringList());
    const QStringList names = agentGroup.readEntry(ModeNamesKey, QStringList());
    const QStringList descriptions = agentGroup.readEntry(ModeDescriptionsKey, QStringList());
    for (int i = 0; i < ids.size(); ++i) {
        modes.choices.append({ids.at(i), names.value(i), descriptions.value(i), QString()});
    }
    return modes;
}

void setKnownModes(const QString &id, const WhaleAcpConfigOption &modes)
{
    KConfigGroup group = aiGroup();
    if (id.isEmpty() || !group.readEntry(AgentsKey, QStringList()).contains(id)) {
        return;
    }
    QStringList ids;
    QStringList names;
    QStringList descriptions;
    for (const WhaleAcpConfigOption::Choice &choice : modes.choices) {
        ids.append(choice.value);
        names.append(choice.name);
        descriptions.append(choice.description);
    }
    KConfigGroup agentGroup = group.group(id);
    if (agentGroup.readEntry(ModeIdsKey, QStringList()) == ids && agentGroup.readEntry(ModeNamesKey, QStringList()) == names
        && agentGroup.readEntry(ModeDescriptionsKey, QStringList()) == descriptions) {
        return;
    }
    agentGroup.writeEntry(ModeIdsKey, ids);
    agentGroup.writeEntry(ModeNamesKey, names);
    agentGroup.writeEntry(ModeDescriptionsKey, descriptions);
    group.sync();
}

QList<WhaleAiAgentPreset> presets(const QStringList &searchPaths)
{
    struct Known {
        QString id;
        QString name;
        QString executable; ///< the ACP program, if installed
        QStringList arguments;
        QString npmPackage; ///< runs through npx otherwise
        QStringList npxArguments;
        QString description;
    };
    const QList<Known> known{
        {QStringLiteral("claude"),
         QStringLiteral("Claude"),
         QStringLiteral("claude-agent-acp"),
         {},
         QStringLiteral("@agentclientprotocol/claude-agent-acp"),
         {},
         i18nc("@info", "Claude Code through its ACP adapter. Uses the login of Claude Code.")},
        {QStringLiteral("codex"),
         QStringLiteral("Codex"),
         QStringLiteral("codex-acp"),
         {},
         QStringLiteral("@agentclientprotocol/codex-acp"),
         {},
         i18nc("@info", "OpenAI Codex through its ACP adapter. Uses the login of the Codex CLI.")},
        {QStringLiteral("opencode"),
         QStringLiteral("OpenCode"),
         QStringLiteral("opencode"),
         {QStringLiteral("acp")},
         QStringLiteral("opencode-ai"),
         {QStringLiteral("acp")},
         i18nc("@info", "OpenCode. Works with local models (LM Studio, Ollama, …) set up in its own configuration.")},
    };

    const QString npx = findExecutable(QStringLiteral("npx"), searchPaths);
    QList<WhaleAiAgentPreset> list;
    for (const Known &agent : known) {
        WhaleAiAgentPreset preset;
        preset.id = agent.id;
        preset.name = agent.name;
        preset.description = agent.description;
        if (!findExecutable(agent.executable, searchPaths).isEmpty()) {
            preset.commandLine = KShell::joinArgs(QStringList{agent.executable} + agent.arguments);
        } else if (!npx.isEmpty()) {
            preset.commandLine = KShell::joinArgs(QStringList{QStringLiteral("npx"), QStringLiteral("-y"), agent.npmPackage} + agent.npxArguments);
            preset.viaNpx = true;
        } else {
            preset.installHint = i18nc("@info %1 is an npm package", "Install it with: npm install -g %1", agent.npmPackage);
        }
        list.append(preset);
    }
    return list;
}

WhaleAiAgentKind kind(const WhaleAiAgent &agent)
{
    const QStringList words = KShell::splitArgs(agent.commandLine);
    auto uses = [&words](const QString &name) {
        for (const QString &word : words) {
            if (QFileInfo(word).fileName() == name || word.endsWith(QLatin1Char('/') + name)) {
                return true;
            }
        }
        return false;
    };
    if (uses(QStringLiteral("codex-acp"))) {
        return WhaleAiAgentKind::Codex;
    }
    if (uses(QStringLiteral("claude-agent-acp"))) {
        return WhaleAiAgentKind::Claude;
    }
    if (uses(QStringLiteral("opencode")) || uses(QStringLiteral("opencode-ai"))) {
        return WhaleAiAgentKind::OpenCode;
    }
    return WhaleAiAgentKind::Unknown;
}

WhaleAiLoginHint loginHint(const WhaleAiAgent &agent, const QStringList &searchPaths)
{
    // The CLI that logs in, or npx with its package if it is not installed.
    auto loginCommand = [&searchPaths](const QString &program, const QString &npmPackage, const QStringList &arguments) {
        if (!findExecutable(program, searchPaths).isEmpty()) {
            return KShell::joinArgs(QStringList{program} + arguments);
        }
        return KShell::joinArgs(QStringList{QStringLiteral("npx"), QStringLiteral("-y"), npmPackage} + arguments);
    };

    WhaleAiLoginHint hint;
    switch (kind(agent)) {
    case WhaleAiAgentKind::Codex:
        hint.text = i18nc("@info",
                          "Codex uses the login of the Codex CLI: run “codex login” in a terminal once. "
                          "With an API key instead, set OPENAI_API_KEY or CODEX_API_KEY in the agent's environment (Edit…).");
        hint.command = loginCommand(QStringLiteral("codex"), QStringLiteral("@openai/codex"), {QStringLiteral("login")});
        hint.url = QUrl(QStringLiteral("https://github.com/openai/codex"));
        break;
    case WhaleAiAgentKind::Claude:
        hint.text = i18nc("@info", "Claude uses the login of Claude Code: start “claude” in a terminal once and log in there.");
        hint.command = loginCommand(QStringLiteral("claude"), QStringLiteral("@anthropic-ai/claude-code"), {});
        hint.url = QUrl(QStringLiteral("https://github.com/anthropics/claude-code"));
        break;
    case WhaleAiAgentKind::OpenCode:
        hint.text = i18nc("@info",
                          "OpenCode keeps its providers in its own configuration: run “opencode auth login” in a terminal, "
                          "or set up a local model there.");
        hint.command = loginCommand(QStringLiteral("opencode"), QStringLiteral("opencode-ai"), {QStringLiteral("auth"), QStringLiteral("login")});
        hint.url = QUrl(QStringLiteral("https://opencode.ai"));
        break;
    case WhaleAiAgentKind::Unknown:
        break;
    }
    return hint;
}

QString maskSecrets(const QString &commandLine)
{
    // --api-key x, --api-key=x, --token "x y", -password=x ...: the value goes, the option stays.
    static const QRegularExpression secret(QStringLiteral(R"((-{1,2}[\w-]*(?:key|token|secret|password|passwd|auth)[\w-]*)(=|\s+)("[^"]*"|'[^']*'|\S+))"),
                                           QRegularExpression::CaseInsensitiveOption);
    QString masked = commandLine;
    masked.replace(secret, QStringLiteral("\\1\\2…"));
    return masked;
}

QJsonObject describeForAgent(const WhaleAiAgent &agent)
{
    QJsonArray variables;
    for (const QString &entry : agent.environment) {
        variables.append(entry.section(QLatin1Char('='), 0, 0).trimmed());
    }
    QJsonObject result{{QStringLiteral("id"), agent.id},
                       {QStringLiteral("name"), agent.name},
                       {QStringLiteral("command"), maskSecrets(agent.commandLine)},
                       {QStringLiteral("environmentVariables"), variables}};
    if (!agent.startMode.isEmpty()) {
        result.insert(QStringLiteral("startMode"), agent.startMode);
    }
    if (agent.usesLocalModel()) {
        QJsonObject local{{QStringLiteral("provider"), agent.localProvider}, {QStringLiteral("model"), agent.localModel}};
        if (agent.localContextLength > 0) {
            local.insert(QStringLiteral("contextLength"), agent.localContextLength);
        }
        result.insert(QStringLiteral("localModel"), local);
    }
    return result;
}

std::optional<WhaleAiAgent> proposal(const QJsonObject &arguments, const QList<WhaleAiAgent> &agents, QString *error, const WhaleAiLocalSetup &setup)
{
    auto fail = [error](const QString &message) -> std::optional<WhaleAiAgent> {
        if (error) {
            *error = message;
        }
        return std::nullopt;
    };
    WhaleAiAgent agent;
    agent.name = arguments.value(QLatin1String("name")).toString().trimmed();
    agent.commandLine = arguments.value(QLatin1String("command")).toString().trimmed();

    // AI12: a model Orcinus starts for the agent; it fills in what is left out.
    const QJsonValue localValue = arguments.value(QLatin1String("local_model"));
    const WhaleAiLocalProvider *provider = nullptr;
    std::optional<WhaleAiLocalModel> localModel;
    int contextLength = 0;
    if (!localValue.isUndefined() && !localValue.isNull()) {
        const QJsonObject local = localValue.toObject();
        const QString providerId = local.value(QLatin1String("provider")).toString(QStringLiteral("lmstudio"));
        const QString modelId = local.value(QLatin1String("model")).toString().trimmed();
        if (providerId != QLatin1String("lmstudio") && providerId != QLatin1String("strata")) {
            return fail(QStringLiteral("\"local_model.provider\" can be \"lmstudio\" (LM Studio) or \"strata\" (Strata)."));
        }
        if (modelId.isEmpty()) {
            return fail(QStringLiteral("\"local_model.model\" must be a model id from list_local_models."));
        }
        for (const WhaleAiLocalProvider &candidate : setup.providers) {
            if (candidate.id == providerId) {
                provider = &candidate;
            }
        }
        if (!provider) {
            return fail(providerId == QLatin1String("strata") ? QStringLiteral("Strata is not installed on this computer (Orcinus looks in ~/Strata).")
                                                              : QStringLiteral("LM Studio is not installed on this computer."));
        }
        for (const WhaleAiLocalModel &model : provider->models) {
            if (model.id == modelId) {
                localModel = model;
            }
        }
        if (!localModel) {
            return fail(QStringLiteral("%1 has no model \"%2\"; list_local_models shows the ids.").arg(provider->name, modelId));
        }
        if (providerId == QLatin1String("strata")) {
            contextLength = 0; // its config sets it
        } else {
            const int asked = local.value(QLatin1String("contextLength")).toInt();
            contextLength = asked > 0 ? asked : defaultContextLength(*localModel);
            if (localModel->maxContextLength > 0) {
                contextLength = std::min(contextLength, localModel->maxContextLength);
            }
        }
        if (agent.name.isEmpty()) {
            agent.name = localAgentName(*localModel);
        }
        if (agent.commandLine.isEmpty()) {
            for (const WhaleAiLocalClient &client : setup.clients) {
                if (client.id == QLatin1String("opencode")) {
                    agent.commandLine = client.commandLine;
                }
            }
            if (agent.commandLine.isEmpty()) {
                return fail(
                    QStringLiteral("OpenCode, which runs local models as an agent, is not installed; give a \"command\", or ask "
                                   "the user to install it (npm i -g opencode-ai)."));
            }
        }
    }

    if (agent.name.isEmpty() || agent.name.size() > 80) {
        return fail(QStringLiteral("\"name\" must have 1 to 80 characters."));
    }
    KShell::Errors splitError = KShell::NoError;
    if (agent.commandLine.isEmpty() || agent.commandLine.size() > 2000 || KShell::splitArgs(agent.commandLine, KShell::TildeExpand, &splitError).isEmpty()
        || splitError != KShell::NoError) {
        return fail(QStringLiteral("\"command\" must be the program and its arguments, quoted as in a shell."));
    }
    static const QRegularExpression variableName(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    const QJsonArray environment = arguments.value(QLatin1String("environment")).toArray();
    if (environment.size() > 50) {
        return fail(QStringLiteral("At most 50 environment variables."));
    }
    for (const QJsonValue &value : environment) {
        const QString entry = value.toString().trimmed();
        const QString name = entry.section(QLatin1Char('='), 0, 0);
        if (!variableName.match(name).hasMatch()) {
            return fail(QStringLiteral("Environment entries are NAME or NAME=value; \"%1\" is not.").arg(entry));
        }
        agent.environment.append(entry.contains(QLatin1Char('=')) ? entry : entry + QLatin1Char('='));
    }
    if (localModel) {
        agent.localProvider = provider->id;
        agent.localModel = localModel->id;
        agent.localContextLength = contextLength;
        if (kind(agent) == WhaleAiAgentKind::OpenCode) {
            agent.environment = withOpenCodeConfig(agent.environment, *provider, *localModel, contextLength);
        }
    }
    agent.id = uniqueId(agent.name, agents);
    return agent;
}

double defaultFreeDelaySeconds(const QString &provider)
{
    return provider == QLatin1String("strata") ? 600 : 3;
}

double freeDelaySeconds(const QString &provider)
{
    return std::max(0.0, aiGroup().readEntry(QStringLiteral("FreeDelay_") + provider, defaultFreeDelaySeconds(provider)));
}

void setFreeDelaySeconds(const QString &provider, double seconds)
{
    KConfigGroup group = aiGroup();
    if (qFuzzyCompare(seconds + 1, defaultFreeDelaySeconds(provider) + 1)) {
        group.deleteEntry(QStringLiteral("FreeDelay_") + provider);
    } else {
        group.writeEntry(QStringLiteral("FreeDelay_") + provider, seconds);
    }
    group.sync();
}

int defaultContextLength(const WhaleAiLocalModel &model)
{
    const int preferred = 65536;
    return model.maxContextLength > 0 ? std::min(preferred, model.maxContextLength) : preferred;
}

QString localAgentName(const WhaleAiLocalModel &model)
{
    return i18nc("@item %1 is the name of a language model", "%1 (local)", model.name);
}

QString openCodeConfig(const WhaleAiLocalProvider &provider, const WhaleAiLocalModel &model, int contextLength)
{
    const int context = contextLength > 0 ? contextLength : (model.maxContextLength > 0 ? model.maxContextLength : 65536);
    QJsonObject entry{{QStringLiteral("name"), model.name},
                      {QStringLiteral("limit"), QJsonObject{{QStringLiteral("context"), context}, {QStringLiteral("output"), std::min(32000, context / 4)}}},
                      {QStringLiteral("tool_call"), model.toolUse}};
    if (model.vision) {
        entry.insert(QStringLiteral("modalities"),
                     QJsonObject{{QStringLiteral("input"), QJsonArray{QStringLiteral("text"), QStringLiteral("image")}},
                                 {QStringLiteral("output"), QJsonArray{QStringLiteral("text")}}});
    }
    const QJsonObject config{
        {QStringLiteral("$schema"), QStringLiteral("https://opencode.ai/config.json")},
        {QStringLiteral("model"), QString(provider.id + QLatin1Char('/') + model.id)},
        {QStringLiteral("provider"),
         QJsonObject{{provider.id,
                      QJsonObject{{QStringLiteral("npm"), QStringLiteral("@ai-sdk/openai-compatible")},
                                  {QStringLiteral("name"), provider.name},
                                  {QStringLiteral("options"), QJsonObject{{QStringLiteral("baseURL"), provider.apiBase()}}},
                                  {QStringLiteral("models"), QJsonObject{{model.id, entry}}}}}}},
        // Local means local: nothing shared, no updates in the background.
        {QStringLiteral("share"), QStringLiteral("disabled")},
        {QStringLiteral("autoupdate"), false},
    };
    return QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact));
}

bool isLocalOpenCodeConfig(const QJsonObject &config)
{
    // What openCodeConfig() writes: a model of LM Studio or Strata, with that provider defined.
    const QString model = config.value(QLatin1String("model")).toString();
    const QString provider = model.section(QLatin1Char('/'), 0, 0);
    return (provider == QLatin1String("lmstudio") || provider == QLatin1String("strata"))
        && config.value(QLatin1String("provider")).toObject().contains(provider);
}

QStringList withOpenCodeConfig(const QStringList &environment, const WhaleAiLocalProvider &provider, const WhaleAiLocalModel &model, int contextLength)
{
    const QString name = QStringLiteral("OPENCODE_CONFIG_CONTENT");
    const QString line = name + QLatin1Char('=') + openCodeConfig(provider, model, contextLength);
    QStringList result;
    bool replaced = false;
    for (const QString &entry : environment) {
        if (entry.section(QLatin1Char('='), 0, 0).trimmed() != name) {
            result.append(entry);
            continue;
        }
        const QJsonObject config = QJsonDocument::fromJson(entry.section(QLatin1Char('='), 1).toUtf8()).object();
        if (!isLocalOpenCodeConfig(config)) {
            // The user's own configuration: theirs to change.
            return environment;
        }
        if (!replaced) {
            result.append(line);
            replaced = true;
        }
    }
    if (!replaced) {
        result.append(line);
    }
    return result;
}

WhaleAiAgent
localAgent(const WhaleAiLocalProvider &provider, const WhaleAiLocalModel &model, const WhaleAiLocalClient &client, const QList<WhaleAiAgent> &agents)
{
    WhaleAiAgent agent;
    agent.name = localAgentName(model);
    agent.id = uniqueId(agent.name, agents);
    agent.commandLine = client.commandLine;
    agent.localProvider = provider.id;
    agent.localModel = model.id;
    agent.localContextLength = defaultContextLength(model);
    agent.environment = withOpenCodeConfig({}, provider, model, agent.localContextLength);
    return agent;
}

QString uniqueId(const QString &base, const QList<WhaleAiAgent> &agents)
{
    QString stem = base.toLower();
    stem.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    stem.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
    if (stem.isEmpty()) {
        stem = QStringLiteral("agent");
    }
    auto taken = [&agents](const QString &id) {
        return std::any_of(agents.cbegin(), agents.cend(), [&id](const WhaleAiAgent &agent) {
            return agent.id == id;
        });
    };
    QString id = stem;
    for (int i = 2; taken(id); ++i) {
        id = stem + QLatin1Char('-') + QString::number(i);
    }
    return id;
}

QString defaultSystemPrompt()
{
    // English, like the context of each message: it goes to the agent, not the user. Keep it in
    // step with what the back channel (AI6) actually does.
    return QStringLiteral(
        "You are an AI assistant working with the user's files through the Orcinus file manager. The user is working in "
        "Orcinus next to this chat, and each message tells you the folder they have open and what they have selected.\n\n"
        "You can see and change what the user sees with your Orcinus tools: get_location and get_selection to read where "
        "they are, set_location to open a folder, select_items to select files in it, and list_actions and trigger_action "
        "to run the commands of the Orcinus window (view modes, sorting, panels, split view, profiles, ...), "
        "get_window_state and the window tools for the window size, panel widths, the split, messages and the built-in "
        "viewer, set_filter to filter a view by name, list_items for what a view shows (names, sizes, dates, in the order shown), list_settings and "
        "set_setting for Orcinus' settings, and list_profiles, apply_profile and save_profile for the user's profiles. Prefer these "
        "over guessing paths, and use them to show the user a result; they see every change you make.\n\n"
        "Operate Orcinus only through these tools, never through D-Bus (qdbus, gdbus, busctl), keyboard or mouse "
        "automation: other Orcinus windows of the user may be running. If a tool for what you want is missing, tell the "
        "user instead of working around it.\n\n"
        "Your Orcinus tools never change the user's files: they change what Orcinus shows (folder, selection, view, sorting, "
        "window) and, with set_setting and save_profile, Orcinus' own settings and profiles. Except for these two they are "
        "allowed in every mode, also in "
        "a plan or read-only mode, so when the user asks you to show, sort, select or arrange something, do it with them right "
        "away instead of presenting a plan. Settings are saved: change one only when the user asks for a setting, not for the "
        "current view. In a plan or read-only mode Orcinus refuses set_setting and save_profile; then tell the user to choose another mode in "
        "the AI panel's Mode list, and do not ask them to confirm instead. The user can take back your lasting changes "
        "(settings, folder views, profiles) with \"Undo the Agent's Changes\" in the AI panel. Orcinus' AI agents are not "
        "settings: read them with list_agents and suggest a new one with propose_agent; the user adds it, and enters API "
        "keys or other secrets themselves (remind them). For a local model, first read what is installed with "
        "list_local_models (servers, models, clients).\n\n"
        "You work with the user's own permissions on their real files. Before you change, move or delete anything outside "
        "what they asked for, or anything that is hard to undo, say what you will do and ask first.");
}

bool customSystemPrompt()
{
    return aiGroup().readEntry(CustomSystemPromptKey, false);
}

QString systemPrompt()
{
    if (!customSystemPrompt()) {
        return defaultSystemPrompt();
    }
    return aiGroup().readEntry(SystemPromptKey, defaultSystemPrompt());
}

void setSystemPrompt(const QString &text)
{
    KConfigGroup group = aiGroup();
    group.writeEntry(SystemPromptKey, text);
    group.sync();
}

void setCustomSystemPrompt(bool custom)
{
    KConfigGroup group = aiGroup();
    group.writeEntry(CustomSystemPromptKey, custom);
    group.sync();
}

bool showActions()
{
    return aiGroup().readEntry(ShowActionsKey, true);
}

void setShowActions(bool show)
{
    KConfigGroup group = aiGroup();
    group.writeEntry(ShowActionsKey, show);
    group.sync();
}

bool disclaimerAccepted()
{
    return aiGroup().readEntry(DisclaimerAcceptedKey, false);
}

void setDisclaimerAccepted(bool accepted)
{
    KConfigGroup group = aiGroup();
    group.writeEntry(DisclaimerAcceptedKey, accepted);
    group.sync();
}
}
