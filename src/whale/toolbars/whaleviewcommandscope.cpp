/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleviewcommandscope.h"

#include "dolphinviewcontainer.h"
#include "views/dolphinview.h"
#include "views/dolphinviewactionhandler.h"
#include "whalecontainercommands.h"
#include "whaleviewactions.h"
#include "whaleviewservices.h"

#include <KActionCollection>
#include <KActionMenu>

#include <QMenu>

WhaleViewCommandScope::WhaleViewCommandScope(DolphinViewContainer *container, QObject *parent)
    : QObject(parent)
    , m_container(container)
    , m_actionCollection(new KActionCollection(this, QStringLiteral("whale-file-view")))
{
    Q_ASSERT(container);
    m_commands = new WhaleContainerCommands(container, m_actionCollection, this);

    // Without the SelectionMode::ActionTextHelper: that one belongs to the window's actions.
    // Its "basic_actions" menu misses Cut/Copy/Paste here (Qt warns about null actions, as in
    // DolphinPart); the File View Toolbar does not offer that menu.
    m_actionHandler = new DolphinViewActionHandler(m_actionCollection, nullptr, this);
    m_actionHandler->setCurrentView(container->view());
    // setCurrentView() leaves the state of the actions to the owner (the window calls this
    // for its own handler on every change of the active view).
    m_actionHandler->updateViewActions();

    // DolphinViewActionHandler parents the "view_settings" menu to QApplication::activeWindow()
    // (for accessibility). QWidget::setParent() drops the popup type then, so when this view
    // is created in an active window (e.g. when the split view opens), the menu became a
    // plain child widget of that (or even another) window and never opened. A popup of the
    // view's own window instead.
    if (auto viewSettings = qobject_cast<KActionMenu *>(action(QStringLiteral("view_settings")))) {
        QMenu *menu = viewSettings->menu();
        if (menu->windowType() != Qt::Popup || menu->parentWidget() != container->window()) {
            menu->setParent(container->window(), Qt::Popup);
        }
    }

    m_viewActions = new WhaleViewActions(WhaleViewServices::ensure(container->window()), m_actionCollection, m_actionHandler, this);
    m_viewActions->setCurrentHost(container->viewHost());

    // What the window sets for its own actions (DolphinMainWindow::updateViewActions()).
    connect(container->view(), &DolphinView::viewSettingsChanged, this, &WhaleViewCommandScope::updateRestoreDefaultsAction);
    updateRestoreDefaultsAction(container->view()->isViewSettingsDefaults());

    // Last: the creators above set the actions' default shortcuts.
    WhaleViewActions::clearShortcuts(m_actionCollection);
}

WhaleViewCommandScope::~WhaleViewCommandScope() = default;

DolphinViewContainer *WhaleViewCommandScope::container() const
{
    return m_container;
}

KActionCollection *WhaleViewCommandScope::actionCollection() const
{
    return m_actionCollection;
}

QAction *WhaleViewCommandScope::action(const QString &name) const
{
    return m_actionCollection->action(name);
}

WhaleViewActions *WhaleViewCommandScope::viewActions() const
{
    return m_viewActions;
}

void WhaleViewCommandScope::updateRestoreDefaultsAction(bool isDefault)
{
    if (QAction *restore = action(QStringLiteral("restore_view_settings_default"))) {
        restore->setEnabled(!isDefault);
    }
}

#include "moc_whaleviewcommandscope.cpp"
