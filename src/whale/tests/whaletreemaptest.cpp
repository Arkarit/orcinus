/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
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
#include "whalefileoperationsimpl.h"
#include "whaletreecache.h"
#include "whaleview.h"
#include "whaleviewactions.h"
#include "whaleviewapiversion.h"
#include "whaleviewcontextimpl.h"
#include "whaleviewhost.h"
#include "whaleviewplugin.h"
#include "whaleviewregistry.h"

#include <KActionCollection>
#include <KConfig>
#include <KConfigGroup>
#include <KLocalizedString>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDrag>
#include <QDropEvent>
#include <QListView>
#include <QMimeData>
#include <QSignalSpy>
#include <QSplitter>
#include <QStandardPaths>
#include <QTest>
#include <QToolButton>

namespace
{
const QString treemapId = QStringLiteral("whale_treemap");
const QString testListId = QStringLiteral("whale_testlist");

QVariantMap treemapState(QWidget *view)
{
    QVariantMap state;
    QMetaObject::invokeMethod(view, "treemapState", Qt::DirectConnection, Q_RETURN_ARG(QVariantMap, state));
    return state;
}

QRect cellRect(QWidget *view, const QUrl &url)
{
    QRect rect;
    QMetaObject::invokeMethod(view, "cellRect", Qt::DirectConnection, Q_RETURN_ARG(QRect, rect), Q_ARG(QUrl, url));
    return rect;
}

QUrl urlAt(QWidget *view, const QPoint &position)
{
    QUrl url;
    QMetaObject::invokeMethod(view, "urlAt", Qt::DirectConnection, Q_RETURN_ARG(QUrl, url), Q_ARG(QPoint, position));
    return url;
}

QWidget *canvasOf(QWidget *view)
{
    return view->findChild<QWidget *>(QStringLiteral("whaleTreemapCanvas"));
}

QString status(QWidget *view)
{
    return treemapState(view).value(QStringLiteral("status")).toString();
}
}

class WhaleTreemapTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testDiscovery();
    void testSupportsUrl();
    void testNonLocalFallsBack();
    void testScan();
    void testCacheHit();
    void testInvalidation();
    void testLoadingShowsNoOldTree();
    void testFailure();
    void testHover();
    void testSelection();
    void testDeepSelectionRoutesActions();
    void testKeyboard();
    void testNavigation();
    void testZoom();
    void testViewSwitchKeepsState();
    void testHistoryRestore();
    void testSessionRestore();
    void testSplitView();
    void testResize();
    void testCapabilityGating();
    void testDropsAreRefused();

private:
    DolphinViewContainer *showMainWindow(int expectedItems = 4);
    QWidget *showTreemap(DolphinViewContainer *container);
    WhaleViewActions *viewActions() const;
    QAction *action(const QString &name) const;
    QUrl url(const QString &path) const;
    /// Clicks into the cell of @p target and goes up (Up key) until it is selected.
    bool select(QWidget *view, const QUrl &target);

    QTemporaryDir m_tmpHome;
    QScopedPointer<TestDir> m_dir;
    QList<WhaleFileOperations::Operation> m_confirmed;
    QScopedPointer<DolphinMainWindow> m_mainWindow;
};

void WhaleTreemapTest::initTestCase()
{
    QVERIFY(m_tmpHome.isValid());
    QCoreApplication::setApplicationName(QStringLiteral(WHALE_COMPONENT));
    QCoreApplication::setOrganizationDomain(QStringLiteral(WHALE_ORG_DOMAIN));
    qputenv("HOME", m_tmpHome.path().toUtf8());
    qputenv("XDG_DATA_HOME", m_tmpHome.filePath(QStringLiteral("data")).toUtf8());
    QStandardPaths::setTestModeEnabled(true);
    TestHelpers::disableAnimations();
    KLocalizedString::setApplicationDomain({"whale"});
    QCoreApplication::addLibraryPath(QStringLiteral(WHALE_TEST_PLUGIN_DIR));
    QCoreApplication::addLibraryPath(QStringLiteral(WHALE_TEST_VIEW_PLUGIN_DIR));
    // Deleting must not ask, so a Delete that got through would really delete.
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    QVERIFY(QDir().mkpath(config));
    QFile kiorc(config + QStringLiteral("/kiorc"));
    QVERIFY(kiorc.open(QIODevice::WriteOnly));
    kiorc.write("[Confirmations]\nConfirmDelete=false\nConfirmTrash=false\n");
    kiorc.close();
    GeneralSettings::self()->setRememberOpenedTabs(false);
    GeneralSettings::self()->save();
}

void WhaleTreemapTest::init()
{
    m_dir.reset(new TestDir());
    m_dir->createFile(QStringLiteral("big.bin"), QByteArray(60000, 'b'));
    m_dir->createFile(QStringLiteral("docs/report.pdf"), QByteArray(20000, 'r'));
    m_dir->createFile(QStringLiteral("docs/notes/a.txt"), QByteArray(3000, 'a'));
    m_dir->createFile(QStringLiteral("docs/notes/b.txt"), QByteArray(2000, 'b'));
    m_dir->createFile(QStringLiteral("media/photos/p1.jpg"), QByteArray(30000, 'p'));
    m_dir->createFile(QStringLiteral("media/photos/p2.jpg"), QByteArray(25000, 'q'));
    m_dir->createFile(QStringLiteral("media/song.flac"), QByteArray(15000, 's'));
    m_dir->createFile(QStringLiteral("empty.txt"), QByteArray());
    m_dir->createFile(QStringLiteral(".hidden"), QByteArray(5000, 'h'));
    // Removing and cutting in a treemap are always confirmed: confirmed here, recorded.
    m_confirmed.clear();
    WhaleFileOperationsImpl::setConfirmationRunner([this](WhaleFileOperations::Operation operation, const QList<QUrl> &) {
        m_confirmed.append(operation);
        return true;
    });
    m_mainWindow.reset(new DolphinMainWindow());
}

