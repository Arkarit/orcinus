/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEVIEWCONTEXTIMPL_H
#define WHALEVIEWCONTEXTIMPL_H

#include "whaleviewcontext.h"

class DolphinView;

/**
 * @brief The WhaleViewContext of one view, backed by the DolphinView of its view area.
 */
class WhaleViewContextImpl : public WhaleViewContext
{
    Q_OBJECT

public:
    explicit WhaleViewContextImpl(DolphinView *dolphinView, QObject *parent = nullptr);
    ~WhaleViewContextImpl() override;

    QUrl url() const override;

private:
    DolphinView *const m_dolphinView;
};

#endif
