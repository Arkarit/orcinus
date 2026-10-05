/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEVIEWCONTEXTIMPL_H
#define WHALEVIEWCONTEXTIMPL_H

#include "whaleviewcapabilities.h"
#include "whaleviewcontext.h"

#include <QPointer>

#include <optional>

class DolphinViewBridge;
class WhaleFileOperationsImpl;
class WhalePreviewProviderImpl;
class WhaleTreeCache;
class WhaleTreeScannerImpl;

/**
 * @brief The WhaleViewContext of one view, backed by the DolphinView of its view area.
 *
 * All DolphinView state comes through the DolphinViewBridge, which must outlive the context.
 */
class WhaleViewContextImpl : public WhaleViewContext
{
    Q_OBJECT

public:
    explicit WhaleViewContextImpl(DolphinViewBridge *bridge, QObject *parent = nullptr);
    ~WhaleViewContextImpl() override;

    QUrl url() const override;
    WhaleItemList *items() const override;
    bool isLoading() const override;
    WhaleSelection selection() const override;
    void setSelection(const WhaleSelection &selection) override;
    void activateItems(const QList<QUrl> &urls) override;
    void requestContextMenu(const QPoint &globalPos, const QUrl &url) override;
    void requestItemInfo(const QUrl &url) override;
    WhalePreviewProvider *previews() const override;
    WhaleFileOperations *fileOperations() const override;
    WhaleTreeScanner *treeScanner() const override;

    /**
     * Tells the context the capabilities of its view (set by the host after creating
     * the view and whenever they change): the previews follow WhaleViewCapability::Previews.
     */
    void setViewCapabilities(WhaleViewCapabilities capabilities);

    /**
     * Lets the context's tree scanner share the scans of @p cache (set by the host before
     * the view is created). Without it the scanner scans on its own.
     */
    void setTreeCache(WhaleTreeCache *cache);

    /**
     * @return The preview provider if the view has asked for it; for development tools.
     */
    WhalePreviewProviderImpl *previewProviderIfCreated() const;

private:
    void slotSelectionChanged(const WhaleSelection &selection);

    DolphinViewBridge *const m_bridge;
    // The selection this context's view has just set, so its echo is not sent back.
    std::optional<WhaleSelection> m_ownSelection;
    mutable WhaleItemList *m_items = nullptr;
    mutable WhalePreviewProviderImpl *m_previews = nullptr;
    mutable WhaleFileOperationsImpl *m_fileOperations = nullptr;
    mutable WhaleTreeScannerImpl *m_treeScanner = nullptr;
    QPointer<WhaleTreeCache> m_treeCache;
    WhaleViewCapabilities m_viewCapabilities;
};

#endif
