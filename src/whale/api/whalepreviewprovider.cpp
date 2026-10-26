/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "whalepreviewprovider.h"

class WhalePreviewProviderPrivate
{
};

WhalePreviewProvider::WhalePreviewProvider(QObject *parent)
    : QObject(parent)
{
}

WhalePreviewProvider::~WhalePreviewProvider() = default;

#include "moc_whalepreviewprovider.cpp"
