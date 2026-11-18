/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAIAGENTDIALOG_H
#define WHALEAIAGENTDIALOG_H

#include "whaleaiagents.h"

#include <QDialog>

#include <optional>

class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;

/**
 * @brief Adds or edits one AI agent: name, command line and environment (AI2), and the local
 *        model Orcinus starts for it (AI12).
 */
class WhaleAiAgentDialog : public QDialog
{
    Q_OBJECT

public:
    /** @p setup: the local models to offer; found on this computer if not given. */
    explicit WhaleAiAgentDialog(const WhaleAiAgent &agent, QWidget *parent = nullptr, const std::optional<WhaleAiLocalSetup> &setup = std::nullopt);

    /** @return The agent as edited; the id is the one given. */
    WhaleAiAgent agent() const;

private:
    void updateState();
    /** The local model was chosen or its context length changed: name, command and OpenCode's configuration follow. */
    void applyLocalModel();
    const WhaleAiLocalProvider *lmStudio() const;
    /** @return The chosen model if LM Studio has it. */
    std::optional<WhaleAiLocalModel> chosenModel() const;

    WhaleAiAgent m_agent;
    WhaleAiLocalSetup m_setup;
    QString m_generatedName; ///< the name the dialog gave; replaced when the model changes
    QComboBox *m_localModel = nullptr;
    QSpinBox *m_contextLength = nullptr;
    QLabel *m_localState = nullptr;
    QLineEdit *m_name = nullptr;
    QLineEdit *m_command = nullptr;
    QLabel *m_commandState = nullptr;
    QPlainTextEdit *m_environment = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
};

#endif
