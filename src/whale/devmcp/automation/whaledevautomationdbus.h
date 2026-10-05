/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEDEVAUTOMATIONDBUS_H
#define WHALEDEVAUTOMATIONDBUS_H

#include <QDBusContext>
#include <QObject>

class WhaleDevAutomation;

/**
 * @brief The D-Bus face of WhaleDevAutomation.
 *
 * Exactly the methods below are exported, nothing else of Whale. Results are JSON
 * strings in the format of WhaleDevAutomation. A pane of -1 means the active pane.
 */
class WhaleDevAutomationDBus : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.arkarit.whale.DevAutomation")

public:
    explicit WhaleDevAutomationDBus(WhaleDevAutomation *automation);

public Q_SLOTS:
    Q_SCRIPTABLE QString UiState();
    Q_SCRIPTABLE QString FocusState();
    Q_SCRIPTABLE QStringList ActionIds();
    Q_SCRIPTABLE QString SetView(int pane, const QString &viewId);
    Q_SCRIPTABLE QString ActivatePane(int pane);
    Q_SCRIPTABLE QString SetSplitView(bool enabled);
    Q_SCRIPTABLE QString SetLocation(int pane, const QString &url);
    Q_SCRIPTABLE QString SetFilterText(int pane, const QString &text);
    Q_SCRIPTABLE QString ClearFilter(int pane);
    Q_SCRIPTABLE QString SetHiddenFiles(int pane, bool shown);
    /// @p foldersFirst is "true", "false" or "" (unchanged); empty @p field or @p order are unchanged too.
    Q_SCRIPTABLE QString SetSort(int pane, const QString &field, const QString &order, const QString &foldersFirst);
    Q_SCRIPTABLE QString TriggerAction(const QString &actionId);
    /// @p current may be empty (keep the current item).
    Q_SCRIPTABLE QString SetSelection(int pane, const QStringList &urls, const QString &current);
    Q_SCRIPTABLE QString ActivateItems(int pane, const QStringList &urls);
    /// @p url may be empty (the location's menu). Returns before the menu closes.
    Q_SCRIPTABLE QString OpenContextMenu(int pane, const QString &url);
    Q_SCRIPTABLE QString ClosePopups();
    Q_SCRIPTABLE QString ScrollToItem(int pane, const QString &url);
    Q_SCRIPTABLE QString Quit();
    Q_SCRIPTABLE QString RequestItemInfo(int pane, const QString &url);
    Q_SCRIPTABLE QString AnswerRenameDialog(const QString &newName);
    Q_SCRIPTABLE QString CloseDialogs();
    Q_SCRIPTABLE QString ActionState();
    /// Replies when the conditions (a JSON object) hold or after @p timeoutMs (at most 60000).
    Q_SCRIPTABLE QString WaitForState(const QString &conditions, int timeoutMs);
    Q_SCRIPTABLE QByteArray Screenshot();
    Q_SCRIPTABLE QString PreviewState();
    Q_SCRIPTABLE QString RequestPreview(int pane, const QStringList &urls, int size);
    /// @p targetUrl may be empty (the location); @p action is copy, move, link or ask.
    Q_SCRIPTABLE QString PerformDragDrop(int sourcePane, int targetPane, const QString &targetUrl, const QString &action);
    Q_SCRIPTABLE QString ChoosePopupEntry(const QString &text);

private:
    WhaleDevAutomation *const m_automation;
};

#endif
