/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleviewactions.h"

#include "whaletreecache.h"

#include "dolphin_generalsettings.h"
#include "dolphinviewbridge.h"
#include "views/dolphinview.h"
#include "views/dolphinviewactionhandler.h"
#include "whalecommandrouter.h"
#include "whalecontentscope.h"
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
#include <KToggleAction>
#include <QActionGroup>

#include <KXMLGUIClient>
#include <KXMLGUIFactory>
#include <QMenu>
#include <QToolButton>

#include <optional>

namespace
{
// The mode of the file list that a view mode entry chooses, if it is one of the file list's.
std::optional<DolphinView::Mode> fileListMode(const QAction *action)
{
    if (action->data().metaType() != QMetaType::fromType<DolphinView::Mode>()) {
        return std::nullopt;
    }
    return action->data().value<DolphinView::Mode>();
}
}

WhaleViewActions::WhaleViewActions(KActionCollection *actionCollection, DolphinViewActionHandler *actionHandler, QObject *parent)
    : QObject(parent)
    , m_registry(new WhaleViewRegistry(this))
    , m_treeCache(new WhaleTreeCache(WhaleTreeCache::configuredLimits(), this))
    , m_viewModesSeparator(new QAction(this))
    , m_includeSubfolders(actionCollection->add<KToggleAction>(QStringLiteral("whale_include_subfolders")))
    , m_actionCollection(actionCollection)
{
    m_viewModesSeparator->setSeparator(true);
    // The view modes: the file list's modes, then the plugin views, in one exclusive group.
    auto viewModeGroup = qobject_cast<KSelectAction *>(actionCollection->action(QStringLiteral("view_mode")));
    for (const QString &mode : {QStringLiteral("icons"), QStringLiteral("compact"), QStringLiteral("details")}) {
        if (QAction *action = actionCollection->action(mode)) {
            m_viewModes.append(action);
            // Choosing a view mode of the file list shows the file list.
            connect(action, &QAction::triggered, this, &WhaleViewActions::slotViewModeTriggered);
        }
    }
    const auto plugins = m_registry->plugins();
    for (const KPluginMetaData &metaData : plugins) {
        auto entry = new QAction(QIcon::fromTheme(metaData.iconName()), metaData.name(), this);
        entry->setData(metaData.pluginId());
        entry->setCheckable(true);
        entry->setEnabled(false);
        if (viewModeGroup) {
            viewModeGroup->addAction(entry);
        }
        connect(entry, &QAction::triggered, this, [this, entry]() {
            slotViewSelected(entry);
        });
        m_viewModes.append(entry);
        m_pluginViewEntries.insert(metaData.pluginId(), entry);
    }
    if (m_viewModes.count() > MaxDirectViewModes) {
        m_viewModesMenu = new KActionMenu(QIcon::fromTheme(QStringLiteral("view-choose")), i18nc("@action:inmenu View menu", "View Modes"), this);
        m_viewModesMenu->setPopupMode(QToolButton::InstantPopup);
        for (QAction *action : std::as_const(m_viewModes)) {
            m_viewModesMenu->addAction(action);
        }
    }

    // F1: the content scope of the file list, a property of its content, not a view.
    m_includeSubfolders->setText(i18nc("@action:inmenu View menu", "Show flat (no folders)"));
    m_includeSubfolders->setIcon(QIcon::fromTheme(QStringLiteral("folder-tree")));
    m_includeSubfolders->setToolTip(i18nc("@info:tooltip", "Show the files of all subfolders in the file list, without the folders"));
    m_includeSubfolders->setEnabled(false);
    connect(m_includeSubfolders, &QAction::triggered, this, [this](bool checked) {
        if (m_host && m_host->supportsViewOption(WhaleViewHost::ViewOption::FlatContent)) {
            m_host->selectContentScope(checked ? WhaleContentScope::RecursiveFiles : WhaleContentScope::CurrentFolder);
        }
        updateContentScopeAction();
    });

    if (auto viewSettings = qobject_cast<KActionMenu *>(actionCollection->action(QStringLiteral("view_settings")))) {
        arrangeViewSettingsMenu(viewSettings);
    }

    if (actionHandler) {
        // T2: the handler's actions on selected items ask first whether they are routed.
        actionHandler->setSelectionCommandRouter([this](DolphinView *view, const QString &actionName) {
            return routeAction(view, actionName);
        });
        // The handler checks the entry of a shown plugin view instead of the file list's mode.
        actionHandler->setShownViewEntry([this](DolphinView *view) -> QAction * {
            const WhaleViewHost *host = WhaleCommandRouter::hostOf(view);
            return host && !host->isDolphinViewShown() ? m_pluginViewEntries.value(host->currentViewId()) : nullptr;
        });
    }

    // F2 in a plugin view. DolphinViewActionHandler::slotRename() leaves it to us then.
    if (QAction *rename = actionCollection->action(KStandardAction::name(KStandardAction::RenameFile))) {
        connect(rename, &QAction::triggered, this, &WhaleViewActions::slotRename);
    }
}

