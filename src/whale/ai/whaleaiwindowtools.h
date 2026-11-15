/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAIWINDOWTOOLS_H
#define WHALEAIWINDOWTOOLS_H

#include "whaleaimcpserver.h"

class DolphinMainWindow;

/**
 * @brief The window tools of the agent's back channel (AI9): what the commands of the window do
 * not cover.
 *
 * Window size and state (no minimizing: the user would lose the conversation), panel widths, the
 * split of the split view, the messages above the views and the built-in viewer. Each changes only
 * what the user sees, never a file, and reports it for the transcript. See
 * .privatedoc/ai-support-plan.md (AI9).
 */
namespace WhaleAiWindowTools
{
QList<WhaleAiWindowTool> create(DolphinMainWindow *window);
}

#endif
