/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whalemenubaraction.h"

#include <KLocalizedString>

#include <QActionEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QMenuBar>
#include <QTimer>

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
    , m_menuBar(new QMenuBar(this))
{
    setObjectName(QStringLiteral("whaleToolBarMenu"));
    // In the window, never exported as Plasma's global menu: the window's menu bar may be.
    m_menuBar->setNativeMenuBar(false);
    m_menuBar->setObjectName(QStringLiteral("whaleToolBarMenuBar"));

    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_menuBar);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

    m_source->installEventFilter(this);
    sync();
}

WhaleToolBarMenu::~WhaleToolBarMenu() = default;

QMenuBar *WhaleToolBarMenu::menuBar() const
{
    return m_menuBar;
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
}

#include "moc_whalemenubaraction.cpp"
