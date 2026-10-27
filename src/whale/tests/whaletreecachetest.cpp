/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaletreecache.h"
#include "config-dolphin.h"
#include "dolphin_generalsettings.h"
#include "dolphinmainwindow.h"
#include "dolphintabpage.h"
#include "dolphintabwidget.h"
#include "dolphinviewcontainer.h"
#include "testhelpers.h"
#include "whaletreenode.h"
#include "whaletreescanner.h"
#include "whaletreescannerimpl.h"
#include "whaleview.h"
#include "whaleviewactions.h"
#include "whaleviewcontextimpl.h"
#include "whaleviewhost.h"
#include "whaleviewregistry.h"

#include <KActionCollection>
#include <KLocalizedString>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <atomic>

namespace
{
const QString testListId = QStringLiteral("whale_testlist");

void writeFile(const QString &path, qint64 size)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArray(size, 'x'));
}

void appendToFile(const QString &path, qint64 size)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::Append));
    file.write(QByteArray(size, 'y'));
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

QStringList dump(const WhaleTreeNode &node)
{
    QStringList lines = {QStringLiteral("%1 %2 %3 %4 %5").arg(node.path()).arg(int(node.type())).arg(node.size()).arg(node.totalSize()).arg(node.isComplete())};
    const QList<WhaleTreeNode> children = node.children();
    for (const WhaleTreeNode &entry : children) {
        lines += dump(entry);
    }
    return lines;
}

/// Counts the scans that really run (visits of their root).
class ScanCounter
{
public:
    explicit ScanCounter(int delayMicroseconds = 0)
    {
        WhaleTreeScannerImpl::setVisitHookForTesting([this, delayMicroseconds](const QString &path) {
            if (m_roots.contains(path)) {
                ++m_scans;
            }
            if (delayMicroseconds > 0) {
                QThread::usleep(delayMicroseconds);
            }
        });
    }
    ~ScanCounter()
    {
        WhaleTreeScannerImpl::setVisitHookForTesting({});
    }
    void watchRoot(const QString &root)
    {
        m_roots.append(QDir::cleanPath(root));
    }
    int scans() const
    {
        // fts visits a folder twice: before and after its entries.
        return m_scans / 2;
    }

private:
    QStringList m_roots;
    std::atomic<int> m_scans = 0;
};

struct Result {
    bool finished = false;
    bool canceled = false;
    bool failed = false;
    WhaleTreeNode root;
    qint64 elapsed = 0;
    bool finishedWithinScan = false;
};

Result scanAndWait(WhaleTreeScanner *scanner, const QString &path, const WhaleTreeScanner::Options &options = {})
{
    Result result;
    QEventLoop loop;
    QObject guard;
    bool inScan = true;
    QObject::connect(scanner, &WhaleTreeScanner::finished, &guard, [&](const WhaleTreeNode &root) {
        result.finished = true;
        result.finishedWithinScan = inScan;
        result.root = root;
        loop.quit();
    });
    QObject::connect(scanner, &WhaleTreeScanner::canceled, &guard, [&]() {
        result.canceled = true;
        loop.quit();
    });
    QObject::connect(scanner, &WhaleTreeScanner::failed, &guard, [&]() {
        result.failed = true;
        loop.quit();
    });
    QElapsedTimer timer;
    timer.start();
    if (!scanner->scan(QUrl::fromLocalFile(path), options)) {
        return result;
    }
    inScan = false;
    if (!result.finished && !result.canceled && !result.failed) {
        QTimer::singleShot(30000, &loop, &QEventLoop::quit);
        loop.exec();
    }
    result.elapsed = timer.nsecsElapsed() / 1000;
    return result;
}

WhaleTreeCache::Limits limits(qint64 memoryBytes = 64 * 1024 * 1024, int watchedFolders = 5000)
{
    return {memoryBytes, watchedFolders};
}
}

/**
 * M8b: the tree scans shared through WhaleTreeCache: reuse, invalidation by inotify,
 * consumers and canceling, budgets and eviction, and the compact nodes.
 */
class WhaleTreeCacheTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testHit();
    void testOptionsAreKeys();
    void testTwoScannersShareOneScan();
    void testCancelOneConsumer();
    void testCancelAllConsumers();
    void testInvalidation_data();
    void testInvalidation();
    void testEviction();
    void testSnapshotOutlivesEviction();
    void testChangeDuringScan();
    void testChangeRightAfterScan();
    void testOneScanPerScanner();
    void testWatchBudget();
    void testNotKept();
    void testMemoryEstimate();
    void testContextsOfAWindowShareTheCache();
    void testMeasure();

private:
    QString path(const QString &relative) const;

    QTemporaryDir m_tmpHome;
    QTemporaryDir m_big; // 30 folders with 10 subfolders and 20 files each
    QScopedPointer<QTemporaryDir> m_dir;
};

void WhaleTreeCacheTest::initTestCase()
{
    QVERIFY(m_tmpHome.isValid());
    QCoreApplication::setApplicationName(QStringLiteral(WHALE_COMPONENT));
    QCoreApplication::setOrganizationDomain(QStringLiteral(WHALE_ORG_DOMAIN));
    qputenv("HOME", m_tmpHome.path().toUtf8());
    QStandardPaths::setTestModeEnabled(true);
    TestHelpers::disableAnimations();
    TestHelpers::openNewWindowsInSingleView();
    KLocalizedString::setApplicationDomain(TRANSLATION_DOMAIN);
    QCoreApplication::addLibraryPath(QStringLiteral(WHALE_TEST_PLUGIN_DIR));
    QCoreApplication::addLibraryPath(QStringLiteral(WHALE_TEST_VIEW_PLUGIN_DIR));
    for (int i = 0; i < 30; ++i) {
        for (int j = 0; j < 10; ++j) {
            for (int k = 0; k < 20; ++k) {
                writeFile(m_big.filePath(QStringLiteral("d%1/e%2/f%3").arg(i).arg(j).arg(k)), k + 1);
            }
        }
    }
}

void WhaleTreeCacheTest::init()
{
    m_dir.reset(new QTemporaryDir);
    writeFile(path(QStringLiteral("a.txt")), 100);
    writeFile(path(QStringLiteral("sub/b.txt")), 200);
    writeFile(path(QStringLiteral("sub/deeper/c.txt")), 300);
    writeFile(path(QStringLiteral(".hidden")), 7);
}

void WhaleTreeCacheTest::cleanup()
{
    WhaleTreeScannerImpl::setVisitHookForTesting({});
    QTRY_COMPARE_WITH_TIMEOUT(WhaleTreeScannerImpl::runningWorkers(), 0, 5000);
    m_dir.reset();
}

QString WhaleTreeCacheTest::path(const QString &relative) const
{
    return m_dir->filePath(relative);
}

void WhaleTreeCacheTest::testHit()
{
    WhaleTreeCache cache(limits());
    WhaleTreeScannerImpl scanner(&cache);
    ScanCounter counter;
    counter.watchRoot(m_big.path());

    const Result first = scanAndWait(&scanner, m_big.path());
    QVERIFY(first.finished);
    QCOMPARE(counter.scans(), 1);
    QCOMPARE(cache.statistics().entries, 1);
    // One watch per folder of the tree.
    QCOMPARE(cache.statistics().watchedFolders, 1 + 30 + 300);

    QSignalSpy started(&scanner, &WhaleTreeScanner::started);
    QSignalSpy progress(&scanner, &WhaleTreeScanner::progress);
    const Result second = scanAndWait(&scanner, m_big.path());
    QVERIFY(second.finished);
    // The same signals as a scan: started() within scan(), finished() afterwards.
    QCOMPARE(started.count(), 1);
    QVERIFY(!second.finishedWithinScan);
    QCOMPARE(progress.count(), 0);
    QCOMPARE(counter.scans(), 1); // no worker
    QCOMPARE(dump(second.root), dump(first.root));
    const WhaleTreeCache::Statistics statistics = cache.statistics();
    QCOMPARE(statistics.hits, 1);
    QCOMPARE(statistics.misses, 1);
    qInfo() << "6000 files: scan" << first.elapsed << "us, kept tree" << second.elapsed << "us," << statistics.bytes / 1024 << "KiB estimated,"
            << statistics.watchedFolders << "watched folders";
}

