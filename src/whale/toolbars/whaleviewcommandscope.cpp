/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleviewcommandscope.h"

#include "dolphinviewcontainer.h"
#include "views/dolphinview.h"
#include "views/dolphinviewactionhandler.h"
#include "whalecommandrouter.h"
#include "whalecontainercommands.h"
#include "whaleviewactions.h"
#include "whaleviewservices.h"

#include <KActionCollection>
#include <KActionMenu>
#include <KFileItemListProperties>
#include <KLocalizedString>

#include <QMenu>

QStringList WhaleViewCommandScope::allowedActions()
{
    return {
        // What the view shows and how
        QStringLiteral("icons"),
        QStringLiteral("compact"),
        QStringLiteral("details"),
        QStringLiteral("view_mode"),
        QStringLiteral("view_settings"),
        QStringLiteral("view_zoom_in"),
        QStringLiteral("view_zoom_out"),
        QStringLiteral("view_zoom_reset"),
        QStringLiteral("zoom"),
        QStringLiteral("show_preview"),
        QStringLiteral("show_hidden_files"),
        QStringLiteral("whale_include_subfolders"),
        QStringLiteral("sort"),
        QStringLiteral("folders_first"),
        QStringLiteral("hidden_last"),
        QStringLiteral("group_by"),
        QStringLiteral("additional_info"),
        QStringLiteral("view_properties"),
        QStringLiteral("restore_view_settings_default"),
        // Navigation and state of the view
        QStringLiteral("go_back"),
        QStringLiteral("go_forward"),
        QStringLiteral("go_up"),
        QStringLiteral("go_home"),
        QStringLiteral("view_redisplay"),
        QStringLiteral("stop"),
        QStringLiteral("toggle_filter"),
        QStringLiteral("show_filter_bar"),
        QStringLiteral("toggle_search"),
        // The selection of the view
        QStringLiteral("renamefile"),
        QStringLiteral("movetotrash"),
        QStringLiteral("deletefile"),
        QStringLiteral("duplicate"),
        QStringLiteral("properties"),
        QStringLiteral("copy_location"),
        QStringLiteral("edit_select_all"),
        QStringLiteral("invert_selection"),
    };
}

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

    // The handler asks for the selection mode where the window's actions start or leave it.
    // Starting it is the window's business (its bottom bar shows the window's actions), and
    // the actions here are disabled without a selection; leaving it after an action on the
    // selected items is done here as well.
    connect(m_actionHandler, &DolphinViewActionHandler::selectionModeChangeTriggered, this, [this](bool enabled) {
        if (!enabled && m_container && m_container->isSelectionModeEnabled()) {
            m_container->setSelectionModeEnabled(false);
        }
    });

    m_viewActions = new WhaleViewActions(WhaleViewServices::ensure(container->window()), m_actionCollection, m_actionHandler, this);
    // Why Whale disables an action; shown in this view's message area (disabledReason()).
    m_viewActions->setDisabledReasonHook([this](QAction *action, const QString &reason) {
        if (reason.isEmpty()) {
            m_whaleDisabledReasons.remove(action);
        } else {
            m_whaleDisabledReasons.insert(action, reason);
        }
    });
    m_viewActions->setCurrentHost(container->viewHost());

    // The actions on selected items follow the selection of this view: the file list's, or
    // a plugin view's own (T2), which WhaleViewActions announces.
    connect(container->view(), &DolphinView::selectionChanged, this, &WhaleViewCommandScope::updateSelectionActions);
    connect(m_viewActions, &WhaleViewActions::commandSelectionChanged, this, &WhaleViewCommandScope::updateSelectionActions);
    updateSelectionActions();

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
    static const QStringList allowed = allowedActions();
    return allowed.contains(name) ? m_actionCollection->action(name) : nullptr;
}

WhaleContainerCommands *WhaleViewCommandScope::commands() const
{
    return m_commands;
}

QString WhaleViewCommandScope::disabledReason(const QAction *action) const
{
    if (!action || action->isEnabled()) {
        return QString();
    }
    const QString whaleReason = m_whaleDisabledReasons.value(action);
    return whaleReason.isEmpty() ? m_disabledReasons.value(action) : whaleReason;
}

void WhaleViewCommandScope::setDisabledReason(const QString &name, const QString &reason)
{
    if (const QAction *action = m_actionCollection->action(name)) {
        if (reason.isEmpty() || action->isEnabled()) {
            m_disabledReasons.remove(action);
        } else {
            m_disabledReasons.insert(action, reason);
        }
    }
}

