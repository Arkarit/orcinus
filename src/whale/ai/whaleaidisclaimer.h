/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAIDISCLAIMER_H
#define WHALEAIDISCLAIMER_H

#include <QWidget>

class QCheckBox;
class QPushButton;

/** Says when the AI disclaimer was accepted, wherever it was shown (AI5f). */
class WhaleAiDisclaimerNotifier : public QObject
{
    Q_OBJECT

public:
    static WhaleAiDisclaimerNotifier *instance();

Q_SIGNALS:
    void accepted();
};

/**
 * @brief What the user should know before using AI agents (AI5f), until they accept it.
 *
 * Shown over the AI panel and on the "AI Agents" settings page, each the first time it opens;
 * accepting it in one place hides it in both (WhaleAiDisclaimerNotifier). The text ends with the
 * warranty and liability terms of the GPL 2 (sections 11 and 12), with the license one click
 * away.
 *
 * The accepted state is stored in orcinusrc [AI] (WhaleAiAgents::disclaimerAccepted()).
 */
class WhaleAiDisclaimer : public QWidget
{
    Q_OBJECT

public:
    explicit WhaleAiDisclaimer(QWidget *parent = nullptr);

private:
    void showLicense();

    QCheckBox *m_understood = nullptr;
    QPushButton *m_continue = nullptr;
};

#endif