void WhaleTreeCacheTest::testOptionsAreKeys()
{
    WhaleTreeCache cache(limits());
    WhaleTreeScannerImpl scanner(&cache);
    WhaleTreeScanner::Options shallow;
    shallow.maxDepth = 1;
    WhaleTreeScanner::Options visible;
    visible.includeHidden = false;
    WhaleTreeScanner::Options crossing;
    crossing.stayOnFilesystem = false;

    const QList<WhaleTreeScanner::Options> all = {WhaleTreeScanner::Options(), shallow, visible, crossing};
    for (const WhaleTreeScanner::Options &options : all) {
        QVERIFY(scanAndWait(&scanner, m_dir->path(), options).finished);
    }
    QCOMPARE(cache.statistics().misses, 4);
    QCOMPARE(cache.statistics().entries, 4);
    QCOMPARE(scanAndWait(&scanner, m_dir->path(), visible).root.totalSize(), 600);
    QCOMPARE(scanAndWait(&scanner, m_dir->path()).root.totalSize(), 607);
    QVERIFY(scanAndWait(&scanner, m_dir->path(), shallow).root.children().count() == 3);
    QCOMPARE(cache.statistics().hits, 3);

    // A trailing slash is the same root; any negative depth means no limit.
    QVERIFY(scanAndWait(&scanner, m_dir->path() + QLatin1Char('/')).finished);
    WhaleTreeScanner::Options unlimited;
    unlimited.maxDepth = -7;
    QVERIFY(scanAndWait(&scanner, m_dir->path(), unlimited).finished);
    QCOMPARE(cache.statistics().misses, 4);
    QCOMPARE(cache.statistics().hits, 5);
}

void WhaleTreeCacheTest::testTwoScannersShareOneScan()
{
    WhaleTreeCache cache(limits());
    WhaleTreeScannerImpl a(&cache);
    WhaleTreeScannerImpl b(&cache);
    ScanCounter counter(20);
    counter.watchRoot(m_big.path());
    QSignalSpy finishedA(&a, &WhaleTreeScanner::finished);
    QSignalSpy finishedB(&b, &WhaleTreeScanner::finished);
    QVERIFY(a.scan(QUrl::fromLocalFile(m_big.path())));
    QVERIFY(b.scan(QUrl::fromLocalFile(m_big.path())));
    QTRY_VERIFY_WITH_TIMEOUT(finishedA.count() == 1 && finishedB.count() == 1, 30000);
    // One worker for both, the same tree for both.
    QCOMPARE(counter.scans(), 1);
    QCOMPARE(cache.statistics().misses, 1);
    QCOMPARE(cache.statistics().joins, 1);
    QCOMPARE(dump(finishedA.first().at(0).value<WhaleTreeNode>()), dump(finishedB.first().at(0).value<WhaleTreeNode>()));
    // A third one later gets the kept tree.
    WhaleTreeScannerImpl c(&cache);
    QVERIFY(scanAndWait(&c, m_big.path()).finished);
    QCOMPARE(counter.scans(), 1);
    QCOMPARE(cache.statistics().hits, 1);
}

void WhaleTreeCacheTest::testCancelOneConsumer()
{
    WhaleTreeCache cache(limits());
    WhaleTreeScannerImpl a(&cache);
    WhaleTreeScannerImpl b(&cache);
    ScanCounter counter(50);
    counter.watchRoot(m_big.path());
    QSignalSpy finishedA(&a, &WhaleTreeScanner::finished);
    QSignalSpy canceledA(&a, &WhaleTreeScanner::canceled);
    QSignalSpy finishedB(&b, &WhaleTreeScanner::finished);
    QVERIFY(a.scan(QUrl::fromLocalFile(m_big.path())));
    QVERIFY(b.scan(QUrl::fromLocalFile(m_big.path())));
    QTest::qWait(50);
    QVERIFY(WhaleTreeScannerImpl::runningWorkers() == 1);

    // A leaves; the scan goes on for B.
    a.cancel();
    QCOMPARE(canceledA.count(), 1);
    QVERIFY(!a.isRunning());
    QVERIFY(b.isRunning());
    QTRY_COMPARE_WITH_TIMEOUT(finishedB.count(), 1, 30000);
    QCOMPARE(finishedA.count(), 0);
    QCOMPARE(finishedB.first().at(0).value<WhaleTreeNode>().fileCount(), 6000);
    QCOMPARE(counter.scans(), 1);
    QCOMPARE(cache.statistics().entries, 1);
}

