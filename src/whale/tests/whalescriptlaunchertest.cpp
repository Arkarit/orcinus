/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whalescriptlauncher.h"

#include "settings/interface/confirmationssettingspage.h"
#include "testdir.h"

#include <KConfigGroup>
#include <KFileItem>
#include <KLocalizedString>
#include <KService>
#include <KSharedConfig>

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

/**
 * Executable scripts in a terminal: the mode in orcinusrc, which files count as scripts, the
 * command, and running it in a stand-in terminal that writes down what it got and runs it
 * without a window and without input (so the "Press Enter" wait ends at once).
 */
class WhaleScriptLauncherTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();
    void cleanupTestCase();

    void testMode();
    void testIsScript();
    void testTerminalCommand();
    void testDefaultTerminal();
    void testRunInTerminal_data();
    void testRunInTerminal();
    void testOffLeavesItToKio();
    void testSettingsPage();

private:
    QString createScript(const QString &name, const QByteArray &body, bool executable = true);
    void setTerminal(const QString &terminalApplication, const QString &terminalService = QString());
    // Waits until the stand-in terminal has finished; returns its log.
    QString waitForTerminal();

    // A runtime directory of the test's own: the status files of scripts run meanwhile (by Whale
    // or other tests) are none of its business.
    QTemporaryDir m_runtime;
    QScopedPointer<TestDir> m_dir;
    QString m_terminal;
    QString m_log;
    // What would have gone to KWin.
    QList<std::pair<WhaleScriptLauncher::WindowRequest, qint64>> m_windowRequests;
};

void WhaleScriptLauncherTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QVERIFY(m_runtime.isValid());
    qputenv("XDG_RUNTIME_DIR", QFile::encodeName(m_runtime.path()));
    QCOMPARE(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation), m_runtime.path());
    KLocalizedString::setApplicationDomain(TRANSLATION_DOMAIN);
    // Never KWin: no window of the user's desktop is touched.
    WhaleScriptLauncher::setWindowRequestHandler([this](WhaleScriptLauncher::WindowRequest request, qint64 pid) {
        m_windowRequests.append({request, pid});
    });
}

void WhaleScriptLauncherTest::init()
{
    m_windowRequests.clear();
    m_dir.reset(new TestDir());
    m_log = m_dir->path() + QStringLiteral("/terminal.log");
    // The stand-in terminal: "-e" and the command, as KDE's terminals take it.
    m_terminal = createScript(QStringLiteral("fake-terminal"),
                              QByteArrayLiteral("#!/bin/sh\n"
                                                "log=\"$(dirname \"$0\")/terminal.log\"\n"
                                                "{ echo \"pid=$$\"; echo \"pwd=$PWD\"; echo \"first=$1\"; } > \"$log.tmp\"\n"
                                                "shift\n"
                                                "\"$@\" < /dev/null >> \"$log.tmp\" 2>&1\n"
                                                "echo \"status=$?\" >> \"$log.tmp\"\n"
                                                "mv \"$log.tmp\" \"$log\"\n"));
    setTerminal(m_terminal);
}

void WhaleScriptLauncherTest::cleanup()
{
    WhaleScriptLauncher::setMode(WhaleScriptLauncher::Mode::Off);
    KSharedConfig::openConfig(QStringLiteral("kiorc"))->deleteGroup(QStringLiteral("Executable scripts"));
    KSharedConfig::openConfig(QStringLiteral("kiorc"))->sync();
    m_dir.reset();
}

void WhaleScriptLauncherTest::cleanupTestCase()
{
    // No stand-in terminal for other tests of the test profile.
    KSharedConfig::Ptr globals = KSharedConfig::openConfig(QStringLiteral("kdeglobals"), KConfig::SimpleConfig);
    KConfigGroup general(globals, QStringLiteral("General"));
    general.deleteEntry("TerminalApplication");
    general.deleteEntry("TerminalService");
    globals->sync();
}

QString WhaleScriptLauncherTest::createScript(const QString &name, const QByteArray &body, bool executable)
{
    m_dir->createFile(name, body);
    const QString path = m_dir->path() + QLatin1Char('/') + name;
    QFile::Permissions permissions = QFile::ReadOwner | QFile::WriteOwner;
    if (executable) {
        permissions |= QFile::ExeOwner;
    }
    QFile::setPermissions(path, permissions);
    return path;
}

