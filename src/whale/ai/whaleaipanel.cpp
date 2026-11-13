/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaipanel.h"
#include "whaleaiagents.h"
#include "whaleaidebug.h"
#include "whaleaitranscript.h"

#include <KLocalizedString>

#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{
enum Page {
    ChatPage,
    SetupPage,
};
}

WhaleAiPanel::WhaleAiPanel(QWidget *parent)
    : Panel(parent)
{
    // Chat
    auto *chat = new QWidget(this);

    m_agents = new QComboBox(chat);
    m_agents->setObjectName(QStringLiteral("agents"));
    m_agents->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_agents->setMinimumContentsLength(8);
    m_agents->setToolTip(i18nc("@info:tooltip", "The AI agent to talk to"));

    m_newConversation = new QToolButton(chat);
    m_newConversation->setObjectName(QStringLiteral("newConversation"));
    m_newConversation->setIcon(QIcon::fromTheme(QStringLiteral("document-new")));
    m_newConversation->setToolTip(i18nc("@info:tooltip", "New Conversation"));
    m_newConversation->setAutoRaise(true);

    auto *top = new QHBoxLayout;
    top->addWidget(m_agents, 1);
    top->addWidget(m_newConversation);

    m_sessionInfo = new QLabel(chat);
    m_sessionInfo->setObjectName(QStringLiteral("sessionInfo"));
    m_sessionInfo->setForegroundRole(QPalette::PlaceholderText);
    m_sessionInfo->setWordWrap(true);
    m_sessionInfo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    // The permission bar
    m_modes = new QComboBox(chat);
    m_modes->setObjectName(QStringLiteral("modes"));
    m_modes->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_modes->setMinimumContentsLength(8);
    auto *modesLabel = new QLabel(i18nc("@label:listbox what an AI agent may do on its own", "Mode:"), chat);
    modesLabel->setBuddy(m_modes);
    auto *modesRow = new QHBoxLayout;
    modesRow->addWidget(modesLabel);
    modesRow->addWidget(m_modes, 1);

    m_transcript = new WhaleAiTranscript(chat);
    m_transcript->setObjectName(QStringLiteral("transcript"));

    m_input = new QPlainTextEdit(chat);
    m_input->setObjectName(QStringLiteral("input"));
    m_input->setPlaceholderText(i18nc("@info:placeholder", "Ask the agent… (Enter sends, Shift+Enter: new line)"));
    m_input->setTabChangesFocus(true);
    m_input->setFixedHeight(m_input->fontMetrics().lineSpacing() * 3 + 2 * m_input->frameWidth() + 8);
    m_input->installEventFilter(this);

    m_send = new QToolButton(chat);
    m_send->setObjectName(QStringLiteral("send"));
    m_send->setAutoRaise(true);

    auto *bottom = new QHBoxLayout;
    bottom->addWidget(m_input, 1);
    bottom->addWidget(m_send, 0, Qt::AlignBottom);

    auto *chatLayout = new QVBoxLayout(chat);
    chatLayout->setContentsMargins(0, 0, 0, 0);
    chatLayout->addLayout(top);
    chatLayout->addWidget(m_sessionInfo);
    chatLayout->addLayout(modesRow);
    chatLayout->addWidget(m_transcript, 1);
    chatLayout->addLayout(bottom);

    // Setup, while no agent is set up
    auto *setup = new QWidget(this);
    auto *setupText = new QLabel(i18nc("@info",
                                       "No AI agent is set up yet. The AI panel talks to agents installed on this computer, such as Claude Code, "
                                       "Codex or OpenCode with a local model."),
                                 setup);
    setupText->setWordWrap(true);
    setupText->setAlignment(Qt::AlignCenter);
    auto *setupButton = new QPushButton(QIcon::fromTheme(QStringLiteral("configure")), i18nc("@action:button", "Set Up Agents…"), setup);
    setupButton->setObjectName(QStringLiteral("setUp"));
    auto *setupLayout = new QVBoxLayout(setup);
    setupLayout->addStretch();
    setupLayout->addWidget(setupText);
    setupLayout->addWidget(setupButton, 0, Qt::AlignCenter);
    setupLayout->addStretch();

    m_stack = new QStackedWidget(this);
    m_stack->insertWidget(ChatPage, chat);
    m_stack->insertWidget(SetupPage, setup);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_stack);

    connect(setupButton, &QPushButton::clicked, this, &WhaleAiPanel::configureRequested);
    connect(m_agents, &QComboBox::activated, this, &WhaleAiPanel::agentChosen);
    connect(m_modes, &QComboBox::activated, this, [this](int index) {
        chooseMode(m_modes->itemData(index).toString());
    });
    connect(m_newConversation, &QToolButton::clicked, this, &WhaleAiPanel::newConversation);
    connect(m_send, &QToolButton::clicked, this, [this] {
        if (m_send->property("busy").toBool()) {
            stop();
        } else if (sendPrompt(m_input->toPlainText())) {
            m_input->clear();
        }
    });
    connect(m_input, &QPlainTextEdit::textChanged, this, &WhaleAiPanel::updateState);
    connect(m_transcript, &WhaleAiTranscript::linkActivated, this, &WhaleAiPanel::openLink);
    connect(m_transcript, &WhaleAiTranscript::permissionAnswered, this, [this](const QString &key, const QString &optionId) {
        if (m_session) {
            m_session->answerPermission(key, optionId);
        }
    });

    setFocusProxy(m_input);
    fillAgents(WhaleAiAgents::defaultAgentId());
    updateState();
    updateSessionInfo();
}

