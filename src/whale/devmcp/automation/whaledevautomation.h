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
class QMimeData;
class QWidget;
class WhaleViewActions;

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
    /// LK6: the lock of the split view of the current tab.
    QJsonValue splitLockState() const;
    /// ST3: the styles, their actions and what they set.
    QJsonObject stylesState() const;
    QJsonObject focusState() const;

    /**
     * Shows @p viewId in a pane, as an automatic change (nothing is remembered). With
     * @p remember like the user's choice of the view's View menu entry (for the file list:
     * the entry of its mode), which makes it the preferred view of the location (O3).
     */
    QJsonObject setView(int pane, const QString &viewId, bool remember = false);
    /// O3: the preferred view of @p url ("view" null if there is none) and its location key.
    QJsonObject preferredView(const QString &url) const;
    /// O3: @p viewId "dolphin" or a plugin ID; an empty one forgets the preference.
    QJsonObject setPreferredView(const QString &url, const QString &viewId);
    /// Treats the view plugin @p viewId as missing (or back) in this window's registry.
    QJsonObject setViewPluginAvailable(const QString &viewId, bool available);
    QJsonObject activatePane(int pane);
    QJsonObject setSplitView(bool enabled);
    /**
     * Moves the handle between the two halves of the split view, as the user drags it:
     * the left half gets @p leftShare (0.1 to 0.9) of the width.
     */
    QJsonObject setSplitRatio(double leftShare);
    /// LK6: links or unlinks the views of the split view through the lock button, as the user does.
    QJsonObject setSplitLock(bool locked);
    /// ST3: applies a style through its action, as its menu entry or shortcut does.
    QJsonObject applyStyle(const QString &id);
    /// ST3: saves the current state of the window as a style (as "Save as Style…" without the dialog);
    /// without a name, the style applied or saved last ("Save Style").
    QJsonObject saveStyle(const QString &name);
    /// Resizes the main window, as the user does by dragging its border.
    QJsonObject setWindowSize(int width, int height);
    QJsonObject setLocation(int pane, const QString &url);
    QJsonObject setFilterText(int pane, const QString &text);
    QJsonObject clearFilter(int pane);
    QJsonObject setHiddenFiles(int pane, bool shown);
    /// F1: @p scope "current-folder" or "recursive-files" for the file list of a pane, through
    /// "Show flat (no folders)" as the user chooses it (remembered for the location).
    QJsonObject setContentScope(int pane, const QString &scope);
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
     * Scrolls a plugin item view so that the item @p url is at its top (a view state
     * that the view saves with saveState()).
     */
    QJsonObject scrollToItem(int pane, const QString &url);

    /**
     * Closes the main window as the user does, after answering: Whale saves its session
     * and quits. For restart tests.
     */
    QJsonObject quit();

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
     * Clicks the button @p actionId of a toolbar with mouse events through the window, as the
     * user does (TB6): @p toolBar is "global" (the main toolbar, acting on the active view),
     * a pane index of the current tab, "active", or the instanceId of a file view (any tab).
     * With @p menuEntry, opens the button's menu as the user does and chooses that entry (ID
     * or text, "Submenu > Entry" for an entry of a submenu). Fails for an unknown toolbar or
     * action, an action not in the toolbar, a disabled action or entry, a hidden toolbar,
     * and for actions that change files unless WHALE_DEV_MCP_FILE_OPERATIONS=1.
     */
    QJsonObject triggerToolbarAction(const QString &toolBar, const QString &actionId, const QString &menuEntry);

    /**
     * Enters @p text into the location bar of a file view as the user does (location bar plan,
     * LB4): a click on its toggle button if it shows the breadcrumb, the text typed over the
     * editor's, and with @p submit Return. @p toolBar is a pane index of the current tab,
     * "active", or the instanceId of a file view. Fails for an unknown pane or instanceId, and
     * if the location bar is not in its File View Toolbar or not shown.
     */
    QJsonObject locationBarInput(const QString &toolBar, const QString &text, bool submit);

    /**
     * Operates Configure Toolbars through its widgets, as the user does (TB6). @p request may
     * hold, done in this order: "open" (true; with "select" the toolbar chosen when it opens),
     * "select" ("global" or "file-view"), "remove" and "insert" (action IDs, or "-"/"|" for a
     * separator/spacer; inserted at the end), "defaults" (KDE's Defaults, its question
     * answered with Reset), "apply", "ok", "cancel". The result has the dialog's state.
     */
    QJsonObject configureToolbars(const QJsonObject &request);

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

    /// V1d: the built-in viewer ("viewer" of uiState()): open or not, the file, its list, the settings.
    QJsonObject viewerState() const;
    /**
     * V1d: input for the built-in viewer as the user gives it, events sent to its window:
     * @p action "key" (@p argument as in QKeySequence, e.g. "Right", "PgDown", "Escape"),
     * "wheel" (@p argument the angle, 120 a notch, negative is down = next) or "click"
     * (@p argument "left", "center" or "right": a click into that part of the window).
     */
    QJsonObject viewerInput(const QString &action, const QString &argument);
    /**
     * V1d: changes the viewer's settings, as the settings page does: @p options with
     * "wrapAround" (bool), "mimeTypes" (list) and the hidden "multiSelection" ("keep" or "move").
     */
    QJsonObject setViewerOptions(const QJsonObject &options);
    /// V1d: a PNG image of the open viewer, rendered by Whale itself; empty without one.
    QByteArray viewerScreenshot() const;

    /**
     * @return The preview state of all panes ("previews" of uiState()) and the results
     *         of requestPreview().
     */
    QJsonObject previewState() const;

    /**
     * Requests previews of @p urls (items of the pane) with @p size through the preview
     * provider of the pane's plugin view, as the view would. The answers are listed in
     * previewState() as "results".
     */
    QJsonObject requestPreview(int pane, const QStringList &urls, int size);

    /**
     * Drags the selection of @p sourcePane and drops it onto the item @p targetUrl (a
     * folder; empty: the location) of @p targetPane, with the modifier keys of @p action
     * ("copy": Ctrl, "move": Shift, "link": Ctrl+Shift, "ask": none, which opens the drop
     * menu). Only the transport of the drag differs from the user's: a plugin item view
     * gets the drag gesture (press, move) and starts its drag through its file operations;
     * for the file list the drag data comes from the same function as its own drags. The
     * target widget gets the drag enter, move and drop events and handles them itself.
     * Only with WHALE_DEV_MCP_FILE_OPERATIONS=1.
     */
    QJsonObject performDragDrop(int sourcePane, int targetPane, const QString &targetUrl, const QString &action);

    /**
     * Chooses the entry with the text @p text (without "&", with or without the shortcut
     * after a tab) of the open popup menu, e.g. "Copy Here" in the drop menu. Only with WHALE_DEV_MCP_FILE_OPERATIONS=1, as menu
     * entries may change files.
     */
    QJsonObject choosePopupEntry(const QString &text);

    /**
     * Input for the treemap view (plugin whale_treemap) as the user gives it: real mouse
     * and key events sent to its canvas, at the cell of @p url. @p action is one of
     * hover (leave without url), select (click, then Up until the folder is selected),
     * activate (select, then Enter), double_click, wheel_in, wheel_out, key (@p key as
     * in QKeySequence, e.g. "Up", "Delete", "Ctrl+C"; shortcuts of the window apply).
     */
    QJsonObject treemapInput(int pane, const QString &action, const QString &url, const QString &key);
    /**
     * Sets the treemap's display options through its own controls: @p colorMode
     * "fileType" or "topFolder" (empty: unchanged), @p labels "true", "false" or "".
     */
    QJsonObject treemapSetOptions(int pane, const QString &colorMode, const QString &labels);

    /**
     * Carries out @p operation on @p urls through the file operations of the pane's
     * plugin view (WhaleFileOperations, T1), as the view does from its own context menu;
     * the items need not be in the listing. @p operation is one of open, open_with,
     * show_in_folder, properties, rename, trash, delete, copy, cut, copy_location, or
     * supported (only reports supportedOperations()). rename, trash, delete, copy, cut
     * and copy_location only with WHALE_DEV_MCP_FILE_OPERATIONS=1. Returns at once; the
     * end of an operation that changes files is "fileOperations" in the pane's state.
     */
    QJsonObject fileOperation(int pane, const QString &operation, const QStringList &urls);

    /**
     * Presses the button @p text (without "&") of an open dialog, e.g. "Delete" of KIO's
     * confirmation. Only with WHALE_DEV_MCP_FILE_OPERATIONS=1, as it may confirm a deletion.
     */
    QJsonObject answerDialog(const QString &text);

