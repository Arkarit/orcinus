/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleviewhost.h"

#include "dolphinviewadapter.h"
#include "dolphinviewbridge.h"
#include "views/dolphinview.h"
#include "whalefileoperationsimpl.h"
#include "whalerecursivecontent.h"
#include "whaleviewcontextimpl.h"
#include "whaleviewdebug.h"
#include "whaleviewplugin.h"
#include "whaleviewpreferences.h"
#include "whaleviewregistry.h"
#include "whaleviewservices.h"

#include <KLocalizedString>

#include <QApplication>
#include <QDataStream>
#include <QDropEvent>
#include <QShowEvent>
#include <QStackedLayout>

QString WhaleViewHost::dolphinViewId()
{
    return QStringLiteral("org.kde.dolphin.itemview");
}

WhaleViewHost::WhaleViewHost(DolphinView *dolphinView, QWidget *parent)
    : QWidget(parent)
    , m_dolphinView(dolphinView)
    , m_stack(new QStackedLayout(this))
    , m_active(dolphinView->isActive())
    , m_bridge(new DolphinViewBridge(dolphinView, this))
    , m_dolphinViewContext(new WhaleViewContextImpl(m_bridge, this))
    , m_dolphinViewAdapter(new DolphinViewAdapter(dolphinView, m_dolphinViewContext, this))
    , m_recursiveContent(new WhaleRecursiveContent(dolphinView, m_bridge, this))
{
    m_stack->setContentsMargins(0, 0, 0, 0);
    m_stack->addWidget(dolphinView);
    setFocusProxy(dolphinView);

    connect(qApp, &QApplication::focusChanged, this, &WhaleViewHost::slotFocusChanged);
    connect(m_recursiveContent, &WhaleRecursiveContent::requestMessage, this, &WhaleViewHost::messageRequested);
    // A location the recursive content cannot show (e.g. a remote one reached by a redirection)
    // ends it at once; which scope applies at a new location is decided with its view.
    connect(m_bridge, &DolphinViewBridge::urlChanged, this, [this]() {
        if (m_recursiveContent->isEnabled() && !supportsRecursiveContent()) {
            updateRecursiveContent();
        }
    });
}

WhaleViewHost::~WhaleViewHost()
{
    // The plugin view must be gone before the widgets of this host are destroyed,
    // because it owns and deletes its widget itself.
    destroyPluginView();
}

QString WhaleViewHost::currentViewId() const
{
    return m_pluginView ? m_pluginId : dolphinViewId();
}

WhaleView *WhaleViewHost::currentView() const
{
    return m_pluginView ? m_pluginView.get() : static_cast<WhaleView *>(m_dolphinViewAdapter);
}

bool WhaleViewHost::isDolphinViewShown() const
{
    return m_stack->currentWidget() == m_dolphinView;
}

WhaleSelection WhaleViewHost::linkedSelection() const
{
    if (!m_pluginView) {
        return WhaleSelection(m_dolphinView->selectedItems().urlList(), m_dolphinView->currentItemUrl());
    }
    const QList<QUrl> selected = m_pluginView->selectedItems();
    QUrl current = m_bridge->selection().current();
    if (!selected.isEmpty() && !selected.contains(current)) {
        current = selected.constFirst(); // e.g. a deep selection of the view
    }
    return WhaleSelection(selected, current);
}

void WhaleViewHost::showLinkedSelection(const WhaleSelection &selection)
{
    if (m_pluginView) {
        m_pluginView->showLinkedSelection(selection);
    } else {
        m_dolphinView->showLinkedSelection(selection.selected(), selection.current());
    }
}

void WhaleViewHost::showDolphinView()
{
    m_initialPreferencePending = false;
    if (!m_pluginView) {
        return;
    }

    keepPluginState();
    m_pluginView->deactivate();
    restoreDolphinView();
    // If the focus is in the plugin view, QStackedLayout moves it into the DolphinView.
    m_stack->setCurrentWidget(m_dolphinView);
    destroyPluginView();
    updateDestinationGuards();
    if (m_active) {
        m_dolphinView->setFocus();
    }
    m_dolphinViewAdapter->activate();
    updateRecursiveContent();

    Q_EMIT currentViewChanged(dolphinViewId());
    notifyWindow();
}