void WhaleTreemapTest::cleanup()
{
    WhaleFileOperationsImpl::setConfirmationRunner({});
    m_mainWindow.reset();
    if (m_dir) {
        QFile::setPermissions(m_dir->path() + QStringLiteral("/locked"), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    }
    m_dir.reset();
}

DolphinViewContainer *WhaleTreemapTest::showMainWindow(int expectedItems)
{
    m_mainWindow->openDirectories({m_dir->url()}, false);
    m_mainWindow->resize(1000, 700);
    m_mainWindow->show();
    if (!QTest::qWaitForWindowActive(m_mainWindow.data())) {
        return nullptr;
    }
    DolphinViewContainer *container = m_mainWindow->activeViewContainer();
    container->view()->setHiddenFilesShown(false);
    if (!QTest::qWaitFor(
            [container, expectedItems]() {
                return container->view()->itemsCount() == expectedItems;
            },
            5000)) {
        return nullptr;
    }
    return container;
}

QWidget *WhaleTreemapTest::showTreemap(DolphinViewContainer *container)
{
    WhaleViewPlugin *plugin = viewActions()->registry()->plugin(treemapId);
    if (!plugin || !container->viewHost()->showPluginView(treemapId, plugin)) {
        return nullptr;
    }
    QWidget *view = container->viewHost()->currentView()->widget();
    if (!QTest::qWaitFor(
            [view]() {
                return status(view) == QLatin1String("ready");
            },
            5000)) {
        return nullptr;
    }
    return view;
}

WhaleViewActions *WhaleTreemapTest::viewActions() const
{
    return m_mainWindow->findChild<WhaleViewActions *>();
}

QAction *WhaleTreemapTest::action(const QString &name) const
{
    return m_mainWindow->actionCollection()->action(name);
}

QUrl WhaleTreemapTest::url(const QString &path) const
{
    return QUrl::fromLocalFile(m_dir->path() + (path.isEmpty() ? QString() : QLatin1Char('/') + path));
}

bool WhaleTreemapTest::select(QWidget *view, const QUrl &target)
{
    const QRect rect = cellRect(view, target);
    if (rect.isEmpty()) {
        return false;
    }
    QTest::mouseClick(canvasOf(view), Qt::LeftButton, {}, rect.center());
    for (int step = 0; step < 10; ++step) {
        const QUrl selected(treemapState(view).value(QStringLiteral("selected")).toString());
        if (selected == target) {
            return true;
        }
        QTest::keyClick(canvasOf(view), Qt::Key_Up);
    }
    return false;
}

void WhaleTreemapTest::testDiscovery()
{
    QVERIFY(showMainWindow());
    WhaleViewRegistry *registry = viewActions()->registry();
    const KPluginMetaData metaData = registry->pluginMetaData(treemapId);
    QVERIFY(metaData.isValid());
    QCOMPARE(metaData.name(), QStringLiteral("Cushion Treemap"));
    QVERIFY(!metaData.description().isEmpty());
    QCOMPARE(metaData.iconName(), QStringLiteral("folder-chart"));
    QCOMPARE(metaData.rawData().value(QLatin1String(WHALE_VIEW_API_VERSION_KEY)).toInt(), WHALE_VIEW_API_VERSION);
    QCOMPARE(metaData.value(QStringLiteral("X-Whale-RequiresLocal"), false), true);
    QCOMPARE(metaData.value(QStringLiteral("X-Whale-Capabilities"), QStringList()),
             QStringList({QStringLiteral("Selection"), QStringLiteral("RecursiveContent"), QStringLiteral("KeyboardNavigation")}));
    QVERIFY(registry->plugin(treemapId));

    // Offered as a view mode.
    QVERIFY(viewActions()->viewModeAction(treemapId));
}

void WhaleTreemapTest::testSupportsUrl()
{
    QVERIFY(showMainWindow());
    WhaleViewPlugin *plugin = viewActions()->registry()->plugin(treemapId);
    QVERIFY(plugin);
    QVERIFY(plugin->supportsUrl(m_dir->url()));
    QVERIFY(plugin->supportsUrl(QUrl::fromLocalFile(QStringLiteral("/"))));
    for (const QString &remote : {QStringLiteral("sftp://host/home"),
                                  QStringLiteral("smb://server/share"),
                                  QStringLiteral("trash:/"),
                                  QStringLiteral("zip:/tmp/a.zip/"),
                                  QStringLiteral("remote:/")}) {
        QVERIFY2(!plugin->supportsUrl(QUrl(remote)), qPrintable(remote));
    }
}

void WhaleTreemapTest::testNonLocalFallsBack()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    WhaleViewHost *host = container->viewHost();
    QVERIFY(showTreemap(container));
    QAction *treemapEntry = viewActions()->viewModeAction(treemapId);
    QVERIFY(treemapEntry && treemapEntry->isEnabled());

    // A location the treemap cannot show: the file list shows it.
    container->setUrl(QUrl(QStringLiteral("trash:/")));
    QTRY_COMPARE(container->url(), QUrl(QStringLiteral("trash:/")));
    QTRY_VERIFY(host->isDolphinViewShown());
    QCOMPARE(host->lastRestore().source, QStringLiteral("location"));
    QCOMPARE(host->lastRestore().requestedViewId, treemapId);
    QVERIFY(host->lastRestore().fallback);
    // Not offered there, and not shown when asked anyway.
    QVERIFY(!treemapEntry->isEnabled());
    QVERIFY(!host->showPluginView(treemapId, viewActions()->registry()->plugin(treemapId)));
    QVERIFY(host->isDolphinViewShown());

    // Back in the local folder, the history brings the treemap back.
    m_mainWindow->actionCollection()->action(KStandardAction::name(KStandardAction::Back))->trigger();
    QTRY_COMPARE(container->url(), m_dir->url());
    QTRY_COMPARE(host->currentViewId(), treemapId);
    QVERIFY(treemapEntry->isEnabled());
    QTRY_COMPARE(status(host->currentView()->widget()), QStringLiteral("ready"));
}

