/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaisettingspage.h"
#include "whaleaiagentdialog.h"
#include "whaleaiconversationstore.h"
#include "whaleaidisclaimer.h"

#include <KLocalizedString>
#include <KMessageBox>
#include <KStandardGuiItem>
#include <KTerminalLauncherJob>

#include <QCheckBox>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace
{
constexpr int LogLines = 12;
}

/** @return The seconds one step of the free delay of @p provider stands for: minutes for Strata. */
static int freeDelayUnit(const QString &provider)
{
    return provider == QLatin1String("strata") ? 60 : 1;
}

WhaleAiSettingsPage::WhaleAiSettingsPage(QWidget *parent)
    : SettingsPageBase(parent)
{
    auto *intro = new QLabel(i18nc("@info",
                                   "The AI panel talks to AI agents installed on this computer, through the Agent Client Protocol (ACP). "
                                   "An agent may use a cloud service or a local model, as it is set up. "
                                   "Nothing is sent before you write in the AI panel."),
                             this);
    intro->setWordWrap(true);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("agents"));

    m_add = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), i18nc("@action:button", "Add"), this);
    m_add->setObjectName(QStringLiteral("add"));
    m_addMenu = new QMenu(m_add);
    m_add->setMenu(m_addMenu);
    // Look for the known agents each time: the user may install one while the dialog is open.
    connect(m_addMenu, &QMenu::aboutToShow, this, &WhaleAiSettingsPage::fillAddMenu);

    m_edit = new QPushButton(QIcon::fromTheme(QStringLiteral("document-edit")), i18nc("@action:button", "Edit…"), this);
    m_edit->setObjectName(QStringLiteral("edit"));
    m_remove = new QPushButton(QIcon::fromTheme(QStringLiteral("list-remove")), i18nc("@action:button", "Remove"), this);
    m_remove->setObjectName(QStringLiteral("remove"));
    m_default = new QPushButton(QIcon::fromTheme(QStringLiteral("starred-symbolic")), i18nc("@action:button", "Use by Default"), this);
    m_default->setObjectName(QStringLiteral("default"));
    m_test = new QPushButton(QIcon::fromTheme(QStringLiteral("system-run")), i18nc("@action:button", "Test"), this);
    m_test->setObjectName(QStringLiteral("test"));
    m_test->setToolTip(i18nc("@info:tooltip", "Starts the agent without a prompt and shows what it reports. This costs nothing."));

    auto *buttons = new QVBoxLayout;
    buttons->addWidget(m_add);
    buttons->addWidget(m_edit);
    buttons->addWidget(m_remove);
    buttons->addWidget(m_default);
    buttons->addSpacing(m_add->sizeHint().height() / 2);
    buttons->addWidget(m_test);
    buttons->addStretch();

    auto *listRow = new QHBoxLayout;
    listRow->addWidget(m_list, 1);
    listRow->addLayout(buttons);

    m_result = new QLabel(this);
    m_result->setObjectName(QStringLiteral("testResult"));
    m_result->setWordWrap(true);
    m_result->setTextFormat(Qt::RichText);
    m_result->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    m_result->setOpenExternalLinks(true);
    m_result->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    m_logIn = new QPushButton(QIcon::fromTheme(QStringLiteral("utilities-terminal")), i18nc("@action:button", "Log In in a Terminal…"), this);
    m_logIn->setObjectName(QStringLiteral("logIn"));
    m_logIn->hide();

    // The system prompt sent once to every agent (AI): a built-in briefing by default, which the
    // user may take over with a warning.
    auto *promptGroup = new QGroupBox(i18nc("@title:group", "System prompt"), this);
    m_editPrompt = new QCheckBox(i18nc("@option:check", "Write my own instructions for every agent"), promptGroup);
    m_editPrompt->setObjectName(QStringLiteral("editPrompt"));
    auto *promptHint = new QLabel(i18nc("@info",
                                        "These instructions lead the first message of every conversation: who the agent is and its Orcinus "
                                        "tools. Orcinus keeps them up to date. You can write your own, but a weak prompt can confuse the "
                                        "agent."),
                                  promptGroup);
    promptHint->setWordWrap(true);
    m_prompt = new QPlainTextEdit(promptGroup);
    m_prompt->setObjectName(QStringLiteral("systemPrompt"));
    m_prompt->setReadOnly(true);
    m_prompt->setTabChangesFocus(true);
    m_resetPrompt = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-reset")), i18nc("@action:button", "Reset to Orcinus' Text"), promptGroup);
    m_resetPrompt->setObjectName(QStringLiteral("resetPrompt"));
    m_resetPrompt->setEnabled(false);
    auto *promptLayout = new QVBoxLayout(promptGroup);
    promptLayout->addWidget(m_editPrompt);
    promptLayout->addWidget(promptHint);
    promptLayout->addWidget(m_prompt, 1);
    promptLayout->addWidget(m_resetPrompt, 0, Qt::AlignRight);

    connect(m_editPrompt, &QCheckBox::toggled, this, &WhaleAiSettingsPage::toggleEditPrompt);
    connect(m_prompt, &QPlainTextEdit::textChanged, this, [this] {
        if (!m_prompt->isReadOnly()) {
            Q_EMIT changed();
        }
    });
    connect(m_resetPrompt, &QPushButton::clicked, this, [this] {
        m_prompt->setPlainText(WhaleAiAgents::defaultSystemPrompt());
    });

    // AI13: how long a closed AI panel keeps its local model (the user's choice).
    auto *localGroup = new QGroupBox(i18nc("@title:group", "Local models"), this);
    auto *localForm = new QFormLayout(localGroup);
    auto *localHint = new QLabel(i18nc("@info",
                                       "When the AI panel is closed, Orcinus frees the memory of the local model it started after this "
                                       "time. Loading it again takes a while: a few seconds for LM Studio, about a minute and a half "
                                       "for Strata."),
                                 localGroup);
    localHint->setWordWrap(true);
    localForm->addRow(localHint);
    for (const auto &[id, name] :
         {std::pair{QStringLiteral("lmstudio"), QStringLiteral("LM Studio")}, std::pair{QStringLiteral("strata"), QStringLiteral("Strata")}}) {
        // Strata in minutes (its default is 10), LM Studio in seconds (its default is 3).
        auto *delay = new QSpinBox(localGroup);
        delay->setObjectName(QStringLiteral("freeDelay-") + id);
        delay->setRange(0, 24 * 3600 / freeDelayUnit(id));
        delay->setSuffix(freeDelayUnit(id) == 60 ? i18nc("@item:valuesuffix minutes", " min") : i18nc("@item:valuesuffix seconds", " s"));
        delay->setSpecialValueText(i18nc("@item:inrange", "At once"));
        delay->setValue(qRound(WhaleAiAgents::freeDelaySeconds(id) / freeDelayUnit(id)));
        connect(delay, &QSpinBox::valueChanged, this, &WhaleAiSettingsPage::changed);
        localForm->addRow(i18nc("@label:spinbox %1 is LM Studio or Strata", "%1:", name), delay);
        m_freeDelays.insert(id, delay);
    }

    // AI14: the panel saves its conversations, so they can be continued.
    auto *conversationGroup = new QGroupBox(i18nc("@title:group", "Conversations"), this);
    auto *conversationForm = new QFormLayout(conversationGroup);
    m_saveConversations = new QCheckBox(i18nc("@option:check", "Save conversations"), conversationGroup);
    m_saveConversations->setObjectName(QStringLiteral("saveConversations"));
    m_saveConversations->setChecked(WhaleAiAgents::saveConversations());
    conversationForm->addRow(m_saveConversations);
    auto *conversationHint = new QLabel(i18nc("@info %1 is a folder",
                                              "Only on this computer, in %1. They hold what you and the agent wrote, also paths and "
                                              "what the agent read of your files.",
                                              WhaleAiConversationStore::defaultFolder()),
                                        conversationGroup);
    conversationHint->setWordWrap(true);
    conversationHint->setTextInteractionFlags(Qt::TextSelectableByMouse);
    conversationForm->addRow(conversationHint);
    m_keepConversations = new QSpinBox(conversationGroup);
    m_keepConversations->setObjectName(QStringLiteral("keepConversations"));
    m_keepConversations->setRange(0, 3650);
    m_keepConversations->setSuffix(i18nc("@item:valuesuffix days", " days"));
    m_keepConversations->setSpecialValueText(i18nc("@item:inrange keep saved conversations", "Always"));
    m_keepConversations->setValue(WhaleAiAgents::keepConversationsDays());
    m_keepConversations->setEnabled(m_saveConversations->isChecked());
    conversationForm->addRow(i18nc("@label:spinbox", "Keep for:"), m_keepConversations);
    connect(m_saveConversations, &QCheckBox::toggled, this, [this](bool on) {
        m_keepConversations->setEnabled(on);
        Q_EMIT changed();
    });
    connect(m_keepConversations, &QSpinBox::valueChanged, this, &WhaleAiSettingsPage::changed);

    auto *content = new QWidget(this);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(intro);
    layout->addLayout(listRow, 1);
    layout->addWidget(m_result);
    layout->addWidget(m_logIn, 0, Qt::AlignLeft);
    layout->addWidget(conversationGroup);
    layout->addWidget(localGroup);
    layout->addWidget(promptGroup);

    // The disclaimer first, until the user accepts it here or in the AI panel (AI5f).
    m_pages = new QStackedWidget(this);
    m_pages->addWidget(new WhaleAiDisclaimer(m_pages));
    m_pages->addWidget(content);
    m_pages->setCurrentIndex(WhaleAiAgents::disclaimerAccepted() ? 1 : 0);
    connect(WhaleAiDisclaimerNotifier::instance(), &WhaleAiDisclaimerNotifier::accepted, this, [this] {
        m_pages->setCurrentIndex(1);
    });
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(m_pages);

    m_tester = new WhaleAiAgentTest(this);
    connect(m_tester, &WhaleAiAgentTest::finished, this, [this](const WhaleAiAgentTest::Result &result) {
        const WhaleAiLoginHint hint = WhaleAiAgents::loginHint(m_testedAgent, m_presetSearchPaths);
        m_result->setText(resultText(result, hint));
        m_loginCommand = result.needsLogin ? hint.command : QString();
        m_logIn->setToolTip(i18nc("@info:tooltip %1 is a command line", "Runs “%1” in a terminal.", m_loginCommand));
        m_logIn->setVisible(!m_loginCommand.isEmpty());
        updateButtons();
    });

    connect(m_list, &QListWidget::currentRowChanged, this, &WhaleAiSettingsPage::updateButtons);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &WhaleAiSettingsPage::editCurrent);
    connect(m_edit, &QPushButton::clicked, this, &WhaleAiSettingsPage::editCurrent);
    connect(m_remove, &QPushButton::clicked, this, &WhaleAiSettingsPage::removeCurrent);
    connect(m_default, &QPushButton::clicked, this, &WhaleAiSettingsPage::makeCurrentDefault);
    connect(m_test, &QPushButton::clicked, this, &WhaleAiSettingsPage::testCurrent);
    connect(m_logIn, &QPushButton::clicked, this, &WhaleAiSettingsPage::logInInTerminal);

    m_agents = WhaleAiAgents::agents();
    m_defaultId = WhaleAiAgents::defaultAgentId();
    fillList(m_defaultId);

    // Load the system prompt without the warning that a user click would bring.
    m_prompt->setPlainText(WhaleAiAgents::systemPrompt());
    {
        const QSignalBlocker blocker(m_editPrompt);
        m_editPrompt->setChecked(WhaleAiAgents::customSystemPrompt());
    }
    m_prompt->setReadOnly(!m_editPrompt->isChecked());
    m_resetPrompt->setEnabled(m_editPrompt->isChecked());
}

