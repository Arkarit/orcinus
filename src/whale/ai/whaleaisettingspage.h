/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAISETTINGSPAGE_H
#define WHALEAISETTINGSPAGE_H

#include "settings/settingspagebase.h"
#include "whaleaiagents.h"
#include "whaleaiagenttest.h"

class QLabel;
class QListWidget;
class QMenu;
class QPushButton;

/**
 * @brief The "AI Agents" page of the settings dialog (AI2).
 *
 * The agents the AI panel can talk to: add a known one (Claude, Codex, OpenCode, as found on
 * this system) or any other program that speaks ACP, edit, remove, choose the default, and test
 * one: it is started without a prompt and shows what it reports, or why it does not work.
 *
 * Nothing is sent anywhere before the user writes in the AI panel. "Defaults" leaves the
 * agents alone: they are the user's data, like the profiles.
 */
class WhaleAiSettingsPage : public SettingsPageBase
{
    Q_OBJECT

public:
    explicit WhaleAiSettingsPage(QWidget *parent = nullptr);
    ~WhaleAiSettingsPage() override;

    void applySettings() override;
    void restoreDefaults() override;

    /** @return The agents as shown, not applied yet. */
    QList<WhaleAiAgent> agents() const;
    QString defaultAgentId() const;

    /** For tests: where the known agents are looked for (empty: PATH). */
    void setPresetSearchPaths(const QStringList &paths);
    /** For tests: how long the test waits for the agent. */
    void setTestTimeout(int milliseconds);

    /** @return The text of a test result, as the page shows it (rich text). */
    static QString resultText(const WhaleAiAgentTest::Result &result);

private:
    void fillList(const QString &currentId);
    void fillAddMenu();
    void updateButtons();
    int currentRow() const;
    void addPreset(const WhaleAiAgentPreset &preset);
    void addCustom();
    void editCurrent();
    void removeCurrent();
    void makeCurrentDefault();
    void testCurrent();
    void setAgentsChanged();

    QList<WhaleAiAgent> m_agents;
    QString m_defaultId;
    QListWidget *m_list = nullptr;
    QMenu *m_addMenu = nullptr;
    QPushButton *m_add = nullptr;
    QPushButton *m_edit = nullptr;
    QPushButton *m_remove = nullptr;
    QPushButton *m_default = nullptr;
    QPushButton *m_test = nullptr;
    QLabel *m_result = nullptr;
    WhaleAiAgentTest *m_tester = nullptr;
    QStringList m_presetSearchPaths;
    int m_testTimeout = 30000;
};

#endif