bool WhaleViewHost::selectView(const QString &viewId)
{
    if (viewId == dolphinViewId()) {
        showDolphinView();
    } else {
        WhaleViewRegistry *viewRegistry = registry();
        WhaleViewPlugin *plugin = viewRegistry ? viewRegistry->plugin(viewId) : nullptr;
        if (!plugin || !showPluginView(viewId, plugin)) {
            return false;
        }
    }
    // The user's choice, unlike any automatic change of the view, is the view of the location.
    WhaleViewPreferences::instance()->setPreferredView(m_bridge->url(), viewId);
    m_viewOrigin = QStringLiteral("user");
    return true;
}

bool WhaleViewHost::showPluginView(const QString &pluginId, WhaleViewPlugin *plugin)
{
    m_initialPreferencePending = false;
    if (m_pluginView && m_pluginId == pluginId) {
        return true;
    }
    const QUrl url = m_bridge->url();
    if (!url.isEmpty() && !plugin->supportsUrl(url)) {
        qCDebug(WhaleViewDebug) << "View plugin" << pluginId << "does not support" << url;
        return false;
    }

    // Plugin views see the location's own items, never the file list's recursive content.
    m_recursiveContent->setEnabled(false);
    auto context = std::make_unique<WhaleViewContextImpl>(m_bridge);
    if (WhaleViewServices *services = WhaleViewServices::of(this)) {
        context->setTreeCache(services->treeCache());
    }
    std::unique_ptr<WhaleView> view(plugin->createView(context.get(), nullptr));
    QWidget *widget = view ? view->widget() : nullptr;
    if (!widget) {
        qCWarning(WhaleViewDebug) << "View plugin" << pluginId << "did not provide a view widget";
        updateRecursiveContent();
        return false;
    }

    // Order (plan 9.3): the outgoing view's state is kept, it is deactivated and
    // destroyed; the new view, created with its context, gets its state of this
    // location, then the focus, then activate(); currentViewChanged() updates the gating.
    keepPluginState();
    if (m_pluginView) {
        // Replace one plugin view by another; the DolphinView stays silenced.
        m_pluginView->deactivate();
        QWidget *oldWidget = m_pluginView->widget();
        m_stack->addWidget(widget);
        m_stack->setCurrentWidget(widget);
        m_dolphinView->setFocusProxy(widget);
        m_stack->removeWidget(oldWidget);
        destroyPluginView();
    } else {
        m_dolphinViewAdapter->deactivate();
        m_stack->addWidget(widget);
        // If the focus is in the DolphinView, QStackedLayout moves it into the plugin view.
        // This must happen before the DolphinView is silenced: disabling a focused widget
        // would pass the focus on to the next widget in the window, possibly in the other
        // half of a split view.
        m_stack->setCurrentWidget(widget);
        silenceDolphinView();
        m_dolphinView->setFocusProxy(widget);
    }

    m_pluginId = pluginId;
    m_plugin = plugin;
    m_pluginContext = std::move(context);
    m_pluginView = std::move(view);
    m_pluginContext->setViewCapabilities(m_pluginView->capabilities());
    const QByteArray state = m_views.states.value(pluginId);
    if (!state.isEmpty()) {
        m_pluginView->restoreState(state);
    }
    updateDestinationGuards();
    connect(m_pluginView.get(), &WhaleView::capabilitiesChanged, this, [this]() {
        const WhaleViewCapabilities capabilities = m_pluginView->capabilities();
        m_pluginContext->setViewCapabilities(capabilities);
        if (!(capabilities & WhaleViewCapability::Selection)) {
            m_bridge->setSelection(WhaleSelection());
        }
        updateDestinationGuards();
        Q_EMIT capabilitiesChanged();
    });
    connect(m_pluginView.get(), &WhaleView::selectedItemsChanged, this, &WhaleViewHost::viewSelectionChanged);
    // A view without a selection must not leave an invisible one for the shell's actions.
    if (!(m_pluginView->capabilities() & WhaleViewCapability::Selection)) {
        m_bridge->setSelection(WhaleSelection());
    }
    if (m_active) {
        widget->setFocus();
    }
    m_pluginView->activate();

    Q_EMIT currentViewChanged(pluginId);
    Q_EMIT contentScopeChanged();
    notifyWindow();
    return true;
}

