/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaipanel.h"
#include "whaleaiagentdialog.h"
#include "whaleaiagents.h"
#include "whaleaichangelog.h"
#include "whaleaicontextchips.h"
#include "whaleaidebug.h"
#include "whaleaidisclaimer.h"
#include "whaleailocalmodels.h"
#include "whaleailocalruntime.h"
#include "whaleaimcpserver.h"
#include "whaleaitranscript.h"

#include <KLocalizedString>
#include <KMessageWidget>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDropEvent>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopeGuard>
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
    DisclaimerPage,
};
}

WhaleAiPanel::WhaleAiPanel(QWidget *parent)
    : Panel(parent)
{
    // Local models a crashed Orcinus left loaded (AI12): freed once per process, when idle.
    static bool cleanedUp = false;
    if (!cleanedUp) {
        cleanedUp = true;
        QTimer::singleShot(0, WhaleAiLocalRuntime::instance(), &WhaleAiLocalRuntime::cleanUpAfterCrashes);
    }
    m_hiddenTimer = new QTimer(this);
    m_hiddenTimer->setSingleShot(true);
    connect(m_hiddenTimer, &QTimer::timeout, this, &WhaleAiPanel::freeHiddenLocalModel);

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

    m_undoChanges = new QToolButton(chat);
    m_undoChanges->setObjectName(QStringLiteral("undoChanges"));
    m_undoChanges->setIcon(QIcon::fromTheme(QStringLiteral("edit-undo")));
    m_undoChanges->setToolTip(i18nc("@info:tooltip", "Undo the Agent's Changes (settings, folder views, profiles)"));
    m_undoChanges->setAutoRaise(true);
    m_undoChanges->setEnabled(false);

    auto *configure = new QToolButton(chat);
    configure->setObjectName(QStringLiteral("configure"));
    configure->setIcon(QIcon::fromTheme(QStringLiteral("configure")));
    configure->setToolTip(i18nc("@info:tooltip", "Configure AI Agents…"));
    configure->setAutoRaise(true);

    auto *top = new QHBoxLayout;
    top->addWidget(m_agents, 1);
    top->addWidget(m_newConversation);
    top->addWidget(m_undoChanges);
    top->addWidget(configure);

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
    auto *modesRow = new QGridLayout;
    modesRow->addWidget(modesLabel, 0, 0);
    modesRow->addWidget(m_modes, 0, 1);
    modesRow->setColumnStretch(1, 1);
    // A filter of the transcript: what the agent did (commands, tool calls) or only the conversation.
    // Permission requests stay: they are how the user controls the agent.
    m_showActions = new QCheckBox(i18nc("@option:check show what an AI agent did in the transcript", "Actions"), chat);
    m_showActions->setObjectName(QStringLiteral("showActions"));
    m_showActions->setToolTip(
        i18nc("@info:tooltip", "Show what the agent does: the commands it runs and the tools it uses. Permission requests are always shown."));
    m_showActions->setChecked(WhaleAiAgents::showActions());
    modesRow->addWidget(m_showActions, 0, 2);

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
    // A new agent the agent suggests (AI11); the user decides.
    m_proposalBar = new KMessageWidget(chat);
    m_proposalBar->setObjectName(QStringLiteral("agentProposal"));
    m_proposalBar->setMessageType(KMessageWidget::Information);
    m_proposalBar->setWordWrap(true);
    m_proposalBar->setCloseButtonVisible(false);
    m_proposalBar->hide();
    auto *review = new QAction(QIcon::fromTheme(QStringLiteral("list-add")), i18nc("@action:button", "Add…"), m_proposalBar);
    review->setObjectName(QStringLiteral("proposalAdd"));
    connect(review, &QAction::triggered, this, &WhaleAiPanel::reviewProposal);
    auto *discard = new QAction(QIcon::fromTheme(QStringLiteral("edit-delete-remove")), i18nc("@action:button", "Discard"), m_proposalBar);
    discard->setObjectName(QStringLiteral("proposalDiscard"));
    connect(discard, &QAction::triggered, this, [this] {
        m_proposal.reset();
        m_proposalBar->animatedHide();
    });
    m_proposalBar->addAction(review);
    m_proposalBar->addAction(discard);
    chatLayout->addWidget(m_proposalBar);
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
    // Over everything until the user accepts it, here or in the settings (AI5f).
    m_stack->insertWidget(DisclaimerPage, new WhaleAiDisclaimer(this));
    auto *layout = new QVBoxLayout(this);
    // Some room to the dock's edges, for the chat and the setup page alike.
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(m_stack);

    connect(setupButton, &QPushButton::clicked, this, &WhaleAiPanel::configureRequested);
    connect(WhaleAiDisclaimerNotifier::instance(), &WhaleAiDisclaimerNotifier::accepted, this, [this] {
        const bool here = m_stack->currentIndex() == DisclaimerPage && isAncestorOf(QApplication::focusWidget());
        updatePage();
        if (here) {
            m_input->setFocus();
        }
    });
    connect(configure, &QToolButton::clicked, this, &WhaleAiPanel::configureRequested);
    connect(m_agents, &QComboBox::activated, this, &WhaleAiPanel::agentChosen);
    connect(m_modes, &QComboBox::activated, this, [this](int index) {
        chooseMode(m_modes->itemData(index).toString());
    });
    connect(m_newConversation, &QToolButton::clicked, this, &WhaleAiPanel::newConversation);
    connect(m_undoChanges, &QToolButton::clicked, this, &WhaleAiPanel::undoAgentChanges);
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
    m_transcript->setFocusReturn(m_input);
    m_transcript->setShowActions(m_showActions->isChecked());
    connect(m_showActions, &QCheckBox::toggled, this, [this](bool on) {
        m_transcript->setShowActions(on);
        WhaleAiAgents::setShowActions(on);
    });
    fillAgents(WhaleAiAgents::defaultAgentId());
    greet();
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
    updatePage();
}

