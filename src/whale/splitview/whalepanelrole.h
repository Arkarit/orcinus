/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEPANELROLE_H
#define WHALEPANELROLE_H

/**
 * @brief The position of a view in its tab (PI).
 *
 * With one style for all folders, each position has its own view properties (see
 * ViewProperties), so the left and the right view of a split view keep their own style. The
 * position, not the view, holds it: the view that is left when the left one closes becomes the
 * left one and takes its style along.
 */
enum class WhalePanelRole {
    Left,
    Right,
};

#endif
