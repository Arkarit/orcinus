/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEVIEWCOMMANDSCOPE_H
#define WHALEVIEWCOMMANDSCOPE_H

#include <QObject>
#include <QPointer>

class DolphinViewActionHandler;
class DolphinViewContainer;
class KActionCollection;
class QAction;
class WhaleContainerCommands;
class WhaleViewActions;

/**
 * @brief The actions of one file view, acting on that view only.
 *
 * Bound to one DolphinViewContainer for its whole life; it never looks at the active view.
 * Its actions have the names of the window's actions, but are objects of their own, with
 * the state of this view and without shortcuts (a shortcut acts on the active view through
 * the window's action; the same shortcut here would make it ambiguous).
 *
 * - WhaleContainerCommands: the window's navigation actions for this view.
 * - DolphinViewActionHandler: Dolphin's view actions (view modes, zoom, sorting, previews,
 *   hidden files, "view_settings" …), a second handler bound to this view.
 * - WhaleViewActions: the plugin views, "Show flat (no folders)" and the capability gates,
 *   bound to this view's host and using the window's WhaleViewServices.
 *
 * The private collection is never part of the window's XML GUI factory, so its actions are
 * not listed in Find Action or Configure Keyboard Shortcuts.
 */
class WhaleViewCommandScope : public QObject
{
    Q_OBJECT

public:
    WhaleViewCommandScope(DolphinViewContainer *container, QObject *parent);
    ~WhaleViewCommandScope() override;

    DolphinViewContainer *container() const;
    KActionCollection *actionCollection() const;
    QAction *action(const QString &name) const;
    WhaleViewActions *viewActions() const;

private:
    void updateRestoreDefaultsAction(bool isDefault);

    QPointer<DolphinViewContainer> m_container;
    KActionCollection *m_actionCollection;
    WhaleContainerCommands *m_commands;
    DolphinViewActionHandler *m_actionHandler;
    WhaleViewActions *m_viewActions;
};

#endif