void WhaleAiPanel::updatePage()
{
    if (!WhaleAiAgents::disclaimerAccepted()) {
        m_stack->setCurrentIndex(DisclaimerPage);
    } else {
        m_stack->setCurrentIndex(m_agents->count() == 0 ? SetupPage : ChatPage);
    }
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
    m_viewActions.windowTools += agentTools();
    if (m_mcp) {
        m_mcp->setViewActions(keepingInputFocus(m_viewActions));
    }
}

QList<WhaleAiWindowTool> WhaleAiPanel::agentTools()
{
    QPointer<WhaleAiPanel> guard(this);
    auto property = [](const QString &type, const QString &description) {
        return QJsonObject{{QStringLiteral("type"), type}, {QStringLiteral("description"), description}};
    };
    return {
        {QStringLiteral("list_agents"),
         QStringLiteral("List the AI agents set up in Orcinus"),
         QStringLiteral("Lists the AI agents of Orcinus' AI panel (Settings > AI Agents): id, name, command line, start mode, which one is "
                        "the default and which one this conversation uses. Environment variables are listed by name only, and arguments "
                        "that look like secrets as \"…\": API keys stay with the user."),
         true,
         {},
         {},
         [guard](const QJsonObject &, bool *error, QString *) -> QJsonObject {
             if (!guard) {
                 *error = true;
                 return {{QStringLiteral("error"), QStringLiteral("The AI panel is gone.")}};
             }
             QJsonArray list;
             const QString current = guard->agentId();
             const QString standard = WhaleAiAgents::defaultAgentId();
             for (const WhaleAiAgent &agent : WhaleAiAgents::agents()) {
                 QJsonObject entry = WhaleAiAgents::describeForAgent(agent);
                 entry.insert(QStringLiteral("default"), agent.id == standard);
                 entry.insert(QStringLiteral("current"), agent.id == current);
                 list.append(entry);
             }
             return {{QStringLiteral("agents"), list}};
         }},
        {QStringLiteral("propose_agent"),
         QStringLiteral("Propose a new AI agent for Orcinus"),
         QStringLiteral("Proposes a new agent for Orcinus' AI panel: an ACP agent program (e.g. \"opencode acp\", \"codex-acp\", "
                        "\"claude-code-acp\") with its arguments and environment. Nothing is added by this: the user sees the proposal "
                        "in the AI panel and adds it with Add… (where they can edit it) or discards it. You cannot add, change or remove "
                        "agents yourself. Never put API keys or other secrets into it: list the variable by name (\"OPENAI_API_KEY\") "
                        "and remind the user to enter the value in the dialog. For a local model give only \"local_model\" with a model "
                        "id from list_local_models: Orcinus then sets up OpenCode for it and starts LM Studio and the model whenever the "
                        "agent is used, and frees them afterwards; do not write OpenCode configuration or start scripts yourself. "
                        "Allowed in every mode."),
         false,
         {{QStringLiteral("local_model"),
           QJsonObject{
               {QStringLiteral("type"), QStringLiteral("object")},
               {QStringLiteral("description"), QStringLiteral("Optional: a local model Orcinus starts for the agent (AI12)")},
               {QStringLiteral("properties"),
                QJsonObject{{QStringLiteral("model"), property(QStringLiteral("string"), QStringLiteral("Model id from list_local_models"))},
                            {QStringLiteral("contextLength"),
                             property(QStringLiteral("integer"), QStringLiteral("Optional: tokens to load it with (default 65536 or less)"))},
                            {QStringLiteral("provider"), property(QStringLiteral("string"), QStringLiteral("Optional: \"lmstudio\" (the only one)"))}}},
               {QStringLiteral("required"), QJsonArray{QStringLiteral("model")}},
               {WhaleAiMcpServer::propertyOrderKey(), QJsonArray{QStringLiteral("model"), QStringLiteral("contextLength"), QStringLiteral("provider")}}}},
          {QStringLiteral("name"),
           property(QStringLiteral("string"), QStringLiteral("Name shown in the agent list, e.g. \"OpenCode (Qwen local)\"; optional with local_model"))},
          {QStringLiteral("command"),
           property(QStringLiteral("string"), QStringLiteral("Program and arguments, quoted as in a shell; optional with local_model"))},
          {QStringLiteral("environment"),
           QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                       {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                       {QStringLiteral("description"), QStringLiteral("Optional: NAME=value, or NAME alone for a secret the user enters")}}}},
         {},
         [guard](const QJsonObject &arguments, bool *error, QString *notice) -> QJsonObject {
             if (!guard) {
                 *error = true;
                 return {{QStringLiteral("error"), QStringLiteral("The AI panel is gone.")}};
             }
             QString problem;
             const WhaleAiLocalSetup setup = arguments.contains(QLatin1String("local_model")) ? WhaleAiLocalModels::discover() : WhaleAiLocalSetup();
             const std::optional<WhaleAiAgent> agent = WhaleAiAgents::proposal(arguments, WhaleAiAgents::agents(), &problem, setup);
             if (!agent) {
                 *error = true;
                 return {{QStringLiteral("error"), problem}};
             }
             guard->showProposal(*agent);
             *notice = i18nc("@info %1 is an agent's name", "The agent proposed a new agent: “%1”.", agent->name);
             QString note = QStringLiteral(
                 "Nothing is added yet: the user adds it with Add… in the AI panel, or discards it. Tell them so, and "
                 "remind them to enter API keys or other secrets (the environment variables without a value) in "
                 "the dialog themselves, and that the agent may need its own login or configuration.");
             if (agent->usesLocalModel()) {
                 note = QStringLiteral(
                     "Nothing is added yet: the user adds it with Add… in the AI panel, or discards it. Tell them so. Once added, "
                     "Orcinus starts LM Studio and loads the model when they talk to this agent, and frees it again when it is no "
                     "longer used; nothing has to be started by hand.");
             }
             return {{QStringLiteral("proposed"), WhaleAiAgents::describeForAgent(*agent)}, {QStringLiteral("note"), note}};
         },
         {QStringLiteral("local_model"), QStringLiteral("name"), QStringLiteral("command"), QStringLiteral("environment")}},
        {QStringLiteral("list_local_models"),
         QStringLiteral("List the local AI models on this computer"),
         QStringLiteral("Lists what there is for a local AI (AI12; Orcinus knows LM Studio): the inference servers with their address, "
                        "whether they run, and their language models (id, size, context length, tool use, vision, loaded), and the "
                        "clients that can run them as an agent (OpenCode, with the ACP command and whether that command is stable). Read "
                        "it before you propose an agent for a local model. Reads only, starts nothing."),
         true,
         {},
         {},
         [](const QJsonObject &, bool *, QString *) -> QJsonObject {
             return WhaleAiLocalModels::describeForAgent(WhaleAiLocalModels::discover());
         }},
    };
}

