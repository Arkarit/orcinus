/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whalemenubaraction.h"

#include <KAcceleratorManager>
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
/** The maximum width and focus policy of a suppressed hamburger menu button (F2). */
constexpr const char *originalHamburgerProperty = "_whale_original_hamburger";
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
    // The titles are the window's menu actions. KMainWindow's KAcceleratorManager would give
    // the same title in both menu bars different accelerators, changing the shared actions
    // back and forth on every hover: both menu bars flickered.
    KAcceleratorManager::setNoAccel(m_menuBar);

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
    if (m_hamburger) {
        restoreHamburger(m_hamburger);
    }
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
    // The titles may have changed (ActionChanged), and with them the width. Only then the
    // toolbar has to lay out again.
    const int fullWidth = m_menuBar->sizeHint().width();
    if (fullWidth != m_fullWidth) {
        m_fullWidth = fullWidth;
        m_menuBar->adjustSize();
        updateGeometry();
        scheduleUpdateMode();
    }
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
    layout()->invalidate();
    updateGeometry();
    // In the toolbar's overflow menu, the widget is hidden and updateGeometry() does not reach
    // the toolbar: it would stay there although it fits now.
    if (m_toolBar && m_toolBar->layout()) {
        m_toolBar->layout()->invalidate();
    }
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
    if (m_hamburger && m_hamburger != hamburger) {
        restoreHamburger(m_hamburger);
    }
    m_hamburger = hamburger;
    if (!hamburger) {
        return;
    }
    // KHamburgerMenu shows and hides its buttons itself (by the window's menu bar), and the
    // toolbar shows them by the action; without width, the button takes no room. The values
    // before are kept with the button: while KXmlGui builds the toolbar, an older widget of
    // the element may still be there and give them back after this one took them.
    if (!hamburger->property(originalHamburgerProperty).isValid()) {
        hamburger->setProperty(originalHamburgerProperty, QVariantList{hamburger->maximumWidth(), int(hamburger->focusPolicy())});
    }
    if (hamburger->maximumWidth() != 0) {
        hamburger->setMaximumWidth(0);
    }
    hamburger->setFocusPolicy(Qt::NoFocus);
}

void WhaleToolBarMenu::restoreHamburger(QWidget *hamburger)
{
    const QVariantList original = hamburger->property(originalHamburgerProperty).toList();
    if (original.size() != 2) {
        return;
    }
    hamburger->setProperty(originalHamburgerProperty, QVariant());
    hamburger->setMaximumWidth(original.at(0).toInt());
    hamburger->setFocusPolicy(static_cast<Qt::FocusPolicy>(original.at(1).toInt()));
    // Another widget of the element in the same toolbar takes it again with the layout request.
}

#include "moc_whalemenubaraction.cpp"
