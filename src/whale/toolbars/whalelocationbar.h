/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 * SPDX-FileCopyrightText: 2020 Felix Ernst <felixernst@kde.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALELOCATIONBAR_H
#define WHALELOCATIONBAR_H

#include <QPointer>
#include <QWidget>
#include <QWidgetAction>

class DolphinUrlNavigator;
class DolphinViewContainer;
class QPushButton;

/**
 * @brief The location bar of one file view.
 *
 * Every DolphinViewContainer has exactly one, created with its File View Toolbar and
 * connected to the container at once (DolphinViewContainer::connectUrlNavigator()); it is
 * never connected to another container. Its navigator is a view of the container's own
 * internal navigator, which keeps the history; so the location bar may be hidden, not
 * placed in the toolbar or rebuilt without losing anything.
 *
 * Next to the navigator are the buttons that belong to the shown location: Empty Trash
 * (trash:), Add Network Folder (remote:) and Open KDE Connect (kdeconnect:). On remote: the
 * navigator is editable and asks for a server URL (as Dolphin's location bar does, bug
 * 414670). The behaviour follows Dolphin's DolphinNavigatorsWidgetAction.
 *
 * In the File View Toolbar it is the layout entry actionName(), placed by action(). It
 * expands into the remaining room of the toolbar; there is no minimum width of its own.
 */
class WhaleLocationBar : public QWidget
{
    Q_OBJECT

public:
    WhaleLocationBar(DolphinViewContainer *container, QWidget *parent);
    ~WhaleLocationBar() override;

    /**
     * @return The ID of the location bar in a toolbar layout.
     */
    static QString actionName();

    DolphinUrlNavigator *urlNavigator() const;

    /**
     * @return The action that places this location bar in its toolbar.
     */
    QWidgetAction *action() const;

protected:
    bool event(QEvent *event) override;

private:
    void updateForLocation();

    QPointer<DolphinViewContainer> m_container;
    DolphinUrlNavigator *m_urlNavigator;
    QPushButton *m_emptyTrashButton;
    QPushButton *m_networkFolderButton;
    QPushButton *m_kdeConnectButton;
    QWidgetAction *m_action;
};

#endif
