/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
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

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QMimeType>
#include <QPointer>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QWidget>

namespace
{
const QString GroupName = QStringLiteral("Executable scripts");
const QString ModeKey = QStringLiteral("RunInTerminal");
// Stands for the exit code in the message, filled in by the shell.
const QString ExitCodeMarker = QStringLiteral("@EXIT@");

// How long a KWin script waits for the terminal's window before Whale unloads it.
constexpr int KWinScriptLifetimeMs = 30000;

std::function<void(WhaleScriptLauncher::WindowRequest, qint64)> &windowRequestHandler()
{
    static std::function<void(WhaleScriptLauncher::WindowRequest, qint64)> handler;
    return handler;
}

bool isMinimized(WhaleScriptLauncher::Mode mode)
{
    return mode == WhaleScriptLauncher::Mode::Minimized || mode == WhaleScriptLauncher::Mode::MinimizedClose;
}

bool closesOnSuccess(WhaleScriptLauncher::Mode mode)
{
    return mode == WhaleScriptLauncher::Mode::TerminalClose || mode == WhaleScriptLauncher::Mode::MinimizedClose;
}

// Runs the script ($1) and waits for Enter afterwards, always or only if it failed. Ctrl+C
// stops the script, not the waiting: the trap runs a no-op in this shell only, the script gets
// the default handling. $2 is the message about the exit code, $3 the one about Enter, $4 the
// file for the exit code (written at once, so Whale sees it while the window still waits).
QString shellScript(bool closeOnSuccess, bool reportStatus)
{
    QString script = QStringLiteral("trap : INT; \"$1\"; s=$?; trap - INT; ");
    if (reportStatus) {
        script += QStringLiteral("printf '%s\\n' \"$s\" > \"$4.tmp\" && mv \"$4.tmp\" \"$4\"; ");
    }
    if (closeOnSuccess) {
        script += QStringLiteral("[ \"$s\" -eq 0 ] && exit 0; ");
    }
    script += QStringLiteral("m=$2; printf '\\n%s%s%s\\n%s' \"${m%%@EXIT@*}\" \"$s\" \"${m#*@EXIT@}\" \"$3\"; read -r _; exit $s");
    return script;
}

QString runtimeFolder()
{
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/whale-scripts");
    QDir().mkpath(folder);
    return folder;
}

// KWin scripts act on the window of the terminal's process and unload themselves when done;
// Whale unloads them anyway after a while, e.g. when the window never appeared.
const char *const MinimizeScript =
    "const pid = %1;\n"
    "let done = false;\n"
    "function take(w) {\n"
    "    if (done || w.pid !== pid || !w.normalWindow) return;\n"
    "    done = true;\n"
    "    w.minimized = true;\n"
    "    callDBus('org.kde.KWin', '/Scripting', 'org.kde.kwin.Scripting', 'unloadScript', '%2');\n"
    "}\n"
    "workspace.windowAdded.connect(take);\n"
    "workspace.windowList().forEach(take);\n";
const char *const RaiseScript =
    "const pid = %1;\n"
    "for (const w of workspace.windowList()) {\n"
    "    if (w.pid === pid && w.normalWindow) {\n"
    "        w.minimized = false;\n"
    "        workspace.activeWindow = w;\n"
    "        break;\n"
    "    }\n"
    "}\n"
    "callDBus('org.kde.KWin', '/Scripting', 'org.kde.kwin.Scripting', 'unloadScript', '%2');\n";

QDBusMessage kwinScripting(const QString &method)
{
    return QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"), QStringLiteral("org.kde.kwin.Scripting"), method);
}

void requestFromKWin(WhaleScriptLauncher::WindowRequest request, qint64 pid)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.interface() || !bus.interface()->isServiceRegistered(QStringLiteral("org.kde.KWin"))) {
        return; // not Plasma: the window stays as the terminal shows it
    }
    const bool minimize = request == WhaleScriptLauncher::WindowRequest::Minimize;
    const QString name = QStringLiteral("whale-%1-%2").arg(minimize ? QStringLiteral("minimize") : QStringLiteral("raise")).arg(pid);
    const QString path = runtimeFolder() + QLatin1Char('/') + name + QStringLiteral(".js");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    file.write(QString::fromLatin1(minimize ? MinimizeScript : RaiseScript).arg(pid).arg(name).toUtf8());
    file.close();

    QDBusMessage load = kwinScripting(QStringLiteral("loadScript"));
    load << path << name;
    const QDBusMessage loaded = bus.call(load);
    if (loaded.type() == QDBusMessage::ReplyMessage && !loaded.arguments().isEmpty() && loaded.arguments().first().toInt() >= 0) {
        // Not "run" on /Scripting/Script<id>: KWin hands out the ID of an unloaded script again
        // while that object path still belongs to another script (KWin 6.6). start() runs every
        // loaded script that does not run yet.
        bus.call(kwinScripting(QStringLiteral("start")));
    }
    // KWin has read it when start() returns.
    QFile::remove(path);
    QTimer::singleShot(KWinScriptLifetimeMs, qApp, [name] {
        QDBusMessage unload = kwinScripting(QStringLiteral("unloadScript"));
        unload << name;
        QDBusConnection::sessionBus().call(unload, QDBus::NoBlock);
    });
}

