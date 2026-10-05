/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleviewcontextimpl.h"

#include "dolphinviewbridge.h"

WhaleViewContextImpl::WhaleViewContextImpl(DolphinViewBridge *bridge, QObject *parent)
    : WhaleViewContext(parent)
    , m_bridge(bridge)
{
    connect(bridge, &DolphinViewBridge::urlChanged, this, &WhaleViewContext::urlChanged);
    connect(bridge, &DolphinViewBridge::loadingStarted, this, &WhaleViewContext::loadingStarted);
    connect(bridge, &DolphinViewBridge::loadingCompleted, this, &WhaleViewContext::loadingCompleted);
    connect(bridge, &DolphinViewBridge::loadingCanceled, this, &WhaleViewContext::loadingCanceled);
}

WhaleViewContextImpl::~WhaleViewContextImpl() = default;

QUrl WhaleViewContextImpl::url() const
{
    return m_bridge->url();
}

WhaleItemList *WhaleViewContextImpl::items() const
{
    if (!m_items) {
        // A child of the context, so its connections to the model end with the context.
        m_items = m_bridge->createItemList(const_cast<WhaleViewContextImpl *>(this));
    }
    return m_items;
}

#include "moc_whaleviewcontextimpl.cpp"
