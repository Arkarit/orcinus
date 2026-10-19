/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALESPLITVIEWBUTTON_H
#define WHALESPLITVIEWBUTTON_H

#include <QObject>
#include <QPointer>

class DolphinTabPage;
class DolphinViewContainer;
class QToolButton;

/**
 * @brief The split button of a tab: at the end of the File View Toolbar of the rightmost view.
 *
 * One per DolphinTabPage, created by it. Without a split view, the button (a double arrow
 * pointing into the window) is in the only view and opens the right view, as the window's
 * "split_view" does. With a split view, it is in the right view, points back and closes it,
 * whatever the settings say about which view to close (DolphinMainWindow::closeRightSplitView()).
 * So it stays at the same place in the window.
 *
 * The button is fixed: not part of the toolbar's layout, so not in Configure Toolbars and
 * never in the overflow menu (WhaleFileViewToolBar::setTrailingWidget()). It belongs to this
 * object, not to the toolbar: it moves between the toolbars as the split view opens and
 * closes, also when the window's button closes the left view.
 *
 * The File View Toolbars do not know about the sides of a split view; this does.
 */
class WhaleSplitViewButton : public QObject
{
    Q_OBJECT

public:
    explicit WhaleSplitViewButton(DolphinTabPage *page);
    ~WhaleSplitViewButton() override;

    /**
     * @return The button of the tab of @p page, or nullptr.
     */
    static QToolButton *buttonOf(const DolphinTabPage *page);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    /// Puts the button into the toolbar of the rightmost view and shows the state of the split view.
    void update();
    void scheduleUpdate();
    void toggle();

    QPointer<DolphinTabPage> m_page;
    QPointer<QToolButton> m_button;
    QPointer<DolphinViewContainer> m_container; // the one with the button
    bool m_updateScheduled = false;
};

#endif
