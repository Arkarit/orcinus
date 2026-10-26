/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleviewservices.h"

#include "whaletreecache.h"
#include "whaleviewregistry.h"

#include <QWidget>

WhaleViewServices::WhaleViewServices(QWidget *window)
    : QObject(window)
    , m_registry(new WhaleViewRegistry(this))
    , m_treeCache(new WhaleTreeCache(WhaleTreeCache::configuredLimits(), this))
{
}

WhaleViewServices::~WhaleViewServices() = default;

WhaleViewServices *WhaleViewServices::ensure(QWidget *window)
{
    Q_ASSERT(window && window->isWindow());
    if (WhaleViewServices *services = of(window)) {
        return services;
    }
    return new WhaleViewServices(window);
}

WhaleViewServices *WhaleViewServices::of(const QWidget *widget)
{
    if (!widget) {
        return nullptr;
    }
    // Only the window's own: never the services of another window inside it.
    return widget->window()->findChild<WhaleViewServices *>(QString(), Qt::FindDirectChildrenOnly);
}

WhaleViewRegistry *WhaleViewServices::registry() const
{
    return m_registry;
}

WhaleTreeCache *WhaleViewServices::treeCache() const
{
    return m_treeCache;
}

void WhaleViewServices::notifyViewChanged()
{
    Q_EMIT viewChanged();
}

#include "moc_whaleviewservices.cpp"