WhaleAiPanel::~WhaleAiPanel()
{
    endSession();
}

void WhaleAiPanel::readSettings()
{
    Panel::readSettings();
    fillAgents(agentId());
    // The agent of the conversation was removed.
    if (m_session && WhaleAiAgents::agent(m_sessionAgentId).id.isEmpty()) {
        endSession();
        m_transcript->addNotice(i18nc("@info", "The agent of this conversation was removed."));
    }
    updateState();
    updateSessionInfo();
}

void WhaleAiPanel::fillAgents(const QString &preferredId)
{
    const QList<WhaleAiAgent> agents = WhaleAiAgents::agents();
    m_agents->clear();
    for (const WhaleAiAgent &agent : agents) {
        m_agents->addItem(agent.name, agent.id);
    }
    int index = m_agents->findData(preferredId);
    if (index < 0) {
        index = m_agents->findData(WhaleAiAgents::defaultAgentId());
    }
    m_agents->setCurrentIndex(qMax(index, 0));
    m_stack->setCurrentIndex(agents.isEmpty() ? SetupPage : ChatPage);
}

QString WhaleAiPanel::agentId() const
{
    return m_agents->currentData().toString();
}

void WhaleAiPanel::setAgentId(const QString &id)
{
    const int index = m_agents->findData(id);
    if (index >= 0 && index != m_agents->currentIndex()) {
        m_agents->setCurrentIndex(index);
        agentChosen();
    }
}

WhaleAcpSession *WhaleAiPanel::session() const
{
    return m_session;
}

WhaleAiTranscript *WhaleAiPanel::transcript() const
{
    return m_transcript;
}

void WhaleAiPanel::agentChosen()
{
    if (m_session && m_sessionAgentId != agentId()) {
        endSession();
        m_transcript->addNotice(
            i18nc("@info %1 is the name of an AI agent", "Now talking to %1. The next message starts a new conversation.", m_agents->currentText()));
    }
    updateState();
    updateSessionInfo();
}

bool WhaleAiPanel::sendPrompt(const QString &text)
{
    const QString prompt = text.trimmed();
    if (prompt.isEmpty() || agentId().isEmpty()) {
        return false;
    }
    if (m_session && m_session->state() != WhaleAcpSession::State::Ready) {
        // One prompt at a time; the agent is starting or working.
        return false;
    }
    if (!m_pendingPrompt.isEmpty()) {
        // The last one still waits for a mode.
        return false;
    }
    // A program that cannot be started keeps the text in the input.
    if (!m_session && !startSession()) {
        return false;
    }
    m_transcript->addUserMessage(prompt);
    m_pendingPrompt = prompt;
    sendPending();
    updateState();
    return true;
}