void WhaleAiPanel::showProposal(const WhaleAiAgent &agent)
{
    m_proposal = agent;
    QStringList variables;
    for (const QString &entry : agent.environment) {
        variables.append(entry.section(QLatin1Char('='), 0, 0));
    }
    QString text = i18nc("@info %1 is an agent's name, %2 a command line",
                         "The agent suggests a new agent: <b>%1</b><br/>%2",
                         agent.name.toHtmlEscaped(),
                         WhaleAiAgents::maskSecrets(agent.commandLine).toHtmlEscaped());
    if (!variables.isEmpty()) {
        text += QStringLiteral("<br/>") + i18nc("@info %1 is a list of names", "Environment: %1", variables.join(QStringLiteral(", ")).toHtmlEscaped());
    }
    if (agent.usesLocalModel()) {
        text += QStringLiteral("<br/>")
            + i18nc("@info %1 is the id of a language model", "Local model: %1 (Orcinus starts LM Studio for it)", agent.localModel.toHtmlEscaped());
    }
    m_proposalBar->setText(text);
    m_proposalBar->animatedShow();
}

std::optional<WhaleAiAgent> WhaleAiPanel::proposedAgent() const
{
    return m_proposal;
}

void WhaleAiPanel::reviewProposal()
{
    if (!m_proposal) {
        return;
    }
    WhaleAiAgentDialog dialog(*m_proposal, this);
    if (dialog.exec() == QDialog::Accepted) {
        addProposedAgent(dialog.agent());
    }
}

