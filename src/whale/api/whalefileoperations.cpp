/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "whalefileoperations.h"

class WhaleFileOperationsPrivate
{
};

WhaleFileOperations::WhaleFileOperations(QObject *parent)
    : QObject(parent)
{
}

WhaleFileOperations::~WhaleFileOperations() = default;

#include "moc_whalefileoperations.cpp"
