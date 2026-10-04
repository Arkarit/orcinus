/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleviewcontextimpl.h"

#include "views/dolphinview.h"

WhaleViewContextImpl::WhaleViewContextImpl(DolphinView *dolphinView, QObject *parent)
    : WhaleViewContext(parent)
    , m_dolphinView(dolphinView)
{
    connect(dolphinView, &DolphinView::urlChanged, this, &WhaleViewContext::urlChanged);
}

WhaleViewContextImpl::~WhaleViewContextImpl() = default;

QUrl WhaleViewContextImpl::url() const
{
    return m_dolphinView->url();
}

#include "moc_whaleviewcontextimpl.cpp"
