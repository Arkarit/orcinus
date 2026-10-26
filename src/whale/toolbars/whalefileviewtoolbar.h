/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEFILEVIEWTOOLBAR_H
#define WHALEFILEVIEWTOOLBAR_H

#include <KToolBar>

#include <QPointer>

class DolphinViewContainer;
class QTimer;
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
 * WhaleLocationBar::actionName() only places it. Where it would get less than
 * minimumLocationBarWidth(), it moves into a second row below the buttons, over the whole
 * width of the toolbar (the buttons stay in the first row, the overflow menu is for them only).
 * The toolbars of all views of a tab do that together, so that the views start at the same
 * height.
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

    /**
     * @return Whether the location bar is in the second row of the toolbar.
     */
    bool isLocationBarWrapped() const;

    /**
     * @return The width the location bar gets at least next to the buttons; with less, it moves
     *         into the second row.
     */
    static int minimumLocationBarWidth();

    /**
     * Shows @p widget at the end of the row of the buttons, always: it is not part of the
     * layout (not in Configure Toolbars, never in the overflow menu). The toolbar does not own
     * it: it gives it back (hidden, without parent) when another one is set, when it is set to
     * nullptr, and when the toolbar is destroyed.
     */
    void setTrailingWidget(QWidget *widget);
    QWidget *trailingWidget() const;

    /**
     * Shows @p widget at the start of the row of the buttons, as setTrailingWidget() does at
     * its end.
     */
    void setLeadingWidget(QWidget *widget);
    QWidget *leadingWidget() const;

    /**
     * @return True if @p action keeps the room of the leading widget at the start of the row:
     *         not part of the layout, as the leading widget itself.
     */
    static bool isLeadingPlaceholder(const QAction *action);

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void actionEvent(QActionEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void rebuild();

    /// Whether the location bar would get less than minimumLocationBarWidth() next to the buttons.
    bool needsWrap() const;
    /// Wraps the location bar of all toolbars of this tab if one of them needs it, after the
    /// current event, or (@p settle) when the width has not changed for a moment.
    void scheduleWrapUpdate(bool settle = false);
    void updateWrapOfTab();
    void setLocationBarWrapped(bool wrapped);
    /// Replaces @p slot by @p widget (setTrailingWidget(), setLeadingWidget()).
    void setExtraWidget(QPointer<QWidget> &slot, QWidget *widget);
    /// The second row of a wrapped location bar and the leading and trailing widgets, outside
    /// of the layout.
    void layOutExtras();

    QPointer<DolphinViewContainer> m_container;
    WhaleViewCommandScope *m_scope = nullptr;
    WhaleLocationBar *m_locationBar;
    bool m_hasLocationBar = false; // in the layout
    bool m_locationBarWrapped = false;
    QPointer<QWidget> m_trailingWidget;
    QPointer<QWidget> m_leadingWidget;
    QPointer<QWidget> m_leadingPlaceholder; // in the row, deleted by rebuild()
    QTimer *m_wrapTimer;
};

#endif
