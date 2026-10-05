/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEDEVAUTOMATION_H
#define WHALEDEVAUTOMATION_H

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>

#include <functional>
#include <optional>

class DolphinMainWindow;
class DolphinTabWidget;
class DolphinViewContainer;
class QWidget;

/**
 * @brief Development-only introspection and control of a Whale main window.
 *
 * Exists only in builds with WHALE_ENABLE_DEV_MCP and is only started when the
 * environment variable WHALE_DEV_MCP is "1". It reports the semantic UI state
 * (panes, views, focus, location, sorting, filter, hidden files, selection) as
 * JSON and offers a fixed set of non-destructive commands for UI tests.
 *
 * It is not an API: names, JSON layout and behavior may change at any time.
 * It deliberately offers no file operations, no generic object access and no
 * way to call arbitrary methods.
 *
 * Panes are the view areas of the current tab: 0 is the primary (left) one,
 * 1 the secondary (right) one in split view. -1 means the active pane.
 * Every command returns {"ok": true} or {"ok": false, "error": "..."}.
 */
class WhaleDevAutomation : public QObject
{
    Q_OBJECT

public:
    /**
     * The D-Bus object path and interface, on the session bus name of the instance.
     */
    static QString objectPath();
    static QString interfaceName();

    /**
     * @return True if the environment asks for the automation (WHALE_DEV_MCP=1).
     */
    static bool isRequested();

    /**
     * Creates the automation for @p window and offers it on D-Bus, if requested.
     * The automation is a child of the window.
     */
    static void startIfRequested(DolphinMainWindow *window);

    explicit WhaleDevAutomation(DolphinMainWindow *window);
    ~WhaleDevAutomation() override;

    QJsonObject uiState() const;
    QJsonObject focusState() const;

    QJsonObject setView(int pane, const QString &viewId);
    QJsonObject activatePane(int pane);
    QJsonObject setSplitView(bool enabled);
    QJsonObject setLocation(int pane, const QString &url);
    QJsonObject setFilterText(int pane, const QString &text);
    QJsonObject clearFilter(int pane);
    QJsonObject setHiddenFiles(int pane, bool shown);
    QJsonObject setSort(int pane, const QString &field, const QString &order, std::optional<bool> foldersFirst);
    QJsonObject triggerAction(const QString &actionId);

    /**
     * Sets the selection of a pane through the same host API as plugin views
     * (DolphinViewBridge::setSelection). URLs outside the listing are ignored and
     * reported in "ignored"; an empty @p current keeps the current item.
     */
    QJsonObject setSelection(int pane, const QStringList &urls, const QString &current);

    /**
     * Opens items of a pane through the same host API as plugin views, like Enter.
     */
    QJsonObject activateItems(int pane, const QStringList &urls);

    /**
     * Opens the context menu of a pane through the host API that plugin views use,
     * for @p url (an item of the listing) or, if empty, for the location. Returns at
     * once; the menu is part of the state ("popupMenu") until closePopups().
     */
    QJsonObject openContextMenu(int pane, const QString &url);
    QJsonObject closePopups();

    /**
     * Reports @p url (or none, if empty) as the item under the pointer, through the
     * host API that plugin views use for hovering.
     */
    QJsonObject requestItemInfo(int pane, const QString &url);

    /**
     * Enters @p newName in the open rename dialog and confirms it. Only with
     * WHALE_DEV_MCP_FILE_OPERATIONS=1.
     */
    QJsonObject answerRenameDialog(const QString &newName);

    /**
     * Rejects all open dialogs (rename, properties, ...).
     */
    QJsonObject closeDialogs();

    /**
     * @return Enabled state and gating reason of the actions the view gating may
     *         change, plus Paste, Properties and Show Hidden Files.
     */
    QJsonObject actionState() const;

    /**
     * @return The IDs accepted by triggerAction(). None of them changes files.
     */
    static QStringList actionIds();

    /**
     * Compares @p conditions, a map from a path into uiState() (for example
     * "panes.1.viewId" or "focus.role") to the expected JSON value, or to
     * {"$contains": x} for a string containing x or an array containing x.
     * @return {"matched": bool, "mismatches": [{"path", "expected", "actual"}]}
     */
    QJsonObject evaluate(const QJsonObject &conditions) const;

    /**
     * Calls @p done as soon as all @p conditions hold, or after @p timeoutMs.
     * The result is evaluate()'s plus "timedOut", "elapsedMs" and "state".
     */
    void waitForState(const QJsonObject &conditions, int timeoutMs, std::function<void(const QJsonObject &)> done);

    /**
     * @return A PNG image of the main window, rendered by Whale itself.
     */
    QByteArray screenshot() const;

private:
    DolphinTabWidget *tabWidget() const;
    QList<DolphinViewContainer *> panes() const;
    DolphinViewContainer *pane(int index, QString *error) const;
    int paneIndex(const DolphinViewContainer *container) const;
    QJsonValue popupMenu() const;
    QJsonValue clipboardState() const;
    QJsonArray dialogs() const;
    QJsonObject paneState(DolphinViewContainer *container, int index) const;
    QString focusRole(QWidget *focus, int *paneIndex) const;
    void recordActivation(DolphinViewContainer *container);

    DolphinMainWindow *const m_window;

    struct Activation {
        int seq;
        int tab;
        int pane;
        QString focusRole;
    };
    int m_activationCount = 0;
    QList<Activation> m_recentActivations;

    // What the shell (information panel, KFileItemActions) last got as selection.
    QStringList m_shellSelection;
    int m_shellSelectionSignals = 0;
    QString m_itemInfo;
    int m_itemInfoSignals = 0;
};

#endif
