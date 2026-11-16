/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaledevai.h"

#include "dolphinmainwindow.h"
#include "whaleacpsession.h"
#include "whaleacptypes.h"
#include "whaleaiagents.h"
#include "whaleaimcpserver.h"
#include "whaleaipanel.h"
#include "whaleaitranscript.h"

#include <QAbstractButton>
#include <QComboBox>
#include <QDockWidget>
#include <QLabel>
#include <QTextDocument>

namespace
{
QString statusName(WhaleAcpToolCall::Status status)
{
    switch (status) {
    case WhaleAcpToolCall::Status::Pending:
        return QStringLiteral("pending");
    case WhaleAcpToolCall::Status::InProgress:
        return QStringLiteral("in_progress");
    case WhaleAcpToolCall::Status::Completed:
        return QStringLiteral("completed");
    case WhaleAcpToolCall::Status::Failed:
        return QStringLiteral("failed");
    case WhaleAcpToolCall::Status::Cancelled:
        return QStringLiteral("cancelled");
    }
    return QString();
}

QString stateName(const WhaleAcpSession *session)
{
    if (!session) {
        return QStringLiteral("none");
    }
    switch (session->state()) {
    case WhaleAcpSession::State::Idle:
        return QStringLiteral("idle");
    case WhaleAcpSession::State::Starting:
        return QStringLiteral("starting");
    case WhaleAcpSession::State::Ready:
        return QStringLiteral("ready");
    case WhaleAcpSession::State::Prompting:
        return QStringLiteral("prompting");
    case WhaleAcpSession::State::Failed:
        return QStringLiteral("failed");
    case WhaleAcpSession::State::Closed:
        return QStringLiteral("closed");
    }
    return QString();
}

QString kindName(WhaleAiTranscript::Kind kind)
{
    switch (kind) {
    case WhaleAiTranscript::Kind::UserMessage:
        return QStringLiteral("user");
    case WhaleAiTranscript::Kind::AgentMessage:
        return QStringLiteral("agent");
    case WhaleAiTranscript::Kind::Thought:
        return QStringLiteral("thought");
    case WhaleAiTranscript::Kind::ToolCall:
        return QStringLiteral("toolCall");
    case WhaleAiTranscript::Kind::Permission:
        return QStringLiteral("permission");
    case WhaleAiTranscript::Kind::Notice:
        return QStringLiteral("notice");
    case WhaleAiTranscript::Kind::Action:
        return QStringLiteral("action");
    case WhaleAiTranscript::Kind::Error:
        return QStringLiteral("error");
    }
    return QString();
}

/// The text the user reads in @p entry: its labels, top to bottom, as plain text.
QString entryText(const QWidget *entry)
{
    QStringList parts;
    const QList<QLabel *> labels = entry->findChildren<QLabel *>();
    QList<const QLabel *> all{qobject_cast<const QLabel *>(entry)};
    for (const QLabel *label : labels) {
        all.append(label);
    }
    for (const QLabel *label : std::as_const(all)) {
        if (!label || label->text().isEmpty()) {
            continue;
        }
        QString text = label->text();
        if (label->textFormat() == Qt::RichText || (label->textFormat() == Qt::AutoText && Qt::mightBeRichText(text))) {
            QTextDocument document;
            document.setHtml(text);
            text = document.toPlainText();
        } else if (label->textFormat() == Qt::MarkdownText) {
            QTextDocument document;
            document.setMarkdown(text);
            text = document.toPlainText();
        }
        parts.append(text.trimmed());
    }
    return parts.join(QLatin1Char('\n'));
}

QJsonObject toolCallJson(const WhaleAcpToolCall &toolCall)
{
    return {
        {QStringLiteral("id"), toolCall.id},
        {QStringLiteral("title"), toolCall.title},
        {QStringLiteral("kind"), toolCall.kind},
        {QStringLiteral("status"), statusName(toolCall.status)},
        {QStringLiteral("ownTool"), WhaleAiMcpServer::ownTool(toolCall)},
        {QStringLiteral("rawInput"), toolCall.rawInput},
        {QStringLiteral("locations"), QJsonArray::fromStringList(toolCall.locations)},
    };
}
}

WhaleDevAi::WhaleDevAi(DolphinMainWindow *window)
    : QObject(window)
    , m_window(window)
{
}

WhaleAiPanel *WhaleDevAi::panel() const
{
    return m_window->findChild<WhaleAiPanel *>();
}

void WhaleDevAi::resetRecord() const
{
    m_session.clear();
    m_prompts = 0;
    m_turnsFinished = 0;
    m_stopReasons.clear();
    m_toolCalls = {};
    m_permissions = {};
    m_error.clear();
}

