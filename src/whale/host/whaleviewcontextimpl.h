/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEVIEWCONTEXTIMPL_H
#define WHALEVIEWCONTEXTIMPL_H

#include "whaleviewcontext.h"

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

private:
    DolphinViewBridge *const m_bridge;
    mutable WhaleItemList *m_items = nullptr;
};

#endif
