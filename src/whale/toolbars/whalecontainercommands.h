/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
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
class QAction;

/**
 * @brief The window's actions on a view, as actions on one fixed view.
 *
 * DolphinMainWindow's navigation actions act on the active view. These do the same on the
 * view they were created for, whichever view is active: "go_back", "go_forward" (with their
 * history menus), "go_up", "go_home", "view_redisplay" and "stop", with the window's action
 * names. Their state comes from that view only.
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

private:
    void goBack();
    void goForward();
    void goUp();
    void goHome();
    void reload();
    void stop();

    void updateHistory();
    void updateUp();
    void fillHistoryMenu(KToolBarPopupAction *action, int direction);

    QPointer<DolphinViewContainer> m_container;
    KToolBarPopupAction *m_back;
    KToolBarPopupAction *m_forward;
    QAction *m_up;
    QAction *m_stop;
};

#endif
