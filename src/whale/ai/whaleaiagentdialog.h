/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAIAGENTDIALOG_H
#define WHALEAIAGENTDIALOG_H

#include "whaleaiagents.h"

#include <QDialog>

class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;

/**
 * @brief Adds or edits one AI agent: name, command line and environment (AI2).
 */
class WhaleAiAgentDialog : public QDialog
{
    Q_OBJECT

public:
    explicit WhaleAiAgentDialog(const WhaleAiAgent &agent, QWidget *parent = nullptr);

    /** @return The agent as edited; the id is the one given. */
    WhaleAiAgent agent() const;

private:
    void updateState();

    WhaleAiAgent m_agent;
    QLineEdit *m_name = nullptr;
    QLineEdit *m_command = nullptr;
    QLabel *m_commandState = nullptr;
    QPlainTextEdit *m_environment = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
};

#endif