bool WhaleAiPanel::startSession()
{
    const WhaleAiAgent agent = WhaleAiAgents::agent(agentId());
    QString error;
    const WhaleAcpSession::AgentCommand command = agent.command(&error);
    if (command.program.isEmpty()) {
        m_transcript->addNotice(i18nc("@info %1 is the name of an AI agent, %2 the reason", "%1 cannot be started: %2", agent.name, error),
                                WhaleAiTranscript::Kind::Error);
        return false;
    }

    QString directory = QDir::homePath();
    if (url().isLocalFile() && QFileInfo(url().toLocalFile()).isDir()) {
        directory = url().toLocalFile();
    }

    m_sessionAgentId = agent.id;
    m_modeState = ModeState::Unknown;
    m_modeRequest.clear();
    m_shownMode.clear();
    m_session = new WhaleAcpSession(this);
    m_session->setProperty("workingDirectory", directory);
    m_transcript->addNotice(i18nc("@info %1 is the name of an AI agent, %2 a folder", "%1 works in %2.", agent.name, directory));

    WhaleAcpSession *session = m_session;
    connect(session, &WhaleAcpSession::stateChanged, this, [this, session](WhaleAcpSession::State state) {
        if (state == WhaleAcpSession::State::Ready) {
            if (m_modeState == ModeState::Unknown) {
                applyStartMode();
            } else {
                sendPending();
            }
        } else if (state == WhaleAcpSession::State::Failed) {
            QString details = session->errorString();
            const QString log = session->agentLog().trimmed();
            if (!log.isEmpty()) {
                details += QStringLiteral("\n\n") + log;
            }
            QString message = i18nc("@info", "The agent stopped working.");
            if (session->errorCode() == WhaleAcpError::AuthRequired) {
                message = i18nc("@info", "The agent needs a login first. See “Test” in the AI Agents settings for how to log in.");
            }
            m_transcript->addNotice(message, WhaleAiTranscript::Kind::Error, details);
            // The next prompt starts a new one.
            endSession();
        }
        updateState();
        updateSessionInfo();
    });
    connect(session, &WhaleAcpSession::agentMessageChunk, m_transcript, &WhaleAiTranscript::appendAgentText);
    connect(session, &WhaleAcpSession::agentThoughtChunk, m_transcript, &WhaleAiTranscript::appendThought);
    connect(session, &WhaleAcpSession::toolCallChanged, m_transcript, &WhaleAiTranscript::updateToolCall);
    connect(session, &WhaleAcpSession::permissionRequested, m_transcript, &WhaleAiTranscript::addPermissionRequest);
    connect(session, &WhaleAcpSession::permissionWithdrawn, m_transcript, &WhaleAiTranscript::withdrawPermissionRequest);
    connect(session, &WhaleAcpSession::configOptionsChanged, this, [this, session] {
        if (const WhaleAcpConfigOption *mode = session->configOption(QStringLiteral("mode"))) {
            // For the bar of the next conversation.
            WhaleAiAgents::setKnownModes(m_sessionAgentId, *mode);
        }
        noteModeChange();
        updateSessionInfo();
    });
    connect(session, &WhaleAcpSession::configOptionSet, this, &WhaleAiPanel::modeSet);
    connect(session, &WhaleAcpSession::usageChanged, this, [this](qint64 used, qint64 size) {
        m_sessionInfo->setProperty("usage", size > 0 ? int(used * 100 / size) : -1);
        updateSessionInfo();
    });
    connect(session, &WhaleAcpSession::protocolWarning, this, [](const QString &message) {
        qCWarning(WhaleAiDebug) << message;
    });
    connect(session, &WhaleAcpSession::promptFinished, this, [this](const QString &stopReason) {
        m_transcript->endTurn();
        if (stopReason == QLatin1String("cancelled")) {
            m_transcript->addNotice(i18nc("@info", "Stopped."));
        } else if (stopReason == QLatin1String("refusal")) {
            m_transcript->addNotice(i18nc("@info", "The agent refused to go on."));
        } else if (stopReason == QLatin1String("max_tokens") || stopReason == QLatin1String("max_turn_requests")) {
            m_transcript->addNotice(i18nc("@info", "The agent reached its limit for one answer."));
        } else if (stopReason == QLatin1String("error") && m_session && m_session->state() == WhaleAcpSession::State::Ready) {
            m_transcript->addNotice(i18nc("@info", "The agent could not answer."), WhaleAiTranscript::Kind::Error, m_session->errorString());
        }
        updateState();
    });

    m_session->start(command, directory);
    m_sessionInfo->setProperty("usage", -1);
    return m_session != nullptr;
}

