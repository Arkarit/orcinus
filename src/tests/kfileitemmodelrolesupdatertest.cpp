/*
 * SPDX-FileCopyrightText: 2026 Méven Car <meven@kde.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kitemviews/kfileitemmodelrolesupdater.h"
#include "kitemviews/kfileitemmodel.h"
#include "testdir.h"

#include <KIO/CopyJob>
#include <KSambaShare>

#include <QImage>

#include <QSignalSpy>
#include <QTest>

#include "kitemviews/private/kdirectorycontentscounterworker.h"

class KFileItemModelRolesUpdaterTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testSharesChangedUpdatesOverlays();
    void testDirectoryContentsCounterWorkerDoesNotChangeCwd();
    void testItemChangedWhilePausedGetsAPreview();
};

void KFileItemModelRolesUpdaterTest::testSharesChangedUpdatesOverlays()
{
#ifdef Q_OS_WIN
    QSKIP("KFileItem::overlays() does not look at the samba shares on Windows");
#endif
    TestDir testDir;
    testDir.createDir(QStringLiteral("folder"));

    KFileItemModel model;
    KFileItemModelRolesUpdater updater(&model);
    updater.setRoles({"text", "type"});
    QSignalSpy loadingCompletedSpy(&model, &KFileItemModel::directoryLoadingCompleted);
    model.loadDirectory(testDir.url());
    QVERIFY(loadingCompletedSpy.wait());
    QCOMPARE(model.count(), 1);
    updater.setVisibleIndexRange(0, 1);
    // rolesData() resolved the folder, which connects to KSambaShare.
    QTRY_VERIFY(model.data(0).contains("type"));

    // An overlay that KSambaShare no longer reports, written without the updater seeing it.
    SmallHash data = model.data(0);
    data.insert("iconOverlays", QStringList{QStringLiteral("emblem-shared")});
    model.blockSignals(true);
    model.setData(0, data);
    model.blockSignals(false);

    KSambaShare *const shares = KSambaShare::instance();
    Q_EMIT shares->changed();
    QTRY_VERIFY(!model.data(0).value("iconOverlays").toStringList().contains(QStringLiteral("emblem-shared")));
}

void KFileItemModelRolesUpdaterTest::testDirectoryContentsCounterWorkerDoesNotChangeCwd()
{
#ifdef Q_OS_WIN
    QSKIP("fts is not used on Windows");
#endif
    TestDir testDir;
    testDir.createDir(QStringLiteral("dir1/dir2/dir3"));
    testDir.createFile(QStringLiteral("dir1/dir2/dir3/file1.txt"), "content1");
    testDir.createFile(QStringLiteral("dir1/file2.txt"), "content2");

    const QString initialCwd = QDir::currentPath();

    KDirectoryContentsCounterWorker worker;
    QSignalSpy resultSpy(&worker, &KDirectoryContentsCounterWorker::result);
    QSignalSpy intermediateSpy(&worker, &KDirectoryContentsCounterWorker::intermediateResult);

    QString intermediateCwd;
    connect(&worker, &KDirectoryContentsCounterWorker::intermediateResult, this, [&intermediateCwd]() {
        intermediateCwd = QDir::currentPath();
    });

    worker.countDirectoryContents(testDir.url().toLocalFile(), KDirectoryContentsCounterWorker::NoOptions, 10);

    QCOMPARE(resultSpy.count(), 1);
    const QList<QVariant> arguments = resultSpy.takeFirst();
    QCOMPARE(arguments.at(1).toInt(), 1); // 1 item directly inside testDir (dir1)
    if (!intermediateCwd.isEmpty()) {
        QCOMPARE(intermediateCwd, initialCwd);
    }
    QCOMPARE(QDir::currentPath(), initialCwd);
}

// An item that changes while the updater is paused, e.g. a file renamed while the view scrolls
// to it, gets its roles and its preview again once the updater goes on, also when all items had
// been resolved before. Otherwise a preview that failed for the old name is never replaced.
void KFileItemModelRolesUpdaterTest::testItemChangedWhilePausedGetsAPreview()
{
    TestDir testDir;
    QImage image(64, 64, QImage::Format_RGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(testDir.path() + QStringLiteral("/a.png")));

    KFileItemModel model;
    KFileItemModelRolesUpdater updater(&model);
    updater.setPreviewsShown(true);
    updater.setIconSize(QSize(128, 128));
    QSignalSpy loadingCompletedSpy(&model, &KFileItemModel::directoryLoadingCompleted);
    model.loadDirectory(testDir.url());
    QVERIFY(loadingCompletedSpy.wait());
    QCOMPARE(model.count(), 1);
    QSignalSpy previewSpy(&updater, &KFileItemModelRolesUpdater::previewJobFinished);
    updater.setVisibleIndexRange(0, 1);
    QTRY_VERIFY(!model.data(0).value("iconPixmap").value<QPixmap>().isNull());
    // All items are resolved now.
    QTRY_COMPARE(updater.m_finishedItems.count(), model.count());

    updater.setPaused(true);
    const QUrl renamedUrl = QUrl::fromLocalFile(testDir.path() + QStringLiteral("/b.png"));
    KIO::CopyJob *job = KIO::moveAs(model.fileItem(0).url(), renamedUrl, KIO::HideProgressInfo);
    QVERIFY(job->exec());
    QTRY_VERIFY(model.index(renamedUrl) >= 0);
    // Changes noticed while paused are resolved once the updater goes on.
    QTRY_VERIFY(updater.m_changedItems.contains(model.fileItem(0)));

    previewSpy.clear();
    updater.setPaused(false);
    QVERIFY(previewSpy.wait());
    QCOMPARE(model.fileItem(0).name(), QStringLiteral("b.png"));
    QVERIFY(!model.data(0).value("iconPixmap").value<QPixmap>().isNull());
}

QTEST_MAIN(KFileItemModelRolesUpdaterTest)

#include "kfileitemmodelrolesupdatertest.moc"
