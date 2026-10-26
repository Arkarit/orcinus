/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaletreescanner.h"
#include "dolphinviewbridge.h"
#include "views/dolphinview.h"
#include "whaletreenode.h"
#include "whaletreescannerimpl.h"
#include "whaleviewcontextimpl.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QPointer>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <sys/stat.h>
#include <unistd.h>

namespace
{
void writeFile(const QString &path, qint64 size)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArray(size, 'x'));
}

WhaleTreeNode child(const WhaleTreeNode &node, const QString &name)
{
    const QList<WhaleTreeNode> children = node.children();
    for (const WhaleTreeNode &entry : children) {
        if (entry.name() == name) {
            return entry;
        }
    }
    return {};
}

QStringList childNames(const WhaleTreeNode &node)
{
    QStringList names;
    const QList<WhaleTreeNode> children = node.children();
    for (const WhaleTreeNode &entry : children) {
        names.append(entry.name());
    }
    return names;
}

/// Everything a tree says, one line per node in tree order.
QStringList dump(const WhaleTreeNode &node)
{
    QStringList lines = {QStringLiteral("%1 %2 %3 %4 %5 %6/%7 %8")
                             .arg(node.depth())
                             .arg(node.path())
                             .arg(int(node.type()))
                             .arg(node.size())
                             .arg(node.totalSize())
                             .arg(node.fileCount())
                             .arg(node.directoryCount())
                             .arg(node.isComplete())};
    const QList<WhaleTreeNode> children = node.children();
    for (const WhaleTreeNode &entry : children) {
        lines += dump(entry);
    }
    return lines;
}

int nodeCount(const WhaleTreeNode &node)
{
    int count = 1;
    const QList<WhaleTreeNode> children = node.children();
    for (const WhaleTreeNode &entry : children) {
        count += nodeCount(entry);
    }
    return count;
}

/// The memory the process uses now, from /proc/self/statm.
long residentKilobytes()
{
    QFile statm(QStringLiteral("/proc/self/statm"));
    if (!statm.open(QIODevice::ReadOnly)) {
        return 0;
    }
    const QList<QByteArray> fields = statm.readAll().split(' ');
    return fields.value(1).toLong() * (sysconf(_SC_PAGESIZE) / 1024);
}

/// Runs a visit hook while it exists.
class VisitHook
{
public:
    explicit VisitHook(const std::function<void(const QString &)> &hook)
    {
        WhaleTreeScannerImpl::setVisitHookForTesting(hook);
    }
    ~VisitHook()
    {
        WhaleTreeScannerImpl::setVisitHookForTesting({});
    }
};

/// The outcome of one scan, waited for.
struct Result {
    bool finished = false;
    bool canceled = false;
    bool failed = false;
    WhaleTreeScanner::Error error = WhaleTreeScanner::Error::InvalidUrl;
    QString message;
    WhaleTreeNode root;
};

Result scanAndWait(WhaleTreeScanner *scanner, const QUrl &url, const WhaleTreeScanner::Options &options = {}, int timeout = 30000)
{
    Result result;
    QEventLoop loop;
    QObject guard;
    QObject::connect(scanner, &WhaleTreeScanner::finished, &guard, [&](const WhaleTreeNode &root) {
        result.finished = true;
        result.root = root;
        loop.quit();
    });
    QObject::connect(scanner, &WhaleTreeScanner::canceled, &guard, [&]() {
        result.canceled = true;
        loop.quit();
    });
    QObject::connect(scanner, &WhaleTreeScanner::failed, &guard, [&](WhaleTreeScanner::Error error, const QString &message) {
        result.failed = true;
        result.error = error;
        result.message = message;
        loop.quit();
    });
    if (!scanner->scan(url, options)) {
        return result;
    }
    QTimer::singleShot(timeout, &loop, &QEventLoop::quit);
    loop.exec();
    return result;
}
}

/**
 * M8a: WhaleTreeScanner, the local tree scanner (fts in a worker thread), on trees in
 * temporary folders.
 */
class WhaleTreeScannerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanup();

    void testSimpleTree();
    void testMaxDepth();
    void testSymlinks();
    void testHidden();
    void testCancel();
    void testCancelWhileScanning();
    void testFileChangesDuringScan();
    void testFolderVanishesDuringScan();
    void testPermissionDenied();
    void testRejectedRoots_data();
    void testRejectedRoots();
    void testOneScanAtATime();
    void testThreads();
    void testRepeatable();
    void testLargeTree();
    void testStayOnFilesystem();
    void testContextScanner();

private:
    QString path(const QString &relative) const;

    QTemporaryDir m_big; // 300 folders, 6000 files, shared by several tests
    int m_bigFiles = 0;
    int m_bigFolders = 0;
    qint64 m_bigSize = 0;
};

void WhaleTreeScannerTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QVERIFY(m_big.isValid());
    // 30 folders with 10 subfolders each, 20 files per subfolder.
    for (int i = 0; i < 30; ++i) {
        for (int j = 0; j < 10; ++j) {
            const QString folder = m_big.filePath(QStringLiteral("d%1/e%2").arg(i).arg(j));
            for (int k = 0; k < 20; ++k) {
                writeFile(folder + QStringLiteral("/f%1").arg(k), k + 1);
                m_bigSize += k + 1;
                ++m_bigFiles;
            }
        }
    }
    m_bigFolders = 30 + 30 * 10;
}

void WhaleTreeScannerTest::cleanup()
{
    WhaleTreeScannerImpl::setVisitHookForTesting({});
    // No worker outlives a test.
    QTRY_COMPARE_WITH_TIMEOUT(WhaleTreeScannerImpl::runningWorkers(), 0, 5000);
}

void WhaleTreeScannerTest::testSimpleTree()
{
    QTemporaryDir dir;
    writeFile(dir.filePath(QStringLiteral("a.txt")), 100);
    writeFile(dir.filePath(QStringLiteral("b.txt")), 200);
    writeFile(dir.filePath(QStringLiteral("sub/c.txt")), 300);
    writeFile(dir.filePath(QStringLiteral("sub/deeper/d.txt")), 400);
    QVERIFY(QDir(dir.path()).mkdir(QStringLiteral("empty")));

    WhaleTreeScannerImpl scanner;
    QSignalSpy started(&scanner, &WhaleTreeScanner::started);
    const Result result = scanAndWait(&scanner, QUrl::fromLocalFile(dir.path()));
    QVERIFY(result.finished);
    QCOMPARE(started.count(), 1);
    QVERIFY(!scanner.isRunning());

    const WhaleTreeNode root = result.root;
    QCOMPARE(root.path(), QDir::cleanPath(dir.path()));
    QCOMPARE(root.url(), QUrl::fromLocalFile(QDir::cleanPath(dir.path())));
    QCOMPARE(root.name(), QFileInfo(dir.path()).fileName());
    QCOMPARE(root.type(), WhaleTreeNode::Type::Directory);
    QCOMPARE(root.depth(), 0);
    QCOMPARE(root.size(), 0);
    QCOMPARE(root.totalSize(), 1000);
    QCOMPARE(root.fileCount(), 4);
    QCOMPARE(root.directoryCount(), 3);
    QVERIFY(root.isComplete());
    QCOMPARE(childNames(root), QStringList({QStringLiteral("a.txt"), QStringLiteral("b.txt"), QStringLiteral("empty"), QStringLiteral("sub")}));

    const WhaleTreeNode a = child(root, QStringLiteral("a.txt"));
    QCOMPARE(a.type(), WhaleTreeNode::Type::File);
    QCOMPARE(a.size(), 100);
    QCOMPARE(a.totalSize(), 100);
    QCOMPARE(a.depth(), 1);
    QVERIFY(a.children().isEmpty());
    const WhaleTreeNode sub = child(root, QStringLiteral("sub"));
    QCOMPARE(sub.size(), 0);
    QCOMPARE(sub.totalSize(), 700);
    QCOMPARE(sub.fileCount(), 2);
    QCOMPARE(sub.directoryCount(), 1);
    QCOMPARE(child(child(sub, QStringLiteral("deeper")), QStringLiteral("d.txt")).depth(), 3);
    const WhaleTreeNode empty = child(root, QStringLiteral("empty"));
    QCOMPARE(empty.totalSize(), 0);
    QVERIFY(empty.children().isEmpty());
    QVERIFY(empty.isComplete());

    // A trailing slash makes no difference.
    QCOMPARE(dump(scanAndWait(&scanner, QUrl::fromLocalFile(dir.path() + QLatin1Char('/'))).root), dump(root));
}