void WhaleTreemapTest::testScan()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QWidget *view = showTreemap(container);
    QVERIFY(view);
    const QVariantMap state = treemapState(view);
    // All of it, also the hidden file the file list does not show.
    QCOMPARE(state.value(QStringLiteral("totalSize")).toLongLong(), 160000);
    QCOMPARE(QUrl(state.value(QStringLiteral("scanRoot")).toString()), m_dir->url());
    QVERIFY(state.value(QStringLiteral("cells")).toInt() >= 10);
    QVERIFY(!cellRect(view, url(QStringLiteral(".hidden"))).isEmpty());
    QVERIFY(!cellRect(view, url(QStringLiteral("media/photos/p1.jpg"))).isEmpty());
    // Nothing for the empty file.
    QVERIFY(cellRect(view, url(QStringLiteral("empty.txt"))).isEmpty());
    // The shared cache of the window did the scan.
    QCOMPARE(viewActions()->treeCache()->statistics().misses, 1);
}

void WhaleTreemapTest::testCacheHit()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QWidget *view = showTreemap(container);
    QVERIFY(view);
    WhaleTreeCache *cache = viewActions()->treeCache();
    const WhaleTreeCache::Statistics before = cache->statistics();
    QCOMPARE(before.misses, 1);

    // Elsewhere and back: the location's tree comes from the cache.
    container->setUrl(url(QStringLiteral("docs")));
    QTRY_COMPARE(status(view), QStringLiteral("ready"));
    QCOMPARE(QUrl(treemapState(view).value(QStringLiteral("scanRoot")).toString()), url(QStringLiteral("docs")));
    container->setUrl(m_dir->url());
    QTRY_COMPARE(status(view), QStringLiteral("ready"));
    QCOMPARE(treemapState(view).value(QStringLiteral("totalSize")).toLongLong(), 160000);
    QCOMPARE(cache->statistics().hits, before.hits + 1);
    QCOMPARE(cache->statistics().misses, before.misses + 1); // only "docs" was scanned

    // Switching views reuses it too.
    container->viewHost()->showDolphinView();
    view = showTreemap(container);
    QVERIFY(view);
    QCOMPARE(cache->statistics().hits, before.hits + 2);
}

void WhaleTreemapTest::testInvalidation()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QWidget *view = showTreemap(container);
    QVERIFY(view);
    WhaleTreeCache *cache = viewActions()->treeCache();
    container->setUrl(url(QStringLiteral("docs")));
    QTRY_COMPARE(status(view), QStringLiteral("ready"));

    // A file deep below the first location grows while the treemap shows another one.
    QFile file(m_dir->path() + QStringLiteral("/media/photos/p1.jpg"));
    QVERIFY(file.open(QIODevice::Append));
    file.write(QByteArray(10000, 'x'));
    file.close();
    QTRY_VERIFY(cache->statistics().invalidations >= 1);

    const int misses = cache->statistics().misses;
    container->setUrl(m_dir->url());
    QTRY_COMPARE(status(view), QStringLiteral("ready"));
    QCOMPARE(cache->statistics().misses, misses + 1); // scanned again
    QCOMPARE(treemapState(view).value(QStringLiteral("totalSize")).toLongLong(), 170000);
}

void WhaleTreemapTest::testLoadingShowsNoOldTree()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QWidget *view = showTreemap(container);
    QVERIFY(view);
    QVERIFY(treemapState(view).value(QStringLiteral("cells")).toInt() > 0);
    // Right after the location changed, the old tree is gone; the new one is being scanned.
    container->setUrl(url(QStringLiteral("media")));
    QVariantMap state = treemapState(view);
    QCOMPARE(state.value(QStringLiteral("status")).toString(), QStringLiteral("scanning"));
    QCOMPARE(state.value(QStringLiteral("cells")).toInt(), 0);
    QVERIFY(state.value(QStringLiteral("scanRoot")).toString().isEmpty());
    // A fast scan does not flash a message.
    QVERIFY(!state.value(QStringLiteral("loadingShown")).toBool());
    QTRY_COMPARE(status(view), QStringLiteral("ready"));
    QCOMPARE(QUrl(treemapState(view).value(QStringLiteral("scanRoot")).toString()), url(QStringLiteral("media")));
}

void WhaleTreemapTest::testFailure()
{
    if (getuid() == 0) {
        QSKIP("root can read everything");
    }
    m_dir->createFile(QStringLiteral("locked/secret.txt"), QByteArray(100, 's'));
    DolphinViewContainer *container = showMainWindow(5);
    QVERIFY(container);
    QWidget *view = showTreemap(container);
    QVERIFY(view);
    QVERIFY(QFile::setPermissions(m_dir->path() + QStringLiteral("/locked"), QFile::WriteOwner));

    container->setUrl(url(QStringLiteral("locked")));
    QTRY_COMPARE(status(view), QStringLiteral("failed"));
    const QVariantMap state = treemapState(view);
    QVERIFY(!state.value(QStringLiteral("error")).toString().isEmpty());
    QVERIFY(state.value(QStringLiteral("statusText")).toString().contains(state.value(QStringLiteral("error")).toString()));
    QCOMPARE(state.value(QStringLiteral("cells")).toInt(), 0);
    // Still usable: back to where it works.
    m_mainWindow->actionCollection()->action(KStandardAction::name(KStandardAction::Back))->trigger();
    QTRY_COMPARE(container->url(), m_dir->url());
    QTRY_COMPARE(status(view), QStringLiteral("ready"));
    QCOMPARE(container->viewHost()->currentViewId(), treemapId);
}

