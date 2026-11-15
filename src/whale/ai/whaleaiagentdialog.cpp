/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaiagentdialog.h"

#include <KLocalizedString>

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

WhaleAiAgentDialog::WhaleAiAgentDialog(const WhaleAiAgent &agent, QWidget *parent)
    : QDialog(parent)
    , m_agent(agent)
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
    return agent;
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
    // A command that is not found yet may be installed later: only name and command are needed.
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(!edited.name.isEmpty() && !edited.commandLine.isEmpty());
}

#include "moc_whaleaiagentdialog.cpp"
