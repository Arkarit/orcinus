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
 * while the view is shown: actions a view cannot honor are disabled, so they never
 * act on state the user cannot see (see WhaleViewActions in the host).
 */
enum class WhaleViewCapability : quint32 {
    /// The view shows the host's selection and changes it through the context. Without
    /// it, the host clears the selection when the view is shown and disables every
    /// action that works on selected items.
    Selection = 1 << 0,
    /// More than one item can be selected. Without it, "Select All" and "Invert
    /// Selection" are disabled.
    MultiSelection = 1 << 1,
    /// The view shows the host's items in the host's order, so sorting and filtering
    /// apply. Without it, the sorting, grouping and "Show Additional Information"
    /// actions are disabled.
    HostOrder = 1 << 2,
    /// The view shows the host's groups. Without it, "Group By" is disabled.
    Grouping = 1 << 3,
    /// The view reacts to zoom requests. Without it, the zoom actions are disabled.
    Zoom = 1 << 4,
    /// The view shows previews (WhalePreviewProvider) and follows the "Show Previews"
    /// setting. Without it, the action is disabled and the view gets no previews.
    Previews = 1 << 5,
    /// Items can be dragged out of the view (WhaleFileOperations::startDrag()). (Informative.)
    Drag = 1 << 6,
    /// Items can be dropped onto the view (WhaleFileOperations::drop()). (Informative.)
    Drop = 1 << 7,
    /// The view can rename an item inline: F2 on one selected item calls
    /// WhaleView::startRename(). Without it, Whale renames with a dialog.
    InlineRename = 1 << 8,
    /// The view shows more than the current folder listing. Its items cannot be the
    /// host's selection, so Whale's actions on selected items (and their shortcuts) are
    /// disabled; the view offers operations on its items itself, through
    /// WhaleFileOperations (e.g. from its own context menu).
    RecursiveContent = 1 << 9,
    KeyboardNavigation = 1 << 10, ///< The view can be operated with the keyboard. (Informative.)
    /// The view follows the "Show Hidden Files" setting: it shows hidden items only while
    /// the setting is on. Views that show the host's items (WhaleViewContext::items())
    /// follow it, because the host lists hidden items only then. Without it, the action is
    /// disabled while the view is shown.
    HiddenFiles = 1 << 11,
};
Q_DECLARE_FLAGS(WhaleViewCapabilities, WhaleViewCapability)
Q_DECLARE_OPERATORS_FOR_FLAGS(WhaleViewCapabilities)

#endif
