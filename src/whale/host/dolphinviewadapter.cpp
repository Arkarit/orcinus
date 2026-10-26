/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "dolphinviewadapter.h"

#include "views/dolphinview.h"

DolphinViewAdapter::DolphinViewAdapter(DolphinView *dolphinView, WhaleViewContext *context, QObject *parent)
    : WhaleView(context, parent)
    , m_dolphinView(dolphinView)
{
}

DolphinViewAdapter::~DolphinViewAdapter() = default;

QWidget *DolphinViewAdapter::widget()
{
    return m_dolphinView;
}

WhaleViewCapabilities DolphinViewAdapter::capabilities() const
{
    using C = WhaleViewCapability;
    return C::Selection | C::MultiSelection | C::HostOrder | C::Grouping | C::Zoom | C::Previews | C::Drag | C::Drop | C::InlineRename | C::KeyboardNavigation
        | C::HiddenFiles | C::LinkedSelection;
}

#include "moc_dolphinviewadapter.cpp"
