/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config-dolphin.h"
#include "dolphin_generalsettings.h"
#include "dolphinmainwindow.h"
#include "dolphintabpage.h"
#include "dolphintabwidget.h"
#include "dolphinviewbridge.h"
#include "dolphinviewcontainer.h"
#include "testdir.h"
#include "testhelpers.h"
#include "views/dolphinview.h"
#include "whalepreviewprovider.h"
#include "whalepreviewproviderimpl.h"
#include "whaleview.h"
#include "whaleviewcontextimpl.h"
#include "whaleviewhost.h"
#include "whaleviewregistry.h"

#include <KActionCollection>
#include <KLocalizedString>

#include <QBuffer>
#include <QImage>
#include <QListView>
#include <QPointer>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

namespace
{
const QString testListId = QStringLiteral("whale_testlist");
const QString helloId = QStringLiteral("whale_hello");

QByteArray png(const QColor &color)
{
    QImage image(64, 48, QImage::Format_RGB32);
    image.fill(color);
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return data;
}

/// The rows of the test list that show a preview (a QPixmap instead of the QIcon).
QStringList previewRows(const QListView *listView)
{
    QStringList result;
    const QAbstractItemModel *model = listView->model();
    for (int row = 0; row < model->rowCount(); ++row) {
        if (model->index(row, 0).data(Qt::DecorationRole).typeId() == QMetaType::QPixmap) {
            result.append(model->index(row, 0).data().toString());
        }
    }
    return result;
}

/// The rows the test list shows, first and last.
std::pair<int, int> visibleRows(const QListView *listView)
{
    const QRect area = listView->viewport()->rect();
    const QModelIndex first = listView->indexAt(area.topLeft() + QPoint(1, 1));
    const QModelIndex last = listView->indexAt(QPoint(1, area.bottom() - 1));
    return {first.row(), last.isValid() ? last.row() : listView->model()->rowCount() - 1};
}
}

/**
 * M7: WhalePreviewProvider and the previews of the test list, with real thumbnails
 * of KIO's image thumbnailer in a thumbnail cache of the test.
 */
class WhalePreviewTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testSingleRequest();
    void testSeveralItems();
    void testUnsupportedFile();
    void testCancel();
    void testViewDestroyedWhileRunning();
    void testVisibleRange();
    void testPreviewSetting();
    void testNoPreviewsWithoutCapability();
    void testSwitchViews();
    void testSplitView();

private:
    DolphinViewContainer *showMainWindow(const QUrl &url, int count);
    QListView *showTestList(DolphinViewContainer *container);
    WhalePreviewProviderImpl *provider(DolphinViewContainer *container) const;
    /// A provider of its own on the view area, as a view with Previews gets it, without
    /// the requests of a view.
    std::unique_ptr<WhalePreviewProviderImpl> standaloneProvider(DolphinViewContainer *container) const;
    QAction *showPreviewAction() const;
    QUrl url(const QString &name) const;

    QTemporaryDir m_tmpHome;
    QScopedPointer<TestDir> m_dir;
    QScopedPointer<DolphinMainWindow> m_mainWindow;
    WhaleViewRegistry m_registry;
};

void WhalePreviewTest::initTestCase()
{
    QVERIFY(m_tmpHome.isValid());
    QCoreApplication::setApplicationName(QStringLiteral(WHALE_COMPONENT));
    QCoreApplication::setOrganizationDomain(QStringLiteral(WHALE_ORG_DOMAIN));
    qputenv("HOME", m_tmpHome.path().toUtf8());
    // The thumbnail cache of the test (the thumbnail worker is another process and
    // does not know QStandardPaths' test mode).
    qputenv("XDG_CACHE_HOME", m_tmpHome.filePath(QStringLiteral("cache")).toUtf8());
    QStandardPaths::setTestModeEnabled(true);
    TestHelpers::disableAnimations();
    TestHelpers::openNewWindowsInSingleView();
    KLocalizedString::setApplicationDomain(TRANSLATION_DOMAIN);
    QCoreApplication::addLibraryPath(QStringLiteral(WHALE_TEST_PLUGIN_DIR));
    QCoreApplication::addLibraryPath(QStringLiteral(WHALE_TEST_VIEW_PLUGIN_DIR));
    GeneralSettings::self()->setRememberOpenedTabs(false);
    GeneralSettings::self()->save();
}

