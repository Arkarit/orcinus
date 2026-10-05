/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEVIEWCONTEXTIMPL_H
#define WHALEVIEWCONTEXTIMPL_H

#include "whaleviewcontext.h"

#include <optional>

class DolphinViewBridge;

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

private:
    void slotSelectionChanged(const WhaleSelection &selection);

    DolphinViewBridge *const m_bridge;
    // The selection this context's view has just set, so its echo is not sent back.
    std::optional<WhaleSelection> m_ownSelection;
    mutable WhaleItemList *m_items = nullptr;
};

#endif
