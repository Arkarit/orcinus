/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleviewactions.h"

#include "whaleviewhost.h"
#include "whaleviewregistry.h"

#include <KActionCollection>
#include <KActionMenu>
#include <KLocalizedString>
#include <KSelectAction>

#include <QMenu>

WhaleViewActions::WhaleViewActions(KActionCollection *actionCollection, QObject *parent)
    : QObject(parent)
    , m_registry(new WhaleViewRegistry(this))
    , m_switcher(actionCollection->add<KSelectAction>(QStringLiteral("whale_view_switcher")))
{
    m_switcher->setText(i18nc("@action:inmenu View menu", "View Type"));
    m_switcher->setIcon(QIcon::fromTheme(QStringLiteral("view-choose")));
    m_switcher->setToolBarMode(KSelectAction::MenuMode);

    QAction *fileList =
        m_switcher->addAction(QIcon::fromTheme(QStringLiteral("view-list-details")), i18nc("@item:inmenu View type: the built-in file list", "File List"));
    fileList->setData(WhaleViewHost::dolphinViewId());

    const auto plugins = m_registry->plugins();
    for (const KPluginMetaData &metaData : plugins) {
        QAction *action = m_switcher->addAction(QIcon::fromTheme(metaData.iconName()), metaData.name());
        action->setData(metaData.pluginId());
    }

    // Without any plugin view there is nothing to switch.
    m_switcher->setVisible(!plugins.isEmpty());
    m_switcher->setEnabled(false);

    connect(m_switcher, &KSelectAction::actionTriggered, this, &WhaleViewActions::slotViewSelected);

    // The toolbar's view settings dropdown lists the view modes first; the view type follows them.
    if (auto viewSettings = qobject_cast<KActionMenu *>(actionCollection->action(QStringLiteral("view_settings")))) {
        const auto entries = viewSettings->menu()->actions();
        const auto firstSeparator = std::find_if(entries.cbegin(), entries.cend(), [](const QAction *action) {
            return action->isSeparator();
        });
        viewSettings->insertAction(firstSeparator != entries.cend() ? *firstSeparator : nullptr, m_switcher);
    }
}

WhaleViewActions::~WhaleViewActions() = default;

void WhaleViewActions::setCurrentHost(WhaleViewHost *host)
{
    disconnect(m_hostConnection);
    m_host = host;
    if (host) {
        m_hostConnection = connect(host, &WhaleViewHost::currentViewChanged, this, &WhaleViewActions::updateCheckedView);
    }
    m_switcher->setEnabled(host);
    updateCheckedView();
}

WhaleViewRegistry *WhaleViewActions::registry() const
{
    return m_registry;
}

void WhaleViewActions::slotViewSelected(QAction *action)
{
    if (!m_host) {
        return;
    }

    const QString viewId = action->data().toString();
    if (viewId == WhaleViewHost::dolphinViewId()) {
        m_host->showDolphinView();
    } else if (WhaleViewPlugin *plugin = m_registry->plugin(viewId)) {
        m_host->showPluginView(viewId, plugin);
    }
    // Reflect the real state, e.g. if the plugin could not be loaded.
    updateCheckedView();
}

void WhaleViewActions::updateCheckedView()
{
    const QString viewId = m_host ? m_host->currentViewId() : WhaleViewHost::dolphinViewId();
    const auto actions = m_switcher->actions();
    for (QAction *action : actions) {
        action->setChecked(action->data().toString() == viewId);
    }
}

#include "moc_whaleviewactions.cpp"
