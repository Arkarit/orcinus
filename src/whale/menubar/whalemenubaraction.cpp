/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whalemenubaraction.h"

#include <KHamburgerMenu>
#include <KLocalizedString>

#include <QActionEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QMenu>
#include <QMenuBar>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

namespace
{
/** Room the menu bar needs beyond its width before it replaces the button again. */
constexpr int switchBackMargin = 4;
}

WhaleMenuBarAction::WhaleMenuBarAction(QMenuBar *source, QObject *parent)
    : QWidgetAction(parent)
    , m_source(source)
{
    setText(i18nc("@action:intoolbar", "Menu Bar"));
    setIcon(QIcon::fromTheme(QStringLiteral("application-menu")));
    setToolTip(i18nc("@info:tooltip", "The menus of the window"));
}

WhaleMenuBarAction::~WhaleMenuBarAction() = default;

QString WhaleMenuBarAction::actionName()
{
    return QStringLiteral("whale_menu_bar");
}

QWidget *WhaleMenuBarAction::createWidget(QWidget *parent)
{
    if (!m_source) {
        return nullptr;
    }
    return new WhaleToolBarMenu(m_source, parent);
}

WhaleToolBarMenu::WhaleToolBarMenu(QMenuBar *source, QWidget *parent)
    : QWidget(parent)
    , m_source(source)
    , m_toolBar(qobject_cast<QToolBar *>(parent))
    , m_menuBar(new QMenuBar(this))
    , m_burgerButton(new QToolButton(this))
    , m_burgerMenu(new QMenu(this))
{
    setObjectName(QStringLiteral("whaleToolBarMenu"));
    // In the window, never exported as Plasma's global menu: the window's menu bar may be.
    m_menuBar->setNativeMenuBar(false);
    m_menuBar->setObjectName(QStringLiteral("whaleToolBarMenuBar"));

    m_burgerButton->setObjectName(QStringLiteral("whaleToolBarMenuButton"));
    m_burgerButton->setIcon(QIcon::fromTheme(QStringLiteral("application-menu")));
    m_burgerButton->setToolTip(i18nc("@info:tooltip", "Menu"));
    m_burgerButton->setAutoRaise(true);
    m_burgerButton->setPopupMode(QToolButton::InstantPopup);
    m_burgerButton->setMenu(m_burgerMenu);
    m_burgerButton->hide();
    connect(m_burgerMenu, &QMenu::aboutToShow, this, &WhaleToolBarMenu::fillBurgerMenu);

    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_menuBar);
    layout->addWidget(m_burgerButton);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

    m_source->installEventFilter(this);
    if (m_toolBar) {
        m_toolBar->installEventFilter(this);
        m_burgerButton->setIconSize(m_toolBar->iconSize());
        connect(m_toolBar, &QToolBar::iconSizeChanged, m_burgerButton, &QToolButton::setIconSize);
        connect(m_toolBar, &QToolBar::orientationChanged, this, &WhaleToolBarMenu::scheduleUpdateMode);
    }
    sync();
    updateMode();
}

WhaleToolBarMenu::~WhaleToolBarMenu()
{
    suppressHamburger(false);
}

QMenuBar *WhaleToolBarMenu::menuBar() const
{
    return m_menuBar;
}

QToolButton *WhaleToolBarMenu::burgerButton() const
{
    return m_burgerButton;
}

bool WhaleToolBarMenu::isBurger() const
{
    return m_burger;
}