void WhalePreviewTest::init()
{
    m_dir.reset(new TestDir());
    m_dir->createFile(QStringLiteral("blue.png"), png(Qt::blue));
    m_dir->createFile(QStringLiteral("green.png"), png(Qt::green));
    m_dir->createFile(QStringLiteral("red.png"), png(Qt::red));
    // No thumbnailer handles arbitrary binary data.
    m_dir->createFile(QStringLiteral("data.bin"), QByteArray("\x00\x01\x02\x03\xff\xfe", 6));
    m_mainWindow.reset(new DolphinMainWindow());
}

void WhalePreviewTest::cleanup()
{
    m_mainWindow.reset();
    m_dir.reset();
}

DolphinViewContainer *WhalePreviewTest::showMainWindow(const QUrl &url, int count)
{
    m_mainWindow->openDirectories({url}, false);
    m_mainWindow->resize(1000, 600);
    m_mainWindow->show();
    if (!QTest::qWaitForWindowActive(m_mainWindow.data())) {
        return nullptr;
    }
    DolphinViewContainer *container = m_mainWindow->activeViewContainer();
    container->view()->setHiddenFilesShown(false);
    container->view()->setSortRole("text");
    container->view()->setSortOrder(Qt::AscendingOrder);
    container->view()->setPreviewsShown(true);
    if (!QTest::qWaitFor(
            [container, count]() {
                return container->view()->itemsCount() == count;
            },
            5000)) {
        return nullptr;
    }
    return container;
}

QListView *WhalePreviewTest::showTestList(DolphinViewContainer *container)
{
    WhaleViewPlugin *plugin = m_registry.plugin(testListId);
    if (!plugin || !container->viewHost()->showPluginView(testListId, plugin)) {
        return nullptr;
    }
    return qobject_cast<QListView *>(container->viewHost()->currentView()->widget());
}

WhalePreviewProviderImpl *WhalePreviewTest::provider(DolphinViewContainer *container) const
{
    WhaleViewContextImpl *context = container->viewHost()->pluginContext();
    return context ? static_cast<WhalePreviewProviderImpl *>(context->previews()) : nullptr;
}

std::unique_ptr<WhalePreviewProviderImpl> WhalePreviewTest::standaloneProvider(DolphinViewContainer *container) const
{
    auto previews = std::make_unique<WhalePreviewProviderImpl>(container->viewHost()->bridge());
    previews->setViewSupportsPreviews(true);
    return previews;
}

QAction *WhalePreviewTest::showPreviewAction() const
{
    return m_mainWindow->actionCollection()->action(QStringLiteral("show_preview"));
}

QUrl WhalePreviewTest::url(const QString &name) const
{
    return QUrl::fromLocalFile(m_dir->path() + QLatin1Char('/') + name);
}

void WhalePreviewTest::testSingleRequest()
{
    DolphinViewContainer *container = showMainWindow(m_dir->url(), 4);
    QVERIFY(container);
    const auto provider = standaloneProvider(container);
    WhalePreviewProvider *previews = provider.get();
    QVERIFY(previews->isEnabled());

    QSignalSpy available(previews, &WhalePreviewProvider::previewAvailable);
    QSignalSpy failed(previews, &WhalePreviewProvider::previewFailed);
    const KFileItem item(url(QStringLiteral("blue.png")));
    previews->requestPreviews({item}, QSize(32, 32));
    QVERIFY(previews->isPending(item.url()));
    // Asynchronous: nothing has been answered within the call.
    QCOMPARE(available.count(), 0);
    QVERIFY(available.wait(10000));
    QCOMPARE(available.count(), 1);
    QCOMPARE(available.first().at(0).value<KFileItem>().url(), item.url());
    const QPixmap preview = available.first().at(1).value<QPixmap>();
    QVERIFY(!preview.isNull());
    QVERIFY(preview.width() <= 32 && preview.height() <= 32);
    QVERIFY(!previews->isPending(item.url()));
    QCOMPARE(failed.count(), 0);
}

