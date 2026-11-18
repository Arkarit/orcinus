/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaiagentdialog.h"

#include <KLocalizedString>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

namespace
{
const QString OpenCodeConfigName = QStringLiteral("OPENCODE_CONFIG_CONTENT");
}

WhaleAiAgentDialog::WhaleAiAgentDialog(const WhaleAiAgent &agent, QWidget *parent, const std::optional<WhaleAiLocalSetup> &setup)
    : QDialog(parent)
    , m_agent(agent)
    , m_setup(setup ? *setup : WhaleAiLocalModels::discover())
{
    setWindowTitle(agent.name.isEmpty() ? i18nc("@title:window", "Add AI Agent") : i18nc("@title:window", "Edit AI Agent"));

    auto *form = new QFormLayout;

    m_name = new QLineEdit(agent.name, this);
    m_name->setObjectName(QStringLiteral("name"));
    form->addRow(i18nc("@label:textbox", "Name:"), m_name);

    m_command = new QLineEdit(agent.commandLine, this);
    m_command->setObjectName(QStringLiteral("command"));
    m_command->setPlaceholderText(i18nc("@info:placeholder", "e.g. opencode acp"));
    m_command->setToolTip(i18nc("@info:tooltip", "A program that speaks the Agent Client Protocol (ACP) on its standard input and output."));
    form->addRow(i18nc("@label:textbox", "Command:"), m_command);

    m_commandState = new QLabel(this);
    m_commandState->setObjectName(QStringLiteral("commandState"));
    m_commandState->setWordWrap(true);
    m_commandState->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(QString(), m_commandState);

    // AI12: the local model Orcinus starts before the agent and frees after it.
    m_localModel = new QComboBox(this);
    m_localModel->setObjectName(QStringLiteral("localModel"));
    m_localModel->setToolTip(i18nc("@info:tooltip",
                                   "A model of LM Studio that Orcinus starts before the agent and frees again when no conversation uses it. "
                                   "The agent must be set up to use it (for OpenCode, Orcinus does that in the environment)."));
    m_localModel->addItem(i18nc("@item:inlistbox no local model", "None"), QString());
    if (const WhaleAiLocalProvider *provider = lmStudio()) {
        for (const WhaleAiLocalModel &model : provider->models) {
            QStringList details;
            const QString build = QStringList{model.params, model.quantization}.join(QLatin1Char(' ')).trimmed();
            if (!build.isEmpty()) {
                details.append(build);
            }
            if (model.sizeBytes > 0) {
                details.append(i18nc("@item size in gigabytes", "%1 GB", QLocale().toString(model.sizeBytes / 1e9, 'f', 1)));
            }
            if (!model.toolUse) {
                details.append(i18nc("@item of a language model", "no tool calls"));
            }
            const QString text =
                details.isEmpty() ? model.name : i18nc("@item %1 is a model name, %2 details", "%1 – %2", model.name, details.join(QStringLiteral(", ")));
            m_localModel->addItem(text, model.id);
            m_localModel->setItemData(m_localModel->count() - 1, model.id, Qt::ToolTipRole);
        }
    }
    if (agent.usesLocalModel() && m_localModel->findData(agent.localModel) < 0) {
        m_localModel->addItem(i18nc("@item %1 is a model id", "%1 (not found)", agent.localModel), agent.localModel);
    }
    m_localModel->setCurrentIndex(std::max(0, m_localModel->findData(agent.usesLocalModel() ? agent.localModel : QString())));
    form->addRow(i18nc("@label:listbox", "Local model:"), m_localModel);

    m_contextLength = new QSpinBox(this);
    m_contextLength->setObjectName(QStringLiteral("contextLength"));
    m_contextLength->setRange(0, 1 << 20);
    m_contextLength->setSingleStep(4096);
    m_contextLength->setSpecialValueText(i18nc("@item:inrange", "LM Studio's default"));
    m_contextLength->setSuffix(i18nc("@item:valuesuffix", " tokens"));
    m_contextLength->setToolTip(i18nc("@info:tooltip", "How much of the conversation the model keeps in mind. More takes more memory of the graphics card."));
    m_contextLength->setValue(agent.usesLocalModel() ? agent.localContextLength : 0);
    form->addRow(i18nc("@label:spinbox", "Context length:"), m_contextLength);

    m_localState = new QLabel(this);
    m_localState->setObjectName(QStringLiteral("localState"));
    m_localState->setWordWrap(true);
    form->addRow(QString(), m_localState);

    if (const std::optional<WhaleAiLocalModel> model = chosenModel(); model && agent.name == WhaleAiAgents::localAgentName(*model)) {
        m_generatedName = agent.name;
    }

    m_environment = new QPlainTextEdit(agent.environment.join(QLatin1Char('\n')), this);
    m_environment->setObjectName(QStringLiteral("environment"));
    m_environment->setPlaceholderText(i18nc("@info:placeholder", "NAME=value, one per line"));
    m_environment->setTabChangesFocus(true);
    m_environment->setFixedHeight(m_environment->fontMetrics().lineSpacing() * 5);
    form->addRow(i18nc("@label:textbox", "Environment:"), m_environment);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_buttons);

    connect(m_name, &QLineEdit::textChanged, this, &WhaleAiAgentDialog::updateState);
    connect(m_command, &QLineEdit::textChanged, this, &WhaleAiAgentDialog::updateState);
    connect(m_environment, &QPlainTextEdit::textChanged, this, &WhaleAiAgentDialog::updateState);
    connect(m_localModel, &QComboBox::currentIndexChanged, this, [this] {
        // A model of its own: its own default length.
        if (const std::optional<WhaleAiLocalModel> model = chosenModel()) {
            const QSignalBlocker blocker(m_contextLength);
            m_contextLength->setValue(WhaleAiAgents::defaultContextLength(*model));
        }
        applyLocalModel();
    });
    connect(m_contextLength, &QSpinBox::valueChanged, this, &WhaleAiAgentDialog::applyLocalModel);
    updateState();
    resize(qMax(sizeHint().width(), 520), sizeHint().height());
}

