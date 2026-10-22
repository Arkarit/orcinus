/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALESTYLEELEMENTTYPES_H
#define WHALESTYLEELEMENTTYPES_H

class WhaleStyleElements;

/**
 * @brief The element types of styles that come with Whale (ST2).
 *
 * In the order of applying: the split view, the styles of the left and the right view, their
 * filter bars, the zoom of the view modes, then the menu bar, the toolbars of the window, the File View Toolbars, the
 * status bar and the panels. Values are plain text:
 *
 * - "split", "menuBar", "fileViewToolBars": "true" or "false"
 * - "viewStyle.left", "viewStyle.right": ViewProperties::toData() of the position
 * - "filterBar.left", "filterBar.right": "visible=true,lock=true,case=false,mode=Glob,scope=Files,text=…"
 *   (the text last; a hidden filter bar has none)
 * - "zoom": "Icons=48,128;Compact=16,32;Details=22,32" (icon size, preview size)
 * - "statusBar": "Small", "FullWidth" or "Disabled"
 * - "toolBars", "panels": "name=true,name=false"; names not listed are left as they are
 *
 * Applying works on the current tab of the window. Like a change by the user, it is what new
 * windows, tabs and splits start from (E3): the split view is remembered, the styles go to the
 * sets of their positions, the rest are settings or the window's state anyway.
 */
namespace WhaleStyleElementTypes
{
/** Appends the element types of Whale to @p elements. */
void addTo(WhaleStyleElements &elements);
}

#endif
