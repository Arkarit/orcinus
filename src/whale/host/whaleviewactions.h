/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEVIEWACTIONS_H
#define WHALEVIEWACTIONS_H

#include "whalefileoperations.h"

#include <KFileItem>

#include <QHash>
#include <QMetaObject>
#include <QObject>
#include <QPointer>

#include <functional>

class DolphinView;
class DolphinViewActionHandler;
class KActionCollection;
class KActionMenu;
class KToggleAction;
class KXMLGUIClient;
class QAction;
class QActionGroup;
class WhaleViewHost;
class WhaleTreeCache;
class WhaleViewRegistry;

/**
 * @brief The view actions of a main window: the view modes and the gating of
 * Dolphin's actions for plugin views.
 *
 * Owns the WhaleViewRegistry of the window and switches the view of the
 * active view area between the built-in file list and the plugin views.
 * Views are identified by their plugin ID, never by their display name.
 *
 * View modes: the user chooses from one list, viewModeActions(): the file list's modes
 * Icons, Compact and Details (Dolphin's actions), then one entry per plugin view in the
 * registry's order. Built-in and plugin views are alike to the user; the entries are one
 * exclusive group (Dolphin's "view_mode"), so exactly the shown one is checked. Up to
 * MaxDirectViewModes entries are listed directly in the View menus, more go into one
 * "View Modes" submenu together (viewModeMenuEntries()). Entries of views that cannot show
 * the location are disabled, not hidden, and count as well.
 *
 * View options: "Show flat (no folders)", "Show Previews" and "Show Hidden Files" stay
 * visible and are disabled while the shown view does not honor them
 * (WhaleViewHost::supportsViewOption()); their settings are kept meanwhile.
 *
 * Gating: while the active view area shows a plugin view, Dolphin's actions that
 * the view cannot honor (WhaleViewCapabilities) are disabled, so they never act on
 * state the user cannot see. Each gateable action gets an action group of its own
 * that serves as the gate: Dolphin keeps enabling and disabling the action as
 * usual, a disabled group keeps it disabled, and opening the gate restores exactly
 * Dolphin's latest state (QActionGroup semantics). Actions that work correctly on
 * the mirrored selection (e.g. Copy, Move to Trash with a selection) stay as they are.
 *
 * Rename (F2) in a plugin view: views with InlineRename rename themselves, the
 * others get Dolphin's rename dialog; the DolphinView's inline editor is never used.
 *
 * Routing (T2): for a plugin view with a selection of its own (RecursiveContent) the
 * actions on selected items act on that selection (WhaleCommandRouter): Dolphin's slots
 * ask routeAction() first, Dolphin's action states are computed from the routed items,
 * and the gates follow the operations the view's file operations support for them.
 *
 * Content scope (F1): "Show flat (no folders)" (whale_include_subfolders) switches the file
 * list of the active view area between the location's items and the files below it. Paste
 * into a view area that shows the files below its location is refused with an explanation.
 */
class WhaleViewActions : public QObject
{
    Q_OBJECT

public:
    /**
     * Adds the plugin views to the view modes of @p actionCollection and arranges its
     * "view_settings" menu, so it must be created after the actions of the
     * DolphinViewActionHandler.
     */
    WhaleViewActions(KActionCollection *actionCollection, DolphinViewActionHandler *actionHandler, QObject *parent);
    ~WhaleViewActions() override;

    /**
     * Sets the host of the active view area. The switcher shows and changes its view.
     */
    void setCurrentHost(WhaleViewHost *host);

    WhaleViewRegistry *registry() const;

    /**
     * Up to this many view modes are listed directly in the View menus.
     */
    static constexpr int MaxDirectViewModes = 5;

    /**
     * @return The view modes in their order: Dolphin's "icons", "compact" and "details",
     *         then the entries of the plugin views. Each is checkable; the one of the shown
     *         view (and file list mode) is checked.
     */
    QList<QAction *> viewModeActions() const;

    /**
     * @return The entry of the view mode @p id ("icons", "compact", "details" or a plugin
     *         ID; see viewModeId()), or nullptr. For WhaleViewHost::dolphinViewId(): the entry
     *         of the file list's mode in the active view area.
     */
    QAction *viewModeAction(const QString &id) const;

    /**
     * @return The ID of the view mode @p action: the action name of a file list mode, or
     *         the plugin ID.
     */
    QString viewModeId(const QAction *action) const;