void WhaleScriptLauncherTest::setTerminal(const QString &terminalApplication, const QString &terminalService)
{
    KSharedConfig::Ptr globals = KSharedConfig::openConfig(QStringLiteral("kdeglobals"), KConfig::SimpleConfig);
    KConfigGroup general(globals, QStringLiteral("General"));
    general.writePathEntry("TerminalApplication", terminalApplication);
    if (terminalService.isEmpty()) {
        general.deleteEntry("TerminalService");
    } else {
        general.writeEntry("TerminalService", terminalService);
    }
    globals->sync();
}

QString WhaleScriptLauncherTest::waitForTerminal()
{
    if (!QTest::qWaitFor(
            [this] {
                return QFile::exists(m_log);
            },
            10000)) {
        return QString();
    }
    QFile file(m_log);
    file.open(QIODevice::ReadOnly);
    const QString log = QString::fromUtf8(file.readAll());
    QFile::remove(m_log);
    return log;
}

void WhaleScriptLauncherTest::testMode()
{
    using WhaleScriptLauncher::Mode;
    QCOMPARE(WhaleScriptLauncher::mode(), Mode::Off);
    const KConfigGroup group(KSharedConfig::openConfig(), QStringLiteral("Executable scripts"));

    WhaleScriptLauncher::setMode(Mode::Terminal);
    QCOMPARE(WhaleScriptLauncher::mode(), Mode::Terminal);
    QCOMPARE(group.readEntry("RunInTerminal"), QStringLiteral("keepOpen"));
    WhaleScriptLauncher::setMode(Mode::TerminalClose);
    QCOMPARE(WhaleScriptLauncher::mode(), Mode::TerminalClose);
    QCOMPARE(group.readEntry("RunInTerminal"), QStringLiteral("closeOnSuccess"));
    WhaleScriptLauncher::setMode(Mode::Minimized);
    QCOMPARE(WhaleScriptLauncher::mode(), Mode::Minimized);
    QCOMPARE(group.readEntry("RunInTerminal"), QStringLiteral("minimizedKeepOpen"));
    WhaleScriptLauncher::setMode(Mode::MinimizedClose);
    QCOMPARE(WhaleScriptLauncher::mode(), Mode::MinimizedClose);
    QCOMPARE(group.readEntry("RunInTerminal"), QStringLiteral("minimizedCloseOnSuccess"));
    WhaleScriptLauncher::setMode(Mode::Off);
    QCOMPARE(WhaleScriptLauncher::mode(), Mode::Off);
    QVERIFY(!group.hasKey("RunInTerminal"));
    // Only in orcinusrc: KIO and other applications never see it.
    QVERIFY(!KConfigGroup(KSharedConfig::openConfig(QStringLiteral("kiorc")), QStringLiteral("Executable scripts")).hasKey("RunInTerminal"));
}

void WhaleScriptLauncherTest::testIsScript()
{
    const QString script = createScript(QStringLiteral("run.sh"), QByteArrayLiteral("#!/bin/sh\necho hi\n"));
    const QString notExecutable = createScript(QStringLiteral("plain.sh"), QByteArrayLiteral("#!/bin/sh\necho hi\n"), false);
    const QString text = createScript(QStringLiteral("notes.txt"), QByteArrayLiteral("just text\n"));
    m_dir->createDir(QStringLiteral("folder"));

    QVERIFY(WhaleScriptLauncher::isScript(KFileItem(QUrl::fromLocalFile(script))));
    QVERIFY(!WhaleScriptLauncher::isScript(KFileItem(QUrl::fromLocalFile(notExecutable))));
    // An executable text file that is no script is opened as text, as by KIO.
    QVERIFY(!WhaleScriptLauncher::isScript(KFileItem(QUrl::fromLocalFile(text))));
    QVERIFY(!WhaleScriptLauncher::isScript(KFileItem(QUrl::fromLocalFile(m_dir->path() + QStringLiteral("/folder")))));
    // A program, not a script.
    QVERIFY(!WhaleScriptLauncher::isScript(KFileItem(QUrl::fromLocalFile(QStringLiteral("/bin/sh")))));
}

