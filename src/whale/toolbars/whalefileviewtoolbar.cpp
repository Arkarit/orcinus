/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whalefileviewtoolbar.h"

#include "dolphinviewcontainer.h"
#include "whalecontainercommands.h"
#include "whalelocationbar.h"
#include "whaletoolbarconfigurator.h"
#include "whaletoolbarmanager.h"
#include "whaletoolbarsettings.h"
#include "whaleviewcommandscope.h"

#include <KConfig>
#include <KConfigGroup>
#include <KIconLoader>
#include <KLocalizedString>

#include <QActionEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QToolButton>

#include <algorithm>

WhaleFileViewToolBar::WhaleFileViewToolBar(DolphinViewContainer *container)
    : KToolBar(container, false, false)
    , m_container(container)
{
    Q_ASSERT(container);
    setObjectName(QStringLiteral("whaleFileViewToolBar"));
    setMovable(false);
    // Small icons without text, as the user's settings of this toolbar (they stay when KDE's
    // toolbar style changes). Every File View Toolbar looks the same.
    KConfig settings(QString(), KConfig::SimpleConfig);
    KConfigGroup group(&settings, QStringLiteral("File View Toolbar"));
    group.writeEntry("IconSize", int(KIconLoader::SizeSmall));
    group.writeEntry("ToolButtonStyle", QStringLiteral("IconOnly"));
    applySettings(group);

    // Shown or not: one setting for all File View Toolbars. A hidden one creates its actions
    // when it is shown.
    WhaleToolbarSettings *toolbarSettings = WhaleToolbarSettings::self();
    if (!toolbarSettings->fileViewToolBarsVisible()) {
        hide();
    }
    connect(toolbarSettings, &WhaleToolbarSettings::fileViewToolBarsVisibleChanged, this, &QWidget::setVisible);

    // Now, not when shown: the window asks every view for its visible navigator.
    m_locationBar = new WhaleLocationBar(container, this);
}

WhaleFileViewToolBar::~WhaleFileViewToolBar() = default;

WhaleViewCommandScope *WhaleFileViewToolBar::scope() const
{
    return m_scope;
}

WhaleLocationBar *WhaleFileViewToolBar::locationBar() const
{
    return m_locationBar;
}

void WhaleFileViewToolBar::showEvent(QShowEvent *event)
{
    if (!m_scope && m_container) {
        // Shown means part of its window by now: the window's services and toolbar manager.
        m_scope = new WhaleViewCommandScope(m_container, this);
        // Middle click on Back, Forward, Up or Home: in a new tab.
        installEventFilter(m_scope->commands()->middleClickFilter());
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
    const WhaleToolbarLayout layout = manager->fileViewLayout();
    // What the user changed about the actions in Configure Toolbars, for this view's actions.
    m_scope->applyActionProperties(layout.actionProperties());
    const auto entries = layout.entries();
    // A spacer next to the location bar would take half of the room the location bar
    // expands into: then it does not expand.
    const bool hasLocationBar = std::any_of(entries.cbegin(), entries.cend(), [](const WhaleToolbarLayout::Entry &entry) {
        return entry.type == WhaleToolbarLayout::Entry::Type::Action && entry.actionId == WhaleLocationBar::actionName();
    });
    for (const WhaleToolbarLayout::Entry &entry : entries) {
        switch (entry.type) {
        case WhaleToolbarLayout::Entry::Type::Separator:
            addSeparator();
            break;
        case WhaleToolbarLayout::Entry::Type::Spacer: {
            // As KXMLGUIBuilder builds <Spacer/>.
            auto spacer = new QWidget(this);
            spacer->setSizePolicy(hasLocationBar ? QSizePolicy::Minimum : QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
            addWidget(spacer);
            break;
        }
        case WhaleToolbarLayout::Entry::Type::Action:
            if (entry.actionId == WhaleLocationBar::actionName()) {
                // One widget: at its first place only (a hand-edited file may name it twice).
                if (!actions().contains(m_locationBar->action())) {
                    addAction(m_locationBar->action());
                }
                break;
            }
            // Only what a File View Toolbar may show (WhaleViewCommandScope::allowedActions()).
            if (QAction *action = m_scope->action(entry.actionId)) {
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

void WhaleFileViewToolBar::contextMenuEvent(QContextMenuEvent *event)
{
    // A menu of its own: KToolBar's needs a main window as parent, and QToolBar would pass the
    // event on to the window, which answers with the menu of its own toolbars and panels.
    event->accept();
    auto menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    if (WhaleToolbarConfigurator *configurator = WhaleToolbarConfigurator::of(this)) {
        QAction *configure = menu->addAction(QIcon::fromTheme(QStringLiteral("configure-toolbars")), i18nc("@action:inmenu", "Configure File View Toolbar…"));
        configure->setObjectName(QStringLiteral("whale_configure_file_view_toolbar"));
        connect(configure, &QAction::triggered, configurator, [configurator]() {
            configurator->configure(WhaleToolbarDescriptor::fileViewId());
        });
    }
    // All of them; "Show File View Toolbars" shows them again (View menu, hamburger menu, the
    // main toolbar's context menu and the menu of a view's background).
    QAction *hide = menu->addAction(i18nc("@action:inmenu", "Hide File View Toolbars"));
    hide->setObjectName(QStringLiteral("whale_hide_file_view_toolbars"));
    connect(hide, &QAction::triggered, WhaleToolbarSettings::self(), [] {
        WhaleToolbarSettings::self()->setFileViewToolBarsVisible(false);
    });
    menu->popup(event->globalPos());
}

bool WhaleFileViewToolBar::eventFilter(QObject *watched, QEvent *event)
{
    // Working with the toolbar of a view makes it the active view, as clicking into the view
    // or its location bar does. The actions act on this view in any case.
    if (event->type() == QEvent::MouseButtonPress && m_container) {
        if (!m_container->isActive()) {
            m_container->setActive(true);
        }
        // A disabled button: tell why, in this view's message area. (The window does that for
        // the shortcut of a disabled action, in the active view; the actions here have none.)
        auto button = qobject_cast<QToolButton *>(watched);
        if (button && m_scope && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
            const QString reason = m_scope->disabledReason(button->defaultAction());
            if (!reason.isEmpty()) {
                m_container->showMessage(reason, KMessageWidget::Warning);
            }
        }
    }
    return KToolBar::eventFilter(watched, event);
}

#include "moc_whalefileviewtoolbar.cpp"
