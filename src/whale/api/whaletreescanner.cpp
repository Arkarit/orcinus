/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "whaletreescanner.h"

class WhaleTreeScannerPrivate
{
};

WhaleTreeScanner::WhaleTreeScanner(QObject *parent)
    : QObject(parent)
{
}

WhaleTreeScanner::~WhaleTreeScanner() = default;

#include "moc_whaletreescanner.cpp"