void WhaleAiPanel::addProposedAgent(const WhaleAiAgent &agent)
{
    QList<WhaleAiAgent> agents = WhaleAiAgents::agents();
    WhaleAiAgent added = agent;
    added.id = WhaleAiAgents::uniqueId(agent.id.isEmpty() ? agent.name : agent.id, agents);
    agents.append(added);
    WhaleAiAgents::setAgents(agents);
    m_proposal.reset();
    m_proposalBar->animatedHide();
    // The list changes; the conversation stays with its agent.
    fillAgents(agentId());
    m_transcript->addNotice(i18nc("@info %1 is an agent's name", "Added the agent “%1”. Choose it in the agent list to talk to it.", added.name));
}

void WhaleAiPanel::addActionNotice(const QString &text, const QString &toolTip)
{
    m_transcript->addNotice(text, WhaleAiTranscript::Kind::Action, QString(), toolTip);
}

int WhaleAiPanel::activePane() const
{
    return m_viewActions.activePane ? m_viewActions.activePane() : -1;
}

WhaleAiViewActions WhaleAiPanel::keepingInputFocus(const WhaleAiViewActions &actions)
{
    // What the agent does in the window (open a folder, switch the view, choose a split view)
    // moves the focus there, as it would for the user. The user is typing here, though: if the
    // input had the focus, it gets it back, also after what the window does later on its own.
    auto wrap = [this](auto function) {
        return [this, function](auto &&...arguments) {
            // Also while the user is in another window: then it has no focus, but is the focus widget.
            const bool hadFocus = m_input->hasFocus() || m_input->window()->focusWidget() == m_input;
            auto restore = qScopeGuard([this, hadFocus] {
                if (hadFocus) {
                    m_input->setFocus(Qt::OtherFocusReason);
                    QTimer::singleShot(0, m_input, [input = m_input] {
                        input->setFocus(Qt::OtherFocusReason);
                    });
                }
            });
            return function(std::forward<decltype(arguments)>(arguments)...);
        };
    };
    WhaleAiViewActions result = actions;
    if (actions.openFolder) {
        result.openFolder = wrap(actions.openFolder);
    }
    if (actions.select) {
        result.select = wrap(actions.select);
    }
    if (actions.trigger) {
        result.trigger = wrap(actions.trigger);
    }
    if (actions.activatePane) {
        result.activatePane = wrap(actions.activatePane);
    }
    for (WhaleAiWindowTool &tool : result.windowTools) {
        if (tool.call) {
            tool.call = wrap(tool.call);
        }
        const bool savesSettings = tool.name == QLatin1String("set_setting") || tool.name == QLatin1String("save_profile");
        // Canvas tools (CV3): writing their files, or running their code, changes things too.
        const bool changesTools = tool.name == QLatin1String("tool_write_file") || tool.name == QLatin1String("tool_delete_file")
            || tool.name == QLatin1String("tool_open") || tool.name == QLatin1String("tool_invoke");
        if ((savesSettings || changesTools) && tool.call) {
            // Saved settings stay untouched in the read-only mode (user, 2026-11-16, until undo
            // exists, AI9e); views and windows may still change there.
            tool.call = [this, call = tool.call, savesSettings](const QJsonObject &arguments, bool *error, QString *notice) -> QJsonObject {
                const WhaleAcpConfigOption *mode = m_session ? m_session->configOption(QStringLiteral("mode")) : nullptr;
                if (mode && !mode->currentValue.isEmpty() && mode->currentValue == WhaleAiAgents::restrictiveMode(*mode)) {
                    // The names the user sees in the Mode list, so the agent does not guess them.
                    QStringList others;
                    for (const WhaleAcpConfigOption::Choice &choice : mode->choices) {
                        if (choice.value != mode->currentValue) {
                            others.append(QStringLiteral("\"%1\"").arg(choice.name.isEmpty() ? choice.value : choice.name));
                        }
                    }
                    *error = true;
                    return {{QStringLiteral("error"),
                             QStringLiteral("%3 in the read-only mode \"%1\". Tell the user to choose another "
                                            "mode in the Mode list of the AI panel (%2), then try again.")
                                 .arg(modeName(mode->currentValue),
                                      others.join(QStringLiteral(", ")),
                                      savesSettings ? QStringLiteral("Settings and profiles cannot be saved")
                                                    : QStringLiteral("Canvas tools cannot be written, opened or used"))}};
                }
                return call(arguments, error, notice);
            };
        }
    }
    return result;
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
        WhaleAiChangeLog::of(window())->clear();
        m_transcript->addNotice(
            i18nc("@info %1 is the name of an AI agent", "Now talking to %1. The next message starts a new conversation.", m_agents->currentText()));
    }
    updateState();
    updateSessionInfo();
}

