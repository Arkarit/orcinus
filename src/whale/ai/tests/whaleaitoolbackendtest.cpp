/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaitoolbackend.h"
#include "whaleaitoolstore.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <signal.h>

namespace
{
/** A fake backend: answers each message, and does what some of them ask for. */
const char *const FakeBackend = R"(import json, os, signal, sys, time
print("hello, not json", flush=True)
print("starting", file=sys.stderr, flush=True)
for line in sys.stdin:
    message = json.loads(line)
    if isinstance(message, dict) and "exit" in message:
        print("bye", file=sys.stderr, flush=True)
        sys.exit(message["exit"])
    if isinstance(message, dict) and "env" in message:
        print(json.dumps({"cwd": os.getcwd(), "id": os.environ.get("ORCINUS_TOOL_ID"),
                          "folder": os.environ.get("ORCINUS_TOOL_FOLDER"),
                          "scratch": os.environ.get("ORCINUS_TOOL_SCRATCH")}), flush=True)
        continue
    if isinstance(message, dict) and "big" in message:
        sys.stdout.write("x" * message["big"] + "\n")
        print(json.dumps("after big"), flush=True)
        continue
    if isinstance(message, dict) and "stubborn" in message:
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
        print(json.dumps({"pid": os.getpid()}), flush=True)
        while True:
            time.sleep(1)
    print(json.dumps({"echo": message}), flush=True)
)";

bool processAlive(qint64 pid)
{
    return ::kill(pid, 0) == 0;
}
}

class WhaleAiToolBackendTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void testParseCommand();
    void testManifest();
    void testRoundTrip();
    void testQueueWhileWaiting();
    void testEnvironment();
    void testExitAndRestart();
    void testMissingProgram();
    void testLongLine();
    void testStopKillsStubborn();
    void testDestructorKillsStubborn();

private:
    WhaleAiTool tool() const;
    /** The next message of @p spy's backend, waiting for it. */
    static QVariant next(QSignalSpy &spy);

    QTemporaryDir m_dir;
    QString m_folder;
};

void WhaleAiToolBackendTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QVERIFY(m_dir.isValid());
    if (QStandardPaths::findExecutable(QStringLiteral("python3")).isEmpty()) {
        QSKIP("python3 is needed for the fake backend");
    }
    m_folder = m_dir.filePath(QStringLiteral("fake"));
    QVERIFY(QDir().mkpath(m_folder));
    QFile script(m_folder + QStringLiteral("/backend.py"));
    QVERIFY(script.open(QIODevice::WriteOnly));
    script.write(FakeBackend);
}

WhaleAiTool WhaleAiToolBackendTest::tool() const
{
    WhaleAiTool tool;
    tool.id = QStringLiteral("fake");
    tool.folder = m_folder;
    tool.name = QStringLiteral("Fake");
    tool.backend = QJsonObject{{QStringLiteral("command"), QJsonArray{QStringLiteral("python3"), QStringLiteral("backend.py")}}};
    return tool;
}

QVariant WhaleAiToolBackendTest::next(QSignalSpy &spy)
{
    if (spy.isEmpty() && !spy.wait(10000)) {
        return QVariant();
    }
    return spy.takeFirst().first();
}

void WhaleAiToolBackendTest::testParseCommand()
{
    QString error;
    const auto command =
        WhaleAiToolBackend::parseCommand(QJsonObject{{QStringLiteral("command"), QJsonArray{QStringLiteral("python3"), QStringLiteral("b.py")}}}, &error);
    QVERIFY(command);
    QCOMPARE(*command, QStringList({QStringLiteral("python3"), QStringLiteral("b.py")}));
    QVERIFY(!WhaleAiToolBackend::parseCommand(QStringLiteral("python3 b.py"), &error));
    QVERIFY(error.contains(QLatin1String("command")));
    QVERIFY(!WhaleAiToolBackend::parseCommand(QJsonObject{{QStringLiteral("command"), QJsonArray()}}, &error));
    QVERIFY(!WhaleAiToolBackend::parseCommand(QJsonObject{{QStringLiteral("command"), QJsonArray{1}}}, &error));
    QVERIFY(!WhaleAiToolBackend::parseCommand(QJsonObject{{QStringLiteral("command"), QJsonArray{QStringLiteral(" ")}}}, &error));
    QVERIFY(!WhaleAiToolBackend::parseCommand(QJsonObject{{QStringLiteral("command"), QStringLiteral("python3")}}, &error));

    WhaleAiTool none = tool();
    none.backend = QJsonObject();
    QVERIFY(!WhaleAiToolBackend(none).hasBackend());
    QVERIFY(WhaleAiToolBackend(tool()).hasBackend());
}