void WhalePreviewTest::testSeveralItems()
{
    DolphinViewContainer *container = showMainWindow(m_dir->url(), 4);
    QVERIFY(container);
    const auto provider = standaloneProvider(container);
    WhalePreviewProviderImpl *previews = provider.get();
    QSignalSpy available(previews, &WhalePreviewProvider::previewAvailable);

    // Two requests at the same time, one of them repeated: each item is answered once.
    previews->requestPreviews({KFileItem(url(QStringLiteral("blue.png"))), KFileItem(url(QStringLiteral("green.png")))}, QSize(32, 32));
    previews->requestPreviews({KFileItem(url(QStringLiteral("red.png"))), KFileItem(url(QStringLiteral("green.png")))}, QSize(32, 32));
    QTRY_COMPARE_WITH_TIMEOUT(available.count(), 3, 10000);
    QStringList names;
    for (const QList<QVariant> &signal : std::as_const(available)) {
        names.append(signal.at(0).value<KFileItem>().url().fileName());
    }
    names.sort();
    QCOMPARE(names, QStringList({QStringLiteral("blue.png"), QStringLiteral("green.png"), QStringLiteral("red.png")}));
    QTest::qWait(200);
    QCOMPARE(available.count(), 3);
    QCOMPARE(previews->runningJobs(), 0);
}

void WhalePreviewTest::testUnsupportedFile()
{
    DolphinViewContainer *container = showMainWindow(m_dir->url(), 4);
    QVERIFY(container);
    const auto provider = standaloneProvider(container);
    WhalePreviewProvider *previews = provider.get();
    QSignalSpy failed(previews, &WhalePreviewProvider::previewFailed);
    QSignalSpy available(previews, &WhalePreviewProvider::previewAvailable);
    previews->requestPreviews({KFileItem(url(QStringLiteral("data.bin")))}, QSize(32, 32));
    QVERIFY(failed.wait(10000));
    QCOMPARE(failed.first().at(0).value<KFileItem>().url(), url(QStringLiteral("data.bin")));
    QCOMPARE(available.count(), 0);

    // The test list shows the icon for it and thumbnails for the images.
    QListView *listView = showTestList(container);
    QVERIFY(listView);
    QTRY_COMPARE_WITH_TIMEOUT(previewRows(listView), QStringList({QStringLiteral("blue.png"), QStringLiteral("green.png"), QStringLiteral("red.png")}), 10000);
}

void WhalePreviewTest::testCancel()
{
    for (int i = 0; i < 30; ++i) {
        m_dir->createFile(QStringLiteral("many/%1.png").arg(i, 2, 10, QLatin1Char('0')), png(QColor::fromHsv(i * 12, 200, 200)));
    }
    DolphinViewContainer *container = showMainWindow(m_dir->url(), 5);
    QVERIFY(container);
    const auto provider = standaloneProvider(container);
    WhalePreviewProviderImpl *previews = provider.get();
    QSignalSpy available(previews, &WhalePreviewProvider::previewAvailable);
    QSignalSpy failed(previews, &WhalePreviewProvider::previewFailed);
    const WhalePreviewProviderImpl::Statistics before = previews->statistics();

    KFileItemList items;
    for (int i = 0; i < 30; ++i) {
        items.append(KFileItem(QUrl::fromLocalFile(m_dir->path() + QStringLiteral("/many/%1.png").arg(i, 2, 10, QLatin1Char('0')))));
    }
    previews->requestPreviews(items, QSize(64, 64));
    QCOMPARE(previews->pendingUrls().count(), 30);
    // Canceling some leaves the others.
    previews->cancel({items.at(0).url(), items.at(1).url()});
    QCOMPARE(previews->pendingUrls().count(), 28);
    QVERIFY(!previews->isPending(items.at(0).url()));
    previews->cancelAll();
    QCOMPARE(previews->pendingUrls().count(), 0);
    QCOMPARE(previews->runningJobs(), 0);

    // No answer for canceled requests, and the job is stopped.
    QTest::qWait(1000);
    QCOMPARE(available.count(), 0);
    QCOMPARE(failed.count(), 0);
    const WhalePreviewProviderImpl::Statistics after = previews->statistics();
    QCOMPARE(after.canceled - before.canceled, 30);
    QCOMPARE(after.jobsKilled - before.jobsKilled, 1);
}

void WhalePreviewTest::testViewDestroyedWhileRunning()
{
    for (int i = 0; i < 40; ++i) {
        m_dir->createFile(QStringLiteral("img%1.png").arg(i, 2, 10, QLatin1Char('0')), png(QColor::fromHsv(i * 9, 200, 200)));
    }
    DolphinViewContainer *container = showMainWindow(m_dir->url(), 44);
    QVERIFY(container);
    QVERIFY(showTestList(container));
    QPointer<WhalePreviewProviderImpl> previews = provider(container);
    // The test list requests the previews of the rows in sight.
    QTRY_VERIFY(previews->runningJobs() > 0);

    // Switching views destroys the view, its context and the provider with running jobs.
    container->viewHost()->showDolphinView();
    QVERIFY(previews.isNull());
    QTest::qWait(500);
    QVERIFY(showTestList(container));
    previews = provider(container);
    QTRY_VERIFY(previews->runningJobs() > 0);

    // So does closing the window.
    m_mainWindow.reset();
    QVERIFY(previews.isNull());
    QTest::qWait(500);
}

