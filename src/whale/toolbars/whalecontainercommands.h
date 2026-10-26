/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALECONTAINERCOMMANDS_H
#define WHALECONTAINERCOMMANDS_H

#include <QObject>
#include <QPointer>

class DolphinViewContainer;
class KActionCollection;
class KToolBarPopupAction;
class MiddleClickActionEventFilter;
class QAction;

/**
 * @brief The window's actions on a view, as actions on one fixed view.
 *
 * DolphinMainWindow's navigation actions act on the active view. These do the same on the
 * view they were created for, whichever view is active: "go_back", "go_forward" (with their
 * history menus), "go_up", "go_home", "view_redisplay", "stop", "toggle_filter",
 * "show_filter_bar", "toggle_search", "edit_select_all" and "invert_selection", with the
 * window's action names. Their state comes from that view only.
 *
 * A middle click on Back, Forward, Up, Home or an entry of a history menu opens its
 * location in a new tab, as in the window's toolbar (middleClickFilter()).
 */
class WhaleContainerCommands : public QObject
{
    Q_OBJECT

public:
    /**
     * Adds the actions to @p actionCollection.
     */
    WhaleContainerCommands(DolphinViewContainer *container, KActionCollection *actionCollection, QObject *parent);
    ~WhaleContainerCommands() override;

    /**
     * @return The event filter for a toolbar that shows the actions: it opens the location
     *         of a middle-clicked navigation action in a new tab.
     */
    QObject *middleClickFilter() const;

    /**
     * Opens the location that the navigation action @p action would go to in a new tab of
     * the window, in the background. Nothing for other actions.
     */
    void openInNewTab(QAction *action);

private:
    void goBack();
    void goForward();
    void goUp();
    void goHome();
    void reload();
    void stop();
    void toggleFilterBar();
    void showFilterBar();
    void toggleSearchBar(bool visible);
    void selectAll();
    void invertSelection();

    void updateHistory();
    void updateUp();
    void fillHistoryMenu(KToolBarPopupAction *action, int direction);

    QPointer<DolphinViewContainer> m_container;
    KToolBarPopupAction *m_back;
    KToolBarPopupAction *m_forward;
    QAction *m_up;
    QAction *m_home;
    QAction *m_stop;
    QAction *m_toggleFilter;
    QAction *m_toggleSearch;
    MiddleClickActionEventFilter *m_middleClickFilter;
};

#endif