void WhaleTreeScannerTest::testMaxDepth()
{
    QTemporaryDir dir;
    writeFile(dir.filePath(QStringLiteral("top.txt")), 5);
    writeFile(dir.filePath(QStringLiteral("l1/l2/l3/l4/leaf.txt")), 10);
    writeFile(dir.filePath(QStringLiteral("l1/l2/l3/mid.txt")), 20);
    WhaleTreeScannerImpl scanner;

    WhaleTreeScanner::Options options;
    options.maxDepth = 0;
    WhaleTreeNode root = scanAndWait(&scanner, QUrl::fromLocalFile(dir.path()), options).root;
    QVERIFY(root.children().isEmpty());
    // The sizes and counts still cover everything.
    QCOMPARE(root.totalSize(), 35);
    QCOMPARE(root.fileCount(), 3);
    QCOMPARE(root.directoryCount(), 4);
    QVERIFY(root.isComplete());

    options.maxDepth = 2;
    root = scanAndWait(&scanner, QUrl::fromLocalFile(dir.path()), options).root;
    const WhaleTreeNode l2 = child(child(root, QStringLiteral("l1")), QStringLiteral("l2"));
    QCOMPARE(l2.depth(), 2);
    QVERIFY(l2.children().isEmpty());
    QCOMPARE(l2.totalSize(), 30);
    QCOMPARE(l2.directoryCount(), 2);
    QCOMPARE(nodeCount(root), 4); // root, top.txt, l1, l2

    options.maxDepth = -1;
    root = scanAndWait(&scanner, QUrl::fromLocalFile(dir.path()), options).root;
    QCOMPARE(nodeCount(root), 8);
}

void WhaleTreeScannerTest::testSymlinks()
{
    QTemporaryDir dir;
    writeFile(dir.filePath(QStringLiteral("target.txt")), 500);
    writeFile(dir.filePath(QStringLiteral("folder/inside.txt")), 1000);
    QVERIFY(QFile::link(dir.filePath(QStringLiteral("target.txt")), dir.filePath(QStringLiteral("filelink"))));
    QVERIFY(QFile::link(dir.filePath(QStringLiteral("folder")), dir.filePath(QStringLiteral("folderlink"))));
    // A loop: a link inside the tree to its root.
    QVERIFY(QFile::link(dir.path(), dir.filePath(QStringLiteral("folder/loop"))));
    QVERIFY(QFile::link(dir.filePath(QStringLiteral("nothing")), dir.filePath(QStringLiteral("broken"))));

    WhaleTreeScannerImpl scanner;
    const Result result = scanAndWait(&scanner, QUrl::fromLocalFile(dir.path()));
    QVERIFY(result.finished);
    const WhaleTreeNode root = result.root;
    // Links are entries of their own, never followed and never counted with their target.
    for (const QString &name : {QStringLiteral("filelink"), QStringLiteral("folderlink"), QStringLiteral("broken")}) {
        const WhaleTreeNode link = child(root, name);
        QCOMPARE(link.type(), WhaleTreeNode::Type::Symlink);
        QCOMPARE(link.size(), 0);
        QCOMPARE(link.totalSize(), 0);
        QVERIFY(link.children().isEmpty());
        QVERIFY(link.isComplete());
    }
    const WhaleTreeNode loop = child(child(root, QStringLiteral("folder")), QStringLiteral("loop"));
    QCOMPARE(loop.type(), WhaleTreeNode::Type::Symlink);
    QVERIFY(loop.children().isEmpty());
    QCOMPARE(root.totalSize(), 1500);
    QCOMPARE(root.fileCount(), 6); // target, inside, 4 links
    QCOMPARE(root.directoryCount(), 1);
    QVERIFY(root.isComplete());

    // Only a link given as the root is followed.
    const WhaleTreeNode linkedRoot = scanAndWait(&scanner, QUrl::fromLocalFile(dir.filePath(QStringLiteral("folderlink")))).root;
    QCOMPARE(linkedRoot.type(), WhaleTreeNode::Type::Directory);
    QCOMPARE(childNames(linkedRoot), QStringList({QStringLiteral("inside.txt"), QStringLiteral("loop")}));
    QCOMPARE(linkedRoot.totalSize(), 1000);
}

