/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaiagenttest.h"

#include <KLocalizedString>

#include <QTemporaryDir>
#include <QTimer>

WhaleAiAgentTest::WhaleAiAgentTest(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<WhaleAiAgentTest::Result>();
}

WhaleAiAgentTest::~WhaleAiAgentTest() = default;

bool WhaleAiAgentTest::isRunning() const
{
    return m_session != nullptr;
}

void WhaleAiAgentTest::run(const WhaleAiAgent &agent, int timeoutMs)
{
    if (m_session) {
        return;
    }

    QString commandError;
    const WhaleAcpSession::AgentCommand command = agent.command(&commandError);
    if (command.program.isEmpty()) {
        Result result;
        result.error = commandError;
        // Asynchronous, like a real test.
        QTimer::singleShot(0, this, [this, result] {
            Q_EMIT finished(result);
        });
        return;
    }

    m_dir = std::make_unique<QTemporaryDir>();
    m_session = new WhaleAcpSession(this);
    m_session->setStartTimeout(timeoutMs);
    connect(m_session, &WhaleAcpSession::stateChanged, this, [this](WhaleAcpSession::State state) {
        if (state == WhaleAcpSession::State::Ready) {
            Result result;
            result.ok = true;
            result.agentInfo = m_session->agentInfo();
            result.configOptions = m_session->configOptions();
            result.log = m_session->agentLog();
            finish(result);
        } else if (state == WhaleAcpSession::State::Failed) {
            Result result;
            result.agentInfo = m_session->agentInfo();
            result.detail = m_session->errorString();
            result.log = m_session->agentLog();
            switch (m_session->errorCode()) {
            case WhaleAcpError::AuthRequired:
                result.needsLogin = true;
                result.error = i18nc("@info", "The agent needs a login first.");
                break;
            case WhaleAcpError::Timeout:
                result.error = i18nc("@info", "The agent did not answer in time.");
                break;
            case WhaleAcpError::ConnectionClosed:
                result.error = i18nc("@info", "The agent ended before it was ready.");
                break;
            default:
                result.error = i18nc("@info", "The agent could not be started.");
                break;
            }
            finish(result);
        }
    });
    m_session->start(command, m_dir->path());
}

void WhaleAiAgentTest::finish(const Result &result)
{
    WhaleAcpSession *session = std::exchange(m_session, nullptr);
    session->disconnect(this);
    session->close();
    // The session stops the agent on its own; let it do that before it goes.
    QTimer::singleShot(10000, session, &QObject::deleteLater);
    Q_EMIT finished(result);
}

#include "moc_whaleaiagenttest.cpp"