void WhaleTreeCacheTest::testCancelAllConsumers()
{
    WhaleTreeCache cache(limits());
    WhaleTreeScannerImpl a(&cache);
    WhaleTreeScannerImpl b(&cache);
    ScanCounter counter(200);
    counter.watchRoot(m_big.path());
    QSignalSpy finishedB(&b, &WhaleTreeScanner::finished);
    QVERIFY(a.scan(QUrl::fromLocalFile(m_big.path())));
    QVERIFY(b.scan(QUrl::fromLocalFile(m_big.path())));
    QTest::qWait(50);
    a.cancel();
    QElapsedTimer timer;
    timer.start();
    b.cancel();
    // Nobody waits: the worker stops, nothing is kept, its watches are gone.
    QTRY_COMPARE_WITH_TIMEOUT(WhaleTreeScannerImpl::runningWorkers(), 0, 2000);
    qInfo() << "worker stopped" << timer.elapsed() << "ms after the last consumer left";
    QTest::qWait(100);
    QCOMPARE(finishedB.count(), 0);
    const WhaleTreeCache::Statistics statistics = cache.statistics();
    QCOMPARE(statistics.entries, 0);
    QCOMPARE(statistics.runningScans, 0);
    QCOMPARE(statistics.watchedFolders, 0);

    // The next request scans anew.
    WhaleTreeScanner::Options options;
    QVERIFY(scanAndWait(&a, m_dir->path(), options).finished);
    QCOMPARE(cache.statistics().misses, 2);
}

void WhaleTreeCacheTest::testInvalidation_data()
{
    QTest::addColumn<QString>("change");
    QTest::addColumn<QStringList>("expectedAfter"); // paths relative to the root, in tree order

    QTest::newRow("file created") << QStringLiteral("create") << QStringList{QStringLiteral("sub/new.txt")};
    QTest::newRow("file deleted") << QStringLiteral("delete") << QStringList();
    QTest::newRow("file renamed") << QStringLiteral("rename") << QStringList{QStringLiteral("sub/renamed.txt")};
    QTest::newRow("file grew") << QStringLiteral("grow") << QStringList();
    QTest::newRow("subfolder created") << QStringLiteral("mkdir") << QStringList{QStringLiteral("sub/deeper/newfolder")};
    QTest::newRow("subfolder deleted") << QStringLiteral("rmdir") << QStringList();
    QTest::newRow("subfolder renamed") << QStringLiteral("renamedir") << QStringList{QStringLiteral("sub/moved")};
    QTest::newRow("deep file changed") << QStringLiteral("deep") << QStringList();
}

