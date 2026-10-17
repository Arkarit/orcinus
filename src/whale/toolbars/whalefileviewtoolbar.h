/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEFILEVIEWTOOLBAR_H
#define WHALEFILEVIEWTOOLBAR_H

#include <KToolBar>

#include <QPointer>

class DolphinViewContainer;
class WhaleLocationBar;
class WhaleViewCommandScope;

/**
 * @brief The File View Toolbar: the toolbar of one file view.
 *
 * Every DolphinViewContainer has one, at its top. It belongs to that view for its whole
 * life and acts on it only (WhaleViewCommandScope), whichever view is active and whatever
 * the view shows (file list or a plugin view). All File View Toolbars of a window show the
 * same layout (WhaleToolbarManager::fileViewLayout()).
 *
 * The actions are created when the toolbar is shown for the first time, so views that are
 * never shown (other tabs) cost nothing. Pressing a button activates the view, as clicking
 * into it or its location bar does; the actions do not depend on that.
 *
 * The view's location bar (WhaleLocationBar) exists and is connected from the start, also
 * while the toolbar is hidden or the layout has no place for it; the layout entry
 * WhaleLocationBar::actionName() only places it.
 *
 * Shown or hidden with all other File View Toolbars (WhaleToolbarSettings). Its context menu
 * opens Configure Toolbars with the File View Toolbar chosen, or hides them all.
 */
class WhaleFileViewToolBar : public KToolBar
{
    Q_OBJECT

public:
    explicit WhaleFileViewToolBar(DolphinViewContainer *container);
    ~WhaleFileViewToolBar() override;

    /**
     * @return The actions of the view, or nullptr before the toolbar was shown.
     */
    WhaleViewCommandScope *scope() const;

    /**
     * @return The view's location bar; it exists as long as the toolbar does.
     */
    WhaleLocationBar *locationBar() const;

protected:
    void showEvent(QShowEvent *event) override;
    void actionEvent(QActionEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void rebuild();

    QPointer<DolphinViewContainer> m_container;
    WhaleViewCommandScope *m_scope = nullptr;
    WhaleLocationBar *m_locationBar;
};

#endif
