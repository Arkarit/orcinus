/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleviewactions.h"

#include "dolphin_generalsettings.h"
#include "dolphinviewbridge.h"
#include "whaleview.h"
#include "whaleviewdebug.h"
#include "whaleviewhost.h"
#include "whaleviewregistry.h"

#include <KActionCollection>
#include <KActionMenu>
#include <KLocalizedString>
#include <KStandardAction>

#include <KSelectAction>
#include <QActionGroup>

#include <QMenu>

WhaleViewActions::WhaleViewActions(KActionCollection *actionCollection, QObject *parent)
    : QObject(parent)
    , m_registry(new WhaleViewRegistry(this))
    , m_switcher(actionCollection->add<KSelectAction>(QStringLiteral("whale_view_switcher")))
    , m_actionCollection(actionCollection)
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

    // F2 in a plugin view. DolphinViewActionHandler::slotRename() leaves it to us then.
    if (QAction *rename = actionCollection->action(KStandardAction::name(KStandardAction::RenameFile))) {
        connect(rename, &QAction::triggered, this, &WhaleViewActions::slotRename);
    }
    // Choosing a view mode of the file list shows the file list.
    for (const QString &mode : {QStringLiteral("icons"), QStringLiteral("compact"), QStringLiteral("details")}) {
        if (QAction *action = actionCollection->action(mode)) {
            connect(action, &QAction::triggered, this, &WhaleViewActions::slotViewModeTriggered);
        }
    }
}

WhaleViewActions::~WhaleViewActions() = default;

void WhaleViewActions::setCurrentHost(WhaleViewHost *host)
{
    disconnect(m_hostConnection);
    disconnect(m_selectionConnection);
    disconnect(m_capabilitiesConnection);
    m_host = host;
    if (host) {
        m_hostConnection = connect(host, &WhaleViewHost::currentViewChanged, this, [this]() {
            updateCheckedView();
            updateGates();
        });
        m_capabilitiesConnection = connect(host, &WhaleViewHost::capabilitiesChanged, this, &WhaleViewActions::updateGates);
        // Immediately, so a shortcut pressed right after a click finds the right state.
        m_selectionConnection = connect(host->bridge(), &DolphinViewBridge::hasSelectionChanged, this, &WhaleViewActions::updateGates);
    }
    m_switcher->setEnabled(host);
    updateCheckedView();
    updateGates();
}

QStringList WhaleViewActions::gateableActions()
{
    return {
        // The selection mode of the file list
        QStringLiteral("toggle_selection_mode"),
        QStringLiteral("toggle_selection_mode_tool_bar"),
        // Zoom and previews
        KStandardAction::name(KStandardAction::ZoomIn),
        KStandardAction::name(KStandardAction::ZoomOut),
        QStringLiteral("view_zoom_reset"),
        QStringLiteral("show_preview"),
        // Order and additional information of the items
        QStringLiteral("sort"),
        QStringLiteral("folders_first"),
        QStringLiteral("hidden_last"),
        QStringLiteral("additional_info"),
        QStringLiteral("group_by"),
        // Selecting
        KStandardAction::name(KStandardAction::SelectAll),
        QStringLiteral("invert_selection"),
        // Working on the selected items
        KStandardAction::name(KStandardAction::RenameFile),
        KStandardAction::name(KStandardAction::MoveToTrash),
        KStandardAction::name(KStandardAction::DeleteFile),
        QStringLiteral("delete_shortcut"),
        KStandardAction::name(KStandardAction::Cut),
        KStandardAction::name(KStandardAction::Copy),
        QStringLiteral("duplicate"),
        QStringLiteral("copy_location"),
        QStringLiteral("show_target"),
        QStringLiteral("copy_to_inactive_split_view"),
        QStringLiteral("move_to_inactive_split_view"),
        QStringLiteral("compare_files"),
    };
}

void WhaleViewActions::notifyViewChanged()
{
    Q_EMIT sessionChanged();
}

QString WhaleViewActions::gateReason(const QString &actionName) const
{
    return m_gateReasons.value(actionName);
}