void WhalePreviewTest::testVisibleRange()
{
    TestDir big;
    for (int i = 0; i < 300; ++i) {
        big.createFile(QStringLiteral("p%1.png").arg(i, 3, 10, QLatin1Char('0')), png(QColor::fromHsv(i % 360, 180, 220)));
    }
    DolphinViewContainer *container = showMainWindow(big.url(), 300);
    QVERIFY(container);
    QListView *listView = showTestList(container);
    QVERIFY(listView);
    WhalePreviewProviderImpl *previews = provider(container);

    // The rows in sight get their previews.
    auto [first, last] = visibleRows(listView);
    QVERIFY(first == 0 && last > 3 && last < 100);
    const QString lastName = listView->model()->index(last, 0).data().toString();
    QTRY_VERIFY_WITH_TIMEOUT(previewRows(listView).contains(lastName), 20000);
    QTRY_COMPARE_WITH_TIMEOUT(previews->runningJobs(), 0, 20000);
    // Only them and one page below: no work for the whole folder.
    const int page = last - first + 1;
    QVERIFY2(previews->statistics().requested <= 2 * page + 1, qPrintable(QString::number(previews->statistics().requested)));
    QVERIFY(previews->statistics().maxPending <= 2 * page + 1);
    QVERIFY(!previewRows(listView).contains(QStringLiteral("p299.png")));

    // Scrolling to the end: the new rows get previews, the old ones are dropped.
    listView->scrollToBottom();
    std::tie(first, last) = visibleRows(listView);
    QCOMPARE(last, 299);
    QTRY_VERIFY_WITH_TIMEOUT(previewRows(listView).contains(QStringLiteral("p299.png")), 20000);
    QTRY_VERIFY_WITH_TIMEOUT(previewRows(listView).contains(listView->model()->index(first, 0).data().toString()), 20000);
    QVERIFY(!previewRows(listView).contains(QStringLiteral("p000.png")));
    QTRY_COMPARE_WITH_TIMEOUT(previews->runningJobs(), 0, 20000);
    QVERIFY2(previews->statistics().requested < 300, qPrintable(QString::number(previews->statistics().requested)));

    // Scrolling quickly through the folder: requests of rows that went out of sight are
    // canceled, never more than about three pages are pending.
    for (int value = listView->verticalScrollBar()->maximum(); value >= 0; value -= listView->verticalScrollBar()->pageStep()) {
        listView->verticalScrollBar()->setValue(value);
        QCoreApplication::processEvents();
    }
    listView->scrollToTop();
    QTRY_VERIFY_WITH_TIMEOUT(previewRows(listView).contains(QStringLiteral("p000.png")), 20000);
    QTRY_COMPARE_WITH_TIMEOUT(previews->runningJobs(), 0, 20000);
    QVERIFY2(previews->statistics().maxPending <= 3 * page + 1, qPrintable(QString::number(previews->statistics().maxPending)));
    for (const QUrl &url : previews->pendingUrls()) {
        QFAIL(qPrintable(QStringLiteral("still pending: ") + url.toString()));
    }
}

void WhalePreviewTest::testPreviewSetting()
{
    DolphinViewContainer *container = showMainWindow(m_dir->url(), 4);
    QVERIFY(container);
    QListView *listView = showTestList(container);
    QVERIFY(listView);
    WhalePreviewProvider *previews = provider(container);
    const QStringList images = {QStringLiteral("blue.png"), QStringLiteral("green.png"), QStringLiteral("red.png")};
    QTRY_COMPARE_WITH_TIMEOUT(previewRows(listView), images, 10000);
    const QSize previewIconSize = listView->iconSize();

    // Dolphin's "Show Previews" action works on the test list: off shows the icons again.
    QAction *action = showPreviewAction();
    QVERIFY(action->isEnabled());
    QVERIFY(action->isChecked());
    QSignalSpy enabled(previews, &WhalePreviewProvider::enabledChanged);
    action->trigger();
    QVERIFY(!container->view()->previewsShown());
    QCOMPARE(enabled.count(), 1);
    QVERIFY(!previews->isEnabled());
    QCOMPARE(previewRows(listView), QStringList());
    QVERIFY(listView->iconSize() != previewIconSize);
    // Requests are not served while off.
    previews->requestPreviews({KFileItem(url(QStringLiteral("blue.png")))}, QSize(32, 32));
    QVERIFY(!previews->isPending(url(QStringLiteral("blue.png"))));

    action->trigger();
    QVERIFY(previews->isEnabled());
    QCOMPARE(listView->iconSize(), previewIconSize);
    QTRY_COMPARE_WITH_TIMEOUT(previewRows(listView), images, 10000);
}

