/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaletoolbarmanager.h"

#include "whalefileviewtoolbarclient.h"
#include "whalelocationbar.h"
#include "whaletoolbarsettings.h"
#include "whaleviewcommandscope.h"

#include <KActionCollection>
#include <KLocalizedString>
#include <KToggleAction>
#include <KXMLGUIFactory>
#include <KXmlGuiWindow>

#include <QMenu>
#include <QWidget>

namespace
{
QList<WhaleToolbarManager *> s_managers;
}

WhaleToolbarManager::WhaleToolbarManager(QWidget *window)
    : QObject(window)
    , m_fileViewClient(std::make_unique<WhaleFileViewToolbarClient>())
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
        }
    });
    plugIntoViewMenu();
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
