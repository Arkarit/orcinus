/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "whaleview.h"

#include "whaleviewcontext.h"

class WhaleViewPrivate
{
public:
    WhaleViewContext *context;
};

WhaleView::WhaleView(WhaleViewContext *context, QObject *parent)
    : QObject(parent)
    , d(new WhaleViewPrivate{context})
{
}

WhaleView::~WhaleView() = default;

WhaleViewCapabilities WhaleView::capabilities() const
{
    return {};
}

void WhaleView::activate()
{
}

void WhaleView::deactivate()
{
}

QByteArray WhaleView::saveState() const
{
    return {};
}

void WhaleView::restoreState(const QByteArray &state)
{
    Q_UNUSED(state)
}

void WhaleView::startRename(const QUrl &url)
{
    Q_UNUSED(url)
}

QList<QUrl> WhaleView::selectedItems() const
{
    return d->context->selection().selected();
}

WhaleViewContext *WhaleView::context() const
{
    return d->context;
}

#include "moc_whaleview.cpp"
