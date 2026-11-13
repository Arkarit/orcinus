/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAISANDBOX_H
#define WHALEAISANDBOX_H

#include "whaleacpsession.h"
#include "whaleaiagents.h"

#include <QDir>
#include <QStringList>

/**
 * @brief Starting an agent in orcinus-sandbox, so that it reaches only its folder (AI5d).
 *
 * The helper (src/whale/ai/sandbox/orcinus-sandbox.cpp) gives the agent the system read-only
 * and the paths it is given; the rest of the user's data is not there. See
 * .privatedoc/ai-support-plan.md (AI5d).
 */
namespace WhaleAiSandbox
{
/** What an agent needs in its sandbox besides the folder it works in. */
struct Paths {
    QStringList readWrite; ///< its settings and login, caches
    QStringList readOnly; ///< where it is installed, git settings
    QStringList create; ///< folders of readWrite to make if missing, so the agent keeps what it writes there

    bool operator==(const Paths &other) const = default;
};

/** @return The helper: next to the program (build tree), else the installed one; empty if none. */
QString helper();

/**
 * @return Why the sandbox cannot start (translated, with the helper's own message), or an
 *         empty string if it works. Runs @p helper once with --check; a success is remembered.
 */
QString check(const QString &helper);

/**
 * @return What @p agent needs (by its kind, WhaleAiAgents::kind(), plus its sandboxPaths), with
 *         the package of @p program (the resolved agent command) and of node read-only.
 */
Paths agentPaths(const WhaleAiAgent &agent, const QString &program, const QString &home = QDir::homePath());

/**
 * @return @p command run by @p helper with @p folders read-write, @p paths, in
 *         @p workingDirectory.
 */
WhaleAcpSession::AgentCommand
wrap(const WhaleAcpSession::AgentCommand &command, const QString &helper, const QStringList &folders, const Paths &paths, const QString &workingDirectory);

/** @return Whether @p path is one of @p folders or inside one. */
bool isInside(const QString &path, const QStringList &folders);

/**
 * @return Whether an agent in the sandbox reaches @p path: inside @p shared (its folders and
 *         paths) or in what every sandbox has (the system read-only, its own /tmp).
 */
bool reaches(const QString &path, const QStringList &shared);
}

#endif