void WhaleAiPanel::sendPending()
{
    if (!m_session || m_session->state() != WhaleAcpSession::State::Ready || m_modeState != ModeState::Set || m_pendingPrompt.isEmpty()) {
        return;
    }
    m_session->prompt({WhaleAcpContent::text(std::exchange(m_pendingPrompt, QString()))});
    updateState();
}

void WhaleAiPanel::applyStartMode()
{
    const WhaleAiAgent agent = WhaleAiAgents::agent(m_sessionAgentId);
    const WhaleAcpConfigOption *mode = m_session->configOption(QStringLiteral("mode"));
    if (!mode) {
        // ACP allows it; none of the agents checked in AI0 does it.
        m_modeState = ModeState::Set;
        m_transcript->addNotice(
            i18nc("@info %1 is the name of an AI agent", "%1 offers no modes, so Orcinus cannot choose what it may do. It works as it is set up.", agent.name));
        sendPending();
        return;
    }
    if (!agent.startMode.isEmpty()) {
        if (!mode->choice(agent.startMode)) {
            blockForMode(i18nc("@info %1 is the name of an AI agent, %2 the id of a mode",
                               "%1 does not offer the mode “%2” any more. Choose the mode it should work in.",
                               agent.name,
                               agent.startMode));
            return;
        }
        requestMode(agent.startMode, false);
        return;
    }
    const QString restrictive = WhaleAiAgents::restrictiveMode(*mode);
    if (restrictive.isEmpty()) {
        blockForMode(
            i18nc("@info %1 is the name of an AI agent", "Orcinus does not know which mode of %1 only reads. Choose the mode it should work in.", agent.name));
        return;
    }
    requestMode(restrictive, false);
}

void WhaleAiPanel::requestMode(const QString &value, bool byUser)
{
    const WhaleAcpConfigOption *mode = m_session ? m_session->configOption(QStringLiteral("mode")) : nullptr;
    if (!mode) {
        return;
    }
    m_modeRequest = value;
    m_modeRequestByUser = byUser;
    if (mode->currentValue == value) {
        modeSet(mode->id, value, QString());
        return;
    }
    if (m_modeState != ModeState::Set) {
        m_modeState = ModeState::Applying;
    }
    m_session->setConfigOption(mode->id, value);
}

void WhaleAiPanel::modeSet(const QString &id, const QString &value, const QString &error)
{
    const WhaleAcpConfigOption *mode = m_session ? m_session->configOption(QStringLiteral("mode")) : nullptr;
    if (!mode || mode->id != id || value != m_modeRequest) {
        return;
    }
    m_modeRequest.clear();
    if (!error.isEmpty()) {
        const QString message =
            i18nc("@info %1 is the name of a mode, %2 the agent's reason", "The agent did not switch to the mode “%1”: %2", modeName(value), error);
        if (m_modeState == ModeState::Set) {
            // It keeps working in the mode it has.
            m_transcript->addNotice(message, WhaleAiTranscript::Kind::Error);
        } else {
            blockForMode(message + QLatin1Char(' ') + i18nc("@info", "Choose the mode it should work in."));
        }
        return;
    }
    if (m_modeRequestByUser) {
        WhaleAiAgents::setStartMode(m_sessionAgentId, value);
    }
    m_shownMode = value;
    m_transcript->addNotice(m_modeRequestByUser
                                ? i18nc("@info %1 is the name of a mode of an AI agent", "You switched the agent to the mode “%1”.", modeName(value))
                                : i18nc("@info %1 is the name of a mode of an AI agent", "The agent works in the mode “%1”.", modeName(value)));
    m_modeState = ModeState::Set;
    sendPending();
    updateSessionInfo();
}

void WhaleAiPanel::blockForMode(const QString &message)
{
    m_modeState = ModeState::Blocked;
    QString text = message;
    if (!m_pendingPrompt.isEmpty()) {
        text += QLatin1Char(' ') + i18nc("@info", "Your message is sent once the agent works in the chosen mode.");
    }
    m_transcript->addNotice(text, WhaleAiTranscript::Kind::Error);
    updateState();
    updateSessionInfo();
}

void WhaleAiPanel::noteModeChange()
{
    // The agent may switch on its own (e.g. Claude leaving plan mode after a permission request).
    const WhaleAcpConfigOption *mode = m_session ? m_session->configOption(QStringLiteral("mode")) : nullptr;
    if (!mode || m_modeState != ModeState::Set || !m_modeRequest.isEmpty() || mode->currentValue == m_shownMode) {
        return;
    }
    m_shownMode = mode->currentValue;
    m_transcript->addNotice(i18nc("@info %1 is the name of a mode of an AI agent", "The agent switched to the mode “%1”.", modeName(m_shownMode)));
}