WhaleAiSettingsPage::~WhaleAiSettingsPage() = default;

void WhaleAiSettingsPage::applySettings()
{
    // The start mode is chosen in the panel, maybe while this page is open: keep the stored one.
    QList<WhaleAiAgent> agents = m_agents;
    for (WhaleAiAgent &agent : agents) {
        const WhaleAiAgent stored = WhaleAiAgents::agent(agent.id);
        if (!stored.id.isEmpty()) {
            agent.startMode = stored.startMode;
        }
    }
    WhaleAiAgents::setAgents(agents);
    WhaleAiAgents::setDefaultAgentId(m_defaultId);
    WhaleAiAgents::setCustomSystemPrompt(m_editPrompt->isChecked());
    if (m_editPrompt->isChecked()) {
        WhaleAiAgents::setSystemPrompt(m_prompt->toPlainText());
    }
    for (auto it = m_freeDelays.cbegin(); it != m_freeDelays.cend(); ++it) {
        WhaleAiAgents::setFreeDelaySeconds(it.key(), it.value()->value() * freeDelayUnit(it.key()));
    }
    WhaleAiAgents::setSaveConversations(m_saveConversations->isChecked());
    WhaleAiAgents::setKeepConversationsDays(m_keepConversations->value());
}

void WhaleAiSettingsPage::restoreDefaults()
{
    // The agents are the user's own: they stay. The delays go back to Orcinus' defaults.
    for (auto it = m_freeDelays.cbegin(); it != m_freeDelays.cend(); ++it) {
        it.value()->setValue(qRound(WhaleAiAgents::defaultFreeDelaySeconds(it.key()) / freeDelayUnit(it.key())));
    }
    m_saveConversations->setChecked(true);
    m_keepConversations->setValue(90);
}

