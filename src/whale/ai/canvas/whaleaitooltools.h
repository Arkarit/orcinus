/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAITOOLTOOLS_H
#define WHALEAITOOLTOOLS_H

#include "whaleaimcpserver.h"

class WhaleAiCanvasPanel;

/**
 * @brief The agent's tools for writing canvas tools (CV3): the development loop.
 *
 * tool_guide, tool_list and tool_read_file read; tool_write_file and tool_delete_file change the
 * tool's files (only through them, so Orcinus sees every change); tool_open (re)loads a tool,
 * tool_close closes it; tool_log, tool_inspect and tool_screenshot show how it went, tool_invoke
 * acts in it as the user would. Writing, opening and acting are refused in a read-only mode (the
 * AI panel). See .privatedoc/ai-canvas-plan.md.
 */
namespace WhaleAiToolTools
{
QList<WhaleAiWindowTool> create(WhaleAiCanvasPanel *panel, const std::function<void()> &showPanel);

/** The names refused in a read-only mode. */
const QStringList &changingTools();

/** The most bytes of a tool file. */
constexpr qint64 MaxFileSize = 256 * 1024;
/** The most files of a tool. */
constexpr int MaxFiles = 50;
}

#endif