void WhaleTreeScannerTest::testHidden()
{
    QTemporaryDir parent;
    const QString dir = parent.filePath(QStringLiteral(".dotroot"));
    writeFile(dir + QStringLiteral("/visible.txt"), 10);
    writeFile(dir + QStringLiteral("/.hidden.txt"), 50);
    writeFile(dir + QStringLiteral("/.hiddendir/inner.txt"), 70);
    writeFile(dir + QStringLiteral("/sub/.deephidden"), 3);

    WhaleTreeScannerImpl scanner;
    // Everything by default.
    WhaleTreeNode root = scanAndWait(&scanner, QUrl::fromLocalFile(dir)).root;
    QCOMPARE(root.totalSize(), 133);
    QCOMPARE(childNames(root),
             QStringList({QStringLiteral(".hidden.txt"), QStringLiteral(".hiddendir"), QStringLiteral("sub"), QStringLiteral("visible.txt")}));

    // Without hidden entries at any depth; a hidden root is scanned anyway.
    WhaleTreeScanner::Options options;
    options.includeHidden = false;
    root = scanAndWait(&scanner, QUrl::fromLocalFile(dir), options).root;
    QCOMPARE(root.totalSize(), 10);
    QCOMPARE(childNames(root), QStringList({QStringLiteral("sub"), QStringLiteral("visible.txt")}));
    QCOMPARE(root.fileCount(), 1);
    QCOMPARE(root.directoryCount(), 1);
    QVERIFY(child(root, QStringLiteral("sub")).children().isEmpty());
    QVERIFY(root.isComplete());
}

void WhaleTreeScannerTest::testCancel()
{
    WhaleTreeScannerImpl scanner;
    QSignalSpy finished(&scanner, &WhaleTreeScanner::finished);
    QSignalSpy canceled(&scanner, &WhaleTreeScanner::canceled);

    // Canceling right after starting, many times in a row.
    QElapsedTimer timer;
    for (int i = 0; i < 25; ++i) {
        QVERIFY(scanner.scan(QUrl::fromLocalFile(m_big.path())));
        QVERIFY(scanner.isRunning());
        scanner.cancel();
        QVERIFY(!scanner.isRunning());
        QCOMPARE(canceled.count(), i + 1); // at once, before cancel() returns
    }
    timer.start();
    QTRY_COMPARE_WITH_TIMEOUT(WhaleTreeScannerImpl::runningWorkers(), 0, 5000);
    qInfo() << "25 canceled scans: workers gone after" << timer.elapsed() << "ms";
    QTest::qWait(200);
    QCOMPARE(finished.count(), 0);
    // Canceling without a scan does nothing.
    scanner.cancel();
    QCOMPARE(canceled.count(), 25);

    // A new scan after all that is complete and correct.
    const Result result = scanAndWait(&scanner, QUrl::fromLocalFile(m_big.path()));
    QVERIFY(result.finished);
    QCOMPARE(result.root.fileCount(), m_bigFiles);
    QCOMPARE(result.root.directoryCount(), m_bigFolders);
    QCOMPARE(result.root.totalSize(), m_bigSize);
}

void WhaleTreeScannerTest::testCancelWhileScanning()
{
    // Slowed down, so the scan surely runs when it is canceled.
    std::atomic<int> visited = 0;
    VisitHook hook([&visited](const QString &) {
        ++visited;
        QThread::usleep(200);
    });
    WhaleTreeScannerImpl scanner;
    QSignalSpy finished(&scanner, &WhaleTreeScanner::finished);
    QSignalSpy progress(&scanner, &WhaleTreeScanner::progress);
    QVERIFY(scanner.scan(QUrl::fromLocalFile(m_big.path())));
    QVERIFY(progress.wait(5000));
    QVERIFY(progress.first().at(0).toInt() > 0);
    QVERIFY(WhaleTreeScannerImpl::runningWorkers() == 1);

    QElapsedTimer timer;
    timer.start();
    scanner.cancel();
    const int visitedAtCancel = visited;
    QTRY_COMPARE_WITH_TIMEOUT(WhaleTreeScannerImpl::runningWorkers(), 0, 2000);
    const qint64 latency = timer.elapsed();
    qInfo() << "cancel latency while scanning:" << latency << "ms, entries visited" << visitedAtCancel << "of" << (m_bigFiles + 2 * m_bigFolders);
    QVERIFY(latency < 500);
    QVERIFY(visited < m_bigFiles); // it really stopped early
    QTest::qWait(100);
    QCOMPARE(finished.count(), 0);
}

