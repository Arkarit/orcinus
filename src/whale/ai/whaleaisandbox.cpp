/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaisandbox.h"

#include <KLocalizedString>

#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace
{
QString underHome(const QString &home, const QString &path)
{
    return QDir::cleanPath(home + QLatin1Char('/') + path);
}

/**
 * @return The npm package @p program belongs to (…/node_modules/name or …/node_modules/@scope/name;
 *         its dependencies are inside), else the folder of the resolved program.
 */
QString packageRoot(const QString &program)
{
    const QString resolved = QFileInfo(program).canonicalFilePath();
    if (resolved.isEmpty()) {
        return QString();
    }
    const QString modules = QStringLiteral("/node_modules/");
    const qsizetype at = resolved.indexOf(modules);
    if (at > 0) {
        const QStringList parts = resolved.mid(at + modules.size()).split(QLatin1Char('/'));
        const int depth = parts.value(0).startsWith(QLatin1Char('@')) ? 2 : 1;
        if (parts.size() > depth) {
            return resolved.left(at + modules.size()) + parts.mid(0, depth).join(QLatin1Char('/'));
        }
    }
    return QFileInfo(resolved).absolutePath();
}

/** @return Whether the sandbox shows @p path anyway (read-only system folders). */
bool isSystem(const QString &path)
{
    static const QStringList system{QStringLiteral("/usr"),
                                    QStringLiteral("/etc"),
                                    QStringLiteral("/opt"),
                                    QStringLiteral("/var"),
                                    QStringLiteral("/bin"),
                                    QStringLiteral("/sbin"),
                                    QStringLiteral("/lib"),
                                    QStringLiteral("/lib32"),
                                    QStringLiteral("/lib64"),
                                    QStringLiteral("/nix")};
    return WhaleAiSandbox::isInside(path, system);
}

void appendOnce(QStringList &list, const QString &path)
{
    if (!path.isEmpty() && !list.contains(path) && !isSystem(path)) {
        list.append(path);
    }
}
}

