/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALESPLITLOCK_H
#define WHALESPLITLOCK_H

#include <QObject>
#include <QPointer>
#include <QString>

class DolphinTabPage;
class DolphinViewContainer;
class QToolButton;

/**
 * @brief The lock of a split view: links the two views of a tab (LK, plan
 *        .privatedoc/design-notes.md).
 *
 * One per DolphinTabPage, created by it. The toggle button (a chain link) is at the start of
 * the File View Toolbar of the right view, while the split view is open; fixed as the split
 * button (WhaleFileViewToolBar::setLeadingWidget()), so not in Configure Toolbars.
 *
 * The views can be locked while both are lockable (lockability()); otherwise the button is
 * disabled and its tooltip says why. Locked, both views are at the same location (E1): on
 * locking, the other view goes to the location of the active one, and from then on each view
 * follows where the other one goes. The following view navigates as usual, so it has an
 * entry in its history and stays the inactive view. Each view keeps its own view mode
 * (DolphinView::setViewModeKept()): the point is to see the same folder in two ways.
 *
 * The inactive view shows the selection and the current item of the active one (LK4,
 * WhaleViewHost::linkedSelection() and showLinkedSelection(); a plugin view takes part with
 * WhaleViewCapability::LinkedSelection, LK5). The user selects in the active view, so this goes both
 * ways, while changes of the inactive view (e.g. while it lists its new location) never reach
 * the active one. Where the inactive view cannot show the items where it is (an item of a
 * subfolder it has not expanded, e.g. in icons), it goes into their folder, with one history
 * entry for such folders, and back when the selection returns (LK7, E6/E7); it never expands
 * folders for them. The active view stays where it is. A selection is limited to the items of one folder (A1,
 * DolphinView::setSelectionLimitedToOneFolder()); a view says so when it refuses or leaves out
 * items.
 *
 * When a locked view becomes unlockable (another location or another view), the lock is
 * released and that view says why (E3); the other view stays where it is. Closing the split
 * view releases it too. The lock is not part of the session (E4).
 */
class WhaleSplitLock : public QObject
{
    Q_OBJECT

public:
    explicit WhaleSplitLock(DolphinTabPage *page);
    ~WhaleSplitLock() override;

    /**
     * @return The lock of the tab of @p page, or nullptr.
     */
    static WhaleSplitLock *of(const DolphinTabPage *page);

    struct Lockability {
        bool lockable = false;
        QString reason; ///< why not, for the user
    };

    /**
     * @return Whether the view area @p container can be part of a lock: a local folder (no
     *         search results) shown in a view with WhaleViewCapability::LinkedSelection.
     */
    static Lockability lockability(const DolphinViewContainer *container);

    bool isLocked() const;

    /**
     * Locks or releases the views. Locking needs an open split view with two lockable views;
     * otherwise nothing happens.
     */
    void setLocked(bool locked);

    QToolButton *button() const;

Q_SIGNALS:
    void lockedChanged(bool locked);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    /// Puts the button into the toolbar of the right view and shows the state.
    void update();
    void scheduleUpdate();
    void connectContainer(DolphinViewContainer *container);
    /// E1: the other view goes to the location of @p leader.
    void follow(DolphinViewContainer *leader);
    /// LK4: the other view shows the selection of @p source.
    void mirror(DolphinViewContainer *source);
    DolphinViewContainer *otherView(const DolphinViewContainer *container) const;
    /// Releases the lock: @p container is the view that became unlockable, nullptr when the
    /// split view closed.
    void release(DolphinViewContainer *container, const QString &reason);

    QPointer<DolphinTabPage> m_page;
    QPointer<QToolButton> m_button;
    QPointer<DolphinViewContainer> m_container; // the one with the button
    QPointer<DolphinViewContainer> m_left;
    QPointer<DolphinViewContainer> m_right;
    bool m_locked = false;
    bool m_updateScheduled = false;
    bool m_following = false;
    bool m_mirroring = false;
    // The view that went into the folder of a selection (E6), so its history entry is not its own.
    QPointer<DolphinViewContainer> m_movedView;
};

#endif
