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
const char ModeIdsKey[] = "ModeIds";
const char ModeNamesKey[] = "ModeNames";
const char ModeDescriptionsKey[] = "ModeDescriptions";

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

WhaleAiLoginHint loginHint(const WhaleAiAgent &agent, const QStringList &searchPaths)
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
    // The CLI that logs in, or npx with its package if it is not installed.
    auto loginCommand = [&searchPaths](const QString &program, const QString &npmPackage, const QStringList &arguments) {
        if (!findExecutable(program, searchPaths).isEmpty()) {
            return KShell::joinArgs(QStringList{program} + arguments);
        }
        return KShell::joinArgs(QStringList{QStringLiteral("npx"), QStringLiteral("-y"), npmPackage} + arguments);
    };

    WhaleAiLoginHint hint;
    if (uses(QStringLiteral("codex-acp"))) {
        hint.text = i18nc("@info",
                          "Codex uses the login of the Codex CLI: run “codex login” in a terminal once. "
                          "With an API key instead, set OPENAI_API_KEY or CODEX_API_KEY in the agent's environment (Edit…).");
        hint.command = loginCommand(QStringLiteral("codex"), QStringLiteral("@openai/codex"), {QStringLiteral("login")});
        hint.url = QUrl(QStringLiteral("https://github.com/openai/codex"));
    } else if (uses(QStringLiteral("claude-agent-acp"))) {
        hint.text = i18nc("@info", "Claude uses the login of Claude Code: start “claude” in a terminal once and log in there.");
        hint.command = loginCommand(QStringLiteral("claude"), QStringLiteral("@anthropic-ai/claude-code"), {});
        hint.url = QUrl(QStringLiteral("https://github.com/anthropics/claude-code"));
    } else if (uses(QStringLiteral("opencode")) || uses(QStringLiteral("opencode-ai"))) {
        hint.text = i18nc("@info",
                          "OpenCode keeps its providers in its own configuration: run “opencode auth login” in a terminal, "
                          "or set up a local model there.");
        hint.command = loginCommand(QStringLiteral("opencode"), QStringLiteral("opencode-ai"), {QStringLiteral("auth"), QStringLiteral("login")});
        hint.url = QUrl(QStringLiteral("https://opencode.ai"));
    }
    return hint;
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
}
