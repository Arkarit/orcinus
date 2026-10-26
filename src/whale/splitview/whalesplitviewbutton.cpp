/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whalesplitviewbutton.h"

#include "dolphinmainwindow.h"
#include "dolphintabpage.h"
#include "dolphinviewcontainer.h"
#include "whalefileviewtoolbar.h"

#include <KActionCollection>
#include <KLocalizedString>

#include <QEvent>
#include <QMenu>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>

WhaleSplitViewButton::WhaleSplitViewButton(DolphinTabPage *page)
    : QObject(page)
    , m_page(page)
    , m_button(new QToolButton)
{
    m_button->setObjectName(QStringLiteral("whale_split_view_button"));
    m_button->setAutoRaise(true);
    m_button->setFocusPolicy(Qt::NoFocus); // as the buttons of a toolbar
    m_button->hide();
    connect(m_button, &QToolButton::clicked, this, &WhaleSplitViewButton::toggle);
    m_button->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_button, &QToolButton::customContextMenuRequested, this, &WhaleSplitViewButton::showContextMenu);

    // The views come and go as children of the splitter.
    if (DolphinViewContainer *container = page->primaryViewContainer()) {
        if (QWidget *splitter = container->parentWidget()) {
            splitter->installEventFilter(this);
        }
    }
    connect(page, &DolphinTabPage::activeViewChanged, this, &WhaleSplitViewButton::scheduleUpdate);
    update();
}

WhaleSplitViewButton::~WhaleSplitViewButton()
{
    delete m_button;
}

QToolButton *WhaleSplitViewButton::buttonOf(const DolphinTabPage *page)
{
    const auto self = page ? page->findChild<WhaleSplitViewButton *>(QString(), Qt::FindDirectChildrenOnly) : nullptr;
    return self ? self->m_button.data() : nullptr;
}

bool WhaleSplitViewButton::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::ChildAdded || event->type() == QEvent::ChildRemoved) {
        scheduleUpdate();
    }
    return QObject::eventFilter(watched, event);
}

void WhaleSplitViewButton::scheduleUpdate()
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

void WhaleSplitViewButton::update()
{
    if (!m_page || !m_button) {
        return;
    }
    // Always at the right end of the window: ">>" in the only view, "<<" in the right one.
    const bool split = m_page->splitViewEnabled();
    DolphinViewContainer *container = split ? m_page->secondaryViewContainer() : m_page->primaryViewContainer();
    auto toolBar = container ? container->findChild<WhaleFileViewToolBar *>(QString(), Qt::FindDirectChildrenOnly) : nullptr;
    if (container != m_container) {
        if (m_container) {
            if (auto oldToolBar = m_container->findChild<WhaleFileViewToolBar *>(QString(), Qt::FindDirectChildrenOnly)) {
                oldToolBar->setTrailingWidget(nullptr);
            }
            disconnect(m_container, nullptr, this, nullptr);
        }
        m_container = container;
        if (container) {
            // Closing the view with the button: the remaining one gets it.
            connect(container, &QObject::destroyed, this, &WhaleSplitViewButton::scheduleUpdate);
        }
    }

    // Points to where the right view opens or closes; mirrored with a right-to-left layout.
    const bool rightToLeft = m_page->layoutDirection() == Qt::RightToLeft;
    m_button->setIcon(QIcon::fromTheme(split != rightToLeft ? QStringLiteral("arrow-left-double") : QStringLiteral("arrow-right-double")));
    m_button->setToolTip(split ? i18nc("@info:tooltip View refer here to split view", "Close Right View\nRight-click to move it to a new tab or window")
                               : i18nc("@info", "Split view"));
    if (toolBar) {
        m_button->setIconSize(toolBar->iconSize());
        toolBar->setTrailingWidget(m_button);
    }
}

void WhaleSplitViewButton::toggle()
{
    auto window = m_page ? qobject_cast<DolphinMainWindow *>(m_page->window()) : nullptr;
    if (!window) {
        return;
    }
    if (m_page->splitViewEnabled()) {
        window->closeRightSplitView();
    } else if (QAction *split = window->actionCollection()->action(QStringLiteral("split_view"))) {
        // As the window's button (also "open the selected folder in the new view").
        split->trigger();
    }
    update();
}

void WhaleSplitViewButton::showContextMenu(const QPoint &pos)
{
    auto window = m_page ? qobject_cast<DolphinMainWindow *>(m_page->window()) : nullptr;
    if (!window || !m_page->splitViewEnabled()) {
        return;
    }
    // The same right view the button closes, whichever view is active.
    auto menu = new QMenu(m_button);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    QAction *toTab =
        menu->addAction(QIcon::fromTheme(QStringLiteral("tab-new")), i18nc("@action:intoolbar Splits both views into separate tabs", "Split View To Tabs"));
    toTab->setObjectName(QStringLiteral("whale_split_view_to_tab"));
    connect(toTab, &QAction::triggered, window, &DolphinMainWindow::moveRightSplitViewToTab);
    QAction *popout =
        menu->addAction(QIcon::fromTheme(QStringLiteral("window-new")), i18nc("@action:intoolbar Move right split view to a new window", "Pop out Right View"));
    popout->setObjectName(QStringLiteral("whale_split_view_popout"));
    connect(popout, &QAction::triggered, window, &DolphinMainWindow::popoutRightSplitView);
    menu->popup(m_button->mapToGlobal(pos));
}