void WhaleViewHost::setActive(bool active)
{
    if (active == m_active) {
        return;
    }
    m_active = active;
    if (active && m_pluginView) {
        m_pluginView->widget()->setFocus();
    }
}

DolphinViewBridge *WhaleViewHost::bridge() const
{
    return m_bridge;
}

DolphinView *WhaleViewHost::dolphinView() const
{
    return m_dolphinView;
}

WhaleViewContextImpl *WhaleViewHost::pluginContext() const
{
    return m_pluginContext.get();
}

void WhaleViewHost::notifyWindow()
{
    // The session records the shown views; a changed one is a reason to save it again.
    if (WhaleViewServices *services = WhaleViewServices::of(this)) {
        services->notifyViewChanged();
    }
}

WhaleViewsState WhaleViewHost::viewsState() const
{
    WhaleViewsState views = m_views;
    views.activeViewId = currentViewId();
    // F1: the content scope is the file list's state, kept while another view is shown.
    const QByteArray fileList = fileListState();
    if (fileList.isEmpty()) {
        views.states.remove(dolphinViewId());
    } else {
        views.states.insert(dolphinViewId(), fileList);
    }
    if (m_pluginView) {
        const QByteArray state = m_pluginView->saveState();
        if (state.isEmpty()) {
            views.states.remove(m_pluginId);
        } else {
            views.states.insert(m_pluginId, state);
        }
    }
    return views;
}

void WhaleViewHost::keepPluginState()
{
    if (m_pluginView && !m_applyingState) {
        m_views = viewsState();
    }
}

QByteArray WhaleViewHost::wrapLocationState(const QByteArray &dolphinState)
{
    return WhalePersistence::wrapLocationState(dolphinState, viewsState());
}

QByteArray WhaleViewHost::unwrapLocationState(const QByteArray &locationState)
{
    // View and content scope are decided together; the recursive content follows once.
    ++m_holdRecursiveContent;
    QByteArray dolphinState;
    WhaleViewsState views;
    if (WhalePersistence::unwrapLocationState(locationState, &dolphinState, &views)) {
        // Back/forward: the view as it was, even if the location prefers another one by now.
        applyViewsState(views, QStringLiteral("history"));
    } else {
        // A new location, or Dolphin's own state: no stored states; the preferred view of the
        // location, otherwise the shown view stays.
        m_views = WhaleViewsState();
        applyPreferredContentScope();
        applyPreferredView();
    }
    checkUrlSupported();
    --m_holdRecursiveContent;
    updateRecursiveContent();
    return dolphinState;
}

void WhaleViewHost::applyPreferredView()
{
    m_initialPreferencePending = false;
    const QString preferred = WhaleViewPreferences::instance()->preferredView(m_bridge->url());
    if (preferred.isEmpty()) {
        m_viewOrigin = QStringLiteral("kept");
        return;
    }

    // The outgoing view has only just seen this location; its state is not worth keeping.
    m_applyingState = true;
    bool fallback = false;
    if (preferred == dolphinViewId()) {
        showDolphinView();
    } else if (!m_pluginView || m_pluginId != preferred) {
        // A missing, incompatible or broken plugin, or one that does not support the location:
        // the file list, quietly. The preference stays for when the plugin is back.
        WhaleViewRegistry *viewRegistry = registry();
        WhaleViewPlugin *plugin = viewRegistry && viewRegistry->pluginMetaData(preferred).isValid() ? viewRegistry->plugin(preferred) : nullptr;
        if (!plugin || !showPluginView(preferred, plugin)) {
            qCDebug(WhaleViewDebug) << "The preferred view" << preferred << "cannot show" << m_bridge->url() << "- showing the file list";
            fallback = true;
            showDolphinView();
        }
    }
    m_applyingState = false;
    m_lastRestore = RestoreInfo{QStringLiteral("preference"), preferred, currentViewId(), fallback};
    m_viewOrigin = fallback ? QStringLiteral("fallback") : QStringLiteral("preference");
}