WhaleAiAgent WhaleAiAgentDialog::agent() const
{
    WhaleAiAgent agent = m_agent;
    agent.name = m_name->text().trimmed();
    agent.commandLine = m_command->text().trimmed();
    agent.environment.clear();
    const QStringList lines = m_environment->toPlainText().split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (!line.trimmed().isEmpty()) {
            agent.environment.append(line.trimmed());
        }
    }
    agent.localModel = m_localModel->currentData().toString();
    agent.localProvider = agent.localModel.isEmpty() ? QString() : QStringLiteral("lmstudio");
    agent.localContextLength = agent.localModel.isEmpty() ? 0 : m_contextLength->value();
    return agent;
}

const WhaleAiLocalProvider *WhaleAiAgentDialog::lmStudio() const
{
    for (const WhaleAiLocalProvider &provider : m_setup.providers) {
        if (provider.id == QLatin1String("lmstudio")) {
            return &provider;
        }
    }
    return nullptr;
}

std::optional<WhaleAiLocalModel> WhaleAiAgentDialog::chosenModel() const
{
    const QString id = m_localModel->currentData().toString();
    const WhaleAiLocalProvider *provider = lmStudio();
    if (id.isEmpty() || !provider) {
        return std::nullopt;
    }
    for (const WhaleAiLocalModel &model : provider->models) {
        if (model.id == id) {
            return model;
        }
    }
    return std::nullopt;
}

void WhaleAiAgentDialog::applyLocalModel()
{
    const WhaleAiLocalProvider *provider = lmStudio();
    const std::optional<WhaleAiLocalModel> model = chosenModel();
    if (provider && model) {
        if (m_name->text().trimmed().isEmpty() || m_name->text() == m_generatedName) {
            m_generatedName = WhaleAiAgents::localAgentName(*model);
            m_name->setText(m_generatedName);
        }
        if (m_command->text().trimmed().isEmpty()) {
            for (const WhaleAiLocalClient &client : std::as_const(m_setup.clients)) {
                if (client.id == QLatin1String("opencode") && !client.commandLine.isEmpty()) {
                    m_command->setText(client.commandLine);
                }
            }
        }
        const WhaleAiAgent edited = agent();
        if (WhaleAiAgents::kind(edited) == WhaleAiAgentKind::OpenCode) {
            const QStringList environment = WhaleAiAgents::withOpenCodeConfig(edited.environment, *provider, *model, edited.localContextLength);
            if (environment != edited.environment) {
                m_environment->setPlainText(environment.join(QLatin1Char('\n')));
            }
        }
    }
    updateState();
}

void WhaleAiAgentDialog::updateState()
{
    const WhaleAiAgent edited = agent();
    QString error;
    const WhaleAcpSession::AgentCommand command = edited.command(&error);
    if (edited.commandLine.isEmpty()) {
        m_commandState->clear();
    } else if (command.program.isEmpty()) {
        m_commandState->setText(error);
    } else {
        m_commandState->setText(i18nc("@info %1 is a path", "Runs %1", command.program));
    }
    // The local model: what may keep it from working.
    const bool local = !m_localModel->currentData().toString().isEmpty();
    m_contextLength->setEnabled(local);
    QStringList notes;
    const std::optional<WhaleAiLocalModel> model = chosenModel();
    if (local && !lmStudio()) {
        notes.append(i18nc("@info", "LM Studio was not found."));
    } else if (local && !model) {
        notes.append(i18nc("@info", "LM Studio has no model with this id."));
    } else if (model && !model->toolUse) {
        notes.append(i18nc("@info", "This model was not trained for tool calls, which an agent needs."));
    }
    if (edited.commandLine.contains(QLatin1String("/_npx/"))) {
        notes.append(i18nc("@info", "The program is in npx' cache, which can be cleaned up at any time."));
    }
    if (local && WhaleAiAgents::kind(edited) == WhaleAiAgentKind::OpenCode) {
        bool own = false;
        for (const QString &entry : std::as_const(edited.environment)) {
            if (entry.section(QLatin1Char('='), 0, 0).trimmed() == OpenCodeConfigName && !entry.contains(QLatin1String("\"lmstudio/"))) {
                own = true;
            }
        }
        if (own) {
            notes.append(i18nc("@info", "The OpenCode configuration in the environment is your own: choose the model there too."));
        }
    } else if (local && edited.commandLine.isEmpty()) {
        for (const WhaleAiLocalClient &client : std::as_const(m_setup.clients)) {
            if (client.id == QLatin1String("opencode") && client.commandLine.isEmpty()) {
                notes.append(i18nc("@info", "OpenCode runs local models as an agent; it is not installed (npm i -g opencode-ai)."));
            }
        }
    }
    m_localState->setText(notes.join(QLatin1Char('\n')));
    m_localState->setVisible(!notes.isEmpty());

    // A command that is not found yet may be installed later: only name and command are needed.
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(!edited.name.isEmpty() && !edited.commandLine.isEmpty());
}

#include "moc_whaleaiagentdialog.cpp"
