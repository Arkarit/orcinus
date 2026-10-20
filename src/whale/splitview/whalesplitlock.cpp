/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whalesplitlock.h"

#include "dolphintabpage.h"
#include "dolphinviewcontainer.h"
#include "views/dolphinview.h"
#include "whalefileviewtoolbar.h"
#include "whaleselection.h"
#include "whaletransientmessage.h"
#include "whaleview.h"
#include "whaleviewhost.h"

#include <KLocalizedString>

#include <QEvent>
#include <QTimer>
#include <QToolButton>

WhaleSplitLock::WhaleSplitLock(DolphinTabPage *page)
    : QObject(page)
    , m_page(page)
    , m_button(new QToolButton)
{
    m_button->setObjectName(QStringLiteral("whale_split_lock_button"));
    m_button->setAutoRaise(true);
    m_button->setCheckable(true);
    m_button->setFocusPolicy(Qt::NoFocus); // as the buttons of a toolbar
    m_button->setIcon(QIcon::fromTheme(QStringLiteral("link")));
    m_button->hide();
    connect(m_button, &QToolButton::clicked, this, [this](bool checked) {
        setLocked(checked);
        // Refused: the button shows the state again.
        m_button->setChecked(m_locked);
    });

    // The views come and go as children of the splitter.
    if (DolphinViewContainer *container = page->primaryViewContainer()) {
        if (QWidget *splitter = container->parentWidget()) {
            splitter->installEventFilter(this);
        }
    }
    connect(page, &DolphinTabPage::activeViewChanged, this, &WhaleSplitLock::scheduleUpdate);
    update();
}

WhaleSplitLock::~WhaleSplitLock()
{
    delete m_button;
}

WhaleSplitLock *WhaleSplitLock::of(const DolphinTabPage *page)
{
    return page ? page->findChild<WhaleSplitLock *>(QString(), Qt::FindDirectChildrenOnly) : nullptr;
}

WhaleSplitLock::Lockability WhaleSplitLock::lockability(const DolphinViewContainer *container)
{
    if (!container) {
        return {};
    }
    const QUrl url = container->url();
    if (isSearchUrl(url)) {
        return {false, i18nc("@info", "Search results cannot be linked.")};
    }
    if (!url.isLocalFile()) {
        return {false, i18nc("@info", "Only local folders can be linked.")};
    }
    // The shown view has to show the selection of the other one (LK5).
    const WhaleViewHost *host = container->viewHost();
    if (host && !(host->currentView()->capabilities() & WhaleViewCapability::LinkedSelection)) {
        return {false, i18nc("@info", "This view cannot be linked.")};
    }
    return {true, QString()};
}

bool WhaleSplitLock::isLocked() const
{
    return m_locked;
}

void WhaleSplitLock::setLocked(bool locked)
{
    if (locked == m_locked) {
        return;
    }
    if (locked) {
        if (!m_page || !m_page->splitViewEnabled() || !lockability(m_left).lockable || !lockability(m_right).lockable) {
            return;
        }
    }
    m_locked = locked;
    update();
    Q_EMIT lockedChanged(m_locked);
    if (m_locked) {
        // The other view goes where the active one is.
        DolphinViewContainer *active = m_page->activeViewContainer() == m_right ? m_right.data() : m_left.data();
        follow(active);
        mirror(active);
    }
}

QToolButton *WhaleSplitLock::button() const
{
    return m_button;
}

bool WhaleSplitLock::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::ChildAdded || event->type() == QEvent::ChildRemoved) {
        scheduleUpdate();
    }
    return QObject::eventFilter(watched, event);
}

void WhaleSplitLock::scheduleUpdate()
{
    if (m_updateScheduled) {
        return;
    }
    m_updateScheduled = true;
    QTimer::singleShot(0, this, [this]() {
        m_updateScheduled = false;
        update();
    });
}

void WhaleSplitLock::connectContainer(DolphinViewContainer *container)
{
    // Whatever decides about the lockability: the location and the shown view.
    connect(container->view(), &DolphinView::urlChanged, this, &WhaleSplitLock::scheduleUpdate, Qt::UniqueConnection);
    connect(container->view(), &DolphinView::urlChanged, this, [this, view = QPointer<DolphinViewContainer>(container)]() {
        follow(view);
    });
    if (WhaleViewHost *host = container->viewHost()) {
        connect(host, &WhaleViewHost::currentViewChanged, this, &WhaleSplitLock::scheduleUpdate, Qt::UniqueConnection);
        connect(host, &WhaleViewHost::capabilitiesChanged, this, &WhaleSplitLock::scheduleUpdate, Qt::UniqueConnection);
        // LK5: the selection of a plugin view that is its own (e.g. deep in the treemap).
        connect(host, &WhaleViewHost::viewSelectionChanged, this, [this, view = QPointer<DolphinViewContainer>(container)]() {
            if (view && m_page && m_page->activeViewContainer() == view) {
                mirror(view);
            }
        });
    }
    connect(container, &QObject::destroyed, this, &WhaleSplitLock::scheduleUpdate, Qt::UniqueConnection);
    // LK4: what the user selects in the active view.
    connect(container->view(), &DolphinView::linkedSelectionChanged, this, [this, view = QPointer<DolphinViewContainer>(container)]() {
        if (view && m_page && m_page->activeViewContainer() == view) {
            mirror(view);
        }
    });
    // A1: why an item could not be added to the selection.
    connect(container->view(), &DolphinView::selectionLimited, this, [this, view = QPointer<DolphinViewContainer>(container)]() {
        if (!m_locked || !view) {
            return;
        }
        // The flat view does not show the folders the rule is about.
        const WhaleViewHost *host = view->viewHost();
        if (host && host->isRecursiveContentShown()) {
            WhaleTransientMessage::showIn(view,
                                          i18nc("@info",
                                                "While the views are linked, only items of one folder can be selected. This also applies to the flat view, "
                                                "which does not show the folders."));
        } else {
            WhaleTransientMessage::showIn(view, i18nc("@info", "While the views are linked, only items of one folder can be selected."));
        }
    });
}