namespace WhaleAiSandbox
{
QString helper()
{
    const QString local = QCoreApplication::applicationDirPath() + QStringLiteral("/orcinus-sandbox");
    if (QFileInfo(local).isExecutable()) {
        return local;
    }
    const QString installed = QStringLiteral(WHALE_SANDBOX_INSTALLED);
    if (QFileInfo(installed).isExecutable()) {
        return installed;
    }
    return QString();
}

QString check(const QString &helper)
{
    static QString works;
    if (helper.isEmpty()) {
        return i18nc("@info", "The sandbox program orcinus-sandbox is not installed.");
    }
    if (works == helper) {
        return QString();
    }
    QProcess process;
    process.start(helper, {QStringLiteral("--check")});
    if (!process.waitForStarted()) {
        return i18nc("@info %1 is the message of the sandbox program", "The sandbox cannot start: %1", process.errorString());
    }
    if (!process.waitForFinished(10000)) {
        process.kill();
        process.waitForFinished();
        return i18nc("@info", "The sandbox did not start in time.");
    }
    if (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0) {
        works = helper;
        return QString();
    }
    const QString message = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
    return i18nc("@info %1 is the message of the sandbox program", "The sandbox cannot start: %1", message.isEmpty() ? process.errorString() : message);
}

Paths agentPaths(const WhaleAiAgent &agent, const QString &program, const QString &home)
{
    Paths paths;
    auto readWrite = [&](const QString &path, bool create) {
        const QString full = underHome(home, path);
        appendOnce(paths.readWrite, full);
        if (create) {
            appendOnce(paths.create, full);
        }
    };
    auto readOnly = [&](const QString &path) {
        appendOnce(paths.readOnly, underHome(home, path));
    };

    // Where agents are usually installed (npm with a prefix in the home folder), and git.
    readOnly(QStringLiteral(".local/bin"));
    readOnly(QStringLiteral(".local/lib"));
    readOnly(QStringLiteral(".gitconfig"));
    readOnly(QStringLiteral(".config/git"));
    readOnly(QStringLiteral(".npmrc"));
    appendOnce(paths.readOnly, packageRoot(program));
    const QString node = QStandardPaths::findExecutable(QStringLiteral("node"));
    if (!node.isEmpty()) {
        // nvm and the like: …/versions/node/vX/bin/node; the system's node is there anyway.
        appendOnce(paths.readOnly, QFileInfo(packageRoot(node)).absolutePath());
    }
    // npx keeps its packages there.
    readWrite(QStringLiteral(".npm"), true);

    switch (WhaleAiAgents::kind(agent)) {
    case WhaleAiAgentKind::Claude:
        readWrite(QStringLiteral(".claude"), true);
        readWrite(QStringLiteral(".claude.json"), false);
        readWrite(QStringLiteral(".local/share/claude"), false);
        readWrite(QStringLiteral(".cache/claude"), false);
        break;
    case WhaleAiAgentKind::Codex:
        readWrite(QStringLiteral(".codex"), true);
        break;
    case WhaleAiAgentKind::OpenCode: {
        // OpenCode follows the XDG base directories.
        auto xdg = [&](const char *variable, const QString &fallback) {
            const QString value = qEnvironmentVariable(variable);
            return QDir::isAbsolutePath(value) ? QDir::cleanPath(value) : underHome(home, fallback);
        };
        for (const QString &base : {xdg("XDG_CONFIG_HOME", QStringLiteral(".config")),
                                    xdg("XDG_DATA_HOME", QStringLiteral(".local/share")),
                                    xdg("XDG_STATE_HOME", QStringLiteral(".local/state")),
                                    xdg("XDG_CACHE_HOME", QStringLiteral(".cache"))}) {
            const QString path = base + QStringLiteral("/opencode");
            appendOnce(paths.readWrite, path);
            appendOnce(paths.create, path);
        }
        break;
    }
    case WhaleAiAgentKind::Unknown:
        break;
    }

    for (QString path : agent.sandboxPaths) {
        path = path.trimmed();
        if (path == QLatin1String("~") || path.startsWith(QLatin1String("~/"))) {
            path = home + path.mid(1);
        }
        if (QDir::isAbsolutePath(path)) {
            appendOnce(paths.readWrite, QDir::cleanPath(path));
        }
    }
    return paths;
}

WhaleAcpSession::AgentCommand
wrap(const WhaleAcpSession::AgentCommand &command, const QString &helper, const QStringList &folders, const Paths &paths, const QString &workingDirectory)
{
    WhaleAcpSession::AgentCommand wrapped;
    wrapped.program = helper;
    wrapped.environment = command.environment;
    for (const QString &folder : folders) {
        wrapped.arguments << QStringLiteral("--bind") << folder;
    }
    for (const QString &path : paths.readWrite) {
        wrapped.arguments << QStringLiteral("--bind") << path;
    }
    for (const QString &path : paths.readOnly) {
        wrapped.arguments << QStringLiteral("--ro-bind") << path;
    }
    wrapped.arguments << QStringLiteral("--chdir") << workingDirectory << QStringLiteral("--") << command.program;
    wrapped.arguments += command.arguments;
    return wrapped;
}

bool reaches(const QString &path, const QStringList &shared)
{
    static const QStringList own{QStringLiteral("/tmp"), QStringLiteral("/var/tmp"), QStringLiteral("/dev"), QStringLiteral("/proc")};
    return isInside(path, shared) || isSystem(path) || isInside(path, own);
}

bool isInside(const QString &path, const QStringList &folders)
{
    const QString clean = QDir::cleanPath(path);
    for (const QString &folder : folders) {
        const QString base = QDir::cleanPath(folder);
        if (clean == base || base == QLatin1String("/") || clean.startsWith(base + QLatin1Char('/'))) {
            return true;
        }
    }
    return false;
}
}
