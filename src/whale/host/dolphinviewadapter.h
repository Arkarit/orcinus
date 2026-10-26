/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DOLPHINVIEWADAPTER_H
#define DOLPHINVIEWADAPTER_H

#include "whaleview.h"

class DolphinView;

/**
 * @brief The built-in file list (DolphinView) as a WhaleView.
 *
 * The adapter only presents the existing DolphinView of a view area. Unlike other
 * views it does not own its widget: the DolphinView belongs to the DolphinViewContainer
 * and outlives the adapter.
 */
class DolphinViewAdapter : public WhaleView
{
    Q_OBJECT

public:
    DolphinViewAdapter(DolphinView *dolphinView, WhaleViewContext *context, QObject *parent = nullptr);
    ~DolphinViewAdapter() override;

    QWidget *widget() override;
    WhaleViewCapabilities capabilities() const override;

private:
    DolphinView *const m_dolphinView;
};

#endif