void WhalePreviewTest::testNoPreviewsWithoutCapability()
{
    DolphinViewContainer *container = showMainWindow(m_dir->url(), 4);
    QVERIFY(container);
    WhaleViewPlugin *hello = m_registry.plugin(helloId);
    QVERIFY(hello && container->viewHost()->showPluginView(helloId, hello));
    QVERIFY(!(container->viewHost()->currentView()->capabilities() & WhaleViewCapability::Previews));

    // Even a view that asks gets no preview work without the capability.
    WhalePreviewProviderImpl *previews = provider(container);
    QVERIFY(!previews->isEnabled());
    previews->requestPreviews({KFileItem(url(QStringLiteral("blue.png")))}, QSize(32, 32));
    QCOMPARE(previews->statistics().requested, 0);
    QCOMPARE(previews->runningJobs(), 0);
    QVERIFY(!showPreviewAction()->isEnabled());
}

void WhalePreviewTest::testSwitchViews()
{
    for (int i = 0; i < 40; ++i) {
        m_dir->createFile(QStringLiteral("img%1.png").arg(i, 2, 10, QLatin1Char('0')), png(QColor::fromHsv(i * 9, 200, 200)));
    }
    DolphinViewContainer *container = showMainWindow(m_dir->url(), 44);
    QVERIFY(container);
    WhaleViewHost *host = container->viewHost();
    QList<QPointer<WhalePreviewProviderImpl>> providers;
    for (int i = 0; i < 10; ++i) {
        QVERIFY(showTestList(container));
        providers.append(provider(container));
        QTest::qWait(i * 20);
        host->showDolphinView();
    }
    for (const auto &previews : std::as_const(providers)) {
        QVERIFY(previews.isNull());
    }
    // The file list is unaffected, and a new test list gets its previews.
    QCOMPARE(container->view()->itemsCount(), 44);
    QListView *listView = showTestList(container);
    QVERIFY(listView);
    QTRY_VERIFY_WITH_TIMEOUT(previewRows(listView).contains(QStringLiteral("blue.png")), 10000);
}

void WhalePreviewTest::testSplitView()
{
    TestDir other;
    other.createFile(QStringLiteral("other.png"), png(Qt::yellow));
    other.createFile(QStringLiteral("text.bin"), QByteArray("\x00\x01", 2));
    DolphinViewContainer *left = showMainWindow(m_dir->url(), 4);
    QVERIFY(left);
    m_mainWindow->actionCollection()->action(QStringLiteral("split_view"))->trigger();
    DolphinTabPage *page = m_mainWindow->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"))->currentTabPage();
    DolphinViewContainer *right = page->secondaryViewContainer();
    QVERIFY(right);
    right->setUrl(other.url());
    QTRY_COMPARE(right->view()->itemsCount(), 2);
    right->view()->setPreviewsShown(true);

    QListView *leftList = showTestList(left);
    QListView *rightList = showTestList(right);
    QVERIFY(leftList && rightList);
    QTRY_COMPARE_WITH_TIMEOUT(previewRows(leftList), QStringList({QStringLiteral("blue.png"), QStringLiteral("green.png"), QStringLiteral("red.png")}), 10000);
    QTRY_COMPARE_WITH_TIMEOUT(previewRows(rightList), QStringList({QStringLiteral("other.png")}), 10000);
    // Each view's provider worked only for its own location.
    QCOMPARE(provider(left)->statistics().requested, 4);
    QCOMPARE(provider(right)->statistics().requested, 2);
    QVERIFY(provider(left) != provider(right));
}

QTEST_MAIN(WhalePreviewTest)

#include "whalepreviewtest.moc"
