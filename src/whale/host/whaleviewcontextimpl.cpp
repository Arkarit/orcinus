/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleviewcontextimpl.h"

#include "dolphinviewbridge.h"
#include "whalefileoperationsimpl.h"
#include "whalepreviewproviderimpl.h"
#include "whaletreecache.h"
#include "whaletreescannerimpl.h"

WhaleViewContextImpl::WhaleViewContextImpl(DolphinViewBridge *bridge, QObject *parent)
    : WhaleViewContext(parent)
    , m_bridge(bridge)
{
    connect(bridge, &DolphinViewBridge::urlChanged, this, &WhaleViewContext::urlChanged);
    connect(bridge, &DolphinViewBridge::loadingStarted, this, &WhaleViewContext::loadingStarted);
    connect(bridge, &DolphinViewBridge::loadingCompleted, this, &WhaleViewContext::loadingCompleted);
    connect(bridge, &DolphinViewBridge::loadingCanceled, this, &WhaleViewContext::loadingCanceled);
    connect(bridge, &DolphinViewBridge::selectionChanged, this, &WhaleViewContextImpl::slotSelectionChanged);
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

bool WhaleViewContextImpl::isLoading() const
{
    return m_bridge->isLoading();
}

WhaleSelection WhaleViewContextImpl::selection() const
{
    return m_bridge->selection();
}

void WhaleViewContextImpl::setSelection(const WhaleSelection &selection)
{
    const WhaleSelection before = m_bridge->selection();
    const WhaleSelection result = m_bridge->setSelection(selection);

    // The view shows what it asked for. Unless the result differs from that (URLs
    // outside the listing, an unknown current item), the bridge's report of this
    // change is not sent back to it; other contexts get it as usual.
    QList<QUrl> asked = selection.selected();
    QList<QUrl> applied = result.selected();
    std::sort(asked.begin(), asked.end());
    std::sort(applied.begin(), applied.end());
    asked.erase(std::unique(asked.begin(), asked.end()), asked.end());
    const bool asAsked = asked == applied && (selection.current().isEmpty() || selection.current() == result.current());
    if (asAsked && result != before) {
        m_ownSelection = result;
    } else {
        m_ownSelection.reset();
    }
}

void WhaleViewContextImpl::slotSelectionChanged(const WhaleSelection &selection)
{
    const bool echo = m_ownSelection && *m_ownSelection == selection;
    m_ownSelection.reset();
    if (!echo) {
        Q_EMIT selectionChanged(selection);
    }
}

void WhaleViewContextImpl::activateItems(const QList<QUrl> &urls)
{
    m_bridge->activateItems(urls);
}

void WhaleViewContextImpl::openFolder(const QUrl &url)
{
    m_bridge->openFolder(url);
}

void WhaleViewContextImpl::requestContextMenu(const QPoint &globalPos, const QUrl &url)
{
    m_bridge->requestContextMenu(globalPos, url);
}

void WhaleViewContextImpl::requestItemInfo(const QUrl &url)
{
    m_bridge->requestItemInfo(url);
}

WhalePreviewProvider *WhaleViewContextImpl::previews() const
{
    if (!m_previews) {
        m_previews = new WhalePreviewProviderImpl(m_bridge, const_cast<WhaleViewContextImpl *>(this));
        m_previews->setViewSupportsPreviews(m_viewCapabilities & WhaleViewCapability::Previews);
    }
    return m_previews;
}

WhaleFileOperations *WhaleViewContextImpl::fileOperations() const
{
    if (!m_fileOperations) {
        m_fileOperations = new WhaleFileOperationsImpl(m_bridge, const_cast<WhaleViewContextImpl *>(this));
    }
    return m_fileOperations;
}

WhaleTreeScanner *WhaleViewContextImpl::treeScanner() const
{
    if (!m_treeScanner) {
        m_treeScanner = new WhaleTreeScannerImpl(m_treeCache, const_cast<WhaleViewContextImpl *>(this));
    }
    return m_treeScanner;
}

void WhaleViewContextImpl::setTreeCache(WhaleTreeCache *cache)
{
    m_treeCache = cache;
}

void WhaleViewContextImpl::setViewCapabilities(WhaleViewCapabilities capabilities)
{
    m_viewCapabilities = capabilities;
    if (m_previews) {
        m_previews->setViewSupportsPreviews(capabilities & WhaleViewCapability::Previews);
    }
}

WhalePreviewProviderImpl *WhaleViewContextImpl::previewProviderIfCreated() const
{
    return m_previews;
}

#include "moc_whaleviewcontextimpl.cpp"
