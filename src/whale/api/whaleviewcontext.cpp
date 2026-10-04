/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "whaleviewcontext.h"

class WhaleViewContextPrivate
{
};

WhaleViewContext::WhaleViewContext(QObject *parent)
    : QObject(parent)
{
}

WhaleViewContext::~WhaleViewContext() = default;

#include "moc_whaleviewcontext.cpp"