bool WhaleAiPanel::sendPrompt(const QString &text)
{
    const QString prompt = text.trimmed();
    if (prompt.isEmpty() || agentId().isEmpty() || !WhaleAiAgents::disclaimerAccepted()) {
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
    if (url().isLocalFile() && QFileInfo(url().toLocalFile()).isDir()) {
        directory = QDir::cleanPath(url().toLocalFile());
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

    m_sessionAgentId = agent.id;
    m_systemPromptSent = false;
    m_modeState = ModeState::Unknown;
    m_modeRequest.clear();
    m_shownMode.clear();
    m_session = new WhaleAcpSession(this);
    m_session->setProperty("workingDirectory", directory);
    // The panel is in its window by now (not yet when it is created).
    connect(WhaleAiChangeLog::of(window()), &WhaleAiChangeLog::changed, this, &WhaleAiPanel::updateState, Qt::UniqueConnection);
    m_mcp = mcp;
    QList<WhaleAcpMcpServer> mcpServers;
    if (mcp) {
        // Goes with the session.
        mcp->setParent(m_session);
        mcp->setContextProvider([this] {
            return currentContext();
        });
        mcp->setViewActions(keepingInputFocus(m_viewActions));
        connect(mcp, &WhaleAiMcpServer::locationSet, this, [this](const QString &folder) {
            // The name only (paths get long); the path in the tooltip.
            const QString name = QDir(folder).dirName().isEmpty() ? folder : QDir(folder).dirName();
            switch (activePane()) {
            case 0:
                addActionNotice(i18nc("@info %1 is a folder name", "The agent opened “%1” in the left view.", name), folder);
                break;
            case 1:
                addActionNotice(i18nc("@info %1 is a folder name", "The agent opened “%1” in the right view.", name), folder);
                break;
            default:
                addActionNotice(i18nc("@info %1 is a folder name", "The agent opened “%1”.", name), folder);
            }
        });
        connect(mcp, &WhaleAiMcpServer::itemsSelected, this, [this](int count) {
            switch (activePane()) {
            case 0:
                addActionNotice(i18ncp("@info", "The agent selected %1 item in the left view.", "The agent selected %1 items in the left view.", count));
                break;
            case 1:
                addActionNotice(i18ncp("@info", "The agent selected %1 item in the right view.", "The agent selected %1 items in the right view.", count));
                break;
            default:
                addActionNotice(i18ncp("@info", "The agent selected %1 item.", "The agent selected %1 items.", count));
            }
        });
        connect(mcp, &WhaleAiMcpServer::windowToolUsed, this, [this](const QString &notice) {
            addActionNotice(notice);
        });
        connect(mcp, &WhaleAiMcpServer::actionTriggered, this, [this](const WhaleAiUiAction &action) {
            const QString command = action.menu.isEmpty() ? action.text : action.menu + QStringLiteral(" ▸ ") + action.text;
            if (action.checkable && !action.exclusive) {
                addActionNotice(action.checked ? i18nc("@info %1 is a command, e.g. View ▸ Show Hidden Files", "The agent turned on %1.", command)
                                               : i18nc("@info %1 is a command, e.g. View ▸ Show Hidden Files", "The agent turned off %1.", command));
            } else {
                addActionNotice(i18nc("@info %1 is a command, e.g. View ▸ View Mode ▸ Details", "The agent used %1.", command));
            }
        });
        mcpServers.append(mcp->mcpServer(relay));
    }
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
    connect(session, &WhaleAcpSession::permissionRequested, this, [this](const WhaleAcpPermissionRequest &request) {
        if (answerOwnToolRequest(request)) {
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

    m_sessionInfo->setProperty("usage", -1);
    if (!agent.usesLocalModel()) {
        m_session->start(command, directory, mcpServers);
        return m_session != nullptr;
    }

    // AI12: LM Studio and the model first, then the agent.
    m_lease = WhaleAiLocalRuntime::instance()->acquire(agent.localProvider, agent.localModel, agent.localContextLength, this);
    m_localProgress = i18nc("@info", "Starting the local model…");
    m_transcript->addNotice(i18nc("@info %1 is the id of a language model", "Starting the local model %1 first.", agent.localModel));
    WhaleAiLocalLease *lease = m_lease;
    connect(lease, &WhaleAiLocalLease::progress, this, [this, lease](const QString &text) {
        if (m_lease == lease) {
            m_localProgress = text;
            updateSessionInfo();
        }
    });
    connect(lease, &WhaleAiLocalLease::ready, this, [this, lease, session, command, directory, mcpServers] {
        if (m_lease == lease && m_session == session) {
            session->start(command, directory, mcpServers);
            updateSessionInfo();
        }
    });
    connect(lease, &WhaleAiLocalLease::failed, this, [this, lease](const QString &error) {
        if (m_lease != lease) {
            return;
        }
        m_transcript->addNotice(i18nc("@info", "The local model could not be started."), WhaleAiTranscript::Kind::Error, error);
        endSession(true);
        updateState();
    });
    updateSessionInfo();
    return true;
}

void WhaleAiPanel::sendPending()
{
    if (!m_session || m_session->state() != WhaleAcpSession::State::Ready || m_modeState != ModeState::Set || m_pendingPrompt.isEmpty()) {
        return;
    }
    QJsonArray blocks = WhaleAiContextPrompt::blocks(std::exchange(m_pendingContext, {}), std::exchange(m_pendingPrompt, QString()));
    if (!m_systemPromptSent) {
        // The system prompt leads the first message of the conversation (ACP has no system
        // prompt of its own); later messages carry only the live context.
        m_systemPromptSent = true;
        blocks.prepend(WhaleAcpContent::text(WhaleAiAgents::systemPrompt()));
    }
    m_session->prompt(blocks);
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

void WhaleAiPanel::endSession(bool freeLocalModelNow)
{
    m_hiddenTimer->stop();
    if (m_lease) {
        WhaleAiLocalLease *lease = m_lease;
        m_lease = nullptr;
        lease->disconnect(this);
        lease->release(freeLocalModelNow);
        lease->deleteLater();
    }
    m_localProgress.clear();
    m_pendingPrompt.clear();
    m_pendingContext = {};
    m_modeState = ModeState::Unknown;
    m_modeRequest.clear();
    if (m_mcp) {
        // It goes with the session; nothing it does after the end belongs here.
        m_mcp->disconnect(this);
        m_mcp->setViewActions({});
    }
    m_mcp = nullptr;
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
    // What the agent changed stays; it can no longer be undone from here (user, 2026-11-16).
    WhaleAiChangeLog::of(window())->clear();
    m_transcript->clear();
    greet();
    m_input->setFocus();
    Q_EMIT conversationCleared();
}

void WhaleAiPanel::undoAgentChanges()
{
    const QStringList undone = WhaleAiChangeLog::of(window())->undo();
    if (!undone.isEmpty()) {
        m_transcript->addNotice(i18nc("@info %1 is a list of things, e.g. the setting “Status bar”, the view of “Pictures”",
                                      "Undid the agent's changes: %1.",
                                      undone.join(i18nc("@info separator of a list", ", "))));
    }
    updateState();
}

void WhaleAiPanel::greet()
{
    // From Orcinus, not from the agent: every agent gets the same briefing, and the agent starts
    // only with the first message.
    m_transcript->addNotice(i18nc("@info",
                                  "Ask the agent about your files. It sees the folder you have open and what you selected, and it can open "
                                  "folders and select files for you."));
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
    // Not while the agent works: it may be changing the same things.
    const auto *log = window()->findChild<WhaleAiChangeLog *>(QString(), Qt::FindDirectChildrenOnly);
    m_undoChanges->setEnabled(!busy && log && log->count() > 0);
}

void WhaleAiPanel::updateSessionInfo()
{
    updateModes();
    if (!m_session) {
        m_sessionInfo->setText(i18nc("@info", "Starts with your first message."));
        return;
    }
    if (m_lease && m_lease->state() == WhaleAiLocalLease::State::Preparing) {
        m_sessionInfo->setText(m_localProgress);
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
    m_hiddenTimer->stop();
}

void WhaleAiPanel::hideEvent(QHideEvent *event)
{
    Panel::hideEvent(event);
    // A hidden panel holds a local model only for the grace period (AI12): its memory is the
    // user's, toggling the panel must not reload it.
    if (m_lease && !event->spontaneous()) {
        m_hiddenTimer->start(WhaleAiLocalRuntime::instance()->graceMs());
    }
}

void WhaleAiPanel::freeHiddenLocalModel()
{
    if (!m_lease || isVisible()) {
        return;
    }
    if (m_session && m_session->state() == WhaleAcpSession::State::Prompting) {
        // Not in the middle of an answer; when it is done, the panel is checked again.
        m_hiddenTimer->start(WhaleAiLocalRuntime::instance()->graceMs());
        return;
    }
    endSession(true);
    m_transcript->addNotice(
        i18nc("@info", "The agent was stopped to free the memory of the local model while the panel was hidden. Your next message starts it again."));
    updateState();
}

void WhaleAiPanel::focusInEvent(QFocusEvent *event)
{
    Panel::focusInEvent(event);
    m_input->setFocus();
}

#include "moc_whaleaipanel.cpp"