void WhaleTreeScannerTest::testFileChangesDuringScan()
{
    QTemporaryDir dir;
    writeFile(dir.filePath(QStringLiteral("a/first.txt")), 1);
    writeFile(dir.filePath(QStringLiteral("grows.txt")), 10);
    writeFile(dir.filePath(QStringLiteral("removed.txt")), 20);
    writeFile(dir.filePath(QStringLiteral("z/late.txt")), 30);
    writeFile(dir.filePath(QStringLiteral("z/gone.txt")), 40);

    const QString a = QDir::cleanPath(dir.filePath(QStringLiteral("a")));
    VisitHook hook([&](const QString &path) {
        if (path == a) {
            // The root's entries were read already: they keep what they were then.
            writeFile(dir.filePath(QStringLiteral("grows.txt")), 1000);
            QFile::remove(dir.filePath(QStringLiteral("removed.txt")));
            // z is not read yet: what vanishes there is missing, what is new is there.
            QFile::remove(dir.filePath(QStringLiteral("z/gone.txt")));
            writeFile(dir.filePath(QStringLiteral("z/new.txt")), 5);
        }
    });
    WhaleTreeScannerImpl scanner;
    const Result result = scanAndWait(&scanner, QUrl::fromLocalFile(dir.path()));
    QVERIFY(result.finished);
    QCOMPARE(child(result.root, QStringLiteral("grows.txt")).size(), 10);
    QCOMPARE(child(result.root, QStringLiteral("removed.txt")).size(), 20);
    QCOMPARE(childNames(child(result.root, QStringLiteral("z"))), QStringList({QStringLiteral("late.txt"), QStringLiteral("new.txt")}));
    QCOMPARE(result.root.totalSize(), 1 + 10 + 20 + 30 + 5);
    QVERIFY(result.root.isComplete());
}

void WhaleTreeScannerTest::testFolderVanishesDuringScan()
{
    QTemporaryDir dir;
    writeFile(dir.filePath(QStringLiteral("a/x.txt")), 1);
    writeFile(dir.filePath(QStringLiteral("b/y.txt")), 2);
    writeFile(dir.filePath(QStringLiteral("b/deeper/z.txt")), 3);
    writeFile(dir.filePath(QStringLiteral("c/w.txt")), 4);

    const QString a = QDir::cleanPath(dir.filePath(QStringLiteral("a")));
    VisitHook hook([&](const QString &path) {
        if (path == a) {
            QDir(dir.filePath(QStringLiteral("b"))).removeRecursively();
        }
    });
    WhaleTreeScannerImpl scanner;
    const Result result = scanAndWait(&scanner, QUrl::fromLocalFile(dir.path()));
    QVERIFY(result.finished);
    // b was listed before it vanished: an entry without content, marked incomplete.
    const WhaleTreeNode b = child(result.root, QStringLiteral("b"));
    QCOMPARE(b.type(), WhaleTreeNode::Type::Directory);
    QVERIFY(b.children().isEmpty());
    QVERIFY(!b.isComplete());
    QVERIFY(!result.root.isComplete());
    QVERIFY(child(result.root, QStringLiteral("c")).isComplete());
    QCOMPARE(result.root.totalSize(), 5);

    // The root itself vanishing before the scan reads it.
    QTemporaryDir gone;
    const QString goneRoot = QDir::cleanPath(gone.path());
    VisitHook rootHook([&](const QString &path) {
        if (path == goneRoot) {
            QDir(goneRoot).removeRecursively();
        }
    });
    const Result goneResult = scanAndWait(&scanner, QUrl::fromLocalFile(goneRoot));
    QVERIFY(goneResult.failed);
    QCOMPARE(goneResult.error, WhaleTreeScanner::Error::NotFound);
}