void WhaleDevAi::attach() const
{
    WhaleAiPanel *aiPanel = panel();
    WhaleAcpSession *session = aiPanel ? aiPanel->session() : nullptr;
    if (!session || session == m_session) {
        return;
    }
    // A new session: the panel started one for the first prompt, or again after a failure.
    m_session = session;
    auto *self = const_cast<WhaleDevAi *>(this);
    connect(session, &WhaleAcpSession::toolCallChanged, self, [this](const WhaleAcpToolCall &toolCall) {
        const QJsonObject json = toolCallJson(toolCall);
        for (int i = 0; i < m_toolCalls.size(); ++i) {
            if (m_toolCalls.at(i).toObject().value(QStringLiteral("id")).toString() == toolCall.id) {
                m_toolCalls.replace(i, json);
                return;
            }
        }
        m_toolCalls.append(json);
    });
    connect(session, &WhaleAcpSession::permissionRequested, self, [this](const WhaleAcpPermissionRequest &request) {
        QJsonArray options;
        for (const WhaleAcpPermissionRequest::Option &option : request.options) {
            options.append(QJsonObject{{QStringLiteral("id"), option.id}, {QStringLiteral("name"), option.name}, {QStringLiteral("kind"), option.kind}});
        }
        m_permissions.append(QJsonObject{
            {QStringLiteral("key"), request.key},
            {QStringLiteral("title"), request.toolCall.title},
            {QStringLiteral("kind"), request.toolCall.kind},
            {QStringLiteral("ownTool"), WhaleAiMcpServer::ownTool(request.toolCall)},
            {QStringLiteral("rawInput"), request.toolCall.rawInput},
            {QStringLiteral("options"), options},
        });
    });
    connect(session, &WhaleAcpSession::promptFinished, self, [this](const QString &stopReason) {
        ++m_turnsFinished;
        m_stopReasons.append(stopReason);
    });
    connect(session, &WhaleAcpSession::stateChanged, self, [this, session](WhaleAcpSession::State state) {
        if (state == WhaleAcpSession::State::Failed) {
            m_error = session->errorString();
        }
    });
}

QJsonObject WhaleDevAi::state() const
{
    WhaleAiPanel *aiPanel = panel();
    if (!aiPanel) {
        return {{QStringLiteral("available"), false}};
    }
    attach();
    WhaleAcpSession *session = aiPanel->session();

    QJsonArray agents;
    const QList<WhaleAiAgent> all = WhaleAiAgents::agents();
    for (const WhaleAiAgent &agent : all) {
        agents.append(agent.id);
    }

    QJsonValue mode = QJsonValue::Null;
    QJsonArray modes;
    if (const WhaleAcpConfigOption *option = session ? session->configOption(QStringLiteral("mode")) : nullptr) {
        mode = option->currentValue;
        for (const WhaleAcpConfigOption::Choice &choice : option->choices) {
            modes.append(choice.value);
        }
    }

    // Open requests: recorded, not answered by the panel itself (own tools) and with buttons shown.
    QJsonArray pending;
    for (const QJsonValue &value : std::as_const(m_permissions)) {
        const QJsonObject request = value.toObject();
        const QString key = request.value(QStringLiteral("key")).toString();
        const QWidget *frame = aiPanel->transcript()->findChild<QWidget *>(QStringLiteral("permission-") + key);
        const QWidget *buttons = frame ? frame->findChild<QWidget *>(QStringLiteral("buttons")) : nullptr;
        if (buttons && !buttons->isHidden()) {
            pending.append(request);
        }
    }

    const QWidget *send = aiPanel->findChild<QWidget *>(QStringLiteral("send"));
    const QWidget *dock = qobject_cast<QWidget *>(aiPanel->parent());
    return {
        {QStringLiteral("available"), true},
        {QStringLiteral("panelVisible"), dock && dock->isVisible()},
        {QStringLiteral("agentId"), aiPanel->agentId()},
        {QStringLiteral("agents"), agents},
        {QStringLiteral("disclaimerAccepted"), WhaleAiAgents::disclaimerAccepted()},
        {QStringLiteral("sessionState"), stateName(session)},
        {QStringLiteral("busy"), send && send->property("busy").toBool()},
        {QStringLiteral("waitingForMode"), aiPanel->isWaitingForMode()},
        {QStringLiteral("mode"), mode},
        {QStringLiteral("modes"), modes},
        {QStringLiteral("conversation"), m_conversation},
        {QStringLiteral("prompts"), m_prompts},
        {QStringLiteral("turnsFinished"), m_turnsFinished},
        {QStringLiteral("lastStopReason"), m_stopReasons.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(m_stopReasons.constLast())},
        {QStringLiteral("toolCallCount"), m_toolCalls.size()},
        {QStringLiteral("pendingPermissions"), pending},
        {QStringLiteral("error"), m_error},
    };
}