DolphinViewContainer *WhaleSplitLock::otherView(const DolphinViewContainer *container) const
{
    if (!container) {
        return nullptr;
    }
    return container == m_left ? m_right.data() : container == m_right ? m_left.data() : nullptr;
}

void WhaleSplitLock::follow(DolphinViewContainer *leader)
{
    DolphinViewContainer *other = otherView(leader);
    if (!m_locked || m_following || !other) {
        return;
    }
    // Not where the views cannot be linked: the lock is released instead (E3, update()).
    if (!lockability(leader).lockable) {
        return;
    }
    const QUrl url = leader->url();
    if (other->url().adjusted(QUrl::StripTrailingSlash) == url.adjusted(QUrl::StripTrailingSlash)) {
        return; // also the way back of the other view's own change
    }
    m_following = true;
    other->setUrlWithoutActivation(url);
    m_following = false;
}

void WhaleSplitLock::mirror(DolphinViewContainer *source)
{
    DolphinViewContainer *other = otherView(source);
    if (!m_locked || m_mirroring || !other) {
        return;
    }
    m_mirroring = true;
    const WhaleViewHost *sourceHost = source->viewHost();
    const WhaleSelection selection =
        sourceHost ? sourceHost->linkedSelection() : WhaleSelection(source->view()->selectedItems().urlList(), source->view()->currentItemUrl());
    if (WhaleViewHost *otherHost = other->viewHost()) {
        otherHost->showLinkedSelection(selection);
    } else {
        other->view()->showLinkedSelection(selection.selected(), selection.current());
    }
    m_mirroring = false;
}

void WhaleSplitLock::release(DolphinViewContainer *container, const QString &reason)
{
    m_locked = false;
    if (container) {
        WhaleTransientMessage::showIn(container, i18nc("@info %1 is why", "The views are no longer linked. %1", reason));
    }
    Q_EMIT lockedChanged(false);
}

void WhaleSplitLock::update()
{
    if (!m_page || !m_button) {
        return;
    }
    const bool split = m_page->splitViewEnabled();
    DolphinViewContainer *left = m_page->primaryViewContainer();
    DolphinViewContainer *right = split ? m_page->secondaryViewContainer() : nullptr;
    for (DolphinViewContainer *container : {left, right}) {
        if (container && container != m_left && container != m_right) {
            connectContainer(container);
        }
    }
    m_left = left;
    m_right = right;

    const Lockability leftLockability = lockability(left);
    const Lockability rightLockability = lockability(right);
    if (m_locked) {
        if (!right) {
            release(nullptr, QString());
        } else if (!leftLockability.lockable) {
            release(left, leftLockability.reason);
        } else if (!rightLockability.lockable) {
            release(right, rightLockability.reason);
        }
    }

    // A1: while locked, a selection is in one folder.
    for (DolphinViewContainer *container : {left, right}) {
        if (container) {
            container->view()->setSelectionLimitedToOneFolder(m_locked);
            // Both show the same folder, each in its own view mode.
            container->view()->setViewModeKept(m_locked);
        }
    }

    // In the right view only.
    auto toolBar = right ? right->findChild<WhaleFileViewToolBar *>(QString(), Qt::FindDirectChildrenOnly) : nullptr;
    if (right != m_container) {
        if (m_container) {
            if (auto oldToolBar = m_container->findChild<WhaleFileViewToolBar *>(QString(), Qt::FindDirectChildrenOnly)) {
                oldToolBar->setLeadingWidget(nullptr);
            }
        }
        m_container = right;
    }

    const QString reason = !leftLockability.lockable ? leftLockability.reason : rightLockability.reason;
    const bool lockable = right && leftLockability.lockable && rightLockability.lockable;
    m_button->setEnabled(m_locked || lockable);
    m_button->setChecked(m_locked);
    m_button->setIcon(QIcon::fromTheme(m_locked ? QStringLiteral("link") : QStringLiteral("remove-link")));
    if (m_locked) {
        m_button->setToolTip(i18nc("@info:tooltip", "Unlink Views"));
    } else if (lockable) {
        m_button->setToolTip(i18nc("@info:tooltip", "Link Views: both views go to the same folder and select the same items"));
    } else {
        m_button->setToolTip(i18nc("@info:tooltip %1 is why", "The views cannot be linked. %1", reason));
    }
    if (toolBar) {
        m_button->setIconSize(toolBar->iconSize());
        toolBar->setLeadingWidget(m_button);
    } else {
        m_button->hide();
    }
}