QList<WhaleAiAgent> WhaleAiSettingsPage::agents() const
{
    return m_agents;
}

QString WhaleAiSettingsPage::defaultAgentId() const
{
    return m_defaultId;
}

void WhaleAiSettingsPage::setPresetSearchPaths(const QStringList &paths)
{
    m_presetSearchPaths = paths;
}

void WhaleAiSettingsPage::setTestTimeout(int milliseconds)
{
    m_testTimeout = milliseconds;
}

void WhaleAiSettingsPage::fillList(const QString &currentId)
{
    m_list->clear();
    for (const WhaleAiAgent &agent : std::as_const(m_agents)) {
        auto *item = new QListWidgetItem(m_list);
        const bool isDefault = agent.id == m_defaultId;
        item->setText(isDefault ? i18nc("@item:inlistbox %1 is the name of an AI agent", "%1 (default)", agent.name) : agent.name);
        item->setToolTip(agent.commandLine);
        item->setData(Qt::UserRole, agent.id);
        if (isDefault) {
            QFont font = item->font();
            font.setBold(true);
            item->setFont(font);
        }
        if (agent.id == currentId) {
            m_list->setCurrentItem(item);
        }
    }
    if (!m_list->currentItem() && m_list->count() > 0) {
        m_list->setCurrentRow(0);
    }
    updateButtons();
}

