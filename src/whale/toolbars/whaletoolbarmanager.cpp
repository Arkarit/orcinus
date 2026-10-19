/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaletoolbarmanager.h"

#include "whalefileviewtoolbar.h"
#include "whalefileviewtoolbarclient.h"
#include "whalelocationbar.h"
#include "whaletoolbarmigration.h"
#include "whaletoolbarsettings.h"
#include "whaleviewcommandscope.h"

#include <KActionCollection>
#include <KLocalizedString>
#include <KToggleAction>
#include <KToolBar>
#include <KXMLGUIFactory>
#include <KXmlGuiWindow>

#include <QChildEvent>
#include <QMenu>
#include <QWidget>

namespace
{
QList<WhaleToolbarManager *> s_managers;

// The TB7 migration before a window reads the File View Toolbar layout (a no-op once done).
std::unique_ptr<WhaleFileViewToolbarClient> migratedClient()
{
    WhaleToolbarMigration::run();
    return std::make_unique<WhaleFileViewToolbarClient>();
}
}

WhaleToolbarManager::WhaleToolbarManager(QWidget *window)
    : QObject(window)
    , m_fileViewClient(migratedClient())
    , m_fileViewLayout(m_fileViewClient->layout())
{
    s_managers.append(this);
}

WhaleToolbarManager::~WhaleToolbarManager()
{
    s_managers.removeOne(this);
}

QString WhaleToolbarManager::showFileViewToolBarsActionName()
{
    return QStringLiteral("whale_show_file_view_toolbars");
}

void WhaleToolbarManager::setupActions(KXmlGuiWindow *window)
{
    Q_ASSERT(window && window == parent());
    if (m_showFileViewToolBars) {
        return;
    }
    m_window = window;
    WhaleToolbarSettings *settings = WhaleToolbarSettings::self();
    m_showFileViewToolBars = window->actionCollection()->add<KToggleAction>(showFileViewToolBarsActionName());
    m_showFileViewToolBars->setText(i18nc("@action:inmenu View", "Show File View Toolbars"));
    m_showFileViewToolBars->setWhatsThis(i18nc("@info:whatsthis",
                                               "This shows or hides the toolbars at the top of the file views, in all views, tabs and windows. "
                                               "They can be configured in Configure Toolbars also while they are hidden."));
    m_showFileViewToolBars->setChecked(settings->fileViewToolBarsVisible());
    connect(m_showFileViewToolBars, &QAction::triggered, settings, &WhaleToolbarSettings::setFileViewToolBarsVisible);
    connect(settings, &WhaleToolbarSettings::fileViewToolBarsVisibleChanged, m_showFileViewToolBars, &QAction::setChecked);

    // Not in the View menu of dolphinui.rc: the users' own copies of that file keep their menus
    // (and the version of the file stays). The GUI is built when the window adds itself to its
    // factory: on setupGUI(), createGUI() and after Configure Toolbars.
    connect(window->guiFactory(), &KXMLGUIFactory::clientAdded, this, [this](KXMLGUIClient *client) {
        if (client == m_window) {
            plugIntoViewMenu();
            watchToolBarMenus();
        }
    });
    plugIntoViewMenu();
    watchToolBarMenus();
}

KToggleAction *WhaleToolbarManager::showFileViewToolBarsAction() const
{
    return m_showFileViewToolBars;
}

void WhaleToolbarManager::plugIntoViewMenu()
{
    KXMLGUIFactory *factory = m_window ? m_window->guiFactory() : nullptr;
    auto menu = factory ? qobject_cast<QMenu *>(factory->container(QStringLiteral("view"), m_window)) : nullptr;
    if (!menu || !m_showFileViewToolBars) {
        return;
    }
    menu->removeAction(m_showFileViewToolBars);
    // With the other parts of the window that can be shown or hidden: after "Show Panels" and
    // "Location Bar".
    const QList<QAction *> actions = menu->actions();
    auto locationBar = qobject_cast<QMenu *>(factory->container(QStringLiteral("location_bar"), m_window));
    const int index = locationBar ? actions.indexOf(locationBar->menuAction()) : -1;
    if (index >= 0 && index + 1 < actions.count()) {
        menu->insertAction(actions.at(index + 1), m_showFileViewToolBars);
    } else {
        menu->addAction(m_showFileViewToolBars);
    }
}