void WhaleAiToolBackendTest::testManifest()
{
    // The store refuses a tool whose backend is not a command.
    const QString root = m_dir.filePath(QStringLiteral("tools"));
    auto write = [&root](const QString &id, const QByteArray &manifest) {
        QDir().mkpath(root + QLatin1Char('/') + id);
        QFile qml(root + QLatin1Char('/') + id + QStringLiteral("/main.qml"));
        QVERIFY(qml.open(QIODevice::WriteOnly));
        qml.write("import QtQuick\nItem {}\n");
        QFile json(root + QLatin1Char('/') + id + QStringLiteral("/tool.json"));
        QVERIFY(json.open(QIODevice::WriteOnly));
        json.write(manifest);
    };
    write(QStringLiteral("good"), R"({"name": "Good", "backend": {"command": ["python3", "backend.py"]}})");
    write(QStringLiteral("bad"), R"({"name": "Bad", "backend": "python3 backend.py"})");
    write(QStringLiteral("plain"), R"({"name": "Plain"})");
    WhaleAiToolStore store(root, KSharedConfig::openConfig(m_dir.filePath(QStringLiteral("rc")), KConfig::SimpleConfig));
    QString error;
    const auto good = store.tool(QStringLiteral("good"), &error);
    QVERIFY2(good, qPrintable(error));
    QVERIFY(WhaleAiToolBackend(*good).hasBackend());
    QVERIFY(!store.tool(QStringLiteral("bad"), &error));
    QVERIFY(error.contains(QLatin1String("\"backend\"")));
    const auto plain = store.tool(QStringLiteral("plain"), &error);
    QVERIFY(plain);
    QVERIFY(!WhaleAiToolBackend(*plain).hasBackend());
}

void WhaleAiToolBackendTest::testRoundTrip()
{
    WhaleAiToolBackend backend(tool());
    QSignalSpy messages(&backend, &WhaleAiToolBackend::message);
    QSignalSpy log(&backend, &WhaleAiToolBackend::logLine);
    QCOMPARE(backend.stateName(), QStringLiteral("none"));
    QVERIFY(!backend.send(1));
    backend.start();
    QCOMPARE(backend.state(), WhaleAiToolBackend::State::Running);
    // Any JSON value goes both ways.
    QVERIFY(backend.send(QVariantMap{{QStringLiteral("text"), QStringLiteral("äö \"x\"\n")}}));
    QCOMPARE(next(messages).toMap().value(QStringLiteral("echo")).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("äö \"x\"\n"));
    QVERIFY(backend.send(42));
    QCOMPARE(next(messages).toMap().value(QStringLiteral("echo")).toInt(), 42);
    QVERIFY(backend.send(QVariantList{QStringLiteral("a"), true}));
    QCOMPARE(next(messages).toMap().value(QStringLiteral("echo")).toList(), (QVariantList{QStringLiteral("a"), true}));
    // Not JSON on stdout and everything on stderr go into the log.
    QTRY_VERIFY(log.size() >= 2);
    QStringList lines;
    for (const QList<QVariant> &arguments : std::as_const(log)) {
        lines.append(arguments.first().toString());
    }
    QVERIFY2(lines.contains(QStringLiteral("backend (not JSON): hello, not json")), qPrintable(lines.join(QLatin1Char('\n'))));
    QVERIFY(lines.contains(QStringLiteral("backend stderr: starting")));
    QCOMPARE(backend.stderrTail(), QStringList{QStringLiteral("starting")});
}

void WhaleAiToolBackendTest::testQueueWhileWaiting()
{
    WhaleAiToolBackend backend(tool());
    QSignalSpy messages(&backend, &WhaleAiToolBackend::message);
    backend.setWaiting();
    QCOMPARE(backend.stateName(), QStringLiteral("waiting"));
    QVERIFY(backend.send(QStringLiteral("first")));
    QVERIFY(backend.send(QStringLiteral("second")));
    QVERIFY(!messages.wait(300));
    backend.start();
    QCOMPARE(next(messages).toMap().value(QStringLiteral("echo")).toString(), QStringLiteral("first"));
    QCOMPARE(next(messages).toMap().value(QStringLiteral("echo")).toString(), QStringLiteral("second"));
}

void WhaleAiToolBackendTest::testEnvironment()
{
    WhaleAiToolBackend backend(tool());
    QSignalSpy messages(&backend, &WhaleAiToolBackend::message);
    backend.start();
    backend.send(QVariantMap{{QStringLiteral("env"), 1}});
    const QVariantMap env = next(messages).toMap();
    QCOMPARE(env.value(QStringLiteral("cwd")).toString(), QDir(m_folder).canonicalPath());
    QCOMPARE(env.value(QStringLiteral("id")).toString(), QStringLiteral("fake"));
    QCOMPARE(env.value(QStringLiteral("folder")).toString(), QDir::cleanPath(m_folder));
    const QString scratch = env.value(QStringLiteral("scratch")).toString();
    QCOMPARE(scratch, WhaleAiToolBackend::scratchFolder(QStringLiteral("fake")));
    QVERIFY(scratch.endsWith(QLatin1String("/orcinus/ai/scratch/tools/fake")));
    QVERIFY(QFileInfo(scratch).isDir());
}

