/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAIUIACTIONS_H
#define WHALEAIUIACTIONS_H

#include <QMetaType>
#include <QString>

/** One action of the window as the agent sees it (AI9b); filled by the window. */
struct WhaleAiUiAction {
    /** The name in the action collection, e.g. "details". */
    QString name;
    /** The text without accelerator markers. */
    QString text;
    QString toolTip;
    /** Where it is in the menu bar, e.g. "View ▸ View Mode"; empty if not in it. */
    QString menu;
    bool checkable = false;
    bool checked = false;
    bool enabled = true;
    /** One of an exclusive group (view modes, sort fields): it cannot be turned off, only another one on. */
    bool exclusive = false;
};
Q_DECLARE_METATYPE(WhaleAiUiAction)

/**
 * @brief Which actions of the window the agent may trigger (AI9b).
 *
 * Generic: every action is allowed unless it is listed here. Blocked are file operations (the
 * agent has its own tools for them, and Orcinus' undo would mix the two), ways out of the panel
 * (terminal, administrator mode, other programs), actions that open dialogs (the agent cannot
 * answer them, and they would block the back channel) and a few that end the conversation or
 * change saved data without undo. This protects against mistakes through our tools, not against
 * a hostile agent: KXMLGUI exports the same actions on the session bus. See
 * .privatedoc/ai-support-plan.md (AI9a).
 *
 * New actions are allowed until they are listed: when adding an action to the window, check
 * whether it belongs here.
 */
namespace WhaleAiUiActions
{
enum class Access {
    Allowed,
    FileOperation,
    WayOut,
    Dialog,
    Window,
    SavedData,
};

Access access(const QString &name);
/** @return Why @p access blocks an action, for the agent (not translated). */
QString reason(Access access);
}

#endif