void WhaleAiSettingsPage::fillAddMenu()
{
    m_addMenu->clear();
    const QList<WhaleAiAgentPreset> presets = WhaleAiAgents::presets(m_presetSearchPaths);
    for (const WhaleAiAgentPreset &preset : presets) {
        QString text = preset.name;
        if (!preset.isAvailable()) {
            text = i18nc("@action:inmenu %1 is the name of an AI agent", "%1 (not found)", preset.name);
        } else if (preset.viaNpx) {
            text = i18nc("@action:inmenu %1 is the name of an AI agent", "%1 (downloads its adapter with npx)", preset.name);
        }
        QAction *action = m_addMenu->addAction(text);
        action->setObjectName(QStringLiteral("preset-") + preset.id);
        action->setEnabled(preset.isAvailable());
        action->setToolTip(preset.isAvailable() ? preset.description : preset.description + QLatin1Char('\n') + preset.installHint);
        connect(action, &QAction::triggered, this, [this, preset] {
            addPreset(preset);
        });
    }
    // AI12/AI13: a local model of each provider. No request to a server: the menu opens at once.
    WhaleAiLocalModels::Options options = m_localOptions;
    options.probeServer = false;
    const WhaleAiLocalSetup setup = WhaleAiLocalModels::discover(options);
    const QList<std::pair<QString, QString>> providers{{QStringLiteral("lmstudio"), QStringLiteral("LM Studio")},
                                                       {QStringLiteral("strata"), QStringLiteral("Strata")}};
    for (const auto &[id, name] : providers) {
        const auto found = std::find_if(setup.providers.cbegin(), setup.providers.cend(), [&id](const WhaleAiLocalProvider &provider) {
            return provider.id == id;
        });
        QString text = i18nc("@action:inmenu %1 is LM Studio or Strata", "Local Model (%1)…", name);
        if (found == setup.providers.cend()) {
            text = i18nc("@action:inmenu %1 is LM Studio or Strata", "Local Model (%1 not found)", name);
        } else if (found->models.isEmpty()) {
            text = i18nc("@action:inmenu %1 is LM Studio or Strata", "Local Model (%1 has no models)", name);
        }
        QAction *local = m_addMenu->addAction(text);
        // "localModel" stays LM Studio's (AI12d).
        local->setObjectName(id == QLatin1String("lmstudio") ? QStringLiteral("localModel") : QStringLiteral("localModel-") + id);
        local->setEnabled(found != setup.providers.cend() && !found->models.isEmpty());
        local->setToolTip(i18nc("@info:tooltip %1 is LM Studio or Strata",
                                "An agent for a language model on this computer, run by OpenCode. Orcinus starts %1 and loads the model "
                                "when you talk to it, and frees them again when it is no longer used.",
                                name));
        connect(local, &QAction::triggered, this, [this, id] {
            addLocalModel(id);
        });
    }

    m_addMenu->setToolTipsVisible(true);
    m_addMenu->addSeparator();
    QAction *custom = m_addMenu->addAction(i18nc("@action:inmenu", "Other Agent…"));
    custom->setObjectName(QStringLiteral("custom"));
    connect(custom, &QAction::triggered, this, &WhaleAiSettingsPage::addCustom);
}