QJsonObject WhaleDevAi::transcript() const
{
    WhaleAiPanel *aiPanel = panel();
    if (!aiPanel) {
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("no AI panel")}};
    }
    attach();
    QJsonArray entries;
    const QList<QWidget *> all = aiPanel->transcript()->entries();
    for (const QWidget *entry : all) {
        entries.append(QJsonObject{
            {QStringLiteral("kind"), kindName(WhaleAiTranscript::kindOf(entry))},
            {QStringLiteral("text"), entryText(entry)},
        });
    }

    QJsonArray permissions;
    for (const QJsonValue &value : std::as_const(m_permissions)) {
        QJsonObject request = value.toObject();
        const QString key = request.value(QStringLiteral("key")).toString();
        const QWidget *frame = aiPanel->transcript()->findChild<QWidget *>(QStringLiteral("permission-") + key);
        QString answered;
        if (!frame) {
            // The panel answered it (the back channel's own tools, AI6c).
            answered = QStringLiteral("auto");
        } else if (const QLabel *answer = frame->findChild<QLabel *>(QStringLiteral("answer")); answer && !answer->isHidden()) {
            answered = answer->text();
        } else if (const QWidget *buttons = frame->findChild<QWidget *>(QStringLiteral("buttons")); buttons && buttons->isHidden()) {
            answered = QStringLiteral("withdrawn");
        }
        request.insert(QStringLiteral("answered"), answered);
        permissions.append(request);
    }

    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("conversation"), m_conversation},
        {QStringLiteral("entries"), entries},
        {QStringLiteral("toolCalls"), m_toolCalls},
        {QStringLiteral("permissions"), permissions},
        {QStringLiteral("stopReasons"), QJsonArray::fromStringList(m_stopReasons)},
        {QStringLiteral("error"), m_error},
    };
}

QJsonObject WhaleDevAi::newConversation(const QString &agentId, const QString &mode)
{
    WhaleAiPanel *aiPanel = panel();
    if (!aiPanel) {
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("no AI panel")}};
    }
    if (!agentId.isEmpty()) {
        const auto *agents = aiPanel->findChild<QComboBox *>(QStringLiteral("agents"));
        if (!agents || agents->findData(agentId) < 0) {
            return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QString(QStringLiteral("unknown agent: ") + agentId)}};
        }
        aiPanel->setAgentId(agentId);
    }
    aiPanel->newConversation();
    resetRecord();
    ++m_conversation;
    if (!mode.isEmpty()) {
        // Without a conversation, this stores the agent's start mode, as the bar does.
        aiPanel->chooseMode(mode);
    }
    if (auto *dock = qobject_cast<QDockWidget *>(aiPanel->parent())) {
        dock->show();
        dock->raise();
    }
    return {{QStringLiteral("ok"), true}, {QStringLiteral("state"), state()}};
}

QJsonObject WhaleDevAi::sendPrompt(const QString &text)
{
    WhaleAiPanel *aiPanel = panel();
    if (!aiPanel) {
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("no AI panel")}};
    }
    if (!WhaleAiAgents::disclaimerAccepted()) {
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("the disclaimer is not accepted")}};
    }
    if (!aiPanel->sendPrompt(text)) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("the panel did not send it (no agent, agent busy, or it could not start)")},
                {QStringLiteral("state"), state()}};
    }
    // The session exists now (it starts asynchronously): record from its first signal on.
    attach();
    ++m_prompts;
    return {{QStringLiteral("ok"), true}, {QStringLiteral("state"), state()}};
}

QJsonObject WhaleDevAi::answerPermission(const QString &key, const QString &optionId)
{
    WhaleAiPanel *aiPanel = panel();
    if (!aiPanel) {
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("no AI panel")}};
    }
    QWidget *frame = aiPanel->transcript()->findChild<QWidget *>(QStringLiteral("permission-") + key);
    auto *button = frame ? frame->findChild<QAbstractButton *>(QStringLiteral("option-") + optionId) : nullptr;
    if (!button || button->parentWidget()->isHidden()) {
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("no open request with this option")}};
    }
    button->click();
    return {{QStringLiteral("ok"), true}, {QStringLiteral("state"), state()}};
}

QJsonObject WhaleDevAi::stop()
{
    WhaleAiPanel *aiPanel = panel();
    if (!aiPanel) {
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("no AI panel")}};
    }
    aiPanel->stop();
    return {{QStringLiteral("ok"), true}, {QStringLiteral("state"), state()}};
}

#include "moc_whaledevai.cpp"
