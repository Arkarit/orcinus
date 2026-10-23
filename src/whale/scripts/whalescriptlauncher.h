/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALESCRIPTLAUNCHER_H
#define WHALESCRIPTLAUNCHER_H

#include <QString>
#include <QStringList>

#include <functional>

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
 *
 * Minimized, the window goes to the taskbar as soon as it appears, through a short KWin script
 * that finds it by the terminal's process ID (Plasma only; elsewhere the window stays as it is).
 * As a failure could go unnoticed there, the shell around the script writes its exit code into
 * a file Whale watches, and Whale brings the window to the front if it is not 0.
 */
namespace WhaleScriptLauncher
{
enum class Mode {
    Off, ///< KIO decides (kiorc)
    Terminal, ///< in a terminal that stays open
    TerminalClose, ///< in a terminal that closes when the script succeeded
    Minimized, ///< as Terminal, minimized; to the front when the script failed
    MinimizedClose, ///< as TerminalClose, minimized; to the front when the script failed
};

/** What Whale asks the window manager to do with a terminal window. */
enum class WindowRequest {
    Minimize, ///< as soon as the terminal's window appears
    Raise, ///< show and activate it
};

/** @return The mode set in whalerc. */
Mode mode();
/** Stores @p mode in whalerc. */
void setMode(Mode mode);

/** @return Whether @p item is a local executable script, as KIO's "Open or execute" sees it. */
bool isScript(const KFileItem &item);

/**
 * @return The program and arguments that run @p scriptPath in @p terminal ("konsole",
 *         "xterm -fa Mono", …) as @p mode says; empty for Mode::Off. With a @p statusPath, the
 *         exit code is written there once the script ended.
 */
QStringList terminalCommand(const QString &terminal, const QString &scriptPath, Mode mode, const QString &statusPath = QString());

/** @return The default terminal of the system settings with its options, e.g. "konsole". */
QString defaultTerminal();

/**
 * Runs @p item in the default terminal if it is a script and the mode says so.
 * @return Whether it was started; otherwise the caller opens @p item as usual.
 */
bool launch(const KFileItem &item, QWidget *window);

/**
 * For tests: @p handler gets the requests for the terminal windows (by the terminal's process
 * ID) instead of KWin. An empty handler sends them to KWin again.
 */
void setWindowRequestHandler(const std::function<void(WindowRequest request, qint64 pid)> &handler);
}

#endif