void requestWindow(WhaleScriptLauncher::WindowRequest request, qint64 pid)
{
    if (pid <= 0) {
        return;
    }
    if (windowRequestHandler()) {
        windowRequestHandler()(request, pid);
    } else {
        requestFromKWin(request, pid);
    }
}

// Waits for the exit code of a script run minimized and brings its window to the front if it
// failed. Lives as long as the script runs (or Whale).
void watchStatus(const QString &statusPath, qint64 pid)
{
    auto watcher = new QFileSystemWatcher({QFileInfo(statusPath).absolutePath()}, qApp);
    auto check = [watcher, statusPath, pid] {
        QFile file(statusPath);
        if (!file.open(QIODevice::ReadOnly)) {
            return;
        }
        const int status = QString::fromLatin1(file.readAll()).trimmed().toInt();
        file.close();
        QFile::remove(statusPath);
        watcher->deleteLater();
        if (status != 0) {
            requestWindow(WhaleScriptLauncher::WindowRequest::Raise, pid);
        }
    };
    QObject::connect(watcher, &QFileSystemWatcher::directoryChanged, watcher, check);
    check(); // the script may have ended already
}
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
    if (value == QLatin1String("minimizedKeepOpen")) {
        return Mode::Minimized;
    }
    if (value == QLatin1String("minimizedCloseOnSuccess")) {
        return Mode::MinimizedClose;
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
    case Mode::Minimized:
        group.writeEntry(ModeKey, QStringLiteral("minimizedKeepOpen"));
        break;
    case Mode::MinimizedClose:
        group.writeEntry(ModeKey, QStringLiteral("minimizedCloseOnSuccess"));
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

QStringList terminalCommand(const QString &terminal, const QString &scriptPath, Mode mode, const QString &statusPath)
{
    if (mode == Mode::Off) {
        return {};
    }
    QStringList command = KShell::splitArgs(terminal);
    if (command.isEmpty()) {
        command = {QStringLiteral("konsole")};
    }
    command << QStringLiteral("-e") << QStringLiteral("/bin/sh") << QStringLiteral("-c") << shellScript(closesOnSuccess(mode), !statusPath.isEmpty())
            << QStringLiteral("whale-script") << scriptPath
            << i18nc("@info:shell %1 is the exit code of a script run in a terminal", "The script ended with exit code %1.", ExitCodeMarker)
            << i18nc("@info:shell", "Press Enter to close this window.");
    if (!statusPath.isEmpty()) {
        command << statusPath;
    }
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
    const QString statusPath =
        isMinimized(current) ? runtimeFolder() + QStringLiteral("/%1.status").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)) : QString();
    QStringList command = terminalCommand(defaultTerminal(), path, current, statusPath);
    const QString program = command.takeFirst();
    auto job = new KIO::CommandLauncherJob(program, command, window);
    job->setWorkingDirectory(QFileInfo(path).absolutePath());
    // A missing terminal is reported in a dialog.
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, window));
    if (isMinimized(current)) {
        // The process has started, its window comes a moment later (in time for the KWin
        // script, checked on Plasma 6.6 with Konsole).
        QObject::connect(job, &KJob::result, qApp, [job, statusPath] {
            if (job->error()) {
                return;
            }
            requestWindow(WindowRequest::Minimize, job->pid());
            watchStatus(statusPath, job->pid());
        });
    }
    job->start();
    return true;
}

void setWindowRequestHandler(const std::function<void(WindowRequest request, qint64 pid)> &handler)
{
    windowRequestHandler() = handler;
}
}