WhaleViewRegistry *WhaleViewHost::registry() const
{
    WhaleViewServices *services = WhaleViewServices::of(this);
    return services ? services->registry() : nullptr;
}

void WhaleViewHost::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // A new view area (window, tab, split view) shows the preferred view of its first
    // location, now that it is part of its window and can find the view plugins.
    ++m_holdRecursiveContent;
    if (m_contentScopeOrigin == QLatin1String("initial")) {
        applyPreferredContentScope();
    }
    if (m_initialPreferencePending) {
        applyPreferredView();
    }
    --m_holdRecursiveContent;
    updateRecursiveContent();
}

void WhaleViewHost::checkUrlSupported()
{
    const QUrl url = m_bridge->url();
    if (!m_pluginView || !m_plugin || url.isEmpty() || m_plugin->supportsUrl(url)) {
        return;
    }
    // E.g. a treemap of local folders entering a remote one: the file list shows it. The
    // view's state belongs to the previous location, which keeps it in the history.
    const QString requested = m_pluginId;
    m_applyingState = true;
    showDolphinView();
    m_applyingState = false;
    m_lastRestore = RestoreInfo{QStringLiteral("location"), requested, currentViewId(), true};
    m_viewOrigin = QStringLiteral("fallback");
}

QByteArray WhaleViewHost::saveSessionState(const QList<WhaleViewHost *> &hosts)
{
    QList<WhaleViewsState> panes;
    for (const WhaleViewHost *host : hosts) {
        panes.append(host->viewsState());
    }
    return WhalePersistence::writeSessionState(panes);
}

void WhaleViewHost::restoreSessionState(const QList<WhaleViewHost *> &hosts, const QByteArray &data)
{
    const QList<WhaleViewsState> panes = WhalePersistence::readSessionState(data);
    for (int i = 0; i < panes.count() && i < hosts.count(); ++i) {
        hosts.at(i)->applyViewsState(panes.at(i), QStringLiteral("session"));
    }
}

WhaleViewHost::RestoreInfo WhaleViewHost::lastRestore() const
{
    return m_lastRestore;
}

QString WhaleViewHost::viewOrigin() const
{
    return m_viewOrigin;
}

void WhaleViewHost::applyViewsState(const WhaleViewsState &views, const QString &source)
{
    ++m_holdRecursiveContent;
    m_initialPreferencePending = false;
    m_applyingState = true;
    m_views = views;
    const QString requested = views.activeViewId.isEmpty() ? dolphinViewId() : views.activeViewId;
    bool fallback = false;

    if (requested == dolphinViewId()) {
        showDolphinView();
    } else if (m_pluginView && m_pluginId == requested) {
        // The view stays; it gets its state of this location.
        const QByteArray state = m_views.states.value(requested);
        if (!state.isEmpty()) {
            m_pluginView->restoreState(state);
        }
    } else {
        WhaleViewRegistry *viewRegistry = registry();
        WhaleViewPlugin *plugin = viewRegistry ? viewRegistry->plugin(requested) : nullptr;
        if (!plugin || !showPluginView(requested, plugin)) {
            // Missing, disabled, incompatible or broken: the file list. The view's stored
            // state stays in m_views, so it is written again and not lost.
            qCWarning(WhaleViewDebug) << "Cannot restore the view" << requested << "- showing the file list instead";
            fallback = true;
            showDolphinView();
        }
    }

    m_lastRestore = RestoreInfo{source, requested, currentViewId(), fallback};
    m_viewOrigin = fallback ? QStringLiteral("fallback") : source;
    m_applyingState = false;

    // F1: the file list's content scope as it was; Whale data without it is the current folder.
    WhaleContentScope scope = WhaleContentScope::CurrentFolder;
    const QByteArray fileList = views.states.value(dolphinViewId());
    if (!fileList.isEmpty()) {
        QDataStream stream(fileList);
        quint32 version = 0;
        QString name;
        stream >> version >> name;
        if (stream.status() == QDataStream::Ok && version == 1) {
            scope = WhaleContentScopes::fromName(name).value_or(WhaleContentScope::CurrentFolder);
        } else {
            qCWarning(WhaleViewDebug) << "Ignoring the file list's state of version" << version;
        }
    }
    setContentScope(scope, source);
    --m_holdRecursiveContent;
    updateRecursiveContent();
}

