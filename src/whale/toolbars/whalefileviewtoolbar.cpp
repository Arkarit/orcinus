/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whalefileviewtoolbar.h"

#include "dolphinviewcontainer.h"
#include "whaletoolbarmanager.h"
#include "whaleviewcommandscope.h"

#include <KConfig>
#include <KConfigGroup>
#include <KIconLoader>

#include <QActionEvent>

WhaleFileViewToolBar::WhaleFileViewToolBar(DolphinViewContainer *container)
    : KToolBar(container, false, false)
    , m_container(container)
{
    Q_ASSERT(container);
    setObjectName(QStringLiteral("whaleFileViewToolBar"));
    setMovable(false);
    // No context menu for now: KToolBar's needs a main window as parent, and QToolBar's would
    // be passed on to the window. (Configure/hide come with the settings, TB4/TB5.)
    setContextMenuPolicy(Qt::PreventContextMenu);
    // Small icons without text, as the user's settings of this toolbar (they stay when KDE's
    // toolbar style changes). Every File View Toolbar looks the same.
    KConfig settings(QString(), KConfig::SimpleConfig);
    KConfigGroup group(&settings, QStringLiteral("File View Toolbar"));
    group.writeEntry("IconSize", int(KIconLoader::SizeSmall));
    group.writeEntry("ToolButtonStyle", QStringLiteral("IconOnly"));
    applySettings(group);
}

WhaleFileViewToolBar::~WhaleFileViewToolBar() = default;

WhaleViewCommandScope *WhaleFileViewToolBar::scope() const
{
    return m_scope;
}

void WhaleFileViewToolBar::showEvent(QShowEvent *event)
{
    if (!m_scope && m_container) {
        // Shown means part of its window by now: the window's services and toolbar manager.
        m_scope = new WhaleViewCommandScope(m_container, this);
        connect(WhaleToolbarManager::ensure(window()), &WhaleToolbarManager::fileViewLayoutChanged, this, &WhaleFileViewToolBar::rebuild);
        rebuild();
    }
    KToolBar::showEvent(event);
}

void WhaleFileViewToolBar::rebuild()
{
    WhaleToolbarManager *manager = WhaleToolbarManager::of(this);
    if (!m_scope || !manager) {
        return;
    }
    clear();
    const QStringList allowed = manager->descriptor(WhaleToolbarDescriptor::fileViewId()).allowedActions;
    const auto entries = manager->fileViewLayout().entries();
    for (const WhaleToolbarLayout::Entry &entry : entries) {
        switch (entry.type) {
        case WhaleToolbarLayout::Entry::Type::Separator:
            addSeparator();
            break;
        case WhaleToolbarLayout::Entry::Type::Spacer: {
            // As KXMLGUIBuilder builds <Spacer/>.
            auto spacer = new QWidget(this);
            spacer->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
            addWidget(spacer);
            break;
        }
        case WhaleToolbarLayout::Entry::Type::Action:
            if (QAction *action = m_scope->action(entry.actionId); action && allowed.contains(entry.actionId)) {
                addAction(action);
            }
            break;
        }
    }
}

void WhaleFileViewToolBar::actionEvent(QActionEvent *event)
{
    KToolBar::actionEvent(event);
    if (event->type() == QEvent::ActionAdded) {
        if (QWidget *button = widgetForAction(event->action())) {
            button->installEventFilter(this);
        }
    }
}

bool WhaleFileViewToolBar::eventFilter(QObject *watched, QEvent *event)
{
    // Working with the toolbar of a view makes it the active view, as clicking into the view
    // or its location bar does. The actions act on this view in any case.
    if (event->type() == QEvent::MouseButtonPress && m_container && !m_container->isActive()) {
        m_container->setActive(true);
    }
    return KToolBar::eventFilter(watched, event);
}

#include "moc_whalefileviewtoolbar.cpp"