void WhaleTreeScannerTest::testPermissionDenied()
{
    if (geteuid() == 0) {
        QSKIP("root may read everything");
    }
    QTemporaryDir dir;
    writeFile(dir.filePath(QStringLiteral("locked/secret.txt")), 100);
    writeFile(dir.filePath(QStringLiteral("open/plain.txt")), 7);
    const QString locked = dir.filePath(QStringLiteral("locked"));
    QVERIFY(::chmod(QFile::encodeName(locked).constData(), 0) == 0);

    WhaleTreeScannerImpl scanner;
    const Result result = scanAndWait(&scanner, QUrl::fromLocalFile(dir.path()));
    QVERIFY(result.finished);
    // An unreadable folder is an entry without content; the rest is scanned.
    const WhaleTreeNode lockedNode = child(result.root, QStringLiteral("locked"));
    QCOMPARE(lockedNode.type(), WhaleTreeNode::Type::Directory);
    QVERIFY(lockedNode.children().isEmpty());
    QVERIFY(!lockedNode.isComplete());
    QVERIFY(!result.root.isComplete());
    QCOMPARE(result.root.totalSize(), 7);
    QVERIFY(child(result.root, QStringLiteral("open")).isComplete());

    // An unreadable root fails.
    const Result rootResult = scanAndWait(&scanner, QUrl::fromLocalFile(locked));
    QVERIFY(rootResult.failed);
    QCOMPARE(rootResult.error, WhaleTreeScanner::Error::AccessDenied);
    QVERIFY(rootResult.message.contains(QDir::cleanPath(locked)));
    QVERIFY(::chmod(QFile::encodeName(locked).constData(), 0700) == 0);
}

void WhaleTreeScannerTest::testRejectedRoots_data()
{
    QTest::addColumn<QUrl>("url");
    QTest::addColumn<WhaleTreeScanner::Error>("error");
    QTest::newRow("empty") << QUrl() << WhaleTreeScanner::Error::InvalidUrl;
    QTest::newRow("invalid") << QUrl(QStringLiteral("http://[::1")) << WhaleTreeScanner::Error::InvalidUrl;
    QTest::newRow("sftp") << QUrl(QStringLiteral("sftp://example.org/home")) << WhaleTreeScanner::Error::NotLocal;
    QTest::newRow("smb") << QUrl(QStringLiteral("smb://server/share")) << WhaleTreeScanner::Error::NotLocal;
    QTest::newRow("trash") << QUrl(QStringLiteral("trash:/")) << WhaleTreeScanner::Error::NotLocal;
    QTest::newRow("archive") << QUrl(QStringLiteral("zip:/tmp/a.zip/")) << WhaleTreeScanner::Error::NotLocal;
    QTest::newRow("missing") << QUrl::fromLocalFile(QStringLiteral("/nonexistent/whale/folder")) << WhaleTreeScanner::Error::NotFound;
    QTest::newRow("file") << QUrl::fromLocalFile(QFINDTESTDATA("whaletreescannertest.cpp")) << WhaleTreeScanner::Error::NotAFolder;
}

void WhaleTreeScannerTest::testRejectedRoots()
{
    QFETCH(QUrl, url);
    QFETCH(WhaleTreeScanner::Error, error);
    WhaleTreeScannerImpl scanner;
    QSignalSpy started(&scanner, &WhaleTreeScanner::started);
    QSignalSpy failed(&scanner, &WhaleTreeScanner::failed);
    QSignalSpy finished(&scanner, &WhaleTreeScanner::finished);
    QVERIFY(scanner.scan(url));
    QCOMPARE(started.count(), 1);
    // Reported later, like every outcome.
    QCOMPARE(failed.count(), 0);
    QVERIFY(failed.wait(5000));
    QCOMPARE(failed.first().at(0).value<WhaleTreeScanner::Error>(), error);
    QVERIFY(!failed.first().at(1).toString().isEmpty());
    QCOMPARE(finished.count(), 0);
    QVERIFY(!scanner.isRunning());
}

