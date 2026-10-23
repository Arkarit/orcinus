/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALESCRIPTLAUNCHER_H
#define WHALESCRIPTLAUNCHER_H

#include <QString>
#include <QStringList>

class KFileItem;
class QWidget;

/**
 * @brief Runs executable scripts in a terminal when they are opened (Whale).
 *
 * KIO knows "ask", "open" and "execute" for executable scripts (kiorc, [Executable scripts]
 * behaviourOnLaunch). Whale adds running them in the user's default terminal, stored in its own
 * whalerc ([Executable scripts] RunInTerminal), so other KDE applications never meet a value
 * they do not know. While it is set, it wins over KIO's choice in Whale.
 *
 * The terminal starts in the script's folder. It either stays open after the script ended until
 * Enter is pressed, or closes then, unless the script failed: its exit code is shown and the
 * window waits for Enter as well.
 *
 * The terminal is the one of the system settings (kdeglobals TerminalService, otherwise
 * TerminalApplication, otherwise Konsole), started without the "keep open" option
 * KTerminalLauncherJob adds for Konsole and xterm: the window is kept open by the script around
 * the user's script instead.
 */
namespace WhaleScriptLauncher
{
enum class Mode {
    Off, ///< KIO decides (kiorc)
    Terminal, ///< in a terminal that stays open
    TerminalClose, ///< in a terminal that closes when the script succeeded
};

/** @return The mode set in whalerc. */
Mode mode();
/** Stores @p mode in whalerc. */
void setMode(Mode mode);

/** @return Whether @p item is a local executable script, as KIO's "Open or execute" sees it. */
bool isScript(const KFileItem &item);

/**
 * @return The program and arguments that run @p scriptPath in @p terminal ("konsole",
 *         "xterm -fa Mono", …) as @p mode says; empty for Mode::Off.
 */
QStringList terminalCommand(const QString &terminal, const QString &scriptPath, Mode mode);

/** @return The default terminal of the system settings with its options, e.g. "konsole". */
QString defaultTerminal();

/**
 * Runs @p item in the default terminal if it is a script and the mode says so.
 * @return Whether it was started; otherwise the caller opens @p item as usual.
 */
bool launch(const KFileItem &item, QWidget *window);
}

#endif
