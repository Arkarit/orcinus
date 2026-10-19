/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEDEVTOOLBARS_H
#define WHALEDEVTOOLBARS_H

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

class DolphinMainWindow;
class DolphinViewContainer;
class KToolBar;
class QAction;

/**
 * @brief The toolbars of a main window for the development automation (multi-toolbar
 * plan, TB6): their state, a click on one of their buttons and Configure Toolbars.
 *
 * Development only, like WhaleDevAutomation. It reads what the user sees: the toolbars'
 * buttons, their menus and the widgets of KDE's dialog. It never creates the actions of a
 * File View Toolbar that has not been shown (they are created lazily, see
 * WhaleFileViewToolBar), and it changes the configuration only through the dialog.
 */
namespace WhaleDevToolbars
{
/**
 * @return The instance ID of the file view @p container: "view-N", unique in the process and
 *         the same for the container's life. Given when the automation first sees the
 *         container (a property of the container that only the automation sets), never
 *         derived from its place (pane, tab, split side).
 */
QString instanceId(DolphinViewContainer *container);

struct Instance {
    DolphinViewContainer *container;
    int tab;
    int pane;
};

/**
 * @return The file views of all tabs of @p window, with their tab and pane index.
 */
QList<Instance> instances(DolphinMainWindow *window);

/**
 * @return The main toolbar: {visible, layout, actions}.
 */
QJsonObject globalState(DolphinMainWindow *window);

/**
 * @return The File View Toolbar of @p container: {instanceId, ownerView, visible, shown,
 *         initialized, layout, actions, locationBar}. Without actions while it has none
 *         (never shown).
 */
QJsonObject fileViewState(DolphinViewContainer *container, const QString &ownerView);

/**
 * @return The location bar of @p container (location bar plan, LB4): {exists, connected (the
 *         view's visible navigator), present (placed in the toolbar), visible, editable, url,
 *         text (of the editor while editable), placeholder, focused (the window's focus
 *         widget is in it, as focus.role), active (of the active
 *         view), widgetKey (the same for the widget's life)}. Creates nothing.
 */
QJsonObject locationBarState(DolphinViewContainer *container);

/**
 * Enters @p text into the location bar of @p container as the user does: a click on its
 * toggle button if it is not editable, then the text typed over the editor's text, and with
 * @p submit Return. Fails if the location bar is not placed, not shown or not editable.
 */
QJsonObject locationBarInput(DolphinViewContainer *container, const QString &text, bool submit);

/**
 * @return The file views of all tabs, without their actions: {instanceId, tab, pane,
 *         visible, initialized, locationBar: {exists, connected, present, widgetKey}}.
 */
QJsonArray instancesState(DolphinMainWindow *window);

/**
 * @return Configure Toolbars of @p window: {open: false}, or {open, selected, toolBars,
 *         available, current, buttons} with the actions' IDs as the dialog has them.
 */
QJsonObject dialogState(DolphinMainWindow *window);

/**
 * Clicks the button of @p action in @p toolBar with mouse events through the window, as
 * the user does; or, with @p menuEntry, opens its menu as the user does and chooses the
 * entry (its ID or text; "Submenu > Entry" for an entry of a submenu). Returns when the click
 * or the entry has been handled. @p disabledReason explains a disabled action.
 */
QJsonObject click(KToolBar *toolBar, QAction *action, const QString &menuEntry, const QString &disabledReason);

/**
 * Operates Configure Toolbars of @p window through its widgets, see WhaleDevAutomation::configureToolbars().
 */
QJsonObject configure(DolphinMainWindow *window, const QJsonObject &request);

/**
 * @return Whether the action @p name may change files or the clipboard (refused without
 *         WHALE_DEV_MCP_FILE_OPERATIONS=1).
 */
bool changesFiles(const QString &name);
}

#endif