void WhaleTreeCacheTest::testInvalidation()
{
    QFETCH(QString, change);
    QFETCH(QStringList, expectedAfter);
    WhaleTreeCache cache(limits());
    WhaleTreeScannerImpl scanner(&cache);
    const WhaleTreeNode before = scanAndWait(&scanner, m_dir->path()).root;
    QCOMPARE(before.totalSize(), 607);
    QCOMPARE(cache.statistics().entries, 1);

    if (change == QLatin1String("create")) {
        writeFile(path(QStringLiteral("sub/new.txt")), 50);
    } else if (change == QLatin1String("delete")) {
        QVERIFY(QFile::remove(path(QStringLiteral("sub/b.txt"))));
    } else if (change == QLatin1String("rename")) {
        QVERIFY(QFile::rename(path(QStringLiteral("sub/b.txt")), path(QStringLiteral("sub/renamed.txt"))));
    } else if (change == QLatin1String("grow")) {
        appendToFile(path(QStringLiteral("a.txt")), 11);
    } else if (change == QLatin1String("mkdir")) {
        QVERIFY(QDir().mkdir(path(QStringLiteral("sub/deeper/newfolder"))));
    } else if (change == QLatin1String("rmdir")) {
        QVERIFY(QDir(path(QStringLiteral("sub/deeper"))).removeRecursively());
    } else if (change == QLatin1String("renamedir")) {
        QVERIFY(QDir().rename(path(QStringLiteral("sub/deeper")), path(QStringLiteral("sub/moved"))));
    } else if (change == QLatin1String("deep")) {
        appendToFile(path(QStringLiteral("sub/deeper/c.txt")), 1);
    }
    // The kept tree is dropped as soon as the change is reported.
    QTRY_COMPARE_WITH_TIMEOUT(cache.statistics().entries, 0, 5000);
    QCOMPARE(cache.statistics().invalidations, 1);
    QCOMPARE(cache.statistics().watchedFolders, 0);

    const WhaleTreeNode after = scanAndWait(&scanner, m_dir->path()).root;
    QCOMPARE(cache.statistics().misses, 2);
    const QString root = QDir::cleanPath(m_dir->path()) + QLatin1Char('/');
    QStringList paths;
    std::function<void(const WhaleTreeNode &)> collect = [&](const WhaleTreeNode &node) {
        paths.append(node.path().mid(root.size()));
        for (const WhaleTreeNode &entry : node.children()) {
            collect(entry);
        }
    };
    collect(after);
    for (const QString &expected : std::as_const(expectedAfter)) {
        QVERIFY2(paths.contains(expected), qPrintable(paths.join(QLatin1Char(','))));
    }
    if (change == QLatin1String("delete") || change == QLatin1String("rename")) {
        QVERIFY(!paths.contains(QStringLiteral("sub/b.txt")));
        QCOMPARE(after.totalSize(), change == QLatin1String("delete") ? 407 : 607);
    } else if (change == QLatin1String("grow")) {
        QCOMPARE(after.totalSize(), 618);
    } else if (change == QLatin1String("rmdir") || change == QLatin1String("renamedir")) {
        QVERIFY(!paths.contains(QStringLiteral("sub/deeper")));
    } else if (change == QLatin1String("deep")) {
        QCOMPARE(after.totalSize(), 608);
    }
    // The tree handed out before is unchanged.
    QCOMPARE(before.totalSize(), 607);
}

void WhaleTreeCacheTest::testEviction()
{
    // Room for about two of the small trees.
    QTemporaryDir one;
    QTemporaryDir two;
    QTemporaryDir three;
    for (QTemporaryDir *dir : {&one, &two, &three}) {
        for (int i = 0; i < 20; ++i) {
            writeFile(dir->filePath(QStringLiteral("f%1").arg(i)), 1);
        }
    }
    WhaleTreeCache probe(limits());
    WhaleTreeScannerImpl probeScanner(&probe);
    QVERIFY(scanAndWait(&probeScanner, one.path()).finished);
    const qint64 treeBytes = probe.statistics().bytes;
    QVERIFY(treeBytes > 0);

    WhaleTreeCache cache(limits(treeBytes * 2 + treeBytes / 2));
    WhaleTreeScannerImpl scanner(&cache);
    QVERIFY(scanAndWait(&scanner, one.path()).finished);
    QVERIFY(scanAndWait(&scanner, two.path()).finished);
    QCOMPARE(cache.statistics().entries, 2);
    // one is used again, so two is the least recently used one.
    QVERIFY(scanAndWait(&scanner, one.path()).finished);
    QVERIFY(scanAndWait(&scanner, three.path()).finished);
    WhaleTreeCache::Statistics statistics = cache.statistics();
    QCOMPARE(statistics.entries, 2);
    QCOMPARE(statistics.evictions, 1);
    QVERIFY(statistics.bytes <= cache.limits().memoryBytes);
    QCOMPARE(statistics.watchedFolders, 2);

    const int misses = statistics.misses;
    QVERIFY(scanAndWait(&scanner, one.path()).finished);
    QVERIFY(scanAndWait(&scanner, three.path()).finished);
    QCOMPARE(cache.statistics().misses, misses); // kept
    QVERIFY(scanAndWait(&scanner, two.path()).finished);
    QCOMPARE(cache.statistics().misses, misses + 1); // evicted

    // A tree larger than the whole budget is delivered, but not kept.
    WhaleTreeCache tiny(limits(treeBytes / 2));
    WhaleTreeScannerImpl tinyScanner(&tiny);
    QVERIFY(scanAndWait(&tinyScanner, one.path()).finished);
    QCOMPARE(tiny.statistics().entries, 0);
    QCOMPARE(tiny.statistics().notKept, 1);
    QCOMPARE(tiny.statistics().watchedFolders, 0);
}