void WhaleTreeScannerTest::testOneScanAtATime()
{
    QTemporaryDir small;
    writeFile(small.filePath(QStringLiteral("only.txt")), 3);
    VisitHook hook([](const QString &) {
        QThread::usleep(50);
    });
    WhaleTreeScannerImpl scanner;
    QSignalSpy finished(&scanner, &WhaleTreeScanner::finished);
    QSignalSpy started(&scanner, &WhaleTreeScanner::started);
    QVERIFY(scanner.scan(QUrl::fromLocalFile(m_big.path())));
    // A second scan while one runs is refused and changes nothing.
    QVERIFY(!scanner.scan(QUrl::fromLocalFile(small.path())));
    QCOMPARE(started.count(), 1);
    QVERIFY(scanner.isRunning());

    // Cancel and start another one at once: only that one is reported.
    scanner.cancel();
    QVERIFY(scanner.scan(QUrl::fromLocalFile(small.path())));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
    QCOMPARE(finished.first().at(0).value<WhaleTreeNode>().totalSize(), 3);
    QTest::qWait(300);
    QCOMPARE(finished.count(), 1);

    // Two scanners scan in parallel.
    WhaleTreeScannerImpl other;
    QSignalSpy otherFinished(&other, &WhaleTreeScanner::finished);
    QVERIFY(scanner.scan(QUrl::fromLocalFile(small.path())));
    QVERIFY(other.scan(QUrl::fromLocalFile(small.path())));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(otherFinished.count(), 1, 10000);
}

void WhaleTreeScannerTest::testThreads()
{
    QThread *const guiThread = QThread::currentThread();
    std::atomic<bool> inGuiThread = false;
    VisitHook hook([&](const QString &) {
        if (QThread::currentThread() == guiThread) {
            inGuiThread = true;
        }
        QThread::usleep(100);
    });
    WhaleTreeScannerImpl scanner;
    QThread *finishedIn = nullptr;
    connect(&scanner, &WhaleTreeScanner::finished, this, [&finishedIn]() {
        finishedIn = QThread::currentThread();
    });
    QThread *progressIn = nullptr;
    connect(&scanner, &WhaleTreeScanner::progress, this, [&progressIn]() {
        progressIn = QThread::currentThread();
    });

    // The GUI thread stays free: a timer keeps ticking while the scan runs.
    int ticks = 0;
    QTimer ticker;
    connect(&ticker, &QTimer::timeout, this, [&ticks]() {
        ++ticks;
    });
    ticker.start(10);
    QElapsedTimer timer;
    timer.start();
    QVERIFY(scanner.scan(QUrl::fromLocalFile(m_big.path())));
    QTRY_VERIFY_WITH_TIMEOUT(finishedIn, 30000);
    const qint64 elapsed = timer.elapsed();
    qInfo() << "slowed scan:" << elapsed << "ms, timer ticks meanwhile:" << ticks;
    QVERIFY(!inGuiThread);
    QCOMPARE(finishedIn, guiThread);
    QCOMPARE(progressIn, guiThread);
    QVERIFY(ticks > elapsed / 10 / 2);
}

void WhaleTreeScannerTest::testRepeatable()
{
    WhaleTreeScannerImpl scanner;
    const QStringList first = dump(scanAndWait(&scanner, QUrl::fromLocalFile(m_big.path())).root);
    QCOMPARE(first.count(), 1 + m_bigFolders + m_bigFiles);
    for (int i = 0; i < 3; ++i) {
        QCOMPARE(dump(scanAndWait(&scanner, QUrl::fromLocalFile(m_big.path())).root), first);
    }
}