QString WhaleAiPanel::modeName(const QString &value) const
{
    const WhaleAcpConfigOption *mode = m_session ? m_session->configOption(QStringLiteral("mode")) : nullptr;
    const WhaleAcpConfigOption::Choice *choice = mode ? mode->choice(value) : nullptr;
    return choice && !choice->name.isEmpty() ? choice->name : value;
}

void WhaleAiPanel::chooseMode(const QString &value)
{
    if (value.isEmpty()) {
        return;
    }
    if (!m_session || m_session->state() == WhaleAcpSession::State::Starting || m_modeState == ModeState::Unknown) {
        // The next conversation (or this one, once started) begins in it.
        WhaleAiAgents::setStartMode(m_session ? m_sessionAgentId : agentId(), value);
        updateSessionInfo();
        return;
    }
    const WhaleAcpConfigOption *mode = m_session->configOption(QStringLiteral("mode"));
    if (!mode || !mode->choice(value)) {
        return;
    }
    requestMode(value, true);
}

bool WhaleAiPanel::isWaitingForMode() const
{
    return m_session && !m_pendingPrompt.isEmpty() && m_modeState == ModeState::Blocked;
}

void WhaleAiPanel::stop()
{
    if (!m_session) {
        return;
    }
    if (m_session->state() == WhaleAcpSession::State::Prompting) {
        m_session->cancel();
    } else if (m_session->state() == WhaleAcpSession::State::Starting || !m_pendingPrompt.isEmpty()) {
        endSession();
        m_transcript->addNotice(i18nc("@info", "Stopped."));
    }
    updateState();
}

void WhaleAiPanel::endSession()
{
    m_pendingPrompt.clear();
    m_modeState = ModeState::Unknown;
    m_modeRequest.clear();
    if (!m_session) {
        return;
    }
    WhaleAcpSession *session = m_session;
    m_session = nullptr;
    session->disconnect(this);
    session->disconnect(m_transcript);
    session->close();
    // It stops the agent on its own; deleting it at once would kill the agent instead.
    session->setParent(nullptr);
    QTimer::singleShot(10000, session, &QObject::deleteLater);
    updateState();
    updateSessionInfo();
}

void WhaleAiPanel::newConversation()
{
    endSession();
    m_transcript->clear();
    m_input->setFocus();
}

void WhaleAiPanel::updateState()
{
    // A message that waits for the start mode counts as sent, unless it waits for the user.
    const bool busy = m_session
        && (m_session->state() == WhaleAcpSession::State::Starting || m_session->state() == WhaleAcpSession::State::Prompting
            || (!m_pendingPrompt.isEmpty() && m_modeState != ModeState::Blocked));
    if (busy) {
        m_send->setIcon(QIcon::fromTheme(QStringLiteral("media-playback-stop")));
        m_send->setToolTip(i18nc("@info:tooltip", "Stop (Escape)"));
        m_send->setEnabled(true);
    } else {
        m_send->setIcon(QIcon::fromTheme(QStringLiteral("document-send")));
        m_send->setToolTip(i18nc("@info:tooltip", "Send (Enter)"));
        m_send->setEnabled(!m_input->toPlainText().trimmed().isEmpty() && !agentId().isEmpty());
    }
    m_send->setProperty("busy", busy);
}

void WhaleAiPanel::updateSessionInfo()
{
    updateModes();
    if (!m_session) {
        m_sessionInfo->setText(i18nc("@info", "Starts with your first message."));
        return;
    }
    if (m_session->state() == WhaleAcpSession::State::Starting) {
        m_sessionInfo->setText(i18nc("@info", "Starting…"));
        return;
    }
    QStringList parts;
    auto valueName = [](const WhaleAcpConfigOption *option) {
        const WhaleAcpConfigOption::Choice *choice = option->choice(option->currentValue);
        return choice && !choice->name.isEmpty() ? choice->name : option->currentValue;
    };
    if (const WhaleAcpConfigOption *model = m_session->configOption(QStringLiteral("model"))) {
        parts.append(i18nc("@info %1 is the name of a language model", "Model: %1", valueName(model)));
    }
    const int usage = m_sessionInfo->property("usage").toInt();
    if (usage >= 0 && m_sessionInfo->property("usage").isValid()) {
        parts.append(i18nc("@info how much of the context window is used", "Context: %1 %", usage));
    }
    m_sessionInfo->setText(parts.isEmpty() ? m_session->agentInfo().displayName() : parts.join(QStringLiteral(" · ")));
}

