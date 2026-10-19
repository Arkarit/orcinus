/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
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
    m_button->setToolTip(split ? i18nc("@info View refer here to split view", "Close Right View") : i18nc("@info", "Split view"));
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