void WhaleViewActions::updateGates()
{
    QHash<QString, QString> reasons;
    if (m_host && !m_host->isDolphinViewShown()) {
        using C = WhaleViewCapability;
        const WhaleViewCapabilities capabilities = m_host->currentView()->capabilities();
        auto gate = [&reasons](const QStringList &names, const QString &reason) {
            for (const QString &name : names) {
                if (!reasons.contains(name)) {
                    reasons.insert(name, reason);
                }
            }
        };

        // Plugin views offer their own way of choosing items.
        gate({QStringLiteral("toggle_selection_mode"), QStringLiteral("toggle_selection_mode_tool_bar")},
             QStringLiteral("selection mode is part of the file list"));
        if (!(capabilities & C::Zoom)) {
            gate({KStandardAction::name(KStandardAction::ZoomIn), KStandardAction::name(KStandardAction::ZoomOut), QStringLiteral("view_zoom_reset")},
                 QStringLiteral("no Zoom capability"));
        }
        if (!(capabilities & C::Previews)) {
            gate({QStringLiteral("show_preview")}, QStringLiteral("no Previews capability"));
        }
        if (!(capabilities & C::HostOrder)) {
            gate({QStringLiteral("sort"),
                  QStringLiteral("folders_first"),
                  QStringLiteral("hidden_last"),
                  QStringLiteral("additional_info"),
                  QStringLiteral("group_by")},
                 QStringLiteral("no HostOrder capability"));
        }
        if (!(capabilities & C::Grouping)) {
            gate({QStringLiteral("group_by")}, QStringLiteral("no Grouping capability"));
        }
        if (!(capabilities & C::Selection)) {
            gate({KStandardAction::name(KStandardAction::SelectAll), QStringLiteral("invert_selection")}, QStringLiteral("no Selection capability"));
        } else if (!(capabilities & C::MultiSelection)) {
            gate({KStandardAction::name(KStandardAction::SelectAll), QStringLiteral("invert_selection")}, QStringLiteral("no MultiSelection capability"));
        }

        const QStringList onSelection = gateableActions().mid(gateableActions().indexOf(KStandardAction::name(KStandardAction::RenameFile)));
        if (!(capabilities & C::Selection)) {
            gate(onSelection, QStringLiteral("no Selection capability"));
        } else if (capabilities & C::RecursiveContent) {
            gate(onSelection, QStringLiteral("RecursiveContent: items outside the listing cannot be selected yet"));
        } else if (!m_host->bridge()->hasSelection()) {
            // In the file list these actions start the selection mode, which plugin views do not have.
            gate(onSelection, QStringLiteral("nothing selected"));
        }
    }
    m_gateReasons = reasons;

    const QStringList names = gateableActions();
    for (const QString &name : names) {
        QAction *action = m_actionCollection->action(name);
        if (!action) {
            continue;
        }
        QActionGroup *group = m_gates.value(name);
        if (!group) {
            if (action->actionGroup()) {
                qCWarning(WhaleViewDebug) << "Cannot gate the action" << name << "because it is in an action group already";
                continue;
            }
            group = new QActionGroup(this);
            group->setExclusionPolicy(QActionGroup::ExclusionPolicy::None);
            group->addAction(action);
            m_gates.insert(name, group);
        }
        group->setEnabled(!reasons.contains(name));
    }
}

void WhaleViewActions::slotRename()
{
    if (!m_host || m_host->isDolphinViewShown()) {
        return; // DolphinViewActionHandler::slotRename() handles the file list.
    }
    DolphinViewBridge *bridge = m_host->bridge();
    const WhaleSelection selection = bridge->selection();
    if (selection.isEmpty()) {
        return; // gated; there is no selection mode in plugin views
    }
    WhaleView *view = m_host->currentView();
    // As in the file list: one item inline if the user wants that and the view can,
    // otherwise the dialog (which also renames several items).
    if (selection.selected().count() == 1 && GeneralSettings::renameInline() && (view->capabilities() & WhaleViewCapability::InlineRename)) {
        view->startRename(selection.selected().first());
    } else {
        bridge->renameSelectedItemsWithDialog();
    }
}

void WhaleViewActions::slotViewModeTriggered()
{
    if (m_host && !m_host->isDolphinViewShown()) {
        m_host->showDolphinView();
    }
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