void WhaleTreemapTest::testHover()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QWidget *view = showTreemap(container);
    QVERIFY(view);
    QSignalSpy info(m_mainWindow.data(), &DolphinMainWindow::requestItemInfo);
    QWidget *canvas = canvasOf(view);

    // A file deep below the location: described from the file system.
    const QUrl deep = url(QStringLiteral("media/photos/p2.jpg"));
    QTest::mouseMove(canvas, cellRect(view, deep).center());
    QTRY_VERIFY(!info.isEmpty() && info.last().at(0).value<KFileItem>().url() == deep);
    const KFileItem item = info.last().at(0).value<KFileItem>();
    QVERIFY(!item.isDir());
    QCOMPARE(item.size(), KIO::filesize_t(25000));
    QCOMPARE(QUrl(treemapState(view).value(QStringLiteral("hovered")).toString()), deep);
    QVERIFY(treemapState(view).value(QStringLiteral("infoText")).toString().contains(QStringLiteral("media/photos/p2.jpg")));

    // An entry of the listing.
    QTest::mouseMove(canvas, cellRect(view, url(QStringLiteral("big.bin"))).center());
    QTRY_COMPARE(info.last().at(0).value<KFileItem>().url(), url(QStringLiteral("big.bin")));

    // Out of the treemap: none.
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(canvas, &leave);
    QTRY_VERIFY(info.last().at(0).value<KFileItem>().isNull());
    QVERIFY(treemapState(view).value(QStringLiteral("hovered")).toString().isEmpty());
}

void WhaleTreemapTest::testSelection()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QWidget *view = showTreemap(container);
    QVERIFY(view);
    DolphinViewBridge *bridge = container->viewHost()->bridge();
    QSignalSpy info(m_mainWindow.data(), &DolphinMainWindow::requestItemInfo);

    // A deep file: the view's own selection; Whale's selection stays empty.
    const QUrl deep = url(QStringLiteral("docs/notes/a.txt"));
    QVERIFY(select(view, deep));
    QVariantMap state = treemapState(view);
    QVERIFY(state.value(QStringLiteral("selectedDeep")).toBool());
    QVERIFY(state.value(QStringLiteral("selectedShown")).toBool());
    QTRY_VERIFY(bridge->selection().isEmpty());
    QVERIFY(container->view()->selectedItems().isEmpty());
    // Away from the treemap, the information panel describes the deep selection.
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(canvasOf(view), &leave);
    QTRY_COMPARE(info.last().at(0).value<KFileItem>().url(), deep);

    // An entry of the listing (a folder, by going up): Whale's selection too.
    QVERIFY(select(view, url(QStringLiteral("docs"))));
    QTRY_COMPARE(bridge->selection().selected(), QList<QUrl>({url(QStringLiteral("docs"))}));
    QCOMPARE(container->view()->selectedItems().count(), 1);
    QVERIFY(!treemapState(view).value(QStringLiteral("selectedDeep")).toBool());

    // A deep entry again clears Whale's selection; nothing invisible stays selected.
    QVERIFY(select(view, url(QStringLiteral("media/song.flac"))));
    QTRY_VERIFY(bridge->selection().isEmpty());

    // Whale's selection changed elsewhere (here: the host, as an action would) is shown.
    bridge->setSelection(WhaleSelection({url(QStringLiteral("big.bin"))}, url(QStringLiteral("big.bin"))));
    QTRY_COMPARE(QUrl(treemapState(view).value(QStringLiteral("selected")).toString()), url(QStringLiteral("big.bin")));
    // Escape clears it.
    QTest::keyClick(canvasOf(view), Qt::Key_Escape);
    QTRY_VERIFY(treemapState(view).value(QStringLiteral("selected")).toString().isEmpty());
    QTRY_VERIFY(bridge->selection().isEmpty());
}

void WhaleTreemapTest::testDeepSelectionRoutesActions()
{
    // Since T2 Whale's actions act on the treemap's own selection, never on the hidden
    // file list's (which stays empty for a deep entry).
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QWidget *view = showTreemap(container);
    QVERIFY(view);
    const QString deepPath = m_dir->path() + QStringLiteral("/media/photos/p1.jpg");
    QVERIFY(select(view, url(QStringLiteral("big.bin"))));
    QTRY_VERIFY(!container->viewHost()->bridge()->selection().isEmpty());
    QVERIFY(select(view, QUrl::fromLocalFile(deepPath)));
    QTRY_VERIFY(container->viewHost()->bridge()->selection().isEmpty());
    for (const QString &name : {KStandardAction::name(KStandardAction::MoveToTrash),
                                KStandardAction::name(KStandardAction::DeleteFile),
                                KStandardAction::name(KStandardAction::Cut),
                                KStandardAction::name(KStandardAction::Copy),
                                KStandardAction::name(KStandardAction::RenameFile)}) {
        QVERIFY2(action(name)->isEnabled(), qPrintable(name));
    }
    for (const QString &name : {KStandardAction::name(KStandardAction::SelectAll), QStringLiteral("invert_selection"), QStringLiteral("compare_files")}) {
        QVERIFY2(!action(name)->isEnabled(), qPrintable(name));
    }

    // Shift+Delete deletes exactly the deep entry, always confirmed (here by the test).
    QTest::keySequence(canvasOf(view), QKeySequence(Qt::SHIFT | Qt::Key_Delete));
    QTRY_VERIFY(!QFile::exists(deepPath));
    QCOMPARE(m_confirmed, QList<WhaleFileOperations::Operation>({WhaleFileOperations::Operation::Delete}));
    QVERIFY(QFile::exists(m_dir->path() + QStringLiteral("/big.bin")));
    QVERIFY(QFile::exists(m_dir->path() + QStringLiteral("/media/photos/p2.jpg")));
    const auto dialogs = QApplication::topLevelWidgets();
    for (QWidget *window : dialogs) {
        QVERIFY2(!qobject_cast<QDialog *>(window) || !window->isVisible(), qPrintable(window->metaObject()->className()));
    }
}

