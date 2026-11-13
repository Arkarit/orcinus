/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaisettingspage.h"
#include "whaleaiagentdialog.h"

#include <KLocalizedString>

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QVBoxLayout>

namespace
{
constexpr int LogLines = 12;
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
    m_result->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_result->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addLayout(listRow, 1);
    layout->addWidget(m_result);

    m_tester = new WhaleAiAgentTest(this);
    connect(m_tester, &WhaleAiAgentTest::finished, this, [this](const WhaleAiAgentTest::Result &result) {
        m_result->setText(resultText(result));
        updateButtons();
    });

    connect(m_list, &QListWidget::currentRowChanged, this, &WhaleAiSettingsPage::updateButtons);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &WhaleAiSettingsPage::editCurrent);
    connect(m_edit, &QPushButton::clicked, this, &WhaleAiSettingsPage::editCurrent);
    connect(m_remove, &QPushButton::clicked, this, &WhaleAiSettingsPage::removeCurrent);
    connect(m_default, &QPushButton::clicked, this, &WhaleAiSettingsPage::makeCurrentDefault);
    connect(m_test, &QPushButton::clicked, this, &WhaleAiSettingsPage::testCurrent);

    m_agents = WhaleAiAgents::agents();
    m_defaultId = WhaleAiAgents::defaultAgentId();
    fillList(m_defaultId);
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
}

void WhaleAiSettingsPage::restoreDefaults()
{
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
    WhaleAiAgentDialog dialog(WhaleAiAgent(), this);
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
    WhaleAiAgentDialog dialog(m_agents.at(row), this);
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
    m_tester->run(m_agents.at(row), m_testTimeout);
    updateButtons();
}

void WhaleAiSettingsPage::setAgentsChanged()
{
    m_result->clear();
    Q_EMIT changed();
}

QString WhaleAiSettingsPage::resultText(const WhaleAiAgentTest::Result &result)
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
    if (!result.agentInfo.authMethods.isEmpty() && (result.needsLogin || !result.ok)) {
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
