/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaipanel.h"
#include "whaleaiagents.h"
#include "whaleaicontextchips.h"
#include "whaleaidebug.h"
#include "whaleaimcpserver.h"
#include "whaleaisandbox.h"
#include "whaleaitranscript.h"

#include <KLocalizedString>

#include <QApplication>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDropEvent>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
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
    // What the agent may reach (AI5d): enforced by the sandbox, not by the agent.
    m_access = new QComboBox(chat);
    m_access->setObjectName(QStringLiteral("access"));
    m_access->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_access->setMinimumContentsLength(8);
    m_access->addItem(QIcon::fromTheme(QStringLiteral("folder")), i18nc("@item:inlistbox what an AI agent may reach", "This folder and subfolders"), false);
    m_access->setItemData(0,
                          i18nc("@info:tooltip",
                                "The agent sees only the folder it works in and its subfolders, besides its own settings and the system "
                                "(read-only). Orcinus' sandbox enforces this, not the agent."),
                          Qt::ToolTipRole);
    m_access->addItem(QIcon::fromTheme(QStringLiteral("drive-harddisk")), i18nc("@item:inlistbox what an AI agent may reach", "All folders"), true);
    m_access->setItemData(1, i18nc("@info:tooltip", "The agent can reach all your files, as far as its mode lets it."), Qt::ToolTipRole);
    auto *accessLabel = new QLabel(i18nc("@label:listbox which folders an AI agent may reach", "Access:"), chat);
    accessLabel->setBuddy(m_access);
    auto *modesRow = new QGridLayout;
    modesRow->addWidget(modesLabel, 0, 0);
    modesRow->addWidget(m_modes, 0, 1);
    modesRow->addWidget(accessLabel, 1, 0);
    modesRow->addWidget(m_access, 1, 1);
    modesRow->setColumnStretch(1, 1);

    // What the agent in its sandbox reaches (AI5d3), with links to take added ones back.
    m_shared = new QLabel(chat);
    m_shared->setObjectName(QStringLiteral("shared"));
    m_shared->setWordWrap(true);
    m_shared->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_shared->setTextFormat(Qt::RichText);
    m_shared->hide();
    modesRow->addWidget(m_shared, 2, 0, 1, 2);

    m_transcript = new WhaleAiTranscript(chat);
    m_transcript->setObjectName(QStringLiteral("transcript"));

    m_chips = new WhaleAiContextChips(chat);
    m_chips->setObjectName(QStringLiteral("contextChips"));

    m_input = new QPlainTextEdit(chat);
    m_input->setObjectName(QStringLiteral("input"));
    m_input->setPlaceholderText(i18nc("@info:placeholder", "Ask the agent… (Enter sends, Shift+Enter: new line)"));
    m_input->setTabChangesFocus(true);
    m_input->setFixedHeight(m_input->fontMetrics().lineSpacing() * 3 + 2 * m_input->frameWidth() + 8);
    m_input->installEventFilter(this);
    // Drops land on the viewport.
    m_input->viewport()->installEventFilter(this);

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
    chatLayout->addWidget(m_chips);
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
    connect(m_shared, &QLabel::linkActivated, this, [this](const QString &link) {
        if (link.startsWith(QLatin1String("remove:")) && m_control) {
            m_control->remove(QUrl::fromPercentEncoding(link.mid(7).toUtf8()));
        }
    });
    connect(m_chips, &WhaleAiContextChips::accessRequested, this, &WhaleAiPanel::giveAccess);
    connect(m_access, &QComboBox::activated, this, [this](int index) {
        chooseAccess(m_access->itemData(index).toBool());
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

WhaleAiContextChips *WhaleAiPanel::contextChips() const
{
    return m_chips;
}

void WhaleAiPanel::setContextProvider(const std::function<WhaleAiContext()> &provider)
{
    m_contextProvider = provider;
    refreshContext();
}

void WhaleAiPanel::setViewActions(const WhaleAiViewActions &actions)
{
    m_viewActions = actions;
    if (m_mcp) {
        m_mcp->setViewActions(actions);
    }
}

void WhaleAiPanel::refreshContext()
{
    m_chips->setContext(currentContext());
}

WhaleAiContext WhaleAiPanel::currentContext() const
{
    WhaleAiContext context;
    if (m_contextProvider) {
        context = m_contextProvider();
    } else if (url().isLocalFile() && QFileInfo(url().toLocalFile()).isDir()) {
        context.folder = url().toLocalFile();
    }
    // Folders of views may end with a slash.
    if (!context.folder.isEmpty()) {
        context.folder = QDir::cleanPath(context.folder);
    }
    if (!context.otherFolder.isEmpty()) {
        context.otherFolder = QDir::cleanPath(context.otherFolder);
    }
    return context;
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
    // Current, also if the other view changed without a signal; what goes is shown.
    refreshContext();
    m_pendingContext = m_chips->context();
    if (m_control) {
        m_pendingContext.reachable = m_sessionFolders + m_sessionAdded;
    }
    m_transcript->addUserMessage(prompt, WhaleAiContextPrompt::summary(m_pendingContext));
    m_chips->messageSent();
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
    QStringList shared;
    if (url().isLocalFile() && QFileInfo(url().toLocalFile()).isDir()) {
        directory = QDir::cleanPath(url().toLocalFile());
        shared.append(directory);
    }

    // The back channel (AI6); the agent works without it if it cannot be set up.
    WhaleAiMcpServer *mcp = nullptr;
    const QString relay = WhaleAiMcpServer::relay();
    if (!relay.isEmpty()) {
        mcp = new WhaleAiMcpServer(this);
        if (!mcp->listen()) {
            qCWarning(WhaleAiDebug) << "No back channel for the agent:" << mcp->errorString();
            delete mcp;
            mcp = nullptr;
        }
    }

    WhaleAcpSession::AgentCommand run = command;
    QStringList reach;
    WhaleAiSandboxControl *control = nullptr;
    if (!agent.allFolders) {
        const QString helper = WhaleAiSandbox::helper();
        const QString problem = WhaleAiSandbox::check(helper);
        if (!problem.isEmpty()) {
            QString details = problem;
            // The build tree has a profile for its own helper (src/whale/ai/sandbox/CMakeLists.txt).
            const QString profile = QFileInfo(helper).absolutePath() + QStringLiteral("/../orcinus-sandbox.apparmor");
            if (!helper.isEmpty() && QFileInfo::exists(profile)) {
                details += QStringLiteral("\n\n")
                    + i18nc("@info %1 is a command",
                            "For this build, load its AppArmor profile once: %1",
                            QStringLiteral("sudo apparmor_parser -r ") + QDir::cleanPath(profile));
            }
            m_transcript->addNotice(i18nc("@info %1 is the name of an AI agent",
                                          "%1 was not started: it may only reach this folder, and the sandbox for that does not work here. "
                                          "Choose “All folders” under Access to start it without the sandbox.",
                                          agent.name),
                                    WhaleAiTranscript::Kind::Error,
                                    details);
            delete mcp;
            return false;
        }
        WhaleAiSandbox::Paths paths = WhaleAiSandbox::agentPaths(agent, command.program);
        if (mcp) {
            // Only the socket of this conversation, not the rest of /run/user.
            paths.readWrite.append(mcp->socketPath());
            if (!WhaleAiSandbox::reaches(relay, {}, false)) {
                // The build tree's relay; the installed one is in the system anyway.
                paths.readOnly.append(relay);
            }
        }
        for (const QString &path : paths.create) {
            QDir().mkpath(path);
        }
        control = new WhaleAiSandboxControl(this);
        run = WhaleAiSandbox::wrap(command, helper, shared, paths, directory, control->arguments());
        run.childSetup = control->childSetup();
        reach = shared + paths.readWrite + paths.readOnly;
    }

    m_sessionAgentId = agent.id;
    m_sessionAllFolders = agent.allFolders;
    m_sessionReach = reach;
    m_sessionFolders = shared;
    m_sessionAdded.clear();
    m_modeState = ModeState::Unknown;
    m_modeRequest.clear();
    m_shownMode.clear();
    m_session = new WhaleAcpSession(this);
    m_session->setProperty("workingDirectory", directory);
    m_control = control;
    m_mcp = mcp;
    QList<WhaleAcpMcpServer> mcpServers;
    if (mcp) {
        // Goes with the session, like the sandbox control.
        mcp->setParent(m_session);
        mcp->setContextProvider([this] {
            return currentContext();
        });
        mcp->setViewActions(m_viewActions);
        connect(mcp, &WhaleAiMcpServer::locationSet, this, [this](const QString &folder) {
            m_transcript->addNotice(i18nc("@info %1 is a folder", "The agent opened %1.", folder));
        });
        connect(mcp, &WhaleAiMcpServer::itemsSelected, this, [this](int count) {
            m_transcript->addNotice(i18ncp("@info", "The agent selected %1 item.", "The agent selected %1 items.", count));
        });
        if (control) {
            mcp->setAccess([this](const QString &path) {
                return WhaleAiSandbox::reaches(path, m_sessionReach, false);
            });
        }
        mcpServers.append(mcp->mcpServer(relay));
    }
    if (control) {
        // Goes with the session.
        control->setParent(m_session);
        connect(control, &WhaleAiSandboxControl::finished, this, &WhaleAiPanel::sandboxChanged);
    }
    if (agent.allFolders) {
        m_transcript->addNotice(
            i18nc("@info %1 is the name of an AI agent, %2 a folder", "%1 works in %2 and can reach all your folders.", agent.name, directory));
    } else if (shared.isEmpty()) {
        m_transcript->addNotice(
            i18nc("@info %1 is the name of an AI agent", "%1 works in an empty folder of its sandbox: this location is not a local folder.", agent.name));
    } else {
        m_transcript->addNotice(i18nc("@info %1 is the name of an AI agent, %2 a folder",
                                      "%1 works in %2 and can reach only this folder and its subfolders.",
                                      agent.name,
                                      directory));
    }

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
    connect(session, &WhaleAcpSession::permissionRequested, this, [this](const WhaleAcpPermissionRequest &request) {
        if (answerOwnToolRequest(request) || answerOutsideRequest(request)) {
            return;
        }
        m_transcript->addPermissionRequest(request);
        if (!isVisible()) {
            Q_EMIT attentionRequested();
        } else if (isAncestorOf(QApplication::focusWidget())) {
            // The user is in the panel: answering with the keyboard is one key away.
            m_transcript->focusPermissionRequest(request.key);
        }
        if (!window()->isActiveWindow()) {
            QApplication::alert(window());
        }
    });
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

    m_session->start(run, directory, mcpServers);
    if (control) {
        control->childStarted();
        m_chips->setAccess([this](const QString &path) {
            return WhaleAiSandbox::reaches(path, m_sessionReach, false);
        });
    }
    updateShared();
    m_sessionInfo->setProperty("usage", -1);
    return m_session != nullptr;
}

void WhaleAiPanel::sendPending()
{
    if (!m_session || m_session->state() != WhaleAcpSession::State::Ready || m_modeState != ModeState::Set || m_pendingPrompt.isEmpty()) {
        return;
    }
    m_session->prompt(WhaleAiContextPrompt::blocks(std::exchange(m_pendingContext, {}), std::exchange(m_pendingPrompt, QString())));
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
    m_pendingContext = {};
    m_modeState = ModeState::Unknown;
    m_modeRequest.clear();
    if (m_control) {
        m_control->disconnect(this);
    }
    m_control = nullptr;
    if (m_mcp) {
        // It goes with the session; nothing it does after the end belongs here.
        m_mcp->disconnect(this);
        m_mcp->setViewActions({});
    }
    m_mcp = nullptr;
    m_sessionReach.clear();
    m_sessionFolders.clear();
    m_sessionAdded.clear();
    m_chips->setAccess({});
    updateShared();
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
    updateAccess();
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

void WhaleAiPanel::updateAccess()
{
    const QSignalBlocker blocker(m_access);
    const WhaleAiAgent agent = WhaleAiAgents::agent(m_session ? m_sessionAgentId : agentId());
    m_access->setCurrentIndex(m_access->findData(agent.allFolders));
    m_access->setEnabled(!agent.id.isEmpty());
    m_access->setToolTip(m_access->currentData(Qt::ToolTipRole).toString());
}

void WhaleAiPanel::chooseAccess(bool allFolders)
{
    const QString id = m_session ? m_sessionAgentId : agentId();
    if (id.isEmpty()) {
        return;
    }
    WhaleAiAgents::setAllFolders(id, allFolders);
    if (m_session && allFolders != m_sessionAllFolders) {
        // The sandbox is set up when the agent starts.
        m_transcript->addNotice(i18nc("@info", "The new access applies from the next conversation (New Conversation)."));
    }
    updateAccess();
}

bool WhaleAiPanel::answerOwnToolRequest(const WhaleAcpPermissionRequest &request)
{
    // Our tools only show something in the window; they check what the agent may reach
    // themselves. No click for each of them (AI5), and only once: "always" stays the user's.
    if (!m_mcp || WhaleAiMcpServer::ownTool(request.toolCall).isEmpty()) {
        return false;
    }
    for (const WhaleAcpPermissionRequest::Option &option : request.options) {
        if (option.kind == QLatin1String("allow_once")) {
            m_session->answerPermission(request.key, option.id);
            return true;
        }
    }
    return false;
}

bool WhaleAiPanel::answerOutsideRequest(const WhaleAcpPermissionRequest &request)
{
    // A second line behind the sandbox: a request for paths the agent cannot reach is answered
    // here. Paths in commands are not known, those requests are shown.
    if (m_sessionAllFolders || request.toolCall.locations.isEmpty()) {
        return false;
    }
    for (const QString &path : request.toolCall.locations) {
        if (WhaleAiSandbox::reaches(path, m_sessionReach)) {
            return false;
        }
    }
    const WhaleAcpPermissionRequest::Option *reject = nullptr;
    for (const WhaleAcpPermissionRequest::Option &option : request.options) {
        if (option.kind == QLatin1String("reject_once") || (!reject && option.kind.startsWith(QLatin1String("reject")))) {
            reject = &option;
        }
    }
    if (!reject) {
        return false;
    }
    m_transcript->addNotice(i18nc("@info %1 is what an AI agent asked for, %2 a list of paths",
                                  "Orcinus said no to “%1”: it is outside the folders the agent may reach (%2).",
                                  request.toolCall.title,
                                  request.toolCall.locations.join(QStringLiteral(", "))),
                            WhaleAiTranscript::Kind::Error);
    m_session->answerPermission(request.key, reject->id);
    return true;
}

void WhaleAiPanel::giveAccess(const QString &path)
{
    const QString clean = QDir::cleanPath(path);
    if (!m_control || !QDir::isAbsolutePath(clean) || WhaleAiSandbox::reaches(clean, m_sessionReach, false)) {
        return;
    }
    m_control->add(clean);
}

void WhaleAiPanel::sandboxChanged(bool added, const QString &path, const QString &error)
{
    if (!error.isEmpty()) {
        m_transcript->addNotice(added ? i18nc("@info %1 is a path, %2 the reason", "%1 could not be shared with the agent: %2", path, error)
                                      : i18nc("@info %1 is a path, %2 the reason", "%1 could not be taken from the agent: %2", path, error),
                                WhaleAiTranscript::Kind::Error);
        return;
    }
    if (added) {
        m_sessionAdded.append(path);
        m_sessionReach.append(path);
        m_transcript->addNotice(i18nc("@info %1 is a path", "The agent may now also reach %1, reading and writing.", path));
    } else {
        m_sessionAdded.removeAll(path);
        m_sessionReach.removeAll(path);
        m_transcript->addNotice(i18nc("@info %1 is a path", "The agent can no longer reach %1.", path));
    }
    updateShared();
    // The chips say again what it reaches.
    m_chips->setAccess([this](const QString &path) {
        return WhaleAiSandbox::reaches(path, m_sessionReach, false);
    });
}

void WhaleAiPanel::updateShared()
{
    if (!m_control) {
        m_shared->hide();
        m_shared->clear();
        return;
    }
    auto name = [](const QString &path) {
        const QString fileName = QFileInfo(path).fileName();
        return (fileName.isEmpty() ? path : fileName).toHtmlEscaped();
    };
    QStringList parts;
    for (const QString &path : std::as_const(m_sessionFolders)) {
        parts.append(QStringLiteral("<b>%1</b>").arg(name(path)));
    }
    for (const QString &path : std::as_const(m_sessionAdded)) {
        parts.append(QStringLiteral("%1 <a href=\"remove:%2\">%3</a>")
                         .arg(name(path), QString::fromLatin1(QUrl::toPercentEncoding(path)), i18nc("@action:button take access back", "(remove)")));
    }
    m_shared->setText(parts.isEmpty() ? i18nc("@info", "The agent reaches none of your folders.")
                                      : i18nc("@info %1 is a list of folders", "The agent reaches: %1", parts.join(QStringLiteral(", "))));
    m_shared->setToolTip((m_sessionFolders + m_sessionAdded).join(QLatin1Char('\n')));
    m_shared->show();
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
    // The folder of the next conversation; a running one stays where it started. The next
    // message names the new folder.
    refreshContext();
    return true;
}

bool WhaleAiPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_input->viewport() && (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove || event->type() == QEvent::Drop)) {
        // Local files and folders become chips, not text in the input.
        auto *drop = static_cast<QDropEvent *>(event);
        QStringList paths;
        if (drop->mimeData()->hasUrls()) {
            const QList<QUrl> urls = drop->mimeData()->urls();
            for (const QUrl &url : urls) {
                if (!url.isLocalFile()) {
                    paths.clear();
                    break;
                }
                paths.append(url.toLocalFile());
            }
        }
        if (paths.isEmpty()) {
            return Panel::eventFilter(watched, event);
        }
        drop->setDropAction(Qt::CopyAction);
        drop->accept();
        if (event->type() == QEvent::Drop) {
            m_chips->addFiles(paths);
            m_input->setFocus();
        }
        return true;
    }
    if (watched == m_input && event->type() == QEvent::FocusIn) {
        refreshContext();
    }
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

void WhaleAiPanel::showEvent(QShowEvent *event)
{
    Panel::showEvent(event);
    // The window does not update a hidden panel.
    refreshContext();
}

void WhaleAiPanel::focusInEvent(QFocusEvent *event)
{
    Panel::focusInEvent(event);
    m_input->setFocus();
}

#include "moc_whaleaipanel.cpp"
