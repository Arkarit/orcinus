/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
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
 *        .privatedoc/whale-split-lock-plan.md).
 *
 * One per DolphinTabPage, created by it. The toggle button (a chain link) is at the start of
 * the File View Toolbar of the right view, while the split view is open; fixed as the split
 * button (WhaleFileViewToolBar::setLeadingWidget()), so not in Configure Toolbars.
 *
 * The views can be locked while both are lockable (lockability()); otherwise the button is
 * disabled and its tooltip says why. When a locked view becomes unlockable (another location
 * or another view), the lock is released and that view says why (E3). Closing the split view
 * releases it too. The lock is not part of the session (E4).
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
     *         search results) shown in the file list.
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
};

#endif
