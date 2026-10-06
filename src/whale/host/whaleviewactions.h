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

class DolphinView;
class DolphinViewActionHandler;
class KActionCollection;
class KSelectAction;
class KToggleAction;
class QAction;
class QActionGroup;
class WhaleViewHost;
class WhaleTreeCache;
class WhaleViewRegistry;

/**
 * @brief The view actions of a main window: the view switcher and the gating of
 * Dolphin's actions for plugin views.
 *
 * Owns the WhaleViewRegistry of the window and switches the view of the
 * active view area between the built-in file list and the plugin views.
 * Views are identified by their plugin ID, never by their display name.
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
 * Content scope (F1): "Include Subfolders" (whale_include_subfolders) switches the file
 * list of the active view area between the location's items and the files below it. Paste
 * into a view area that shows the files below its location is refused with an explanation.
 */
class WhaleViewActions : public QObject
{
    Q_OBJECT

public:
    /**
     * Adds the view switcher to @p actionCollection and to its "view_settings" menu,
     * so it must be created after the actions of the DolphinViewActionHandler.
     */
    WhaleViewActions(KActionCollection *actionCollection, DolphinViewActionHandler *actionHandler, QObject *parent);
    ~WhaleViewActions() override;

    /**
     * Sets the host of the active view area. The switcher shows and changes its view.
     */
    void setCurrentHost(WhaleViewHost *host);

    WhaleViewRegistry *registry() const;

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
    void slotViewSelected(QAction *action);
    void updateCheckedView();
    void updateAvailableViews();
    void updateGates();
    void slotRename();
    void slotViewModeTriggered();
    void notifyCommandSelection();
    void updateContentScopeAction();

    WhaleViewRegistry *m_registry;
    WhaleTreeCache *m_treeCache;
    KSelectAction *m_switcher;
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
};

#endif