bool WhaleToolBarMenu::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_source) {
        switch (event->type()) {
        case QEvent::ActionAdded:
        case QEvent::ActionRemoved:
        case QEvent::ActionChanged:
            scheduleSync();
            break;
        default:
            break;
        }
    } else if (watched == m_toolBar) {
        switch (event->type()) {
        case QEvent::Resize:
        case QEvent::LayoutRequest:
        case QEvent::ActionAdded:
        case QEvent::ActionRemoved:
        case QEvent::ActionChanged:
            scheduleUpdateMode();
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void WhaleToolBarMenu::scheduleSync()
{
    // KXmlGui builds the menu bar one menu at a time.
    if (m_syncScheduled) {
        return;
    }
    m_syncScheduled = true;
    QTimer::singleShot(0, this, [this]() {
        m_syncScheduled = false;
        sync();
    });
}

void WhaleToolBarMenu::sync()
{
    const QList<QAction *> wanted = m_source ? m_source->actions() : QList<QAction *>();
    if (m_menuBar->actions() != wanted) {
        // Not QMenuBar::clear(): the actions belong to the window's menu bar.
        const auto current = m_menuBar->actions();
        for (QAction *action : current) {
            m_menuBar->removeAction(action);
        }
        m_menuBar->addActions(wanted);
    }
    // The titles may have changed (ActionChanged), and with them the width.
    m_menuBar->adjustSize();
    updateGeometry();
    scheduleUpdateMode();
}

void WhaleToolBarMenu::scheduleUpdateMode()
{
    if (m_updateScheduled) {
        return;
    }
    m_updateScheduled = true;
    QTimer::singleShot(0, this, [this]() {
        m_updateScheduled = false;
        updateMode();
    });
}

void WhaleToolBarMenu::updateMode()
{
    suppressHamburger(true);
    if (!m_toolBar) {
        setBurger(false);
        return;
    }
    if (m_toolBar->orientation() == Qt::Vertical) {
        setBurger(true);
        return;
    }
    const int needed = m_menuBar->sizeHint().width() + (m_burger ? switchBackMargin : 0);
    setBurger(availableWidth() < needed);
}

void WhaleToolBarMenu::setBurger(bool burger)
{
    if (burger == m_burger) {
        return;
    }
    m_burger = burger;
    if (m_burgerMenu->isVisible()) {
        m_burgerMenu->close();
    }
    m_menuBar->setVisible(!burger);
    m_burgerButton->setVisible(burger);
    updateGeometry();
}

int WhaleToolBarMenu::availableWidth() const
{
    const QLayout *layout = m_toolBar->layout();
    const QMargins margins = layout ? layout->contentsMargins() : QMargins();
    const int spacing = layout ? qMax(0, layout->spacing()) : 0;
    int width = m_toolBar->contentsRect().width() - margins.left() - margins.right();
    if (m_toolBar->isMovable()) {
        width -= m_toolBar->style()->pixelMetric(QStyle::PM_ToolBarHandleExtent, nullptr, m_toolBar) + spacing;
    }

    // The other elements, those that stretch at their minimum width.
    int items = 1;
    const auto actions = m_toolBar->actions();
    for (QAction *action : actions) {
        QWidget *widget = action->isVisible() ? m_toolBar->widgetForAction(action) : nullptr;
        if (!widget || widget == this || widget == m_hamburger) {
            continue;
        }
        const bool stretches = widget->sizePolicy().expandingDirections() & Qt::Horizontal;
        const QSize hint = stretches ? widget->minimumSizeHint() : widget->sizeHint();
        width -= qBound(widget->minimumWidth(), qMax(0, hint.width()), widget->maximumWidth());
        ++items;
    }
    return width - spacing * (items - 1);
}

void WhaleToolBarMenu::fillBurgerMenu()
{
    m_burgerMenu->clear();
    if (!m_source) {
        return;
    }
    // The menus of the menu bar as submenus (F1).
    const auto actions = m_source->actions();
    for (QAction *action : actions) {
        if (action->isVisible()) {
            m_burgerMenu->addAction(action);
        }
    }
}

void WhaleToolBarMenu::suppressHamburger(bool suppress)
{
    QWidget *hamburger = nullptr;
    if (suppress && m_toolBar) {
        const auto actions = m_toolBar->actions();
        for (QAction *action : actions) {
            if (qobject_cast<KHamburgerMenu *>(action)) {
                hamburger = m_toolBar->widgetForAction(action);
                break;
            }
        }
    }
    if (hamburger == m_hamburger) {
        return;
    }
    if (m_hamburger) {
        m_hamburger->setMaximumWidth(m_hamburgerMaximumWidth);
        m_hamburger->setFocusPolicy(m_hamburgerFocusPolicy);
    }
    m_hamburger = hamburger;
    if (m_hamburger) {
        // KHamburgerMenu shows and hides its buttons itself (by the window's menu bar), and the
        // toolbar shows them by the action; without width, the button takes no room.
        m_hamburgerMaximumWidth = m_hamburger->maximumWidth();
        m_hamburgerFocusPolicy = m_hamburger->focusPolicy();
        m_hamburger->setMaximumWidth(0);
        m_hamburger->setFocusPolicy(Qt::NoFocus);
    }
}

#include "moc_whalemenubaraction.cpp"
