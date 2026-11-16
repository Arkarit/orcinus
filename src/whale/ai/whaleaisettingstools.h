/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAISETTINGSTOOLS_H
#define WHALEAISETTINGSTOOLS_H

#include "whaleaimcpserver.h"

class DolphinMainWindow;

/**
 * @brief The settings tools of the agent's back channel (AI9d): list_settings and set_setting.
 *
 * Only an allow-list of KConfigXT entries whose page of the settings dialog does nothing but write
 * the value; a change is applied as the dialog's Apply does (save, refresh the window's views and
 * the location bars). See .privatedoc/ai-support-plan.md, AI9d.
 */
namespace WhaleAiSettingsTools
{
QList<WhaleAiWindowTool> create(DolphinMainWindow *window);

/** The keys ("Group/Entry") the agent may read and change; for the tests. */
QStringList keys();

/** The choice names of the enum entry @p key, in the order of its .kcfg; empty for others. */
QStringList choices(const QString &key);
}

#endif
