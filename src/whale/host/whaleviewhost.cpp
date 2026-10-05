/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleviewhost.h"

#include "dolphinviewadapter.h"
#include "dolphinviewbridge.h"
#include "views/dolphinview.h"
#include "whaleviewcontextimpl.h"
#include "whaleviewdebug.h"
#include "whaleviewplugin.h"

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
    if (m_active) {
        widget->setFocus();
    }
    m_pluginView->activate();

    Q_EMIT currentViewChanged(pluginId);
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