int WhaleAiSettingsPage::currentRow() const
{
    const int row = m_list->currentRow();
    return row >= 0 && row < m_agents.size() ? row : -1;
}

void WhaleAiSettingsPage::updateButtons()
{
    const int row = currentRow();
    m_edit->setEnabled(row >= 0);
    m_remove->setEnabled(row >= 0);
    m_default->setEnabled(row >= 0 && m_agents.at(row).id != m_defaultId);
    m_test->setEnabled(row >= 0 && !m_tester->isRunning());
}

void WhaleAiSettingsPage::addPreset(const WhaleAiAgentPreset &preset)
{
    WhaleAiAgent agent;
    agent.id = WhaleAiAgents::uniqueId(preset.id, m_agents);
    agent.name = preset.name;
    agent.commandLine = preset.commandLine;
    m_agents.append(agent);
    if (m_defaultId.isEmpty()) {
        m_defaultId = agent.id;
    }
    fillList(agent.id);
    setAgentsChanged();
}

void WhaleAiSettingsPage::addCustom()
{
    WhaleAiAgentDialog dialog(WhaleAiAgent(), this, WhaleAiLocalModels::discover(m_localOptions));
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    WhaleAiAgent agent = dialog.agent();
    agent.id = WhaleAiAgents::uniqueId(agent.name, m_agents);
    m_agents.append(agent);
    if (m_defaultId.isEmpty()) {
        m_defaultId = agent.id;
    }
    fillList(agent.id);
    setAgentsChanged();
}

void WhaleAiSettingsPage::setLocalModelOptions(const WhaleAiLocalModels::Options &options)
{
    m_localOptions = options;
}