void WhaleViewActions::arrangeViewSettingsMenu(KActionMenu *viewSettings)
{
    // The toolbar's view settings dropdown: the view modes, the order and additional
    // information of the items, then the options of what the view shows.
    QMenu *menu = viewSettings->menu();
    const auto entries = menu->actions();
    const auto firstSeparator = std::find_if(entries.cbegin(), entries.cend(), [](const QAction *action) {
        return action->isSeparator();
    });
    QAction *beforeSeparator = firstSeparator != entries.cend() ? *firstSeparator : nullptr;
    if (m_viewModesMenu) {
        for (QAction *action : std::as_const(m_viewModes)) {
            menu->removeAction(action);
        }
        menu->insertAction(beforeSeparator, m_viewModesMenu);
    } else {
        for (QAction *action : std::as_const(m_viewModes)) {
            if (!menu->actions().contains(action)) {
                menu->insertAction(beforeSeparator, action);
            }
        }
    }

    QAction *showPreview = m_actionCollection->action(QStringLiteral("show_preview"));
    if (showPreview && menu->actions().contains(showPreview)) {
        menu->insertSeparator(showPreview);
        menu->insertAction(showPreview, m_includeSubfolders);
    } else {
        menu->insertAction(beforeSeparator, m_includeSubfolders);
    }
}

QList<QAction *> WhaleViewActions::viewModeActions() const
{
    return m_viewModes;
}

QAction *WhaleViewActions::viewModeAction(const QString &id) const
{
    const bool fileList = id == WhaleViewHost::dolphinViewId();
    if (fileList && !m_host) {
        return nullptr;
    }
    for (QAction *action : m_viewModes) {
        if (fileList ? fileListMode(action) == std::optional(m_host->dolphinView()->viewMode()) : viewModeId(action) == id) {
            return action;
        }
    }
    return nullptr;
}

QString WhaleViewActions::viewModeId(const QAction *action) const
{
    const QString pluginId = m_pluginViewEntries.key(const_cast<QAction *>(action));
    return pluginId.isEmpty() ? action->objectName() : pluginId;
}

QList<QAction *> WhaleViewActions::viewModeMenuEntries() const
{
    return m_viewModesMenu ? QList<QAction *>{m_viewModesMenu} : m_viewModes;
}

void WhaleViewActions::plugViewModes(KXMLGUIClient *client)
{
    auto menu = client->factory() ? qobject_cast<QMenu *>(client->factory()->container(QStringLiteral("view"), client)) : nullptr;
    QAction *sort = m_actionCollection->action(QStringLiteral("sort"));
    if (!menu || !sort || !menu->actions().contains(sort)) {
        return;
    }
    const QList<QAction *> entries = viewModeMenuEntries() << m_viewModesSeparator;
    for (QAction *entry : entries) {
        menu->removeAction(entry);
    }
    menu->insertActions(sort, entries);
}

WhaleViewActions::~WhaleViewActions() = default;