private:
    DolphinTabWidget *tabWidget() const;
    QList<DolphinViewContainer *> panes() const;
    DolphinViewContainer *pane(int index, QString *error) const;
    /// A file view by pane index, "active" or instanceId (any tab).
    DolphinViewContainer *fileView(const QString &argument, QString *error) const;
    int paneIndex(const DolphinViewContainer *container) const;
    QJsonValue popupMenu() const;
    /// The view modes and view options as the View menus offer them.
    QJsonValue viewMenuState() const;
    QJsonValue treeCacheState() const;
    WhaleViewActions *windowViewActions() const;
    QJsonValue clipboardState() const;
    QJsonArray dialogs() const;
    QJsonObject paneState(DolphinViewContainer *container, int index) const;
    /// F1: the content scope of the pane's file list and its recursive content.
    QJsonObject contentScopeState(DolphinViewContainer *container) const;
    /// Window positions of the file list's fully shown items and of its background, for real input.
    QJsonValue fileListPoints(DolphinViewContainer *container) const;
    QJsonValue panePreviews(DolphinViewContainer *container) const;
    /// What the file operations of the pane's plugin view have done (T1), see fileOperation().
    QJsonValue paneFileOperations(DolphinViewContainer *container) const;
    /// The source and the items of Whale's actions on selected items in the pane (T2).
    QJsonObject commandState(DolphinViewContainer *container) const;
    void watchFileOperations(QObject *operations) const;
    /// Delivers a drag with @p data to the view of @p target, see performDragDrop().
    QJsonObject deliverDrag(DolphinViewContainer *target, const QUrl &item, const QMimeData *data, Qt::KeyboardModifiers modifiers);
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
    // Time stamps of synthesized input (treemap tools).
    int m_inputTime = 0;
    QUrl m_itemInfoUrl;
    bool m_itemInfoIsDir = false;
    int m_itemInfoSignals = 0;

    // Answers to requestPreview(), newest last.
    QJsonArray m_previewResults;
    QList<QPointer<QObject>> m_watchedProviders;

    // The finished operations of the WhaleFileOperations of plugin views (connected when
    // the state is read or an operation is started).
    struct FileOperationRecord {
        int count = 0;
        QJsonObject last;
    };
    mutable QHash<const QObject *, FileOperationRecord> m_fileOperations;
};

#endif