void WhaleAiToolBackendTest::testExitAndRestart()
{
    WhaleAiToolBackend backend(tool());
    QSignalSpy messages(&backend, &WhaleAiToolBackend::message);
    QSignalSpy log(&backend, &WhaleAiToolBackend::logLine);
    backend.start();
    backend.send(QVariantMap{{QStringLiteral("exit"), 3}});
    QTRY_COMPARE(backend.state(), WhaleAiToolBackend::State::Exited);
    QCOMPARE(backend.exitCode(), 3);
    QCOMPARE(backend.stderrTail(), QStringList({QStringLiteral("starting"), QStringLiteral("bye")}));
    QTRY_VERIFY(!log.isEmpty() && log.last().first().toString() == QStringLiteral("The backend ended (exit code 3)."));
    // Never started again by itself; sending after the end fails.
    QVERIFY(!backend.send(1));
    QTest::qWait(200);
    QCOMPARE(backend.state(), WhaleAiToolBackend::State::Exited);

    backend.restart();
    QCOMPARE(backend.state(), WhaleAiToolBackend::State::Running);
    QCOMPARE(backend.stderrTail(), QStringList());
    QVERIFY(backend.send(7));
    QCOMPARE(next(messages).toMap().value(QStringLiteral("echo")).toInt(), 7);
    backend.stop();
    QCOMPARE(backend.state(), WhaleAiToolBackend::State::None);
}

void WhaleAiToolBackendTest::testMissingProgram()
{
    WhaleAiTool missing = tool();
    missing.backend = QJsonObject{{QStringLiteral("command"), QJsonArray{QStringLiteral("orcinus-no-such-program")}}};
    WhaleAiToolBackend backend(missing);
    QSignalSpy log(&backend, &WhaleAiToolBackend::logLine);
    backend.start();
    QCOMPARE(backend.state(), WhaleAiToolBackend::State::Exited);
    QCOMPARE(backend.exitCode(), -1);
    QVERIFY(log.first().first().toString().contains(QLatin1String("not installed")));

    // A program with a slash must be an executable inside the tool's folder.
    missing.backend = QJsonObject{{QStringLiteral("command"), QJsonArray{QStringLiteral("../../bin/sh")}}};
    WhaleAiToolBackend outside(missing);
    QSignalSpy outsideLog(&outside, &WhaleAiToolBackend::logLine);
    outside.start();
    QCOMPARE(outside.state(), WhaleAiToolBackend::State::Exited);
    QVERIFY(outsideLog.first().first().toString().contains(QLatin1String("tool's folder")));
}

void WhaleAiToolBackendTest::testLongLine()
{
    WhaleAiToolBackend backend(tool());
    QSignalSpy messages(&backend, &WhaleAiToolBackend::message);
    QSignalSpy log(&backend, &WhaleAiToolBackend::logLine);
    backend.start();
    backend.send(QVariantMap{{QStringLiteral("big"), int(WhaleAiToolBackend::MaxLine + 1000)}});
    // The long line is dropped, the next one arrives.
    QCOMPARE(next(messages).toString(), QStringLiteral("after big"));
    QVERIFY(messages.isEmpty());
    bool dropped = false;
    for (const QList<QVariant> &arguments : std::as_const(log)) {
        dropped = dropped || arguments.first().toString().contains(QLatin1String("dropped"));
    }
    QVERIFY(dropped);
}

void WhaleAiToolBackendTest::testStopKillsStubborn()
{
    // A backend that ignores the end of stdin and SIGTERM is killed.
    WhaleAiToolBackend backend(tool());
    QSignalSpy messages(&backend, &WhaleAiToolBackend::message);
    backend.start();
    backend.send(QVariantMap{{QStringLiteral("stubborn"), 1}});
    const qint64 pid = next(messages).toMap().value(QStringLiteral("pid")).toLongLong();
    QVERIFY(pid > 0);
    QElapsedTimer timer;
    timer.start();
    backend.stop();
    QCOMPARE(backend.state(), WhaleAiToolBackend::State::None);
    QVERIFY(timer.elapsed() < 500); // stop() does not wait
    QTRY_VERIFY_WITH_TIMEOUT(!processAlive(pid), 6000);
    QVERIFY(timer.elapsed() >= WhaleAiToolBackend::StopGrace);
}

void WhaleAiToolBackendTest::testDestructorKillsStubborn()
{
    qint64 pid = 0;
    {
        WhaleAiToolBackend backend(tool());
        QSignalSpy messages(&backend, &WhaleAiToolBackend::message);
        backend.start();
        backend.send(QVariantMap{{QStringLiteral("stubborn"), 1}});
        pid = next(messages).toMap().value(QStringLiteral("pid")).toLongLong();
        QVERIFY(pid > 0);
    }
    // The destructor waited for it.
    QVERIFY(!processAlive(pid));
}

QTEST_GUILESS_MAIN(WhaleAiToolBackendTest)

#include "whaleaitoolbackendtest.moc"