void WhaleTreemapTest::testKeyboard()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QWidget *view = showTreemap(container);
    QVERIFY(view);
    QWidget *canvas = canvasOf(view);
    canvas->setFocus();
    auto selected = [view]() {
        return QUrl(treemapState(view).value(QStringLiteral("selected")).toString());
    };
    // Without a selection, an arrow key selects the largest entry (media, 70 000 bytes);
    // Right goes to the next smaller one.
    QTest::keyClick(canvas, Qt::Key_Right);
    QCOMPARE(selected(), url(QStringLiteral("media")));
    QTest::keyClick(canvas, Qt::Key_Right);
    QCOMPARE(selected(), url(QStringLiteral("big.bin")));
    QTest::keyClick(canvas, Qt::Key_Right);
    QCOMPARE(selected(), url(QStringLiteral("docs")));
    QTest::keyClick(canvas, Qt::Key_Left);
    QTest::keyClick(canvas, Qt::Key_Left);
    QCOMPARE(selected(), url(QStringLiteral("media")));
    QTest::keyClick(canvas, Qt::Key_Left); // the first one: stays
    QCOMPARE(selected(), url(QStringLiteral("media")));
    // Down: the largest entry inside; Up: the folder again.
    QTest::keyClick(canvas, Qt::Key_Down);
    QCOMPARE(selected(), url(QStringLiteral("media/photos")));
    QTest::keyClick(canvas, Qt::Key_Down);
    QCOMPARE(selected(), url(QStringLiteral("media/photos/p1.jpg")));
    QTest::keyClick(canvas, Qt::Key_Up);
    QCOMPARE(selected(), url(QStringLiteral("media/photos")));
    QTest::keyClick(canvas, Qt::Key_Up);
    QCOMPARE(selected(), url(QStringLiteral("media")));
    QTest::keyClick(canvas, Qt::Key_Up); // the top level: stays
    QCOMPARE(selected(), url(QStringLiteral("media")));
    QCOMPARE(container->url(), m_dir->url());
}

void WhaleTreemapTest::testNavigation()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QWidget *view = showTreemap(container);
    QVERIFY(view);

    // Enter on a deep folder: it becomes the location.
    QVERIFY(select(view, url(QStringLiteral("media/photos"))));
    QTest::keyClick(canvasOf(view), Qt::Key_Return);
    QTRY_COMPARE(container->url(), url(QStringLiteral("media/photos")));
    QTRY_COMPARE(status(view), QStringLiteral("ready"));
    QCOMPARE(QUrl(treemapState(view).value(QStringLiteral("scanRoot")).toString()), url(QStringLiteral("media/photos")));
    QCOMPARE(container->viewHost()->currentViewId(), treemapId);

    // Back and forward are Whale's.
    m_mainWindow->actionCollection()->action(KStandardAction::name(KStandardAction::Back))->trigger();
    QTRY_COMPARE(container->url(), m_dir->url());
    QTRY_COMPARE(status(view), QStringLiteral("ready"));
    m_mainWindow->actionCollection()->action(KStandardAction::name(KStandardAction::Forward))->trigger();
    QTRY_COMPARE(container->url(), url(QStringLiteral("media/photos")));
    m_mainWindow->actionCollection()->action(KStandardAction::name(KStandardAction::Up))->trigger();
    QTRY_COMPARE(container->url().adjusted(QUrl::StripTrailingSlash), url(QStringLiteral("media")));
    QTRY_COMPARE(status(view), QStringLiteral("ready"));

    // A double click opens the folder of the entry under the pointer.
    container->setUrl(m_dir->url());
    QTRY_COMPARE(container->url(), m_dir->url());
    QTRY_COMPARE(status(view), QStringLiteral("ready"));
    const QPoint notes = cellRect(view, url(QStringLiteral("docs/notes/a.txt"))).center();
    QVERIFY(!notes.isNull());
    QTest::mouseDClick(canvasOf(view), Qt::LeftButton, {}, notes);
    QTRY_COMPARE(container->url(), url(QStringLiteral("docs/notes")));

    // A file directly in the location has no other folder: nothing happens.
    container->setUrl(m_dir->url());
    QTRY_COMPARE(status(view), QStringLiteral("ready"));
    QTest::mouseDClick(canvasOf(view), Qt::LeftButton, {}, cellRect(view, url(QStringLiteral("big.bin"))).center());
    QTest::qWait(200);
    QCOMPARE(container->url(), m_dir->url());
}

