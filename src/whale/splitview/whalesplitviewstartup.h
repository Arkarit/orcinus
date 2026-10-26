/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALESPLITVIEWSTARTUP_H
#define WHALESPLITVIEWSTARTUP_H

/**
 * @brief Whether new windows and tabs open with a split view (PI3).
 *
 * GeneralSettings::splitViewAtStartup() says so: always, never, or as the split view was last
 * (the default). The last state is what a window showed when it closed or when its split view
 * opened or closed, whichever came last. It is kept in the state config, not in the settings.
 */
namespace WhaleSplitViewStartup
{
/** Turns the former option "SplitView" (bool) into GeneralSettings::splitViewAtStartup(), once. */
void migrate();

/** @return Whether a new window or tab opens with a split view. */
bool openInSplitView();

/** Remembers @p enabled as the last state of the split view. */
void rememberSplitView(bool enabled);
}

#endif