QByteArray WhaleViewHost::fileListState() const
{
    if (m_contentScope == WhaleContentScope::CurrentFolder) {
        return {}; // the default, as in data of older Whales
    }
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream << quint32(1) << WhaleContentScopes::name(m_contentScope);
    return data;
}

WhaleContentScope WhaleViewHost::contentScope() const
{
    return m_contentScope;
}

void WhaleViewHost::selectContentScope(WhaleContentScope scope)
{
    // The user's choice: the scope of the location from now on (not inherited by subfolders).
    WhaleViewPreferences::instance()->setContentScope(m_bridge->url(), scope);
    setContentScope(scope, QStringLiteral("user"));
}

void WhaleViewHost::applyPreferredContentScope()
{
    const WhaleContentScope scope = WhaleViewPreferences::instance()->contentScope(m_bridge->url());
    setContentScope(scope, scope == WhaleContentScope::CurrentFolder ? QStringLiteral("default") : QStringLiteral("preference"));
}

void WhaleViewHost::setContentScope(WhaleContentScope scope, const QString &origin)
{
    m_contentScopeOrigin = origin;
    if (scope == m_contentScope) {
        updateRecursiveContent();
        return;
    }
    m_contentScope = scope;
    updateRecursiveContent();
    Q_EMIT contentScopeChanged();
    // The session records the scope.
    notifyWindow();
}

bool WhaleViewHost::supportsRecursiveContent() const
{
    return isDolphinViewShown() && m_bridge->url().isLocalFile();
}

bool WhaleViewHost::supportsViewOption(ViewOption option) const
{
    switch (option) {
    case ViewOption::FlatContent:
        return supportsRecursiveContent();
    case ViewOption::Previews:
        return currentView()->capabilities() & WhaleViewCapability::Previews;
    case ViewOption::HiddenFiles:
        return currentView()->capabilities() & WhaleViewCapability::HiddenFiles;
    }
    return false;
}

bool WhaleViewHost::isRecursiveContentShown() const
{
    return m_recursiveContent->isEnabled();
}

QString WhaleViewHost::contentScopeOrigin() const
{
    return m_contentScopeOrigin;
}

WhaleRecursiveContent *WhaleViewHost::recursiveContent() const
{
    return m_recursiveContent;
}

bool WhaleViewHost::hasDestination() const
{
    if (m_pluginView) {
        return !(m_pluginView->capabilities() & WhaleViewCapability::RecursiveContent);
    }
    return !m_recursiveContent->isEnabled();
}

QString WhaleViewHost::refusal(Refusal kind) const
{
    if (hasDestination()) {
        return {};
    }
    if (!m_pluginView) {
        return m_recursiveContent->refusal(kind == Refusal::Paste        ? WhaleRecursiveContent::Refusal::Paste
                                               : kind == Refusal::Drop   ? WhaleRecursiveContent::Refusal::Drop
                                               : kind == Refusal::Create ? WhaleRecursiveContent::Refusal::Create
                                                                         : WhaleRecursiveContent::Refusal::Transfer);
    }
    switch (kind) {
    case Refusal::Paste:
        return i18nc("@info",
                     "Cannot paste here because this view shows items from multiple folders. "
                     "Switch to another view mode to choose a destination folder.");
    case Refusal::Drop:
        return i18nc("@info",
                     "Cannot drop files here because this view shows items from multiple folders. "
                     "Switch to another view mode to choose a destination folder.");
    case Refusal::Create:
        return i18nc("@info",
                     "Cannot create new items here because this view shows items from multiple folders. "
                     "Switch to another view mode to choose a destination folder.");
    case Refusal::Transfer:
        return i18nc("@info",
                     "Cannot copy or move items into this view because it shows items from multiple folders. "
                     "Switch to another view mode to choose a destination folder.");
    }
    return {};
}