    /**
     * @return The entries the View menus list for the view modes: viewModeActions(), or with
     *         more than MaxDirectViewModes of them the "View Modes" submenu holding them all.
     */
    QList<QAction *> viewModeMenuEntries() const;

    /**
     * Puts viewModeMenuEntries() into the View menu of @p client's XML GUI (the menu bar),
     * as a group of their own before "Sort By". Again whenever the client's GUI was rebuilt.
     * (Not with an XML GUI action list: those end up at a shifted position in a menu that is
     * merged with KDE's standard View menu.)
     */
    void plugViewModes(KXMLGUIClient *client);

    /**
     * @return The tree scans shared by the views of this window.
     */
    WhaleTreeCache *treeCache() const;

    /**
     * @return Why the gating disables the action @p actionName for the shown view, or
     *         an empty string if it does not. For tests and development tools.
     */
    QString gateReason(const QString &actionName) const;

    /**
     * @return The names of the actions that the gating may disable.
     */
    static QStringList gateableActions();

    /**
     * The actions on selected items that are routed to a plugin view's own selection
     * (T2), with the operation of WhaleFileOperations each stands for. All but
     * "properties" (Dolphin keeps it enabled; without a selection it is the location's)
     * are gateable actions. Actions marked as Dolphin's (copying/moving to the other half of
     * a split view) keep Dolphin's code, which takes the routed items.
     */
    struct RoutedAction {
        WhaleFileOperations::Operation operation;
        bool byDolphin = false;
    };
    static const QHash<QString, RoutedAction> &routedActions();

    /**
     * For Dolphin's slots of the actions on selected items: carries the action
     * @p actionName out on the routed selection of the view area of @p view.
     * @return False if that view area is not routed (Dolphin's own path applies).
     *
     * F1: also asked by Dolphin's Paste ("edit_paste"); true if Paste is refused because
     * the file list of the view area shows the files below its location.
     */
    bool routeAction(DolphinView *view, const QString &actionName);

    /**
     * Dolphin's explanation of disabled actions (DisabledActionNotifier), shown when the
     * shortcut of a disabled action is pressed: @p hook(action, reason) explains @p action
     * with @p reason. An empty reason means Whale does not disable the action any longer,
     * and Dolphin's own explanation applies again.
     */
    using DisabledReasonHook = std::function<void(QAction *action, const QString &reason)>;
    void setDisabledReasonHook(const DisabledReasonHook &hook);

    /**
     * Explains the actions disabled for a view area without a destination
     * (WhaleViewHost::hasDestination()) again; for Dolphin after setting its own explanations.
     */
    void updateDisabledReasons();

    /**
     * Called by the view areas of the window when they show another view, so the
     * session (which records the shown views) is saved again.
     */
    void notifyViewChanged();

Q_SIGNALS:
    /**
     * Is emitted when a view area of the window shows another view. Like a changed
     * location, this is a reason to save the session.
     */
    void sessionChanged();

    /**
     * The selection the actions on selected items act on has changed without a selection
     * change of the DolphinView (a routed view's own selection, or the view was switched):
     * @p items are now the selection (WhaleCommandRouter::selectedItems()). Dolphin's main
     * window updates its actions with it.
     */
    void commandSelectionChanged(const KFileItemList &items);

private:
    void slotViewSelected(QAction *entry);
    void arrangeViewSettingsMenu(KActionMenu *viewSettings);
    void updateCheckedView();
    void updateAvailableViews();
    void updateGates();
    void slotRename();
    void slotViewModeTriggered();
    void notifyCommandSelection();
    void updateContentScopeAction();

    WhaleViewRegistry *m_registry;
    WhaleTreeCache *m_treeCache;
    QList<QAction *> m_viewModes;
    QHash<QString, QAction *> m_pluginViewEntries;
    KActionMenu *m_viewModesMenu = nullptr;
    QAction *m_viewModesSeparator;
    KToggleAction *m_includeSubfolders;
    KActionCollection *const m_actionCollection;
    QPointer<WhaleViewHost> m_host;
    QMetaObject::Connection m_hostConnection;
    QMetaObject::Connection m_selectionConnection;
    QMetaObject::Connection m_urlConnection;
    QMetaObject::Connection m_capabilitiesConnection;
    QMetaObject::Connection m_viewSelectionConnection;
    QMetaObject::Connection m_contentScopeConnection;
    QHash<QString, QActionGroup *> m_gates;
    QHash<QString, QString> m_gateReasons;
    DisabledReasonHook m_disabledReasonHook;
    QStringList m_explainedGates; // the actions explained as disabled without a destination
};

#endif