void WhaleTreeScannerTest::testLargeTree()
{
    WhaleTreeScannerImpl scanner;
    const long residentBefore = residentKilobytes();
    QElapsedTimer timer;
    timer.start();
    const Result result = scanAndWait(&scanner, QUrl::fromLocalFile(m_big.path()));
    const qint64 elapsed = timer.elapsed();
    QVERIFY(result.finished);
    QCOMPARE(result.root.fileCount(), m_bigFiles);
    QCOMPARE(result.root.directoryCount(), m_bigFolders);
    QCOMPARE(result.root.totalSize(), m_bigSize);
    QCOMPARE(nodeCount(result.root), 1 + m_bigFolders + m_bigFiles);
    qInfo() << "large tree:" << m_bigFiles << "files," << m_bigFolders << "folders in" << elapsed
            << "ms; resident memory with the tree held:" << (residentKilobytes() - residentBefore) << "KiB more";

    // Optional, for rough measurements of a bigger tree prepared elsewhere.
    if (const QString extra = qEnvironmentVariable("WHALE_TREESCANNER_MEASURE"); !extra.isEmpty()) {
        const long before = residentKilobytes();
        timer.restart();
        const Result measured = scanAndWait(&scanner, QUrl::fromLocalFile(extra), {}, 600000);
        QVERIFY(measured.finished);
        qInfo() << extra << ":" << measured.root.fileCount() << "files," << measured.root.directoryCount() << "folders in" << timer.elapsed()
                << "ms; resident memory with the tree held:" << (residentKilobytes() - before) << "KiB more";
    }

    // Small tree.
    QTemporaryDir small;
    writeFile(small.filePath(QStringLiteral("a/b.txt")), 1);
    timer.restart();
    QVERIFY(scanAndWait(&scanner, QUrl::fromLocalFile(small.path())).finished);
    qInfo() << "small tree:" << timer.elapsed() << "ms";
}

void WhaleTreeScannerTest::testStayOnFilesystem()
{
    // A real mount point below a small, readable folder; tests cannot mount anything.
    const QString parent = QStringLiteral("/sys/fs");
    const QString mountPoint = QStringLiteral("/sys/fs/cgroup");
    struct stat parentStat{};
    struct stat mountStat{};
    if (::stat(QFile::encodeName(parent).constData(), &parentStat) != 0 || ::stat(QFile::encodeName(mountPoint).constData(), &mountStat) != 0
        || parentStat.st_dev == mountStat.st_dev) {
        QSKIP("no mount point at /sys/fs/cgroup");
    }
    WhaleTreeScannerImpl scanner;
    WhaleTreeScanner::Options options;
    options.maxDepth = 1;

    // By default the mount point is listed, but not entered.
    Result result = scanAndWait(&scanner, QUrl::fromLocalFile(parent), options);
    QVERIFY(result.finished);
    WhaleTreeNode cgroup = child(result.root, QStringLiteral("cgroup"));
    QCOMPARE(cgroup.type(), WhaleTreeNode::Type::Directory);
    QCOMPARE(cgroup.directoryCount(), 0);
    QCOMPARE(cgroup.fileCount(), 0);
    QVERIFY(!cgroup.isComplete());
    QVERIFY(!result.root.isComplete());

    options.stayOnFilesystem = false;
    result = scanAndWait(&scanner, QUrl::fromLocalFile(parent), options);
    QVERIFY(result.finished);
    cgroup = child(result.root, QStringLiteral("cgroup"));
    QVERIFY(cgroup.fileCount() > 0);
}

void WhaleTreeScannerTest::testContextScanner()
{
    // The scanner of a view's context: one per context, it goes with the context.
    DolphinView view(QUrl::fromLocalFile(m_big.path()), nullptr);
    DolphinViewBridge bridge(&view);
    auto context = std::make_unique<WhaleViewContextImpl>(&bridge);
    QPointer<WhaleTreeScanner> scanner = context->treeScanner();
    QVERIFY(scanner);
    QCOMPARE(context->treeScanner(), scanner.data());
    QCOMPARE(scanner->parent(), context.get());

    const Result result = scanAndWait(scanner, QUrl::fromLocalFile(m_big.path()));
    QVERIFY(result.finished);
    QCOMPARE(result.root.fileCount(), m_bigFiles);

    // Destroying the context while scanning: no signal, no crash, the worker ends.
    VisitHook hook([](const QString &) {
        QThread::usleep(100);
    });
    QVERIFY(scanner->scan(QUrl::fromLocalFile(m_big.path())));
    QSignalSpy canceled(scanner, &WhaleTreeScanner::canceled);
    QSignalSpy finished(scanner, &WhaleTreeScanner::finished);
    QTest::qWait(50);
    context.reset();
    QVERIFY(scanner.isNull());
    QTRY_COMPARE_WITH_TIMEOUT(WhaleTreeScannerImpl::runningWorkers(), 0, 2000);
}

QTEST_MAIN(WhaleTreeScannerTest)

#include "whaletreescannertest.moc"