void WhaleAiSettingsPage::addLocalModel(const QString &providerId)
{
    const WhaleAiLocalSetup setup = WhaleAiLocalModels::discover(m_localOptions);
    const auto provider = std::find_if(setup.providers.cbegin(), setup.providers.cend(), [&providerId](const WhaleAiLocalProvider &provider) {
        return provider.id == providerId;
    });
    if (provider == setup.providers.cend() || provider->models.isEmpty()) {
        return;
    }
    // A loaded model the agent can work with first, then (Strata) the size used last, then any
    // that can call tools, then any.
    auto pick = [&provider](const std::function<bool(const WhaleAiLocalModel &)> &good) -> const WhaleAiLocalModel * {
        const auto found = std::find_if(provider->models.cbegin(), provider->models.cend(), good);
        return found == provider->models.cend() ? nullptr : &*found;
    };
    const WhaleAiLocalModel *model = pick([](const WhaleAiLocalModel &model) {
        return model.loaded && model.toolUse;
    });
    if (!model && providerId == QLatin1String("strata")) {
        // Its log (named in the config) is written whenever that size runs.
        QDateTime newest;
        for (const WhaleAiLocalModel &candidate : provider->models) {
            QFile config(candidate.file);
            if (!config.open(QIODevice::ReadOnly)) {
                continue;
            }
            const QString log = QJsonDocument::fromJson(config.readAll()).object().value(QLatin1String("log")).toString();
            const QDateTime modified = log.isEmpty() ? QDateTime() : QFileInfo(log).lastModified();
            if (modified.isValid() && (!newest.isValid() || modified > newest)) {
                newest = modified;
                model = &candidate;
            }
        }
    }
    if (!model) {
        model = pick([](const WhaleAiLocalModel &model) {
            return model.toolUse;
        });
    }
    if (!model) {
        model = &provider->models.constFirst();
    }
    WhaleAiLocalClient client;
    for (const WhaleAiLocalClient &candidate : setup.clients) {
        if (candidate.id == QLatin1String("opencode")) {
            client = candidate;
        }
    }

    WhaleAiAgentDialog dialog(WhaleAiAgents::localAgent(*provider, *model, client, m_agents), this, setup);
    dialog.setWindowTitle(i18nc("@title:window", "Add Local Model"));
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    WhaleAiAgent agent = dialog.agent();
    agent.id = WhaleAiAgents::uniqueId(agent.name, m_agents);
    m_agents.append(agent);
    if (m_defaultId.isEmpty()) {
        m_defaultId = agent.id;
    }
    fillList(agent.id);
    setAgentsChanged();
}

void WhaleAiSettingsPage::editCurrent()
{
    const int row = currentRow();
    if (row < 0) {
        return;
    }
    WhaleAiAgentDialog dialog(m_agents.at(row), this, WhaleAiLocalModels::discover(m_localOptions));
    if (dialog.exec() != QDialog::Accepted || dialog.agent() == m_agents.at(row)) {
        return;
    }
    m_agents[row] = dialog.agent();
    fillList(m_agents.at(row).id);
    setAgentsChanged();
}

void WhaleAiSettingsPage::removeCurrent()
{
    const int row = currentRow();
    if (row < 0) {
        return;
    }
    const WhaleAiAgent removed = m_agents.takeAt(row);
    if (removed.id == m_defaultId) {
        m_defaultId = m_agents.isEmpty() ? QString() : m_agents.first().id;
    }
    fillList(m_agents.isEmpty() ? QString() : m_agents.at(qMin(row, m_agents.size() - 1)).id);
    setAgentsChanged();
}

void WhaleAiSettingsPage::makeCurrentDefault()
{
    const int row = currentRow();
    if (row < 0) {
        return;
    }
    m_defaultId = m_agents.at(row).id;
    fillList(m_defaultId);
    setAgentsChanged();
}

void WhaleAiSettingsPage::testCurrent()
{
    const int row = currentRow();
    if (row < 0 || m_tester->isRunning()) {
        return;
    }
    m_result->setText(i18nc("@info", "Starting %1…", m_agents.at(row).name.toHtmlEscaped()));
    m_logIn->hide();
    m_testedAgent = m_agents.at(row);
    m_tester->run(m_agents.at(row), m_testTimeout);
    updateButtons();
}

void WhaleAiSettingsPage::logInInTerminal()
{
    if (m_loginCommand.isEmpty()) {
        return;
    }
    auto *job = new KTerminalLauncherJob(m_loginCommand, this);
    job->setWorkingDirectory(QDir::homePath());
    job->start();
}

void WhaleAiSettingsPage::setAgentsChanged()
{
    m_result->clear();
    m_logIn->hide();
    Q_EMIT changed();
}

void WhaleAiSettingsPage::toggleEditPrompt(bool edit)
{
    if (edit) {
        const auto answer =
            KMessageBox::warningContinueCancel(this,
                                               i18nc("@info",
                                                     "You are about to write your own instructions for every agent, instead of the ones Orcinus keeps up to "
                                                     "date. A weak or misleading prompt can confuse the agent or drop useful guidance. Continue?"),
                                               i18nc("@title:window", "Edit the system prompt"),
                                               KStandardGuiItem::cont(),
                                               KStandardGuiItem::cancel(),
                                               QStringLiteral("whale-ai-edit-system-prompt"));
        if (answer != KMessageBox::Continue) {
            const QSignalBlocker blocker(m_editPrompt);
            m_editPrompt->setChecked(false);
            return;
        }
    } else {
        // Back to Orcinus' built-in text (what will actually be sent); a stored custom one is kept.
        const QSignalBlocker blocker(m_prompt);
        m_prompt->setPlainText(WhaleAiAgents::defaultSystemPrompt());
    }
    m_prompt->setReadOnly(!edit);
    m_resetPrompt->setEnabled(edit);
    Q_EMIT changed();
}