void WhaleScriptLauncherTest::testTerminalCommand()
{
    using WhaleScriptLauncher::Mode;
    QVERIFY(WhaleScriptLauncher::terminalCommand(QStringLiteral("konsole"), QStringLiteral("/a/b.sh"), Mode::Off).isEmpty());

    // Without Konsole's --noclose, which KTerminalLauncherJob would add: the window closes when
    // the shell around the script ends.
    const QStringList command = WhaleScriptLauncher::terminalCommand(QStringLiteral("konsole"), QStringLiteral("/a/my script.sh"), Mode::Terminal);
    QCOMPARE(command.mid(0, 4), (QStringList{QStringLiteral("konsole"), QStringLiteral("-e"), QStringLiteral("/bin/sh"), QStringLiteral("-c")}));
    QVERIFY(!command.contains(QStringLiteral("--noclose")));
    QCOMPARE(command.at(5), QStringLiteral("whale-script"));
    // The path as one argument, never through a shell's word splitting.
    QCOMPARE(command.at(6), QStringLiteral("/a/my script.sh"));

    // The options of the terminal are kept.
    const QStringList xterm =
        WhaleScriptLauncher::terminalCommand(QStringLiteral("xterm -fa 'DejaVu Sans Mono'"), QStringLiteral("/a.sh"), Mode::TerminalClose);
    QCOMPARE(xterm.mid(0, 4), (QStringList{QStringLiteral("xterm"), QStringLiteral("-fa"), QStringLiteral("DejaVu Sans Mono"), QStringLiteral("-e")}));
    QVERIFY(xterm.at(6) != command.at(4)); // another shell script around it

    // The exit code for Whale, only where asked for.
    QVERIFY(!command.at(4).contains(QStringLiteral("$4")));
    const QStringList reporting =
        WhaleScriptLauncher::terminalCommand(QStringLiteral("konsole"), QStringLiteral("/a.sh"), Mode::MinimizedClose, QStringLiteral("/run/x.status"));
    QCOMPARE(reporting.last(), QStringLiteral("/run/x.status"));
    QVERIFY(reporting.at(4).contains(QStringLiteral("$4")));
}

void WhaleScriptLauncherTest::testDefaultTerminal()
{
    setTerminal(QStringLiteral("xterm -fa Mono"));
    QCOMPARE(WhaleScriptLauncher::defaultTerminal(), QStringLiteral("xterm -fa Mono"));
    // The service wins when it exists, without its field codes.
    setTerminal(QStringLiteral("xterm"), QStringLiteral("org.kde.konsole.desktop"));
    if (KService::serviceByStorageId(QStringLiteral("org.kde.konsole.desktop"))) {
        QVERIFY(WhaleScriptLauncher::defaultTerminal().startsWith(QStringLiteral("konsole")));
        QVERIFY(!WhaleScriptLauncher::defaultTerminal().contains(QLatin1Char('%')));
    }
    setTerminal(QStringLiteral("xterm"), QStringLiteral("no-such-terminal.desktop"));
    QCOMPARE(WhaleScriptLauncher::defaultTerminal(), QStringLiteral("xterm"));
}

void WhaleScriptLauncherTest::testRunInTerminal_data()
{
    QTest::addColumn<WhaleScriptLauncher::Mode>("mode");
    QTest::addColumn<int>("exitCode");
    QTest::addColumn<bool>("waits");

    QTest::newRow("open, success") << WhaleScriptLauncher::Mode::Terminal << 0 << true;
    QTest::newRow("open, failure") << WhaleScriptLauncher::Mode::Terminal << 3 << true;
    QTest::newRow("close, success") << WhaleScriptLauncher::Mode::TerminalClose << 0 << false;
    QTest::newRow("close, failure") << WhaleScriptLauncher::Mode::TerminalClose << 3 << true;
    QTest::newRow("minimized, success") << WhaleScriptLauncher::Mode::Minimized << 0 << true;
    QTest::newRow("minimized, failure") << WhaleScriptLauncher::Mode::Minimized << 3 << true;
    QTest::newRow("minimized and close, success") << WhaleScriptLauncher::Mode::MinimizedClose << 0 << false;
    QTest::newRow("minimized and close, failure") << WhaleScriptLauncher::Mode::MinimizedClose << 3 << true;
}

