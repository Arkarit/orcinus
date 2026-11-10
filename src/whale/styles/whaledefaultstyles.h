/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEDEFAULTSTYLES_H
#define WHALEDEFAULTSTYLES_H

#include "whalestyle.h"

#include <QList>

class WhaleStyleStore;

/**
 * @brief The profiles Orcinus ships: created once, on the first start of a new user.
 *
 * Their IDs are fixed; their names are translated when they are created and belong to the
 * user from then on, like every other profile.
 */
namespace WhaleDefaultStyles
{
/** @return The shipped profiles in their order. */
QList<WhaleStyle> styles();

/** @return The ID of the profile a new user starts with. */
QString startStyleId();

/**
 * Puts styles() into @p store if it has no profiles yet; @p store is not saved.
 * @return Whether the profiles were added.
 */
bool seed(WhaleStyleStore &store);
}

#endif
