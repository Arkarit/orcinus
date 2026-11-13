/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaisandbox.h"

#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

/**
 * The sandbox of AI5d: what an agent gets in it, the command line of the helper, and the helper
 * itself where it may run (on Ubuntu only with its AppArmor profile loaded).
 */
class WhaleAiSandboxTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void agentPaths();
    void packageOfTheAgent();
    void wrap();
    void reaches();
    void missingHelper();
    void helper();
    void addFolders();

private:
    static WhaleAiAgent agent(const QString &commandLine)
    {
        WhaleAiAgent agent;
        agent.id = QStringLiteral("a");
        agent.commandLine = commandLine;
        return agent;
    }
};

void WhaleAiSandboxTest::agentPaths()
{
    const QString home = QStringLiteral("/h");
    WhaleAiAgent codex = agent(QStringLiteral("codex-acp"));
    codex.sandboxPaths = {QStringLiteral("~/extra"), QStringLiteral("/data/models"), QStringLiteral("relative")};
    WhaleAiSandbox::Paths paths = WhaleAiSandbox::agentPaths(codex, QString(), home);
    QCOMPARE(paths.readWrite,
             (QStringList{QStringLiteral("/h/.npm"), QStringLiteral("/h/.codex"), QStringLiteral("/h/extra"), QStringLiteral("/data/models")}));
    QCOMPARE(paths.create, (QStringList{QStringLiteral("/h/.npm"), QStringLiteral("/h/.codex")}));
    QVERIFY(paths.readOnly.contains(QStringLiteral("/h/.local/bin")));
    QVERIFY(paths.readOnly.contains(QStringLiteral("/h/.gitconfig")));

    paths = WhaleAiSandbox::agentPaths(agent(QStringLiteral("npx -y @agentclientprotocol/claude-agent-acp")), QString(), home);
    QVERIFY(paths.readWrite.contains(QStringLiteral("/h/.claude")));
    QVERIFY(paths.readWrite.contains(QStringLiteral("/h/.claude.json")));
    QVERIFY(!paths.create.contains(QStringLiteral("/h/.claude.json")));

    for (const char *variable : {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME", "XDG_CACHE_HOME"}) {
        qunsetenv(variable);
    }
    paths = WhaleAiSandbox::agentPaths(agent(QStringLiteral("opencode acp")), QString(), home);
    QVERIFY(paths.readWrite.contains(QStringLiteral("/h/.local/share/opencode")));
    qputenv("XDG_DATA_HOME", "/elsewhere/data");
    paths = WhaleAiSandbox::agentPaths(agent(QStringLiteral("opencode acp")), QString(), home);
    QVERIFY(paths.readWrite.contains(QStringLiteral("/elsewhere/data/opencode")));
    QVERIFY(paths.readWrite.contains(QStringLiteral("/h/.config/opencode")));
    qunsetenv("XDG_DATA_HOME");

    paths = WhaleAiSandbox::agentPaths(agent(QStringLiteral("my-agent")), QString(), home);
    QCOMPARE(paths.readWrite, QStringList{QStringLiteral("/h/.npm")});
}

void WhaleAiSandboxTest::packageOfTheAgent()
{
    // An npm package in a prefix of its own (nvm, ~/.local): the package, read-only; not the
    // prefix (~/.local has the data of all programs).
    QTemporaryDir prefix;
    QVERIFY(prefix.isValid());
    QVERIFY(QDir().mkpath(prefix.filePath(QStringLiteral("lib/node_modules/pkg/dist"))));
    QVERIFY(QDir().mkpath(prefix.filePath(QStringLiteral("bin"))));
    QFile script(prefix.filePath(QStringLiteral("lib/node_modules/pkg/dist/index.js")));
    QVERIFY(script.open(QIODevice::WriteOnly));
    script.close();
    const QString link = prefix.filePath(QStringLiteral("bin/pkg-acp"));
    QVERIFY(QFile::link(QStringLiteral("../lib/node_modules/pkg/dist/index.js"), link));
    const QString canonicalPrefix = QDir(prefix.path()).canonicalPath();
    QVERIFY(WhaleAiSandbox::agentPaths(agent(QStringLiteral("pkg-acp")), link, QStringLiteral("/h"))
                .readOnly.contains(canonicalPrefix + QStringLiteral("/lib/node_modules/pkg")));
    QVERIFY(!WhaleAiSandbox::agentPaths(agent(QStringLiteral("pkg-acp")), link, QStringLiteral("/h")).readOnly.contains(canonicalPrefix));

    // A program elsewhere: its folder. The system is there anyway.
    QVERIFY(QDir().mkpath(prefix.filePath(QStringLiteral("tools"))));
    QFile tool(prefix.filePath(QStringLiteral("tools/agent")));
    QVERIFY(tool.open(QIODevice::WriteOnly));
    tool.close();
    QVERIFY(WhaleAiSandbox::agentPaths(agent(QStringLiteral("x")), tool.fileName(), QStringLiteral("/h"))
                .readOnly.contains(canonicalPrefix + QStringLiteral("/tools")));
    const WhaleAiSandbox::Paths system = WhaleAiSandbox::agentPaths(agent(QStringLiteral("sh")), QStringLiteral("/bin/sh"), QStringLiteral("/h"));
    for (const QString &path : system.readOnly) {
        QVERIFY2(path.startsWith(QLatin1String("/h/")), qPrintable(path));
    }
}

void WhaleAiSandboxTest::wrap()
{
    WhaleAcpSession::AgentCommand command;
    command.program = QStringLiteral("/usr/bin/agent");
    command.arguments = {QStringLiteral("acp"), QStringLiteral("--fast")};
    WhaleAiSandbox::Paths paths;
    paths.readWrite = {QStringLiteral("/h/.agent")};
    paths.readOnly = {QStringLiteral("/h/.local/bin")};
    const WhaleAcpSession::AgentCommand wrapped =
        WhaleAiSandbox::wrap(command, QStringLiteral("/x/orcinus-sandbox"), {QStringLiteral("/h/work")}, paths, QStringLiteral("/h/work"));
    QCOMPARE(wrapped.program, QStringLiteral("/x/orcinus-sandbox"));
    QCOMPARE(wrapped.arguments,
             (QStringList{QStringLiteral("--bind"),
                          QStringLiteral("/h/work"),
                          QStringLiteral("--bind"),
                          QStringLiteral("/h/.agent"),
                          QStringLiteral("--ro-bind"),
                          QStringLiteral("/h/.local/bin"),
                          QStringLiteral("--chdir"),
                          QStringLiteral("/h/work"),
                          QStringLiteral("--"),
                          QStringLiteral("/usr/bin/agent"),
                          QStringLiteral("acp"),
                          QStringLiteral("--fast")}));
}

void WhaleAiSandboxTest::reaches()
{
    const QStringList shared{QStringLiteral("/h/work"), QStringLiteral("/h/.codex")};
    QVERIFY(WhaleAiSandbox::reaches(QStringLiteral("/h/work"), shared));
    QVERIFY(WhaleAiSandbox::reaches(QStringLiteral("/h/work/a/b.txt"), shared));
    QVERIFY(WhaleAiSandbox::reaches(QStringLiteral("/h/work/../work/b.txt"), shared));
    QVERIFY(WhaleAiSandbox::reaches(QStringLiteral("/usr/include/stdio.h"), shared));
    QVERIFY(WhaleAiSandbox::reaches(QStringLiteral("/tmp/scratch"), shared));
    // The host's /tmp is not the agent's.
    QVERIFY(!WhaleAiSandbox::reaches(QStringLiteral("/tmp/scratch"), shared, false));
    QVERIFY(WhaleAiSandbox::reaches(QStringLiteral("/tmp/scratch"), {QStringLiteral("/tmp/scratch")}, false));
    QVERIFY(WhaleAiSandbox::reaches(QStringLiteral("/usr/share"), shared, false));
    QVERIFY(!WhaleAiSandbox::reaches(QStringLiteral("/h/workshop"), shared));
    QVERIFY(!WhaleAiSandbox::reaches(QStringLiteral("/h/work/../secret"), shared));
    QVERIFY(!WhaleAiSandbox::reaches(QStringLiteral("/h"), shared));
    QVERIFY(!WhaleAiSandbox::reaches(QStringLiteral("/media/disk/file"), shared));
}

void WhaleAiSandboxTest::missingHelper()
{
    QVERIFY(!WhaleAiSandbox::check(QString()).isEmpty());
    QVERIFY(WhaleAiSandbox::check(QStringLiteral("/no/such/orcinus-sandbox")).contains(QLatin1String("The sandbox cannot start")));
}

void WhaleAiSandboxTest::helper()
{
    const QString helper = WhaleAiSandbox::helper();
    QVERIFY(!helper.isEmpty());
    const QString problem = WhaleAiSandbox::check(helper);
    if (!problem.isEmpty()) {
        QSKIP(qPrintable(QStringLiteral("The sandbox does not run here: ") + problem));
    }

    QTemporaryDir inside;
    QTemporaryDir outside;
    QVERIFY(inside.isValid() && outside.isValid());
    QFile secret(outside.filePath(QStringLiteral("secret.txt")));
    QVERIFY(secret.open(QIODevice::WriteOnly));
    secret.write("secret");
    secret.close();

    QProcess process;
    const QString script = QStringLiteral(
        "pwd; cat '%1' 2>/dev/null || echo no-secret; echo hi > new.txt && echo written; "
        "touch /usr/orcinus-sandbox-test 2>/dev/null || echo system-read-only; "
        "test -e /run/user || echo no-session-bus; exit 7");
    process.start(helper,
                  {QStringLiteral("--bind"),
                   inside.path(),
                   QStringLiteral("--chdir"),
                   inside.path(),
                   QStringLiteral("--"),
                   QStringLiteral("sh"),
                   QStringLiteral("-c"),
                   script.arg(secret.fileName())});
    QVERIFY(process.waitForFinished(10000));
    QCOMPARE(process.exitCode(), 7);
    const QStringList lines = QString::fromLocal8Bit(process.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QCOMPARE(lines,
             (QStringList{inside.path(),
                          QStringLiteral("no-secret"),
                          QStringLiteral("written"),
                          QStringLiteral("system-read-only"),
                          QStringLiteral("no-session-bus")}));
    QVERIFY(QFile::exists(inside.filePath(QStringLiteral("new.txt"))));

    // A read-write folder inside a read-only one, in any order (the agent's settings below an
    // installation in ~/.local).
    QTemporaryDir outer;
    QVERIFY(outer.isValid() && QDir().mkpath(outer.filePath(QStringLiteral("rw"))));
    process.start(helper,
                  {QStringLiteral("--bind"),
                   outer.filePath(QStringLiteral("rw")),
                   QStringLiteral("--ro-bind"),
                   outer.path(),
                   QStringLiteral("--"),
                   QStringLiteral("sh"),
                   QStringLiteral("-c"),
                   QStringLiteral("touch '%1/x' 2>/dev/null || echo outer-read-only; touch '%1/rw/x' && echo inner-written").arg(outer.path())});
    QVERIFY(process.waitForFinished(10000));
    QCOMPARE(QString::fromLocal8Bit(process.readAllStandardOutput()), QStringLiteral("outer-read-only\ninner-written\n"));

    // Errors of the sandbox itself: status 125 and a line that says so.
    process.start(helper, {QStringLiteral("--bind"), QStringLiteral("relative")});
    QVERIFY(process.waitForFinished(10000));
    QCOMPARE(process.exitCode(), 125);
    QVERIFY(process.readAllStandardError().startsWith("orcinus-sandbox: --bind needs an absolute path"));
}

void WhaleAiSandboxTest::addFolders()
{
    const QString helper = WhaleAiSandbox::helper();
    const QString problem = WhaleAiSandbox::check(helper);
    if (!problem.isEmpty()) {
        QSKIP(qPrintable(QStringLiteral("The sandbox does not run here: ") + problem));
    }
    QTemporaryDir inside;
    QTemporaryDir later;
    QVERIFY(inside.isValid() && later.isValid());
    QFile file(later.filePath(QStringLiteral("f.txt")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("later");
    file.close();

    // The agent waits until the folder is there, writes into it, and waits until it is gone.
    WhaleAiSandboxControl control;
    QVERIFY(control.isValid());
    WhaleAcpSession::AgentCommand command;
    command.program = QStringLiteral("sh");
    command.arguments = {QStringLiteral("-c"),
                         QStringLiteral("test -e '%1' && echo early; while ! test -e '%1'; do sleep 0.05; done; cat '%1'; echo; echo hi > '%2/new.txt' "
                                        "&& echo written; while test -e '%1'; do sleep 0.05; done; echo gone")
                             .arg(file.fileName(), later.path())};
    const WhaleAcpSession::AgentCommand wrapped = WhaleAiSandbox::wrap(command, helper, {inside.path()}, {}, inside.path(), control.arguments());
    QProcess process;
    process.setChildProcessModifier(control.childSetup());
    process.start(wrapped.program, wrapped.arguments);
    QVERIFY(process.waitForStarted());
    control.childStarted();

    QSignalSpy finished(&control, &WhaleAiSandboxControl::finished);
    QTest::qWait(200);
    control.add(later.path());
    QVERIFY(finished.wait(5000));
    QCOMPARE(finished.last(), (QVariantList{true, later.path(), QString()}));
    QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(later.filePath(QStringLiteral("new.txt"))), 5000);
    control.remove(later.path());
    QVERIFY(finished.wait(5000));
    QCOMPARE(finished.last(), (QVariantList{false, later.path(), QString()}));
    QVERIFY(process.waitForFinished(5000));
    QCOMPARE(QString::fromLocal8Bit(process.readAllStandardOutput()), QStringLiteral("later\nwritten\ngone\n"));

    // Errors come back, also when the sandbox has ended.
    control.add(QStringLiteral("/no/such/folder"));
    QVERIFY(finished.wait(5000));
    QVERIFY(!finished.last().at(2).toString().isEmpty());
}

QTEST_GUILESS_MAIN(WhaleAiSandboxTest)

#include "whaleaisandboxtest.moc"
