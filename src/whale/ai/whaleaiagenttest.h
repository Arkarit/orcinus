/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAIAGENTTEST_H
#define WHALEAIAGENTTEST_H

#include "whaleaiagents.h"

#include <QObject>

#include <memory>

class QTemporaryDir;

/**
 * @brief Checks whether an agent works, for the test button of the settings page.
 *
 * Starts the agent in an empty temporary folder and opens a session without MCP servers and
 * without a prompt, which costs nothing. Then ends it again.
 */
class WhaleAiAgentTest : public QObject
{
    Q_OBJECT

public:
    struct Result {
        bool ok = false;
        WhaleAcpAgentInfo agentInfo;
        QList<WhaleAcpConfigOption> configOptions;
        QString error; ///< translated
        QString detail; ///< technical reason, English
        QString log; ///< the end of the agent's stderr
        bool needsLogin = false;
    };

    explicit WhaleAiAgentTest(QObject *parent = nullptr);
    ~WhaleAiAgentTest() override;

    /** Runs the test; finished() follows, also when @p agent cannot be started at all. */
    void run(const WhaleAiAgent &agent, int timeoutMs = 30000);
    bool isRunning() const;

Q_SIGNALS:
    void finished(const WhaleAiAgentTest::Result &result);

private:
    void finish(const Result &result);

    WhaleAcpSession *m_session = nullptr;
    std::unique_ptr<QTemporaryDir> m_dir;
};

Q_DECLARE_METATYPE(WhaleAiAgentTest::Result)

#endif
