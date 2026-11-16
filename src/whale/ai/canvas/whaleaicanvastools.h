/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAICANVASTOOLS_H
#define WHALEAICANVASTOOLS_H

#include "whaleaimcpserver.h"

class WhaleAiCanvasPanel;

/**
 * @brief The canvas tools of the agent's back channel (CV1): show, clear and read the canvas.
 *
 * They change only what the user sees, never a file, so they are allowed in every mode.
 * @p showPanel brings the canvas into view when the agent shows something while it is hidden.
 * See .privatedoc/ai-canvas-plan.md.
 */
namespace WhaleAiCanvasTools
{
QList<WhaleAiWindowTool> create(WhaleAiCanvasPanel *panel, const std::function<void()> &showPanel);
}

#endif
