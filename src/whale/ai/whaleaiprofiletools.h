/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAIPROFILETOOLS_H
#define WHALEAIPROFILETOOLS_H

#include "whaleaimcpserver.h"

class DolphinMainWindow;

/**
 * @brief The profile tools of the agent's back channel (AI9e4): list_profiles, apply_profile and
 * save_profile (profiles are "styles" internally, see WhaleStyleManager).
 *
 * Both changes go into the change log (WhaleAiChangeLog): applying keeps a capture of the window
 * taken before, which undo applies again; saving keeps the profiles before the agent's first save.
 * save_profile is refused in the agent's read-only mode, as set_setting (the panel checks it).
 */
namespace WhaleAiProfileTools
{
QList<WhaleAiWindowTool> create(DolphinMainWindow *window);
}

#endif
