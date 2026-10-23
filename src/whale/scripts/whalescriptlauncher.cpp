/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whalescriptlauncher.h"

#include <KAuthorized>
#include <KConfigGroup>
#include <KFileItem>
#include <KIO/CommandLauncherJob>
#include <KIO/JobUiDelegateFactory>
#include <KLocalizedString>
#include <KService>
#include <KSharedConfig>
#include <KShell>

#include <QFileInfo>
#include <QMimeType>
#include <QWidget>

namespace
{
const QString GroupName = QStringLiteral("Executable scripts");
const QString ModeKey = QStringLiteral("RunInTerminal");
// Stands for the exit code in the message, filled in by the shell.
const QString ExitCodeMarker = QStringLiteral("@EXIT@");

// Runs the script ($1) and waits for Enter afterwards, always or only if it failed. Ctrl+C
// stops the script, not the waiting: the trap runs a no-op in this shell only, the script gets
// the default handling. $2 is the message about the exit code, $3 the one about Enter.
const QString KeepOpenScript = QStringLiteral(
    "trap : INT; \"$1\"; s=$?; trap - INT; "
    "m=$2; printf '\\n%s%s%s\\n%s' \"${m%%@EXIT@*}\" \"$s\" \"${m#*@EXIT@}\" \"$3\"; read -r _; exit $s");
const QString CloseScript = QStringLiteral(
    "trap : INT; \"$1\"; s=$?; trap - INT; [ \"$s\" -eq 0 ] && exit 0; "
    "m=$2; printf '\\n%s%s%s\\n%s' \"${m%%@EXIT@*}\" \"$s\" \"${m#*@EXIT@}\" \"$3\"; read -r _; exit $s");
}

namespace WhaleScriptLauncher
{
Mode mode()
{
    const QString value = KConfigGroup(KSharedConfig::openConfig(), GroupName).readEntry(ModeKey, QString());
    if (value == QLatin1String("keepOpen")) {
        return Mode::Terminal;
    }
    if (value == QLatin1String("closeOnSuccess")) {
        return Mode::TerminalClose;
    }
    return Mode::Off;
}

void setMode(Mode mode)
{
    KConfigGroup group(KSharedConfig::openConfig(), GroupName);
    switch (mode) {
    case Mode::Off:
        group.deleteEntry(ModeKey);
        break;
    case Mode::Terminal:
        group.writeEntry(ModeKey, QStringLiteral("keepOpen"));
        break;
    case Mode::TerminalClose:
        group.writeEntry(ModeKey, QStringLiteral("closeOnSuccess"));
        break;
    }
    group.sync();
}

bool isScript(const KFileItem &item)
{
    // As DolphinViewContainer::slotfileMiddleClickActivated() and KIO's OpenUrlJob see it.
    const QMimeType mimeType = item.currentMimeType();
    return item.isLocalFile() && !item.isDir() && mimeType.inherits(QStringLiteral("application/x-executable"))
        && mimeType.inherits(QStringLiteral("text/plain")) && item.isExecutable();
}

QStringList terminalCommand(const QString &terminal, const QString &scriptPath, Mode mode)
{
    if (mode == Mode::Off) {
        return {};
    }
    QStringList command = KShell::splitArgs(terminal);
    if (command.isEmpty()) {
        command = {QStringLiteral("konsole")};
    }
    command << QStringLiteral("-e") << QStringLiteral("/bin/sh") << QStringLiteral("-c") << (mode == Mode::Terminal ? KeepOpenScript : CloseScript)
            << QStringLiteral("whale-script") << scriptPath
            << i18nc("@info:shell %1 is the exit code of a script run in a terminal", "The script ended with exit code %1.", ExitCodeMarker)
            << i18nc("@info:shell", "Press Enter to close this window.");
    return command;
}

QString defaultTerminal()
{
    const KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("kdeglobals"), KConfig::SimpleConfig), QStringLiteral("General"));
    if (const KService::Ptr service = KService::serviceByStorageId(group.readEntry("TerminalService"))) {
        // The Exec line without field codes such as %u.
        QStringList exec = KShell::splitArgs(service->exec());
        exec.erase(std::remove_if(exec.begin(),
                                  exec.end(),
                                  [](const QString &arg) {
                                      return arg.size() == 2 && arg.startsWith(QLatin1Char('%'));
                                  }),
                   exec.end());
        if (!exec.isEmpty()) {
            return KShell::joinArgs(exec);
        }
    }
    return group.readPathEntry("TerminalApplication", QStringLiteral("konsole"));
}

bool launch(const KFileItem &item, QWidget *window)
{
    const Mode current = mode();
    if (current == Mode::Off || !isScript(item) || !KAuthorized::authorize(QStringLiteral("shell_access"))) {
        return false;
    }
    const QString path = item.localPath();
    QStringList command = terminalCommand(defaultTerminal(), path, current);
    const QString program = command.takeFirst();
    auto job = new KIO::CommandLauncherJob(program, command, window);
    job->setWorkingDirectory(QFileInfo(path).absolutePath());
    // A missing terminal is reported in a dialog.
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, window));
    job->start();
    return true;
}
}