void WhaleToolbarManager::watchToolBarMenus()
{
    // KToolBar offers no way to extend its context menu ("Toolbar Settings"): it creates the
    // menu as its child the first time it is asked for and keeps it. So the manager watches the
    // window's toolbars for new child menus. They are built again with the GUI.
    if (!m_window) {
        return;
    }
    const QList<KToolBar *> toolBars = m_window->toolBars();
    for (KToolBar *toolBar : toolBars) {
        if (qobject_cast<WhaleFileViewToolBar *>(toolBar)) {
            continue; // has a menu of its own, with "Hide File View Toolbars"
        }
        toolBar->installEventFilter(this);
        const QList<QMenu *> menus = toolBar->findChildren<QMenu *>(QString(), Qt::FindDirectChildrenOnly);
        for (QMenu *menu : menus) {
            watchMenu(menu);
        }
    }
}

void WhaleToolbarManager::watchMenu(QMenu *menu)
{
    // Before KToolBar's own handler: the entry comes before "Configure Toolbars…".
    connect(menu, &QMenu::aboutToShow, this, &WhaleToolbarManager::plugIntoToolBarMenu, Qt::UniqueConnection);
}

void WhaleToolbarManager::plugIntoToolBarMenu()
{
    auto menu = qobject_cast<QMenu *>(sender());
    // Not into the short-lived menus KToolBar shows for buttons with a popup (they contain the
    // "Toolbar Settings" menu as a submenu).
    if (!menu || !m_showFileViewToolBars || menu->testAttribute(Qt::WA_DeleteOnClose)) {
        return;
    }
    menu->addAction(m_showFileViewToolBars); // at the end, also when it is there already
}

bool WhaleToolbarManager::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::ChildAdded) {
        // The child is not constructed yet: a menu is recognized by its first event after that
        // (KToolBar adds its actions at once).
        QObject *child = static_cast<QChildEvent *>(event)->child();
        if (child->isWidgetType() && qobject_cast<KToolBar *>(watched)) {
            child->installEventFilter(this);
        }
    } else if (!qobject_cast<KToolBar *>(watched) && watched->isWidgetType()) {
        if (auto menu = qobject_cast<QMenu *>(watched)) {
            watchMenu(menu);
            watched->removeEventFilter(this);
        } else if (watched->metaObject() != &QWidget::staticMetaObject) {
            watched->removeEventFilter(this); // constructed, but no menu
        }
    }
    return QObject::eventFilter(watched, event);
}

WhaleToolbarManager *WhaleToolbarManager::ensure(QWidget *window)
{
    Q_ASSERT(window && window->isWindow());
    if (WhaleToolbarManager *manager = of(window)) {
        return manager;
    }
    return new WhaleToolbarManager(window);
}

WhaleToolbarManager *WhaleToolbarManager::of(const QWidget *widget)
{
    if (!widget) {
        return nullptr;
    }
    return widget->window()->findChild<WhaleToolbarManager *>(QString(), Qt::FindDirectChildrenOnly);
}

QList<WhaleToolbarDescriptor> WhaleToolbarManager::descriptors() const
{
    WhaleToolbarDescriptor global;
    global.id = WhaleToolbarDescriptor::globalId();
    global.displayName = i18nc("@title:menu", "Main Toolbar");
    global.xmlGuiToolBar = QStringLiteral("mainToolBar");
    global.rcFile = QStringLiteral("dolphinui.rc");
    global.target = WhaleToolbarDescriptor::Target::ActiveView;

    WhaleToolbarDescriptor fileView;
    fileView.id = WhaleToolbarDescriptor::fileViewId();
    fileView.displayName = i18nc("@title:menu", "File View Toolbar");
    fileView.xmlGuiToolBar = WhaleFileViewToolbarClient::toolBarName();
    fileView.rcFile = WhaleFileViewToolbarClient::rcFile();
    fileView.target = WhaleToolbarDescriptor::Target::OwnFileView;
    fileView.allowedActions = WhaleViewCommandScope::allowedActions() << WhaleLocationBar::actionName();
    return {global, fileView};
}

WhaleToolbarDescriptor WhaleToolbarManager::descriptor(const QString &id) const
{
    const auto all = descriptors();
    for (const WhaleToolbarDescriptor &descriptor : all) {
        if (descriptor.id == id) {
            return descriptor;
        }
    }
    return {};
}

WhaleToolbarLayout WhaleToolbarManager::fileViewLayout() const
{
    return m_fileViewLayout;
}

void WhaleToolbarManager::reloadFileViewLayout()
{
    m_fileViewClient->reload();
    const WhaleToolbarLayout layout = m_fileViewClient->layout();
    if (layout != m_fileViewLayout) {
        m_fileViewLayout = layout;
        Q_EMIT fileViewLayoutChanged();
    }
}

void WhaleToolbarManager::reloadAllFileViewLayouts()
{
    const auto managers = s_managers;
    for (WhaleToolbarManager *manager : managers) {
        manager->reloadFileViewLayout();
    }
}

#include "moc_whaletoolbarmanager.cpp"