void WhaleAiPanel::updateModes()
{
    const QSignalBlocker blocker(m_modes);
    m_modes->clear();
    const QString id = m_session ? m_sessionAgentId : agentId();
    const WhaleAiAgent agent = WhaleAiAgents::agent(id);
    // The modes of the running conversation once its start mode is handled, else the last known.
    const bool live = m_session && m_modeState != ModeState::Unknown;
    const WhaleAcpConfigOption *liveModes = live ? m_session->configOption(QStringLiteral("mode")) : nullptr;
    const WhaleAcpConfigOption modes = liveModes ? *liveModes : WhaleAiAgents::knownModes(id);
    for (const WhaleAcpConfigOption::Choice &choice : modes.choices) {
        m_modes->addItem(choice.name.isEmpty() ? choice.value : choice.name, choice.value);
        if (!choice.description.isEmpty()) {
            m_modes->setItemData(m_modes->count() - 1, choice.description, Qt::ToolTipRole);
        }
    }

    QString current;
    if (live && !liveModes) {
        m_modes->setPlaceholderText(i18nc("@item:inlistbox", "The agent offers no modes"));
    } else if (live && m_modeState == ModeState::Blocked) {
        m_modes->setPlaceholderText(i18nc("@item:inlistbox", "Choose a mode…"));
    } else if (live) {
        current = m_modeState == ModeState::Applying ? m_modeRequest : liveModes->currentValue;
    } else {
        current = agent.startMode.isEmpty() ? WhaleAiAgents::restrictiveMode(modes) : agent.startMode;
        if (current.isEmpty() || m_modes->findData(current) < 0) {
            // Nothing known yet: the rule, or the stored mode the agent may no longer offer.
            m_modes->setPlaceholderText(agent.startMode.isEmpty() ? i18nc("@item:inlistbox", "Read-only mode") : agent.startMode);
        }
    }
    const int index = current.isEmpty() ? -1 : m_modes->findData(current);
    m_modes->setCurrentIndex(index);
    m_modes->setEnabled(m_modes->count() > 0);

    QString tooltip = index >= 0 ? m_modes->itemData(index, Qt::ToolTipRole).toString() : QString();
    if (!tooltip.isEmpty()) {
        tooltip += QStringLiteral("\n\n");
    }
    // AI0: modes are not strict, the UI must not promise more than the agent keeps.
    tooltip += i18nc("@info:tooltip",
                     "What the agent may do without asking. Each agent starts in its most restrictive mode until you choose another one. "
                     "The agent decides when it asks: Orcinus can only answer what it is asked.");
    m_modes->setToolTip(tooltip);
}

void WhaleAiPanel::openLink(const QString &link)
{
    QUrl target(link);
    if (target.scheme().isEmpty()) {
        // A path, absolute or relative to the agent's folder.
        const QString base = m_session ? m_session->property("workingDirectory").toString() : QDir::homePath();
        target = QUrl::fromLocalFile(QDir(base).absoluteFilePath(link));
    }
    if (target.isLocalFile()) {
        Q_EMIT localUrlActivated(target);
        return;
    }
    if (target.scheme() == QLatin1String("http") || target.scheme() == QLatin1String("https")) {
        QDesktopServices::openUrl(target);
    }
}

bool WhaleAiPanel::urlChanged()
{
    // The folder of the next conversation; a running one stays where it started.
    return true;
}

bool WhaleAiPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if ((keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) && !(keyEvent->modifiers() & Qt::ShiftModifier)) {
            if (sendPrompt(m_input->toPlainText())) {
                m_input->clear();
            }
            return true;
        }
        if (keyEvent->key() == Qt::Key_Escape && m_send->property("busy").toBool()) {
            stop();
            return true;
        }
    }
    return Panel::eventFilter(watched, event);
}

void WhaleAiPanel::focusInEvent(QFocusEvent *event)
{
    Panel::focusInEvent(event);
    m_input->setFocus();
}

#include "moc_whaleaipanel.cpp"