void WhaleTreeCacheTest::testSnapshotOutlivesEviction()
{
    WhaleTreeNode held;
    {
        WhaleTreeCache cache(limits());
        WhaleTreeScannerImpl scanner(&cache);
        held = scanAndWait(&scanner, m_dir->path()).root;
        cache.clear();
        QCOMPARE(cache.statistics().entries, 0);
    }
    // Cache and scanner are gone; the tree handed out is whole.
    QCOMPARE(held.totalSize(), 607);
    const WhaleTreeNode deeper = child(child(held, QStringLiteral("sub")), QStringLiteral("deeper"));
    QCOMPARE(deeper.path(), QDir::cleanPath(path(QStringLiteral("sub/deeper"))));
    QCOMPARE(child(deeper, QStringLiteral("c.txt")).url(), QUrl::fromLocalFile(path(QStringLiteral("sub/deeper/c.txt"))));
    QCOMPARE(child(deeper, QStringLiteral("c.txt")).name(), QStringLiteral("c.txt"));
    QCOMPARE(held.name(), QFileInfo(m_dir->path()).fileName());
}

void WhaleTreeCacheTest::testChangeDuringScan()
{
    WhaleTreeCache cache(limits());
    WhaleTreeScannerImpl scanner(&cache);
    // While the scan visits c.txt, sub has been read and is watched: a change there is
    // reported, so the tree is delivered but not kept.
    const QString c = QDir::cleanPath(path(QStringLiteral("sub/deeper/c.txt")));
    WhaleTreeScannerImpl::setVisitHookForTesting([&](const QString &visited) {
        if (visited == c) {
            QFile file(path(QStringLiteral("sub/during.txt")));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QThread::msleep(100); // the event reaches the GUI thread before the scan ends
        }
    });
    const Result result = scanAndWait(&scanner, m_dir->path());
    WhaleTreeScannerImpl::setVisitHookForTesting({});
    QVERIFY(result.finished);
    QTRY_COMPARE(cache.statistics().notKept + cache.statistics().invalidations, 1);
    QCOMPARE(cache.statistics().entries, 0);

    // The next scan is a real one and sees the file.
    QVERIFY(!child(child(scanAndWait(&scanner, m_dir->path()).root, QStringLiteral("sub")), QStringLiteral("during.txt")).isNull());
    QCOMPARE(cache.statistics().misses, 2);
}

void WhaleTreeCacheTest::testChangeRightAfterScan()
{
    // However the change and the end of the scan meet in the GUI thread, the next scan
    // never gets the old tree.
    for (int round = 0; round < 20; ++round) {
        WhaleTreeCache cache(limits());
        WhaleTreeScannerImpl scanner(&cache);
        QSignalSpy finished(&scanner, &WhaleTreeScanner::finished);
        const QString file = path(QStringLiteral("sub/round%1.txt").arg(round));
        QVERIFY(scanner.scan(QUrl::fromLocalFile(m_dir->path())));
        // Wait for the worker, but not for its outcome to arrive (generously: while many tests
        // start in parallel, even this small scan took over 5 seconds).
        QTRY_COMPARE_WITH_TIMEOUT(WhaleTreeScannerImpl::runningWorkers(), 0, 30000);
        writeFile(file, 1);
        QTRY_COMPARE(finished.count(), 1);
        QTest::qWait(20);
        const WhaleTreeNode again = scanAndWait(&scanner, m_dir->path()).root;
        QVERIFY2(!child(child(again, QStringLiteral("sub")), QFileInfo(file).fileName()).isNull(), qPrintable(QString::number(round)));
    }
}

