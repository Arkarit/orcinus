/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleviewhost.h"

#include "dolphinviewadapter.h"
#include "dolphinviewbridge.h"
#include "views/dolphinview.h"
#include "whaleviewactions.h"
#include "whaleviewcontextimpl.h"
#include "whaleviewdebug.h"
#include "whaleviewplugin.h"
#include "whaleviewregistry.h"

#include <QApplication>
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
{
    m_stack->setContentsMargins(0, 0, 0, 0);
    m_stack->addWidget(dolphinView);
    setFocusProxy(dolphinView);

    connect(qApp, &QApplication::focusChanged, this, &WhaleViewHost::slotFocusChanged);
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

void WhaleViewHost::showDolphinView()
{
    if (!m_pluginView) {
        return;
    }

    keepPluginState();
    m_pluginView->deactivate();
    restoreDolphinView();
    // If the focus is in the plugin view, QStackedLayout moves it into the DolphinView.
    m_stack->setCurrentWidget(m_dolphinView);
    destroyPluginView();
    if (m_active) {
        m_dolphinView->setFocus();
    }
    m_dolphinViewAdapter->activate();

    Q_EMIT currentViewChanged(dolphinViewId());
    notifyWindow();
}

bool WhaleViewHost::showPluginView(const QString &pluginId, WhaleViewPlugin *plugin)
{
    if (m_pluginView && m_pluginId == pluginId) {
        return true;
    }

    auto context = std::make_unique<WhaleViewContextImpl>(m_bridge);
    std::unique_ptr<WhaleView> view(plugin->createView(context.get(), nullptr));
    QWidget *widget = view ? view->widget() : nullptr;
    if (!widget) {
        qCWarning(WhaleViewDebug) << "View plugin" << pluginId << "did not provide a view widget";
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
    m_pluginContext = std::move(context);
    m_pluginView = std::move(view);
    const QByteArray state = m_views.states.value(pluginId);
    if (!state.isEmpty()) {
        m_pluginView->restoreState(state);
    }
    connect(m_pluginView.get(), &WhaleView::capabilitiesChanged, this, [this]() {
        if (!(m_pluginView->capabilities() & WhaleViewCapability::Selection)) {
            m_bridge->setSelection(WhaleSelection());
        }
        Q_EMIT capabilitiesChanged();
    });
    // A view without a selection must not leave an invisible one for the shell's actions.
    if (!(m_pluginView->capabilities() & WhaleViewCapability::Selection)) {
        m_bridge->setSelection(WhaleSelection());
    }
    if (m_active) {
        widget->setFocus();
    }
    m_pluginView->activate();

    Q_EMIT currentViewChanged(pluginId);
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

void WhaleViewHost::notifyWindow()
{
    // The session records the shown views; a changed one is a reason to save it again.
    if (auto actions = window()->findChild<WhaleViewActions *>()) {
        actions->notifyViewChanged();
    }
}

WhaleViewsState WhaleViewHost::viewsState() const
{
    WhaleViewsState views = m_views;
    views.activeViewId = currentViewId();
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
    QByteArray dolphinState;
    WhaleViewsState views;
    if (WhalePersistence::unwrapLocationState(locationState, &dolphinState, &views)) {
        applyViewsState(views, QStringLiteral("history"));
    } else {
        // A new location, or Dolphin's own state: the shown view stays, with no stored states.
        m_views = WhaleViewsState();
    }
    return dolphinState;
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

void WhaleViewHost::applyViewsState(const WhaleViewsState &views, const QString &source)
{
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
        WhaleViewRegistry *registry = window()->findChild<WhaleViewRegistry *>();
        WhaleViewPlugin *plugin = registry ? registry->plugin(requested) : nullptr;
        if (!plugin || !showPluginView(requested, plugin)) {
            // Missing, disabled, incompatible or broken: the file list. The view's stored
            // state stays in m_views, so it is written again and not lost.
            qCWarning(WhaleViewDebug) << "Cannot restore the view" << requested << "- showing the file list instead";
            fallback = true;
            showDolphinView();
        }
    }

    m_lastRestore = RestoreInfo{source, requested, currentViewId(), fallback};
    m_applyingState = false;
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