bool WhaleViewHost::refuse(Refusal kind)
{
    const QString text = refusal(kind);
    if (text.isEmpty()) {
        return false;
    }
    m_recursiveContent->explain(text);
    return true;
}

void WhaleViewHost::updateDestinationGuards()
{
    // The file list refuses drops itself (WhaleRecursiveContent); a plugin view's widgets do
    // not accept drops without a destination, so they reach the host.
    setAcceptDrops(m_pluginView && !hasDestination());
    if (m_pluginContext) {
        // The user may not see which entries a deep selection holds: removing or cutting
        // them is always confirmed.
        static_cast<WhaleFileOperationsImpl *>(m_pluginContext->fileOperations())->setConfirmRemoval(!hasDestination());
    }
}

void WhaleViewHost::dragEnterEvent(QDragEnterEvent *event)
{
    refuseDrag(event);
}

void WhaleViewHost::dragMoveEvent(QDragMoveEvent *event)
{
    refuseDrag(event);
}

void WhaleViewHost::dropEvent(QDropEvent *event)
{
    refuseDrag(event);
}

void WhaleViewHost::refuseDrag(QDropEvent *event)
{
    event->setDropAction(Qt::IgnoreAction);
    event->ignore();
    if (hasDestination() || event->type() == QEvent::DragMove) {
        return;
    }
    // A drag from elsewhere is explained once, when it enters (some platforms never deliver
    // a refused drop) or is dropped anyway. The view's own drag leaves to other places.
    const QMimeData *data = event->mimeData();
    if (!data || data == m_explainedDrag.data() || data == WhaleFileOperationsImpl::currentDragData()) {
        return;
    }
    m_explainedDrag = data;
    refuse(Refusal::Drop);
}

void WhaleViewHost::updateRecursiveContent()
{
    if (m_holdRecursiveContent > 0) {
        return;
    }
    const bool show = m_contentScope == WhaleContentScope::RecursiveFiles && supportsRecursiveContent();
    if (show == m_recursiveContent->isEnabled()) {
        return;
    }
    if (show) {
        if (WhaleViewServices *services = WhaleViewServices::of(this)) {
            m_recursiveContent->setTreeCache(services->treeCache());
        }
    }
    m_recursiveContent->setEnabled(show);
    Q_EMIT contentScopeChanged();
}

void WhaleViewHost::destroyPluginView()
{
    if (!m_pluginView) {
        return;
    }
    if (QWidget *widget = m_pluginView->widget()) {
        m_stack->removeWidget(widget);
    }
    m_pluginView.reset();
    m_pluginContext.reset();
    m_pluginId.clear();
    m_plugin = nullptr;
}

void WhaleViewHost::silenceDolphinView()
{
    m_dolphinViewFocusProxy = m_dolphinView->focusProxy();
    const auto children = m_dolphinView->findChildren<QWidget *>(Qt::FindDirectChildrenOnly);
    for (QWidget *child : children) {
        if (child->isEnabled()) {
            child->setEnabled(false);
            m_disabledDolphinViewChildren.append(child);
        }
    }
}

void WhaleViewHost::restoreDolphinView()
{
    for (const QPointer<QWidget> &child : std::as_const(m_disabledDolphinViewChildren)) {
        if (child) {
            child->setEnabled(true);
        }
    }
    m_disabledDolphinViewChildren.clear();
    m_dolphinView->setFocusProxy(m_dolphinViewFocusProxy);
    m_dolphinViewFocusProxy.clear();
}

void WhaleViewHost::slotFocusChanged(QWidget *old, QWidget *now)
{
    Q_UNUSED(old)
    if (!m_pluginView || !now) {
        return;
    }
    QWidget *widget = m_pluginView->widget();
    if (now == widget || widget->isAncestorOf(now)) {
        Q_EMIT activationRequested();
    }
}

#include "moc_whaleviewhost.cpp"