void WhaleTreeCacheTest::testOneScanPerScanner()
{
    WhaleTreeCache cache(limits());
    WhaleTreeScannerImpl scanner(&cache);
    ScanCounter counter(20);
    QVERIFY(scanner.scan(QUrl::fromLocalFile(m_big.path())));
    QVERIFY(!scanner.scan(QUrl::fromLocalFile(m_dir->path())));
    // Also not for a kept tree.
    QSignalSpy finished(&scanner, &WhaleTreeScanner::finished);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 30000);
    QVERIFY(scanner.scan(QUrl::fromLocalFile(m_big.path())));
    QVERIFY(scanner.isRunning());
    QVERIFY(!scanner.scan(QUrl::fromLocalFile(m_dir->path())));
    // Canceling a kept tree's delivery: canceled, no finished.
    QSignalSpy canceled(&scanner, &WhaleTreeScanner::canceled);
    scanner.cancel();
    QCOMPARE(canceled.count(), 1);
    QTest::qWait(50);
    QCOMPARE(finished.count(), 1);
}

void WhaleTreeCacheTest::testWatchBudget()
{
    // The big tree has 331 folders; a budget of 100 cannot watch it.
    WhaleTreeCache cache(limits(64 * 1024 * 1024, 100));
    WhaleTreeScannerImpl scanner(&cache);
    const Result result = scanAndWait(&scanner, m_big.path());
    QVERIFY(result.finished);
    QCOMPARE(result.root.fileCount(), 6000); // complete all the same
    WhaleTreeCache::Statistics statistics = cache.statistics();
    QCOMPARE(statistics.entries, 0);
    QCOMPARE(statistics.notKept, 1);
    QCOMPARE(statistics.watchedFolders, 0);

    // Small trees fit; the watched folders of evicted trees are given back.
    QVERIFY(scanAndWait(&scanner, m_dir->path()).finished);
    QCOMPARE(cache.statistics().entries, 1);
    QCOMPARE(cache.statistics().watchedFolders, 3);
    cache.clear();
    QCOMPARE(cache.statistics().watchedFolders, 0);
}

void WhaleTreeCacheTest::testNotKept()
{
    WhaleTreeCache cache(limits());
    WhaleTreeScannerImpl scanner(&cache);
    QSignalSpy failed(&scanner, &WhaleTreeScanner::failed);
    // Failures are not kept.
    QVERIFY(scanner.scan(QUrl(QStringLiteral("sftp://example.org/home"))));
    QVERIFY(failed.wait());
    QVERIFY(scanner.scan(QUrl::fromLocalFile(QStringLiteral("/nonexistent/whale"))));
    QVERIFY(failed.wait());
    QCOMPARE(cache.statistics().entries, 0);
    QCOMPARE(cache.statistics().notKept, 2);
    // The configured limits are sane.
    const WhaleTreeCache::Limits configured = WhaleTreeCache::configuredLimits();
    QCOMPARE(configured.memoryBytes, qint64(128) * 1024 * 1024);
    QVERIFY(configured.watchedFolders > 0 && configured.watchedFolders <= 10000);
}

void WhaleTreeCacheTest::testMemoryEstimate()
{
    WhaleTreeCache cache(limits());
    WhaleTreeScannerImpl scanner(&cache);
    const Result result = scanAndWait(&scanner, m_big.path());
    QVERIFY(result.finished);
    const int nodes = 1 + 330 + 6000;
    const qint64 bytes = cache.statistics().bytes;
    qInfo() << "estimated" << bytes << "bytes for" << nodes << "nodes:" << bytes / nodes << "per node";
    QVERIFY(bytes > nodes * 100);
    QVERIFY(bytes < nodes * 400);

    // Scanning many roots never lets the kept trees exceed the budget.
    WhaleTreeCache small(limits(bytes * 3 / 2));
    WhaleTreeScannerImpl smallScanner(&small);
    for (int i = 0; i < 30; ++i) {
        QVERIFY(scanAndWait(&smallScanner, m_big.filePath(QStringLiteral("d%1").arg(i))).finished);
        QVERIFY(small.statistics().bytes <= small.limits().memoryBytes);
    }
    QVERIFY(small.statistics().entries == 30); // each is a tenth of the big tree
    QVERIFY(scanAndWait(&smallScanner, m_big.path()).finished);
    QVERIFY(small.statistics().bytes <= small.limits().memoryBytes);
    QVERIFY(small.statistics().evictions > 0);
}

