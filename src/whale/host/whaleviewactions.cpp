/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleviewactions.h"

#include "whaletreecache.h"

#include "dolphin_generalsettings.h"
#include "dolphinviewbridge.h"
#include "views/dolphinviewactionhandler.h"
#include "whalecommandrouter.h"
#include "whaleview.h"
#include "whaleviewcontextimpl.h"
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

WhaleViewActions::WhaleViewActions(KActionCollection *actionCollection, DolphinViewActionHandler *actionHandler, QObject *parent)
    : QObject(parent)
    , m_registry(new WhaleViewRegistry(this))
    , m_treeCache(new WhaleTreeCache(WhaleTreeCache::configuredLimits(), this))
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

    // T2: the handler's actions on selected items ask first whether they are routed.
    if (actionHandler) {
        actionHandler->setSelectionCommandRouter([this](DolphinView *view, const QString &actionName) {
            return routeAction(view, actionName);
        });
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
    disconnect(m_urlConnection);
    disconnect(m_viewSelectionConnection);
    m_host = host;
    if (host) {
        // Synchronously: a shortcut right after switching views must act on the new source.
        m_hostConnection = connect(host, &WhaleViewHost::currentViewChanged, this, [this]() {
            updateCheckedView();
            updateGates();
            notifyCommandSelection();
        });
        m_capabilitiesConnection = connect(host, &WhaleViewHost::capabilitiesChanged, this, [this]() {
            updateGates();
            notifyCommandSelection();
        });
        m_viewSelectionConnection = connect(host, &WhaleViewHost::viewSelectionChanged, this, [this]() {
            updateGates();
            notifyCommandSelection();
        });
        // Immediately, so a shortcut pressed right after a click finds the right state.
        m_selectionConnection = connect(host->bridge(), &DolphinViewBridge::hasSelectionChanged, this, &WhaleViewActions::updateGates);
        m_urlConnection = connect(host->bridge(), &DolphinViewBridge::urlChanged, this, &WhaleViewActions::updateAvailableViews);
    }
    m_switcher->setEnabled(host);
    updateAvailableViews();
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

const QHash<QString, WhaleViewActions::RoutedAction> &WhaleViewActions::routedActions()
{
    using Operation = WhaleFileOperations::Operation;
    static const QHash<QString, RoutedAction> actions = {
        {KStandardAction::name(KStandardAction::RenameFile), {Operation::Rename}},
        {KStandardAction::name(KStandardAction::MoveToTrash), {Operation::Trash}},
        {KStandardAction::name(KStandardAction::DeleteFile), {Operation::Delete}},
        // Delete with the trash's shortcut, for items that cannot be trashed.
        {QStringLiteral("delete_shortcut"), {Operation::Delete}},
        {KStandardAction::name(KStandardAction::Cut), {Operation::Cut}},
        {KStandardAction::name(KStandardAction::Copy), {Operation::Copy}},
        {QStringLiteral("duplicate"), {Operation::Duplicate}},
        {QStringLiteral("copy_location"), {Operation::CopyLocation}},
        {QStringLiteral("properties"), {Operation::Properties}},
        // Dolphin's code copies/moves the routed items (DolphinTabWidget, WhaleCommandRouter::selectedItems()).
        {QStringLiteral("copy_to_inactive_split_view"), {Operation::Copy, true}},
        {QStringLiteral("move_to_inactive_split_view"), {Operation::Cut, true}},
    };
    return actions;
}

bool WhaleViewActions::routeAction(DolphinView *view, const QString &actionName)
{
    const auto routed = routedActions().constFind(actionName);
    if (routed == routedActions().constEnd() || routed->byDolphin) {
        return false;
    }
    return WhaleCommandRouter::perform(view, routed->operation);
}

void WhaleViewActions::notifyCommandSelection()
{
    if (m_host) {
        Q_EMIT commandSelectionChanged(WhaleCommandRouter::selectedItems(m_host->dolphinView()));
    }
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
            // T2: the view's own selection, through WhaleCommandRouter. Dolphin decides about
            // the action states with the routed items; the gate adds what the view's file
            // operations do not support (e.g. nothing that removes the location).
            const QList<QUrl> urls = m_host->currentView()->selectedItems();
            if (urls.isEmpty()) {
                gate(onSelection, QStringLiteral("nothing selected"));
            } else {
                const WhaleFileOperations::Operations supported = m_host->pluginContext()->fileOperations()->supportedOperations(urls);
                for (const QString &name : onSelection) {
                    const auto routed = routedActions().constFind(name);
                    if (routed == routedActions().constEnd()) {
                        gate({name}, QStringLiteral("not routed to a view's own selection"));
                    } else if (!(supported & routed->operation)) {
                        gate({name}, QStringLiteral("not supported for the selection"));
                    }
                }
            }
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
    if (WhaleCommandRouter::perform(m_host->dolphinView(), WhaleFileOperations::Operation::Rename)) {
        return; // a selection of the view's own: always the dialog (T2)
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
    // Choosing a view mode in a plugin view chooses the file list for the location. The mode
    // itself is the file list's business (Dolphin's view properties), not a preferred view.
    if (m_host && !m_host->isDolphinViewShown()) {
        m_host->selectView(WhaleViewHost::dolphinViewId());
    }
}

WhaleViewRegistry *WhaleViewActions::registry() const
{
    return m_registry;
}

WhaleTreeCache *WhaleViewActions::treeCache() const
{
    return m_treeCache;
}

void WhaleViewActions::slotViewSelected(QAction *action)
{
    if (!m_host) {
        return;
    }

    // The user's choice: it becomes the view of the location (O3).
    m_host->selectView(action->data().toString());
    // Reflect the real state, e.g. if the plugin could not be loaded.
    updateCheckedView();
}

void WhaleViewActions::updateAvailableViews()
{
    // Views that declare "X-Whale-RequiresLocal" are offered for local locations only;
    // their metadata says so without loading them. (WhaleViewPlugin::supportsUrl() is
    // asked when a view is about to be shown, see WhaleViewHost.)
    const bool local = m_host && m_host->bridge()->url().isLocalFile();
    const auto actions = m_switcher->actions();
    for (QAction *action : actions) {
        const QString viewId = action->data().toString();
        if (viewId == WhaleViewHost::dolphinViewId()) {
            continue;
        }
        const KPluginMetaData metaData = m_registry->pluginMetaData(viewId);
        const bool requiresLocal = metaData.value(QStringLiteral("X-Whale-RequiresLocal"), false);
        action->setEnabled(metaData.isValid() && (local || !requiresLocal));
    }
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