void WhaleTreemapTest::testZoom()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QWidget *view = showTreemap(container);
    QVERIFY(view);
    QWidget *canvas = canvasOf(view);
    const QRect canvasRect = canvas->rect();

    // The wheel zooms one level towards the pointer, within the treemap.
    const QPoint photo = cellRect(view, url(QStringLiteral("media/photos/p2.jpg"))).center();
    QWheelEvent wheelIn(QPointF(photo), QPointF(canvas->mapToGlobal(photo)), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(canvas, &wheelIn);
    QVariantMap state = treemapState(view);
    QCOMPARE(state.value(QStringLiteral("zoomPath")).toStringList(), QStringList({QStringLiteral("media")}));
    QCOMPARE(QUrl(state.value(QStringLiteral("zoomRoot")).toString()), url(QStringLiteral("media")));
    QCOMPARE(state.value(QStringLiteral("zoomSize")).toLongLong(), 70000);
    QCOMPARE(container->url(), m_dir->url()); // the location stays
    // media now fills the whole canvas.
    QCOMPARE(cellRect(view, url(QStringLiteral("big.bin"))), QRect());
    QVERIFY(cellRect(view, url(QStringLiteral("media/photos"))).width() > canvasRect.width() / 3);
    QVERIFY(treemapState(view).value(QStringLiteral("zoomOutEnabled")).toBool());

    // + zooms towards the selection, - and the button zoom out.
    QVERIFY(select(view, url(QStringLiteral("media/photos/p1.jpg"))));
    QTest::keyClick(canvas, Qt::Key_Plus);
    QCOMPARE(treemapState(view).value(QStringLiteral("zoomPath")).toStringList(), QStringList({QStringLiteral("media"), QStringLiteral("photos")}));
    QTest::keyClick(canvas, Qt::Key_Minus);
    QCOMPARE(treemapState(view).value(QStringLiteral("zoomPath")).toStringList(), QStringList({QStringLiteral("media")}));
    view->findChild<QToolButton *>(QStringLiteral("zoomOut"))->click();
    QVERIFY(treemapState(view).value(QStringLiteral("zoomPath")).toStringList().isEmpty());
    QVERIFY(!treemapState(view).value(QStringLiteral("zoomOutEnabled")).toBool());

    // Files cannot be zoomed into.
    const QPoint big = cellRect(view, url(QStringLiteral("big.bin"))).center();
    QWheelEvent wheelFile(QPointF(big), QPointF(canvas->mapToGlobal(big)), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(canvas, &wheelFile);
    QVERIFY(treemapState(view).value(QStringLiteral("zoomPath")).toStringList().isEmpty());
}

void WhaleTreemapTest::testViewSwitchKeepsState()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QWidget *view = showTreemap(container);
    QVERIFY(view);
    QVERIFY(select(view, url(QStringLiteral("media/photos/p2.jpg"))));
    QTest::keyClick(canvasOf(view), Qt::Key_Plus);
    view->findChild<QComboBox *>(QStringLiteral("colorMode"))->setCurrentIndex(1);
    view->findChild<QCheckBox *>(QStringLiteral("showLabels"))->setChecked(false);

    container->viewHost()->showDolphinView();
    QVERIFY(container->viewHost()->viewsState().states.contains(treemapId));
    view = showTreemap(container);
    QVERIFY(view);
    const QVariantMap state = treemapState(view);
    QCOMPARE(state.value(QStringLiteral("zoomPath")).toStringList(), QStringList({QStringLiteral("media")}));
    QCOMPARE(QUrl(state.value(QStringLiteral("selected")).toString()), url(QStringLiteral("media/photos/p2.jpg")));
    QCOMPARE(state.value(QStringLiteral("colorMode")).toString(), QStringLiteral("topFolder"));
    QCOMPARE(state.value(QStringLiteral("showLabels")).toBool(), false);

    // A selection made in the file list meanwhile wins over the stored one.
    container->viewHost()->showDolphinView();
    container->viewHost()->bridge()->setSelection(WhaleSelection({url(QStringLiteral("docs"))}, url(QStringLiteral("docs"))));
    view = showTreemap(container);
    QVERIFY(view);
    QCOMPARE(QUrl(treemapState(view).value(QStringLiteral("selected")).toString()), url(QStringLiteral("docs")));
    QCOMPARE(treemapState(view).value(QStringLiteral("zoomPath")).toStringList(), QStringList({QStringLiteral("media")}));
}

void WhaleTreemapTest::testHistoryRestore()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QWidget *view = showTreemap(container);
    QVERIFY(view);
    QVERIFY(select(view, url(QStringLiteral("docs/notes/b.txt"))));
    QTest::keyClick(canvasOf(view), Qt::Key_Plus); // zoom into docs

    // A new location starts unzoomed, without a selection.
    container->setUrl(url(QStringLiteral("media")));
    QTRY_COMPARE(status(view), QStringLiteral("ready"));
    QVERIFY(treemapState(view).value(QStringLiteral("zoomPath")).toStringList().isEmpty());
    QVERIFY(treemapState(view).value(QStringLiteral("selected")).toString().isEmpty());
    QVERIFY(select(view, url(QStringLiteral("media/photos/p1.jpg"))));

    QAction *back = m_mainWindow->actionCollection()->action(KStandardAction::name(KStandardAction::Back));
    QAction *forward = m_mainWindow->actionCollection()->action(KStandardAction::name(KStandardAction::Forward));
    back->trigger();
    QTRY_COMPARE(container->url(), m_dir->url());
    QTRY_COMPARE(status(view), QStringLiteral("ready"));
    QCOMPARE(treemapState(view).value(QStringLiteral("zoomPath")).toStringList(), QStringList({QStringLiteral("docs")}));
    QCOMPARE(QUrl(treemapState(view).value(QStringLiteral("selected")).toString()), url(QStringLiteral("docs/notes/b.txt")));
    QCOMPARE(container->viewHost()->lastRestore().source, QStringLiteral("history"));

    forward->trigger();
    QTRY_COMPARE(container->url(), url(QStringLiteral("media")));
    QTRY_COMPARE(status(view), QStringLiteral("ready"));
    QVERIFY(treemapState(view).value(QStringLiteral("zoomPath")).toStringList().isEmpty());
    QCOMPARE(QUrl(treemapState(view).value(QStringLiteral("selected")).toString()), url(QStringLiteral("media/photos/p1.jpg")));
}

void WhaleTreemapTest::testSessionRestore()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    m_mainWindow->actionCollection()->action(QStringLiteral("split_view"))->trigger();
    DolphinTabPage *page = m_mainWindow->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"))->currentTabPage();
    QTRY_VERIFY(page->secondaryViewContainer());
    DolphinViewContainer *left = page->primaryViewContainer();
    QWidget *view = showTreemap(left);
    QVERIFY(view);
    QVERIFY(select(view, url(QStringLiteral("docs/report.pdf"))));
    QTest::keyClick(canvasOf(view), Qt::Key_Plus);
    view->findChild<QComboBox *>(QStringLiteral("colorMode"))->setCurrentIndex(1);

    KConfig config(m_tmpHome.filePath(QStringLiteral("session")), KConfig::SimpleConfig);
    KConfigGroup group = config.group(QStringLiteral("Window"));
    m_mainWindow->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"))->saveProperties(group);
    m_mainWindow.reset();

    m_mainWindow.reset(new DolphinMainWindow());
    m_mainWindow->resize(1000, 700);
    // As KMainWindow::restore(): the session first, then the window is shown. A shown window
    // without a tab must not get events (e.g. a clipboard change on Wayland: Dolphin's Paste
    // action asks the active view area, which does not exist yet).
    m_mainWindow->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"))->readProperties(group);
    m_mainWindow->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_mainWindow.data()));
    page = m_mainWindow->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"))->currentTabPage();
    QVERIFY(page->splitViewEnabled());
    left = page->primaryViewContainer();
    QCOMPARE(left->viewHost()->currentViewId(), treemapId);
    QCOMPARE(left->viewHost()->lastRestore().source, QStringLiteral("session"));
    QVERIFY(page->secondaryViewContainer()->viewHost()->isDolphinViewShown());
    view = left->viewHost()->currentView()->widget();
    QTRY_COMPARE(status(view), QStringLiteral("ready"));
    const QVariantMap state = treemapState(view);
    QCOMPARE(state.value(QStringLiteral("zoomPath")).toStringList(), QStringList({QStringLiteral("docs")}));
    QCOMPARE(QUrl(state.value(QStringLiteral("selected")).toString()), url(QStringLiteral("docs/report.pdf")));
    QCOMPARE(state.value(QStringLiteral("colorMode")).toString(), QStringLiteral("topFolder"));
}