void WhaleTreeCacheTest::testContextsOfAWindowShareTheCache()
{
    GeneralSettings::self()->setRememberOpenedTabs(false);
    DolphinMainWindow window;
    window.openDirectories({QUrl::fromLocalFile(m_big.path())}, false);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.actionCollection()->action(QStringLiteral("split_view"))->trigger();
    DolphinTabPage *page = window.findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"))->currentTabPage();
    WhaleViewRegistry registry;
    WhaleViewPlugin *plugin = registry.plugin(testListId);
    QVERIFY(plugin);
    WhaleViewHost *left = page->primaryViewContainer()->viewHost();
    WhaleViewHost *right = page->secondaryViewContainer()->viewHost();
    QVERIFY(left->showPluginView(testListId, plugin));
    QVERIFY(right->showPluginView(testListId, plugin));

    WhaleTreeCache *cache = window.findChild<WhaleViewActions *>()->treeCache();
    QVERIFY(cache);
    WhaleTreeScanner *a = left->pluginContext()->treeScanner();
    WhaleTreeScanner *b = right->pluginContext()->treeScanner();
    QVERIFY(a != b);
    QSignalSpy finishedA(a, &WhaleTreeScanner::finished);
    QSignalSpy finishedB(b, &WhaleTreeScanner::finished);
    QVERIFY(a->scan(QUrl::fromLocalFile(m_big.path())));
    QVERIFY(b->scan(QUrl::fromLocalFile(m_big.path())));
    QTRY_VERIFY_WITH_TIMEOUT(finishedA.count() == 1 && finishedB.count() == 1, 30000);
    QCOMPARE(cache->statistics().misses, 1);
    QCOMPARE(cache->statistics().joins, 1);

    // A view switch destroys a context; the kept tree stays for the other view.
    left->showDolphinView();
    QVERIFY(b->scan(QUrl::fromLocalFile(m_big.path())));
    QTRY_COMPARE(finishedB.count(), 2);
    QCOMPARE(cache->statistics().hits, 1);
}

void WhaleTreeCacheTest::testMeasure()
{
    // Optional, for rough measurements of a bigger tree prepared elsewhere.
    const QString root = qEnvironmentVariable("WHALE_TREESCANNER_MEASURE");
    if (root.isEmpty()) {
        QSKIP("set WHALE_TREESCANNER_MEASURE to a folder to measure");
    }
    WhaleTreeCache cache(WhaleTreeCache::configuredLimits());
    WhaleTreeScannerImpl first(&cache);
    WhaleTreeScannerImpl second(&cache);
    const Result scanned = scanAndWait(&first, root);
    QVERIFY(scanned.finished);
    const WhaleTreeCache::Statistics statistics = cache.statistics();
    const Result again = scanAndWait(&first, root);
    const Result other = scanAndWait(&second, root);
    QVERIFY(again.finished && other.finished);
    QCOMPARE(cache.statistics().hits, 2);
    // Any change drops it; the next scan is a real one.
    writeFile(root + QStringLiteral("/whale-measure-change.txt"), 1);
    QTRY_COMPARE_WITH_TIMEOUT(cache.statistics().entries, 0, 10000);
    const Result rescanned = scanAndWait(&first, root);
    QFile::remove(root + QStringLiteral("/whale-measure-change.txt"));
    QVERIFY(rescanned.finished);
    qInfo() << scanned.root.fileCount() << "files," << scanned.root.directoryCount() << "folders: first scan" << scanned.elapsed / 1000 << "ms, again"
            << again.elapsed << "us, from a second scanner" << other.elapsed << "us, after a change" << rescanned.elapsed / 1000 << "ms;"
            << statistics.bytes / 1024 << "KiB estimated," << statistics.watchedFolders << "watched folders, entries" << statistics.entries;
}

QTEST_MAIN(WhaleTreeCacheTest)

#include "whaletreecachetest.moc"