void WhaleScriptLauncherTest::testRunInTerminal()
{
    QFETCH(WhaleScriptLauncher::Mode, mode);
    QFETCH(int, exitCode);
    QFETCH(bool, waits);

    m_dir->createDir(QStringLiteral("sub dir"));
    const QString script =
        createScript(QStringLiteral("sub dir/my script.sh"), QByteArrayLiteral("#!/bin/sh\necho \"ran in $PWD\"\nexit ") + QByteArray::number(exitCode) + '\n');
    WhaleScriptLauncher::setMode(mode);
    QVERIFY(WhaleScriptLauncher::launch(KFileItem(QUrl::fromLocalFile(script)), nullptr));

    const QString log = waitForTerminal();
    QVERIFY2(!log.isEmpty(), "the terminal did not run");
    const QString folder = m_dir->path() + QStringLiteral("/sub dir");
    QVERIFY2(log.contains(QStringLiteral("pwd=") + folder + QLatin1Char('\n')), qPrintable(log));
    QVERIFY2(log.contains(QStringLiteral("first=-e\n")), qPrintable(log));
    QVERIFY2(log.contains(QStringLiteral("ran in ") + folder), qPrintable(log));
    QVERIFY2(log.contains(QStringLiteral("status=%1").arg(exitCode)), qPrintable(log));
    QCOMPARE(log.contains(QStringLiteral("The script ended with exit code %1.").arg(exitCode)), waits);
    QCOMPARE(log.contains(QStringLiteral("Press Enter to close this window.")), waits);

    // Minimized: the terminal's window to the taskbar, and to the front if the script failed.
    using WhaleScriptLauncher::WindowRequest;
    const bool minimized = mode == WhaleScriptLauncher::Mode::Minimized || mode == WhaleScriptLauncher::Mode::MinimizedClose;
    const qint64 pid = QStringView(log).mid(4, log.indexOf(QLatin1Char('\n')) - 4).toLongLong();
    QVERIFY(pid > 0);
    QList<std::pair<WindowRequest, qint64>> expected;
    if (minimized) {
        expected.append({WindowRequest::Minimize, pid});
        if (exitCode != 0) {
            expected.append({WindowRequest::Raise, pid});
        }
    }
    QTRY_COMPARE(m_windowRequests, expected);
    QTest::qWait(100); // nothing more comes
    QCOMPARE(m_windowRequests, expected);
    // The file for the exit code is gone.
    const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/whale-scripts");
    QTRY_VERIFY(QDir(runtime).entryList({QStringLiteral("*.status*")}, QDir::Files).isEmpty());
}

void WhaleScriptLauncherTest::testOffLeavesItToKio()
{
    const QString script = createScript(QStringLiteral("run.sh"), QByteArrayLiteral("#!/bin/sh\nexit 0\n"));
    const QString text = createScript(QStringLiteral("notes.txt"), QByteArrayLiteral("just text\n"));
    QVERIFY(!WhaleScriptLauncher::launch(KFileItem(QUrl::fromLocalFile(script)), nullptr));

    WhaleScriptLauncher::setMode(WhaleScriptLauncher::Mode::Terminal);
    QVERIFY(!WhaleScriptLauncher::launch(KFileItem(QUrl::fromLocalFile(text)), nullptr));
    QTest::qWait(200);
    QVERIFY(!QFile::exists(m_log));
}

void WhaleScriptLauncherTest::testSettingsPage()
{
    KConfigGroup kio(KSharedConfig::openConfig(QStringLiteral("kiorc")), QStringLiteral("Executable scripts"));
    kio.writeEntry("behaviourOnLaunch", "execute");
    kio.sync();

    // The terminal choices are written to orcinusrc only; KIO's choice stays as it was.
    for (const auto &[index, mode] : {std::pair{3, WhaleScriptLauncher::Mode::Terminal},
                                      std::pair{4, WhaleScriptLauncher::Mode::TerminalClose},
                                      std::pair{5, WhaleScriptLauncher::Mode::Minimized},
                                      std::pair{6, WhaleScriptLauncher::Mode::MinimizedClose}}) {
        ConfirmationsSettingsPage page(nullptr);
        auto combo = page.findChild<QComboBox *>();
        QVERIFY(combo);
        QCOMPARE(combo->count(), 7);
        combo->setCurrentIndex(index);
        page.applySettings();
        QCOMPARE(WhaleScriptLauncher::mode(), mode);
        kio.config()->reparseConfiguration();
        QCOMPARE(kio.readEntry("behaviourOnLaunch"), QStringLiteral("execute"));

        ConfirmationsSettingsPage reloaded(nullptr);
        QCOMPARE(reloaded.findChild<QComboBox *>()->currentIndex(), index);
    }

    // A choice of KIO turns the terminal off.
    ConfirmationsSettingsPage page(nullptr);
    page.findChild<QComboBox *>()->setCurrentIndex(0);
    page.applySettings();
    QCOMPARE(WhaleScriptLauncher::mode(), WhaleScriptLauncher::Mode::Off);
    kio.config()->reparseConfiguration();
    QCOMPARE(kio.readEntry("behaviourOnLaunch"), QStringLiteral("alwaysAsk"));

    // "Defaults" as well.
    WhaleScriptLauncher::setMode(WhaleScriptLauncher::Mode::Terminal);
    ConfirmationsSettingsPage defaults(nullptr);
    defaults.restoreDefaults();
    defaults.applySettings();
    QCOMPARE(WhaleScriptLauncher::mode(), WhaleScriptLauncher::Mode::Off);
}

QTEST_MAIN(WhaleScriptLauncherTest)

#include "whalescriptlaunchertest.moc"