void WhaleTreemapTest::testSplitView()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    m_mainWindow->actionCollection()->action(QStringLiteral("split_view"))->trigger();
    DolphinTabPage *page = m_mainWindow->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"))->currentTabPage();
    QTRY_VERIFY(page->secondaryViewContainer());
    DolphinViewContainer *left = page->primaryViewContainer();
    DolphinViewContainer *right = page->secondaryViewContainer();
    QTRY_COMPARE(right->view()->itemsCount(), 4);
    // Dolphin animates opening the second half.
    QTRY_VERIFY(std::abs(right->width() - left->width()) < 10);
    QWidget *view = showTreemap(left);
    QVERIFY(view);
    WhaleViewPlugin *testList = viewActions()->registry()->plugin(testListId);
    QVERIFY(testList && right->viewHost()->showPluginView(testListId, testList));
    right->setActive(true);
    QTRY_VERIFY(right->isActive());

    // A click into the treemap activates its half, and the treemap's actions apply.
    QVERIFY(select(view, url(QStringLiteral("docs/notes/a.txt"))));
    QTRY_VERIFY(left->isActive());
    QVERIFY(!right->isActive());
    QVERIFY(canvasOf(view)->hasFocus());
    // The treemap's deep selection is what the actions act on (T2).
    QTRY_VERIFY(action(KStandardAction::name(KStandardAction::DeleteFile))->isEnabled());

    // The test list on the right: its actions are the file list's again.
    auto listView = qobject_cast<QListView *>(right->viewHost()->currentView()->widget());
    QVERIFY(listView);
    QTest::mouseClick(listView->viewport(), Qt::LeftButton, {}, listView->visualRect(listView->model()->index(3, 0)).center());
    QTRY_VERIFY(right->isActive());
    QTRY_VERIFY(action(KStandardAction::name(KStandardAction::DeleteFile))->isEnabled());
    // The treemap keeps its deep selection meanwhile.
    QCOMPARE(QUrl(treemapState(view).value(QStringLiteral("selected")).toString()), url(QStringLiteral("docs/notes/a.txt")));

    // Moving the splitter lays the treemap out again, and hit tests follow.
    auto splitter = qobject_cast<QSplitter *>(left->parentWidget());
    QVERIFY(splitter);
    const int widthBefore = canvasOf(view)->width();
    const int total = splitter->sizes().at(0) + splitter->sizes().at(1);
    splitter->setSizes({total * 3 / 4, total / 4});
    QTRY_VERIFY(canvasOf(view)->width() > widthBefore);
    const QRect rect = cellRect(view, url(QStringLiteral("big.bin")));
    QVERIFY(!rect.isEmpty());
    QCOMPARE(urlAt(view, rect.center()), url(QStringLiteral("big.bin")));
    QCOMPARE(QUrl(treemapState(view).value(QStringLiteral("selected")).toString()), url(QStringLiteral("docs/notes/a.txt")));
}

void WhaleTreemapTest::testResize()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QWidget *view = showTreemap(container);
    QVERIFY(view);
    QVERIFY(select(view, url(QStringLiteral("media/song.flac"))));
    const int layouts = treemapState(view).value(QStringLiteral("layoutCount")).toInt();
    const QRect before = cellRect(view, url(QStringLiteral("media/song.flac")));

    m_mainWindow->resize(1400, 900);
    QTRY_VERIFY(cellRect(view, url(QStringLiteral("media/song.flac"))) != before);
    const QVariantMap state = treemapState(view);
    QVERIFY(state.value(QStringLiteral("layoutCount")).toInt() > layouts);
    QCOMPARE(state.value(QStringLiteral("canvasWidth")).toInt(), canvasOf(view)->width());
    // Same entries, same selection, hit tests on the new geometry.
    QCOMPARE(QUrl(state.value(QStringLiteral("selected")).toString()), url(QStringLiteral("media/song.flac")));
    QVERIFY(state.value(QStringLiteral("selectedShown")).toBool());
    for (const QString &path : {QStringLiteral("big.bin"), QStringLiteral("media/song.flac"), QStringLiteral("docs/report.pdf")}) {
        const QRect rect = cellRect(view, url(path));
        QVERIFY(!rect.isEmpty());
        QCOMPARE(urlAt(view, rect.center()), url(path));
    }
    // The area keeps its share of the canvas.
    const QRect big = cellRect(view, url(QStringLiteral("big.bin")));
    const double share = double(big.width()) * big.height() / (canvasOf(view)->width() * canvasOf(view)->height());
    QVERIFY2(std::abs(share - 60000.0 / 160000.0) < 0.02, qPrintable(QString::number(share)));
}