void WhaleViewActions::setCurrentHost(WhaleViewHost *host)
{
    disconnect(m_hostConnection);
    disconnect(m_selectionConnection);
    disconnect(m_capabilitiesConnection);
    disconnect(m_urlConnection);
    disconnect(m_viewSelectionConnection);
    disconnect(m_contentScopeConnection);
    m_host = host;
    if (host) {
        // Synchronously: a shortcut right after switching views must act on the new source.
        m_hostConnection = connect(host, &WhaleViewHost::currentViewChanged, this, [this]() {
            updateCheckedView();
            updateGates();
            // "Show flat (no folders)" depends on the shown view as well, not only on the
            // location and the scope.
            updateContentScopeAction();
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
        m_urlConnection = connect(host->bridge(), &DolphinViewBridge::urlChanged, this, [this]() {
            updateAvailableViews();
            updateContentScopeAction();
        });
        m_contentScopeConnection = connect(host, &WhaleViewHost::contentScopeChanged, this, [this]() {
            updateContentScopeAction();
            updateGates(); // F1: no destination in a flat file list
        });
    }
    updateAvailableViews();
    updateCheckedView();
    updateGates();
    updateContentScopeAction();
}

void WhaleViewActions::updateContentScopeAction()
{
    // Only the file list at a local folder can show the files below; elsewhere the action
    // is disabled, while the location keeps its scope (e.g. for when the file list is shown
    // again), so the action still shows it.
    m_includeSubfolders->setEnabled(m_host && m_host->supportsViewOption(WhaleViewHost::ViewOption::FlatContent));
    m_includeSubfolders->setChecked(m_host && m_host->contentScope() == WhaleContentScope::RecursiveFiles);
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
        QStringLiteral("show_hidden_files"),
        // Order and additional information of the items
        QStringLiteral("sort"),
        QStringLiteral("folders_first"),
        QStringLiteral("hidden_last"),
        QStringLiteral("additional_info"),
        QStringLiteral("group_by"),
        // Creating items in the location (Duplicate below as well)
        QStringLiteral("new_menu"),
        QStringLiteral("create_dir"),
        QStringLiteral("create_file"),
        QStringLiteral("create_subdir"),
        KStandardAction::name(KStandardAction::Paste),
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

namespace
{
// The actions that create items in the location, with what they are refused as.
const QHash<QString, WhaleViewHost::Refusal> &creatingActions()
{
    static const QHash<QString, WhaleViewHost::Refusal> actions = {
        {QStringLiteral("new_menu"), WhaleViewHost::Refusal::Create},
        {QStringLiteral("create_dir"), WhaleViewHost::Refusal::Create},
        {QStringLiteral("create_file"), WhaleViewHost::Refusal::Create},
        {QStringLiteral("create_subdir"), WhaleViewHost::Refusal::Create},
        {QStringLiteral("duplicate"), WhaleViewHost::Refusal::Create},
        {KStandardAction::name(KStandardAction::Paste), WhaleViewHost::Refusal::Paste},
    };
    return actions;
}

const QString NoDestination = QStringLiteral("no destination folder");
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
    if (const auto creating = creatingActions().constFind(actionName); creating != creatingActions().constEnd()) {
        // No destination in a view of items from many folders (gated; triggered anyway, e.g.
        // through D-Bus or a menu built before).
        WhaleViewHost *host = WhaleCommandRouter::hostOf(view);
        if (host && host->refuse(*creating)) {
            return true;
        }
    }
    if (actionName == KStandardAction::name(KStandardAction::Paste)) {
        return false;
    }
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
    auto gate = [&reasons](const QStringList &names, const QString &reason) {
        for (const QString &name : names) {
            if (!reasons.contains(name)) {
                reasons.insert(name, reason);
            }
        }
    };
    // Items from many folders (a flat file list, a treemap): nothing is created among them,
    // since it could not be told where. Removing and cutting items still work.
    if (m_host && !m_host->hasDestination()) {
        gate(creatingActions().keys(), NoDestination);
    }
    if (m_host && !m_host->isDolphinViewShown()) {
        using C = WhaleViewCapability;
        const WhaleViewCapabilities capabilities = m_host->currentView()->capabilities();

        // Plugin views offer their own way of choosing items.
        gate({QStringLiteral("toggle_selection_mode"), QStringLiteral("toggle_selection_mode_tool_bar")},
             QStringLiteral("selection mode is part of the file list"));
        if (!(capabilities & C::Zoom)) {
            gate({KStandardAction::name(KStandardAction::ZoomIn), KStandardAction::name(KStandardAction::ZoomOut), QStringLiteral("view_zoom_reset")},
                 QStringLiteral("no Zoom capability"));
        }
        // The settings stay as they are while disabled (QActionGroup gates keep the state).
        if (!m_host->supportsViewOption(WhaleViewHost::ViewOption::Previews)) {
            gate({QStringLiteral("show_preview")}, QStringLiteral("no Previews capability"));
        }
        if (!m_host->supportsViewOption(WhaleViewHost::ViewOption::HiddenFiles)) {
            gate({QStringLiteral("show_hidden_files")}, QStringLiteral("no HiddenFiles capability"));
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
    updateDisabledReasons();
}

void WhaleViewActions::setDisabledReasonHook(const DisabledReasonHook &hook)
{
    m_disabledReasonHook = hook;
    updateDisabledReasons();
}

void WhaleViewActions::updateDisabledReasons()
{
    if (!m_disabledReasonHook) {
        return;
    }
    // Recorded first: Dolphin may set its own explanations from the hook, which calls this again.
    const QStringList wasExplained = m_explainedGates;
    m_explainedGates.clear();
    for (auto it = creatingActions().cbegin(); it != creatingActions().cend(); ++it) {
        if (m_gateReasons.value(it.key()) == NoDestination) {
            m_explainedGates.append(it.key());
        }
    }
    for (auto it = creatingActions().cbegin(); it != creatingActions().cend(); ++it) {
        QAction *action = m_actionCollection->action(it.key());
        if (!action) {
            continue;
        }
        if (m_explainedGates.contains(it.key())) {
            m_disabledReasonHook(action, m_host->refusal(it.value()));
        } else if (wasExplained.contains(it.key())) {
            m_disabledReasonHook(action, QString());
        }
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
    // itself is the file list's business (Dolphin's view properties), not a preferred view,
    // so switching modes within the file list chooses nothing (O3).
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

void WhaleViewActions::slotViewSelected(QAction *entry)
{
    if (!m_host) {
        return;
    }

    // The user's choice: it becomes the view of the location (O3).
    m_host->selectView(m_pluginViewEntries.key(entry));
    // Reflect the real state, e.g. if the plugin could not be loaded.
    updateCheckedView();
}

void WhaleViewActions::updateAvailableViews()
{
    // Views that declare "X-Whale-RequiresLocal" are offered for local locations only;
    // their metadata says so without loading them. (WhaleViewPlugin::supportsUrl() is
    // asked when a view is about to be shown, see WhaleViewHost.) The entries of views
    // that are not offered stay in the menus, disabled.
    const bool local = m_host && m_host->bridge()->url().isLocalFile();
    for (auto it = m_pluginViewEntries.cbegin(); it != m_pluginViewEntries.cend(); ++it) {
        const KPluginMetaData metaData = m_registry->pluginMetaData(it.key());
        const bool requiresLocal = metaData.value(QStringLiteral("X-Whale-RequiresLocal"), false);
        it.value()->setEnabled(m_host && metaData.isValid() && (local || !requiresLocal));
    }
}

void WhaleViewActions::updateCheckedView()
{
    if (!m_host) {
        return;
    }
    if (!m_host->isDolphinViewShown()) {
        if (QAction *entry = m_pluginViewEntries.value(m_host->currentViewId())) {
            entry->setChecked(true);
            return;
        }
    }
    // The file list: the entry of its mode (as DolphinViewActionHandler::updateViewActions()).
    if (QAction *action = viewModeAction(WhaleViewHost::dolphinViewId())) {
        action->setChecked(true);
    }
}

#include "moc_whaleviewactions.cpp"
