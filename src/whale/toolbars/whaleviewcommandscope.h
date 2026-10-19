/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEVIEWCOMMANDSCOPE_H
#define WHALEVIEWCOMMANDSCOPE_H

#include "whaletoolbarlayout.h"

#include <QHash>
#include <QIcon>
#include <QObject>
#include <QPointer>
#include <QStringList>

#include <optional>

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
 *
 * Only the actions of allowedActions() are offered (action()); the collection holds more,
 * because the handler creates all of Dolphin's view actions.
 *
 * Actions on selected items ("renamefile", "movetotrash" …) act on the selection of this
 * view (for a plugin view with a selection of its own: on that, WhaleCommandRouter) and are
 * disabled without one. They never start the selection mode, which belongs to the window's
 * actions.
 */
class WhaleViewCommandScope : public QObject
{
    Q_OBJECT

public:
    WhaleViewCommandScope(DolphinViewContainer *container, QObject *parent);
    ~WhaleViewCommandScope() override;

    /**
     * @return The names of the actions a File View Toolbar can show: the one definition of
     *         that set (for the toolbars, their configuration and the tests). The names are
     *         those of the window's actions.
     */
    static QStringList allowedActions();

    DolphinViewContainer *container() const;
    KActionCollection *actionCollection() const;

    /**
     * @return The action @p name of this view, or nullptr if there is none or it is not one
     *         of allowedActions().
     */
    QAction *action(const QString &name) const;
    WhaleViewActions *viewActions() const;
    WhaleContainerCommands *commands() const;

    /**
     * @return Why @p action of this view is disabled, as a message for the user, or an empty
     *         string if it is enabled or there is nothing to explain. The message belongs
     *         into the message area of this view.
     */
    QString disabledReason(const QAction *action) const;

    /**
     * Gives the actions of this view the texts and icons the user chose for them in
     * Configure Toolbars (@p properties by action ID), and gives all others back what they
     * had before. The window's actions of the same names are not touched.
     */
    void applyActionProperties(const QHash<QString, WhaleToolbarLayout::ActionProperties> &properties);

private:
    // What an action had before applyActionProperties() changed it. A null string: the
    // action derived it from its text.
    struct OriginalProperties {
        QPointer<QAction> action;
        std::optional<QString> text;
        std::optional<QString> iconText;
        std::optional<QString> toolTip;
        std::optional<QIcon> icon;
        std::optional<int> priority;
    };

    void updateRestoreDefaultsAction(bool isDefault);
    void updateSelectionActions();
    void setDisabledReason(const QString &name, const QString &reason);

    QPointer<DolphinViewContainer> m_container;
    KActionCollection *m_actionCollection;
    WhaleContainerCommands *m_commands;
    DolphinViewActionHandler *m_actionHandler;
    WhaleViewActions *m_viewActions;
    QHash<const QAction *, QString> m_disabledReasons; // Dolphin's, by the state of the selection
    QHash<const QAction *, QString> m_whaleDisabledReasons; // no destination; they come first
    QList<OriginalProperties> m_originalProperties;
};

#endif