void WhaleViewCommandScope::updateSelectionActions()
{
    if (!m_container) {
        return;
    }
    // As DolphinMainWindow::updateFileAndEditActions() for the window's actions, with the
    // selection of this view. One difference: without a selection the window's actions stay
    // enabled and start the selection mode; these are disabled.
    const KFileItemList items = WhaleCommandRouter::selectedItems(m_container->view());
    const KFileItemListProperties capabilities(items);
    const bool selected = !items.isEmpty();
    auto setEnabled = [this](const QString &name, bool enabled) {
        if (QAction *action = m_actionCollection->action(name)) {
            action->setEnabled(enabled);
        }
    };
    const QString rename = QStringLiteral("renamefile");
    const QString trash = QStringLiteral("movetotrash");
    const QString deleteFile = QStringLiteral("deletefile");
    const QString duplicate = QStringLiteral("duplicate");

    setEnabled(rename, selected && capabilities.supportsMoving());
    setEnabled(trash, selected && capabilities.isLocal() && capabilities.supportsMoving());
    setEnabled(deleteFile, selected && capabilities.supportsDeleting());
    setEnabled(duplicate, selected && capabilities.supportsWriting());
    setEnabled(QStringLiteral("copy_location"), items.count() == 1);
    // "properties" stays enabled: without a selection it shows the location's.

    // Dolphin's explanations, where the selection is the reason (not for "nothing selected").
    setDisabledReason(rename, selected ? i18nc("@info", "Cannot rename: You do not have permission to rename items in this folder.") : QString());
    setDisabledReason(deleteFile, selected ? i18nc("@info", "Cannot delete: You do not have permission to remove items from this folder.") : QString());
    setDisabledReason(duplicate, selected ? i18nc("@info", "Cannot duplicate here: You do not have permission to create items in this folder.") : QString());
}

void WhaleViewCommandScope::applyActionProperties(const QHash<QString, WhaleToolbarLayout::ActionProperties> &properties)
{
    for (const OriginalProperties &original : std::as_const(m_originalProperties)) {
        QAction *action = original.action;
        if (!action) {
            continue;
        }
        if (original.text) {
            action->setText(*original.text);
        }
        if (original.iconText) {
            action->setIconText(*original.iconText);
        }
        if (original.toolTip) {
            action->setToolTip(*original.toolTip);
        }
        if (original.icon) {
            action->setIcon(*original.icon);
        }
        if (original.priority) {
            action->setPriority(QAction::Priority(*original.priority));
        }
    }
    m_originalProperties.clear();

    for (auto it = properties.cbegin(); it != properties.cend(); ++it) {
        QAction *action = this->action(it.key());
        if (!action) {
            continue;
        }
        const WhaleToolbarLayout::ActionProperties &changed = it.value();
        OriginalProperties original;
        original.action = action;
        // First what the action has, then the changes: its icon text and tooltip follow its
        // text as long as they were not set (resetting them tells).
        if (changed.contains(QStringLiteral("text"))) {
            original.text = action->text();
        }
        if (changed.contains(QStringLiteral("iconText"))) {
            const QString iconText = action->iconText();
            action->setIconText(QString());
            original.iconText = action->iconText() == iconText ? QString() : iconText;
        }
        if (changed.contains(QStringLiteral("toolTip"))) {
            const QString toolTip = action->toolTip();
            action->setToolTip(QString());
            original.toolTip = action->toolTip() == toolTip ? QString() : toolTip;
        }
        if (changed.contains(QStringLiteral("icon"))) {
            original.icon = action->icon();
        }
        if (changed.contains(QStringLiteral("priority"))) {
            original.priority = int(action->priority());
        }
        m_originalProperties.append(original);

        for (auto property = changed.cbegin(); property != changed.cend(); ++property) {
            if (property.key() == QLatin1String("text")) {
                action->setText(property.value());
            } else if (property.key() == QLatin1String("iconText")) {
                action->setIconText(property.value());
            } else if (property.key() == QLatin1String("toolTip")) {
                action->setToolTip(property.value());
            } else if (property.key() == QLatin1String("icon")) {
                action->setIcon(QIcon::fromTheme(property.value()));
            } else if (property.key() == QLatin1String("priority")) {
                action->setPriority(QAction::Priority(property.value().toInt()));
            }
        }
    }
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