void WhaleTreemapTest::testCapabilityGating()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QVERIFY(showTreemap(container));
    WhaleViewActions *actions = viewActions();
    const WhaleViewCapabilities capabilities = container->viewHost()->currentView()->capabilities();
    QCOMPARE(capabilities, WhaleViewCapability::Selection | WhaleViewCapability::RecursiveContent | WhaleViewCapability::KeyboardNavigation);
    QCOMPARE(actions->gateReason(QStringLiteral("sort")), QStringLiteral("no HostOrder capability"));
    QCOMPARE(actions->gateReason(QStringLiteral("group_by")), QStringLiteral("no HostOrder capability"));
    QCOMPARE(actions->gateReason(QStringLiteral("additional_info")), QStringLiteral("no HostOrder capability"));
    QCOMPARE(actions->gateReason(KStandardAction::name(KStandardAction::ZoomIn)), QStringLiteral("no Zoom capability"));
    QCOMPARE(actions->gateReason(QStringLiteral("show_preview")), QStringLiteral("no Previews capability"));
    QCOMPARE(actions->gateReason(KStandardAction::name(KStandardAction::SelectAll)), QStringLiteral("no MultiSelection capability"));
    // Without a selection the actions on selected items are off (no selection mode).
    QCOMPARE(actions->gateReason(KStandardAction::name(KStandardAction::MoveToTrash)), QStringLiteral("nothing selected"));
    for (const QString &name : {QStringLiteral("sort"), QStringLiteral("view_zoom_in"), QStringLiteral("show_preview"), QStringLiteral("movetotrash")}) {
        QVERIFY2(!action(name)->isEnabled(), qPrintable(name));
    }
    // Entries from many folders, no destination folder: nothing is created in the treemap.
    QVERIFY(!container->viewHost()->hasDestination());
    const QStringList creating = {QStringLiteral("new_menu"),
                                  QStringLiteral("create_dir"),
                                  QStringLiteral("create_file"),
                                  QStringLiteral("duplicate"),
                                  KStandardAction::name(KStandardAction::Paste)};
    for (const QString &name : creating) {
        QCOMPARE(actions->gateReason(name), QStringLiteral("no destination folder"));
        QVERIFY2(!action(name)->isEnabled(), qPrintable(name));
    }
    // Back in the file list, all of it is Dolphin's again.
    container->viewHost()->showDolphinView();
    QVERIFY(action(QStringLiteral("sort"))->isEnabled());
    QVERIFY(action(QStringLiteral("view_zoom_in"))->isEnabled());
    QVERIFY(actions->gateReason(QStringLiteral("sort")).isEmpty());
    QVERIFY(container->viewHost()->hasDestination());
    for (const QString &name : creating) {
        QVERIFY2(actions->gateReason(name).isEmpty(), qPrintable(name));
    }
    QTRY_VERIFY(action(QStringLiteral("create_dir"))->isEnabled());
}

namespace
{
// A drag over @p widget as Qt delivers it to the drop target: @return whether it was accepted.
bool sendDrag(QWidget *widget, const QPoint &position, const QMimeData *data)
{
    const Qt::DropActions actions = Qt::CopyAction | Qt::MoveAction | Qt::LinkAction;
    QDragEnterEvent enter(position, actions, data, Qt::LeftButton, Qt::ControlModifier);
    QCoreApplication::sendEvent(widget, &enter);
    bool accepted = enter.isAccepted() && enter.dropAction() != Qt::IgnoreAction;
    QDragMoveEvent move(position + QPoint(1, 0), actions, data, Qt::LeftButton, Qt::ControlModifier);
    QCoreApplication::sendEvent(widget, &move);
    accepted = accepted || (move.isAccepted() && move.dropAction() != Qt::IgnoreAction);
    QDropEvent drop(position, actions, data, Qt::LeftButton, Qt::ControlModifier);
    QCoreApplication::sendEvent(widget, &drop);
    return accepted || (drop.isAccepted() && drop.dropAction() != Qt::IgnoreAction);
}
}

void WhaleTreemapTest::testDropsAreRefused()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QWidget *view = showTreemap(container);
    QVERIFY(view);
    WhaleViewHost *host = container->viewHost();
    // The treemap's widgets do not take drops: Qt delivers them to the host (the first
    // widget up the parents that accepts drops).
    for (QWidget *widget = canvasOf(view); widget != host; widget = widget->parentWidget()) {
        QVERIFY2(!widget->acceptDrops(), widget->metaObject()->className());
    }
    QVERIFY(host->acceptDrops());

    // A drag from elsewhere: refused and explained once, as in a flat file list.
    QSignalSpy messages(host, &WhaleViewHost::messageRequested);
    QMimeData data;
    data.setUrls({url(QStringLiteral("docs/report.pdf"))});
    const QPoint position = host->mapFromGlobal(canvasOf(view)->mapToGlobal(cellRect(view, url(QStringLiteral("media"))).center()));
    QVERIFY(!sendDrag(host, position, &data));
    QCOMPARE(messages.count(), 1);
    QCOMPARE(messages.first().first().toString(), host->refusal(WhaleViewHost::Refusal::Drop));
    QVERIFY(!QFile::exists(m_dir->path() + QStringLiteral("/report.pdf")));
    QVERIFY(!QFile::exists(m_dir->path() + QStringLiteral("/media/report.pdf")));

    // The treemap's own drag passing over it is refused without a word.
    bool ownAccepted = true;
    WhaleFileOperationsImpl::setDragRunner([host, position, &ownAccepted](QDrag *drag) {
        ownAccepted = sendDrag(host, position, drag->mimeData());
        return Qt::IgnoreAction;
    });
    host->pluginContext()->fileOperations()->startDrag({url(QStringLiteral("big.bin"))});
    WhaleFileOperationsImpl::setDragRunner({});
    QVERIFY(!ownAccepted);
    QCOMPARE(messages.count(), 1);

    // The file list takes drops itself again.
    host->showDolphinView();
    QVERIFY(!host->acceptDrops());
}

QTEST_MAIN(WhaleTreemapTest)

#include "whaletreemaptest.moc"