QString WhaleAiSettingsPage::resultText(const WhaleAiAgentTest::Result &result, const WhaleAiLoginHint &loginHint)
{
    QString text;
    const QString name = result.agentInfo.displayName().toHtmlEscaped();

    if (result.ok) {
        const QString version = result.agentInfo.version.toHtmlEscaped();
        text = version.isEmpty() ? i18nc("@info %1 is the name of an AI agent", "<b>%1</b> is ready.", name)
                                 : i18nc("@info %1 is the name of an AI agent, %2 its version", "<b>%1 %2</b> is ready.", name, version);
        QStringList lines;
        for (const WhaleAcpConfigOption &option : result.configOptions) {
            if (option.category != QLatin1String("mode") && option.category != QLatin1String("model")) {
                continue;
            }
            const WhaleAcpConfigOption::Choice *current = option.choice(option.currentValue);
            const QString value = (current ? current->name : option.currentValue).toHtmlEscaped();
            if (option.category == QLatin1String("mode")) {
                QStringList names;
                for (const WhaleAcpConfigOption::Choice &choice : option.choices) {
                    names.append(choice.name.toHtmlEscaped());
                }
                lines.append(i18nc("@info %1 is the current mode, %2 a list of all modes", "Mode: %1 (of: %2)", value, names.join(QStringLiteral(", "))));
            } else {
                lines.append(i18nc("@info %1 is the name of a language model", "Model: %1", value));
            }
        }
        if (!lines.isEmpty()) {
            text += QStringLiteral("<br>") + lines.join(QStringLiteral("<br>"));
        }
        return text;
    }

    text = QStringLiteral("<b>%1</b>").arg(result.error.toHtmlEscaped());
    // The technical reason, unless it only repeats that a login is needed.
    if (!result.detail.isEmpty() && !result.needsLogin) {
        text += QStringLiteral("<br>") + result.detail.toHtmlEscaped();
    }
    if (result.needsLogin && loginHint.isValid()) {
        text += QStringLiteral("<br>") + loginHint.text.toHtmlEscaped();
        if (loginHint.url.isValid()) {
            text += QStringLiteral("<br>")
                + i18nc("@info %1 is a link",
                        "Download and documentation: %1",
                        QStringLiteral("<a href=\"%1\">%2</a>")
                            .arg(loginHint.url.toString(QUrl::FullyEncoded).toHtmlEscaped(), loginHint.url.toDisplayString().toHtmlEscaped()));
        }
    } else if (!result.agentInfo.authMethods.isEmpty() && (result.needsLogin || !result.ok)) {
        QStringList methods;
        for (const WhaleAcpAgentInfo::AuthMethod &method : result.agentInfo.authMethods) {
            methods.append(method.description.isEmpty() ? method.name.toHtmlEscaped()
                                                        : QStringLiteral("%1: %2").arg(method.name.toHtmlEscaped(), method.description.toHtmlEscaped()));
        }
        text += QStringLiteral("<br>") + i18nc("@info", "Ways to log in:") + QStringLiteral("<ul><li>") + methods.join(QStringLiteral("</li><li>"))
            + QStringLiteral("</li></ul>");
    }
    if (!result.log.isEmpty()) {
        QStringList log = result.log.split(QLatin1Char('\n'));
        if (log.size() > LogLines) {
            log = log.mid(log.size() - LogLines);
        }
        text += QStringLiteral("<br>") + i18nc("@info", "The agent's log ends with:") + QStringLiteral("<pre>") + log.join(QLatin1Char('\n')).toHtmlEscaped()
            + QStringLiteral("</pre>");
    }
    return text;
}

#include "moc_whaleaisettingspage.cpp"
