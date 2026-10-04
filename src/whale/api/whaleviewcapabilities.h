/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef WHALEVIEWCAPABILITIES_H
#define WHALEVIEWCAPABILITIES_H

#include <QFlags>

/**
 * Optional abilities of a view. The host uses them to decide which actions apply
 * while the view is shown.
 */
enum class WhaleViewCapability : quint32 {
    Selection = 1 << 0, ///< The view shows and changes the selection of the host.
    MultiSelection = 1 << 1, ///< More than one item can be selected.
    HostOrder = 1 << 2, ///< The view shows the host's items in the host's order, so sorting and filtering apply.
    Grouping = 1 << 3, ///< The view shows the host's groups.
    Zoom = 1 << 4, ///< The view reacts to zoom requests.
    Previews = 1 << 5, ///< The view reacts to the "show previews" setting.
    Drag = 1 << 6, ///< Items can be dragged out of the view.
    Drop = 1 << 7, ///< Items can be dropped onto the view.
    InlineRename = 1 << 8, ///< The view can rename an item inline.
    RecursiveContent = 1 << 9, ///< The view shows more than the current folder listing.
    KeyboardNavigation = 1 << 10, ///< The view can be operated with the keyboard.
};
Q_DECLARE_FLAGS(WhaleViewCapabilities, WhaleViewCapability)
Q_DECLARE_OPERATORS_FOR_FLAGS(WhaleViewCapabilities)

#endif
