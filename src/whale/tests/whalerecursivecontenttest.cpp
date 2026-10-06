/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whalerecursivecontent.h"
#include "config-dolphin.h"
#include "dolphin_generalsettings.h"
#include "dolphinmainwindow.h"
#include "dolphintabpage.h"
#include "dolphintabwidget.h"
#include "dolphinviewbridge.h"
#include "dolphinviewcontainer.h"
#include "kitemviews/kfileitemmodel.h"
#include "kitemviews/kitemlistcontainer.h"
#include "kitemviews/kitemlistcontroller.h"
#include "kitemviews/kitemlistselectionmanager.h"
#include "kitemviews/kitemlistview.h"
#include "testdir.h"
#include "testhelpers.h"
#include "views/dolphinview.h"
#include "whalerecursiveprojection.h"
#include "whaletreecache.h"
#include "whaleviewactions.h"
#include "whaleviewhost.h"
#include "whaleviewpreferences.h"
#include "whaleviewregistry.h"

#include <KActionCollection>
#include <KApplicationTrader>
#include <KConfig>
#include <KConfigGroup>
#include <KCoreDirLister>
#include <KIO/Paste>
#include <KIO/RenameFileDialog>
#include <KLocalizedString>
#include <KMessageWidget>
#include <KPropertiesDialog>
#include <KSharedConfig>
#include <KSycoca>
#include <KUrlMimeData>

#include <QClipboard>
#include <QDialogButtonBox>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QGraphicsView>
#include <QLineEdit>
#include <QMimeData>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QTimer>

namespace
{
const QString treemapId = QStringLiteral("whale_treemap");
const QString fileListId = WhaleViewHost::dolphinViewId();

template<typename T>
T *visibleDialog()
{
    const auto windows = QApplication::topLevelWidgets();
    for (QWidget *window : windows) {
        if (auto dialog = qobject_cast<T *>(window); dialog && dialog->isVisible()) {
            return dialog;
        }
    }
    return nullptr;
}

KFileItemModel *modelOf(DolphinView *view)
{
    return static_cast<KFileItemModel *>(view->itemListContainer()->controller()->model());
}
}

/**
 * F1: "Include Subfolders" — the file list (Icons, Compact, Details) shows the files below
 * its location (WhaleContentScope::RecursiveFiles), with Dolphin's own selection, actions,
 * sorting, filter and drags; drops and Paste into it are refused.
 */
class WhaleRecursiveContentTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    // Scope
    void testCurrentFolderShowsDirectItems();
    void testRecursiveShowsFilesBelow();
    void testHiddenFiles();
    void testScopeKeepsViewMode();
    void testViewModes_data();
    void testViewModes();
    void testProjectionEqualsListing();
    void testExternalItemsModel();
    void testLocationRemoved();
    void testRelativePath();

    // Selection
    void testSelection();

    // Operations
    void testOpen();
    void testRename();
    void testTrashAndDelete();
    void testCopyCut();
    void testDuplicate();
    void testProperties();
    void testCopyAndMoveToOtherPane();
    void testDragOut();

    // Drops and Paste
    void testDropsAreRefused();
    void testPasteIsRefused();
    void testCurrentFolderAcceptsDropsAgain();
    void testSplitDropIntoRecursive();

    // Sorting, filter, changes
    void testSort();
    void testFilter();
    void testChangesAreShown();

    // State
    void testPreferencePerLocation();
    void testHistory();
    void testSession();
    void testPreferredViewIsSeparate();
    void testTreemapIgnoresScope();
    void testRemoteLocation();

    // Performance
    void testPerformance_data();
    void testPerformance();
    void testListingBaseline_data();
    void testListingBaseline();

private:
    DolphinViewContainer *showMainWindow(const QUrl &url);
    bool includeSubfolders(bool on);
    bool waitForRecursive(DolphinViewContainer *container, int files);
    bool waitForListing(DolphinViewContainer *container, int items);
    QStringList shownPaths(DolphinViewContainer *container) const;
    DolphinViewContainer *openSplit(const QUrl &url);
    QAction *action(const QString &name) const;
    QUrl url(const QString &relative) const;
    QString path(const QString &relative) const;
    QStringList openedFiles() const;
    void setConfirmations(bool confirm);
    /// Sends a drag with @p data over @p widget at @p position; returns whether it was accepted.
    bool sendDrag(QWidget *widget, const QPoint &position, const QMimeData *data, int moves, bool drop);
    QPoint itemPosition(DolphinViewContainer *container, const QUrl &item) const;
    QPoint backgroundPosition(DolphinViewContainer *container) const;
    void select(DolphinViewContainer *container, const QList<QUrl> &urls);

    QTemporaryDir m_tmpHome;
    QString m_openerLog;
    QScopedPointer<TestDir> m_dir;
    QScopedPointer<DolphinMainWindow> m_mainWindow;
};

void WhaleRecursiveContentTest::initTestCase()
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

    // Opening a file must never start a real application (see whaleselectiontest).
    m_openerLog = m_tmpHome.filePath(QStringLiteral("opened.log"));
    const QString applications = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/applications");
    QVERIFY(QDir().mkpath(applications));
    QFile desktop(applications + QStringLiteral("/whale-test-opener.desktop"));
    QVERIFY(desktop.open(QIODevice::WriteOnly));
    desktop.write(QStringLiteral("[Desktop Entry]\nType=Application\nName=Whale Test Opener\nMimeType=text/plain;\n"
                                 "Exec=sh -c 'echo \"$1\" >> %1' sh %f\n")
                      .arg(m_openerLog)
                      .toUtf8());
    desktop.close();
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    QVERIFY(QDir().mkpath(config));
    QFile mimeApps(config + QStringLiteral("/mimeapps.list"));
    QVERIFY(mimeApps.open(QIODevice::WriteOnly));
    mimeApps.write("[Default Applications]\ntext/plain=whale-test-opener.desktop\n");
    mimeApps.close();
    KSycoca::self()->ensureCacheValid();
    const KService::Ptr opener = KApplicationTrader::preferredService(QStringLiteral("text/plain"));
    QVERIFY2(opener && opener->desktopEntryName() == QLatin1String("whale-test-opener"), "the test opener must handle text/plain");

    GeneralSettings::self()->setRememberOpenedTabs(false);
    GeneralSettings::self()->save();
    // The preferences of this test live in the test's state directory, never in the user's.
    QCOMPARE(QDir::homePath(), m_tmpHome.path());
}

void WhaleRecursiveContentTest::init()
{
    QFile::remove(QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + QStringLiteral("/whaleviewpreferencesrc"));
    QCOMPARE(WhaleViewPreferences::instance()->contentScopeCount(), 0);
    m_dir.reset(new TestDir());
    const QDateTime base = QDateTime::currentDateTime().addDays(-10);
    m_dir->createFile(QStringLiteral("root/top.txt"), QByteArray(300, 't'), base.addSecs(300));
    m_dir->createFile(QStringLiteral("root/a/one.txt"), QByteArray(100, 'o'), base.addSecs(100));
    m_dir->createFile(QStringLiteral("root/a/b/two.png"), QByteArray(5000, 'p'), base.addSecs(500));
    m_dir->createFile(QStringLiteral("root/a/b/c/three.txt"), QByteArray(200, 'h'), base.addSecs(200));
    m_dir->createFile(QStringLiteral("root/.hidden.txt"), QByteArray(10, 'x'), base);
    m_dir->createFile(QStringLiteral("root/.hiddendir/secret.txt"), QByteArray(10, 's'), base);
    m_dir->createFile(QStringLiteral("root/visible/.dotfile"), QByteArray(10, 'd'), base);
    m_dir->createDir(QStringLiteral("root/empty"));
    m_dir->createFile(QStringLiteral("target/existing.txt"), QByteArray(10, 'e'));
    QFile::remove(m_openerLog);
    setConfirmations(false);
    GeneralSettings::setRenameInline(false);
    // The main window is made when a test shows it: a window without a tab must not get
    // events (see testSession()).
}

void WhaleRecursiveContentTest::cleanup()
{
    const auto windows = QApplication::topLevelWidgets();
    for (QWidget *window : windows) {
        if (auto dialog = qobject_cast<QDialog *>(window); dialog && dialog->isVisible()) {
            dialog->reject();
        }
    }
    m_mainWindow.reset();
    m_dir.reset();
}

void WhaleRecursiveContentTest::setConfirmations(bool confirm)
{
    KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("kiorc"), KConfig::NoGlobals), QStringLiteral("Confirmations"));
    group.writeEntry("ConfirmDelete", confirm);
    group.writeEntry("ConfirmTrash", confirm);
    group.sync();
}

QUrl WhaleRecursiveContentTest::url(const QString &relative) const
{
    return QUrl::fromLocalFile(path(relative));
}

QString WhaleRecursiveContentTest::path(const QString &relative) const
{
    return m_dir->path() + QLatin1Char('/') + relative;
}

QAction *WhaleRecursiveContentTest::action(const QString &name) const
{
    return m_mainWindow->actionCollection()->action(name);
}

QStringList WhaleRecursiveContentTest::openedFiles() const
{
    QFile log(m_openerLog);
    if (!log.open(QIODevice::ReadOnly)) {
        return {};
    }
    QStringList files;
    const QList<QByteArray> lines = log.readAll().split('\n');
    for (const QByteArray &line : lines) {
        if (!line.isEmpty()) {
            files.append(QString::fromUtf8(line));
        }
    }
    return files;
}

DolphinViewContainer *WhaleRecursiveContentTest::showMainWindow(const QUrl &location)
{
    if (!m_mainWindow) {
        m_mainWindow.reset(new DolphinMainWindow());
    }
    m_mainWindow->openDirectories({location}, false);
    m_mainWindow->resize(1000, 700);
    m_mainWindow->show();
    if (!QTest::qWaitForWindowActive(m_mainWindow.data())) {
        return nullptr;
    }
    DolphinViewContainer *container = m_mainWindow->activeViewContainer();
    DolphinView *view = container->view();
    // Whale shares view properties between folders: a known state for every test.
    view->setHiddenFilesShown(false);
    view->setSortRole("text");
    view->setSortOrder(Qt::AscendingOrder);
    view->setSortFoldersFirst(true);
    view->setGroupedSorting(false);
    view->setViewMode(DolphinView::IconsView);
    if (!QTest::qWaitFor(
            [container, location]() {
                return container->url() == location && !container->viewHost()->bridge()->isLoading() && container->view()->itemsCount() > 0;
            },
            5000)) {
        return nullptr;
    }
    return container;
}

bool WhaleRecursiveContentTest::includeSubfolders(bool on)
{
    QAction *toggle = action(QStringLiteral("whale_include_subfolders"));
    if (!toggle || !toggle->isEnabled()) {
        return false;
    }
    if (toggle->isChecked() != on) {
        toggle->trigger();
    }
    return toggle->isChecked() == on;
}

bool WhaleRecursiveContentTest::waitForRecursive(DolphinViewContainer *container, int files)
{
    WhaleViewHost *host = container->viewHost();
    return QTest::qWaitFor(
        [host, container, files]() {
            return host->isRecursiveContentShown() && !host->recursiveContent()->isBusy() && !host->bridge()->isLoading()
                && container->view()->itemsCount() == files;
        },
        10000);
}

bool WhaleRecursiveContentTest::waitForListing(DolphinViewContainer *container, int items)
{
    WhaleViewHost *host = container->viewHost();
    return QTest::qWaitFor(
        [host, container, items]() {
            return !host->isRecursiveContentShown() && !host->bridge()->isLoading() && container->view()->itemsCount() == items;
        },
        10000);
}

QStringList WhaleRecursiveContentTest::shownPaths(DolphinViewContainer *container) const
{
    QStringList paths;
    const KFileItemList items = container->view()->items();
    const QString prefix = m_dir->path() + QLatin1Char('/');
    for (const KFileItem &item : items) {
        paths.append(item.url().toLocalFile().mid(prefix.length()));
    }
    return paths;
}

DolphinViewContainer *WhaleRecursiveContentTest::openSplit(const QUrl &location)
{
    action(QStringLiteral("split_view"))->trigger();
    DolphinTabPage *page = m_mainWindow->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"))->currentTabPage();
    if (!QTest::qWaitFor(
            [page]() {
                return page->secondaryViewContainer() && page->secondaryViewContainer()->isActive();
            },
            5000)) {
        return nullptr;
    }
    DolphinViewContainer *right = page->secondaryViewContainer();
    right->setUrl(location);
    if (!QTest::qWaitFor(
            [right, location]() {
                return right->url() == location && !right->viewHost()->bridge()->isLoading() && right->view()->itemsCount() > 0;
            },
            5000)) {
        return nullptr;
    }
    return right;
}

void WhaleRecursiveContentTest::select(DolphinViewContainer *container, const QList<QUrl> &urls)
{
    container->viewHost()->bridge()->setSelection(WhaleSelection(urls, urls.isEmpty() ? QUrl() : urls.first()));
}

QPoint WhaleRecursiveContentTest::itemPosition(DolphinViewContainer *container, const QUrl &item) const
{
    KItemListContainer *itemListContainer = container->view()->itemListContainer();
    auto graphicsView = qobject_cast<QGraphicsView *>(itemListContainer->viewport());
    KItemListView *itemListView = itemListContainer->controller()->view();
    const int index = modelOf(container->view())->index(item);
    const QRectF rect = index >= 0 ? itemListView->itemRect(index) : QRectF();
    return rect.isEmpty() ? QPoint() : graphicsView->mapFromScene(itemListView->mapToScene(rect.center()));
}

QPoint WhaleRecursiveContentTest::backgroundPosition(DolphinViewContainer *container) const
{
    QWidget *widget = container->viewHost()->bridge()->fileListDragWidget();
    return QPoint(widget->width() - 20, widget->height() - 20);
}

bool WhaleRecursiveContentTest::sendDrag(QWidget *widget, const QPoint &position, const QMimeData *data, int moves, bool drop)
{
    const Qt::DropActions actions = Qt::CopyAction | Qt::MoveAction | Qt::LinkAction;
    QDragEnterEvent enter(position, actions, data, Qt::LeftButton, Qt::ControlModifier);
    QCoreApplication::sendEvent(widget, &enter);
    bool accepted = enter.isAccepted() && enter.dropAction() != Qt::IgnoreAction;
    for (int i = 0; i < moves; ++i) {
        QDragMoveEvent move(position + QPoint(i % 3, 0), actions, data, Qt::LeftButton, Qt::ControlModifier);
        QCoreApplication::sendEvent(widget, &move);
        accepted = move.isAccepted() && move.dropAction() != Qt::IgnoreAction;
    }
    if (drop) {
        QDropEvent dropEvent(position, actions, data, Qt::LeftButton, Qt::ControlModifier);
        QCoreApplication::sendEvent(widget, &dropEvent);
        accepted = dropEvent.isAccepted() && dropEvent.dropAction() != Qt::IgnoreAction;
    } else {
        QDragLeaveEvent leave;
        QCoreApplication::sendEvent(widget, &leave);
    }
    return accepted;
}

// ---------------------------------------------------------------------------------------
// Scope

void WhaleRecursiveContentTest::testCurrentFolderShowsDirectItems()
{
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("root")));
    QVERIFY(container);
    QVERIFY(waitForListing(container, 4));
    QCOMPARE(shownPaths(container),
             QStringList({QStringLiteral("root/a"), QStringLiteral("root/empty"), QStringLiteral("root/visible"), QStringLiteral("root/top.txt")}));
    QVERIFY(!container->viewHost()->isRecursiveContentShown());
}

void WhaleRecursiveContentTest::testRecursiveShowsFilesBelow()
{
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("root")));
    QVERIFY(container);
    WhaleViewHost *host = container->viewHost();
    QCOMPARE(host->contentScope(), WhaleContentScope::CurrentFolder);
    QVERIFY(!action(QStringLiteral("whale_include_subfolders"))->isChecked());

    QVERIFY(includeSubfolders(true));
    QVERIFY(waitForRecursive(container, 4));
    // Files of every depth, no folders (also no empty ones), sorted by name.
    QCOMPARE(
        shownPaths(container),
        QStringList(
            {QStringLiteral("root/a/one.txt"), QStringLiteral("root/a/b/c/three.txt"), QStringLiteral("root/top.txt"), QStringLiteral("root/a/b/two.png")}));
    const KFileItemList items = container->view()->items();
    for (const KFileItem &item : items) {
        QVERIFY(!item.isDir());
    }
    QCOMPARE(host->contentScope(), WhaleContentScope::RecursiveFiles);
    QCOMPARE(host->contentScopeOrigin(), QStringLiteral("user"));
    QCOMPARE(host->recursiveContent()->fileCount(), 4);
    QCOMPARE(container->url(), url(QStringLiteral("root"))); // no navigation

    QVERIFY(includeSubfolders(false));
    QVERIFY(waitForListing(container, 4));
    QCOMPARE(host->contentScope(), WhaleContentScope::CurrentFolder);
    QCOMPARE(container->url(), url(QStringLiteral("root")));
}

void WhaleRecursiveContentTest::testHiddenFiles()
{
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("root")));
    QVERIFY(container);
    QVERIFY(includeSubfolders(true));
    QVERIFY(waitForRecursive(container, 4));
    QVERIFY(!shownPaths(container).contains(QStringLiteral("root/.hidden.txt")));

    // Hidden files and everything in hidden folders appear with "Show Hidden Files".
    container->view()->setHiddenFilesShown(true);
    QVERIFY(waitForRecursive(container, 7));
    const QStringList shown = shownPaths(container);
    QVERIFY(shown.contains(QStringLiteral("root/.hidden.txt")));
    QVERIFY(shown.contains(QStringLiteral("root/.hiddendir/secret.txt")));
    QVERIFY(shown.contains(QStringLiteral("root/visible/.dotfile")));

    container->view()->setHiddenFilesShown(false);
    QVERIFY(waitForRecursive(container, 4));
    QVERIFY(!shownPaths(container).contains(QStringLiteral("root/.hiddendir/secret.txt")));
}

void WhaleRecursiveContentTest::testScopeKeepsViewMode()
{
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("root")));
    QVERIFY(container);
    container->view()->setViewMode(DolphinView::DetailsView);
    QVERIFY(includeSubfolders(true));
    QVERIFY(waitForRecursive(container, 4));
    QCOMPARE(container->view()->viewMode(), DolphinView::DetailsView);
    QVERIFY(container->viewHost()->isDolphinViewShown());
    QVERIFY(includeSubfolders(false));
    QVERIFY(waitForListing(container, 4));
    QCOMPARE(container->view()->viewMode(), DolphinView::DetailsView);
}

void WhaleRecursiveContentTest::testViewModes_data()
{
    QTest::addColumn<int>("mode");
    QTest::newRow("icons") << int(DolphinView::IconsView);
    QTest::newRow("compact") << int(DolphinView::CompactView);
    QTest::newRow("details") << int(DolphinView::DetailsView);
}

void WhaleRecursiveContentTest::testViewModes()
{
    QFETCH(int, mode);
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("root")));
    QVERIFY(container);
    QVERIFY(includeSubfolders(true));
    QVERIFY(waitForRecursive(container, 4));
    // Switching the mode keeps the scope; the same items in the same order.
    const QStringList before = shownPaths(container);
    container->view()->setViewMode(DolphinView::Mode(mode));
    QCOMPARE(container->view()->viewMode(), DolphinView::Mode(mode));
    QVERIFY(container->viewHost()->isRecursiveContentShown());
    QCOMPARE(shownPaths(container), before);
    // Each deep file has a widget in the view.
    QTRY_VERIFY(!itemPosition(container, url(QStringLiteral("root/a/b/c/three.txt"))).isNull());
    QVERIFY(action(QStringLiteral("whale_include_subfolders"))->isChecked());
}

void WhaleRecursiveContentTest::testProjectionEqualsListing()
{
    // A projected item equals the one KIO's directory lister lists for the same file.
    QVERIFY(QFile::link(path(QStringLiteral("root/top.txt")), path(QStringLiteral("root/a/link.txt"))));
    QVERIFY(QFile::link(path(QStringLiteral("root/a/b")), path(QStringLiteral("root/a/folderlink"))));
    QVERIFY(QFile::link(path(QStringLiteral("root/missing")), path(QStringLiteral("root/a/broken"))));
    KCoreDirLister lister;
    lister.setDelayedMimeTypes(true);
    lister.setShowHiddenFiles(true);
    lister.openUrl(url(QStringLiteral("root/a")));
    QTRY_VERIFY(lister.isFinished());
    const KFileItemList listed = lister.items();
    QVERIFY(listed.count() >= 5);
    int compared = 0;
    for (const KFileItem &expected : listed) {
        bool isFolder = false;
        const KFileItem projected = WhaleRecursiveProjection::fileItem(expected.localPath(), &isFolder);
        if (expected.isDir()) {
            QVERIFY2(isFolder && projected.isNull(), qPrintable(expected.name()));
            continue;
        }
        QVERIFY2(!projected.isNull(), qPrintable(expected.name()));
        QCOMPARE(projected.url(), expected.url());
        QCOMPARE(projected.name(), expected.name());
        QCOMPARE(projected.isLink(), expected.isLink());
        QCOMPARE(projected.linkDest(), expected.linkDest());
        QCOMPARE(projected.mode(), expected.mode());
        QCOMPARE(projected.permissions(), expected.permissions());
        QCOMPARE(projected.size(), expected.size());
        QCOMPARE(projected.time(KFileItem::ModificationTime), expected.time(KFileItem::ModificationTime));
        QCOMPARE(projected.user(), expected.user());
        QCOMPARE(projected.group(), expected.group());
        QCOMPARE(projected.isMimeTypeKnown(), expected.isMimeTypeKnown());
        QVERIFY2(projected.cmp(expected), qPrintable(expected.name()));
        ++compared;
    }
    QCOMPARE(compared, 3); // one.txt, link.txt, broken
}

void WhaleRecursiveContentTest::testExternalItemsModel()
{
    // The model on its own: KFileItemModel's external items.
    KFileItemModel model;
    QSignalSpy started(&model, &KFileItemModel::directoryLoadingStarted);
    QSignalSpy completed(&model, &KFileItemModel::directoryLoadingCompleted);
    QSignalSpy inserted(&model, &KFileItemModel::itemsInserted);
    QSignalSpy removed(&model, &KFileItemModel::itemsRemoved);
    model.loadDirectory(url(QStringLiteral("root")));
    QTRY_COMPARE(completed.count(), 1);
    QCOMPARE(model.count(), 4); // a, empty, visible, top.txt (no hidden files)

    auto itemOf = [this](const QString &file) {
        return WhaleRecursiveProjection::fileItem(path(file));
    };
    const KFileItem top = itemOf(QStringLiteral("root/top.txt"));
    const KFileItem one = itemOf(QStringLiteral("root/a/one.txt"));
    const KFileItem two = itemOf(QStringLiteral("root/a/b/two.png"));

    // Enabling empties the model; the items are what is given.
    model.setExternalItemsEnabled(true);
    QVERIFY(model.externalItemsEnabled());
    QCOMPARE(model.count(), 0);
    model.setExternalItems({top, one});
    QCOMPARE(model.count(), 2);
    QCOMPARE(model.index(one.url()) >= 0, true);
    QCOMPARE(model.expandedParentsCount(model.index(one.url())), 0); // flat, no parent

    // A diff: unchanged items stay where they are (no remove/insert for them).
    removed.clear();
    inserted.clear();
    model.setExternalItems({top, one, two});
    QCOMPARE(model.count(), 3);
    QCOMPARE(removed.count(), 0);
    QCOMPARE(inserted.count(), 1);
    model.setExternalItems({top, two});
    QCOMPARE(model.count(), 2);
    QCOMPARE(model.index(one.url()), -1);

    // Filtered items count as items: a diff neither duplicates nor loses them.
    model.setNameFilter(QStringLiteral("*.png"));
    QCOMPARE(model.count(), 1);
    model.setExternalItems({top, two, one});
    QCOMPARE(model.count(), 1);
    model.setNameFilter(QString());
    QCOMPARE(model.count(), 3);
    model.setNameFilter(QStringLiteral("*.png"));
    model.setExternalItems({two});
    model.setNameFilter(QString());
    QCOMPARE(model.count(), 1);

    // Loading the directory again waits for its external items before it completes.
    completed.clear();
    model.refreshDirectory(url(QStringLiteral("root")));
    QCOMPARE(model.count(), 0); // the lister's clear
    QTest::qWait(300);
    QCOMPARE(completed.count(), 0);
    model.setExternalItems({top, one});
    QCOMPARE(completed.count(), 1);
    QCOMPARE(model.count(), 2);

    // Disabling shows the lister's items again, at once when it has loaded the directory.
    model.setExternalItemsEnabled(false);
    QCOMPARE(model.count(), 4);
    QVERIFY(model.index(url(QStringLiteral("root/a"))) >= 0);
    QCOMPARE(model.index(one.url()), -1);

    // External items are ignored while disabled.
    model.setExternalItems({one});
    QCOMPARE(model.count(), 4);
}

void WhaleRecursiveContentTest::testLocationRemoved()
{
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("root/a")));
    QVERIFY(container);
    QVERIFY(includeSubfolders(true));
    QVERIFY(waitForRecursive(container, 3));
    // The location itself vanishes: Dolphin's handling, as for its own listing.
    QSignalSpy removed(container->view(), &DolphinView::currentDirectoryRemoved);
    QVERIFY(QDir(path(QStringLiteral("root/a"))).removeRecursively());
    QTRY_VERIFY(removed.count() > 0);
}

void WhaleRecursiveContentTest::testRelativePath()
{
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("root")));
    QVERIFY(container);
    container->view()->setViewMode(DolphinView::DetailsView);
    container->view()->setVisibleRoles({"text", "relativePath", "size"});
    QVERIFY(includeSubfolders(true));
    QVERIFY(waitForRecursive(container, 4));
    KFileItemModel *model = modelOf(container->view());
    auto relativePathOf = [model, this](const QString &file) {
        return model->data(model->index(url(file))).value("relativePath").toString();
    };
    QCOMPARE(relativePathOf(QStringLiteral("root/a/b/c/three.txt")), QStringLiteral("a/b/c/"));
    QCOMPARE(relativePathOf(QStringLiteral("root/a/one.txt")), QStringLiteral("a/"));
    QCOMPARE(relativePathOf(QStringLiteral("root/top.txt")), QString());
    // A column of the Details view like any other, also for sorting.
    const auto roles = KFileItemModel::rolesInformation();
    QVERIFY(std::any_of(roles.cbegin(), roles.cend(), [](const KFileItemModel::RoleInfo &info) {
        return info.role == "relativePath" && !info.requiresBaloo;
    }));
    container->view()->setSortRole("relativePath");
    QTRY_COMPARE(shownPaths(container).first(), QStringLiteral("root/a/one.txt"));
    QCOMPARE(shownPaths(container).last(), QStringLiteral("root/top.txt")); // empty values last
}

// ---------------------------------------------------------------------------------------
// Selection

void WhaleRecursiveContentTest::testSelection()
{
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("root")));
    QVERIFY(container);
    // The selected item of the location stays selected when subfolders are included.
    select(container, {url(QStringLiteral("root/top.txt"))});
    QTRY_COMPARE(container->view()->selectedItemsCount(), 1);
    QVERIFY(includeSubfolders(true));
    QVERIFY(waitForRecursive(container, 4));
    QTRY_COMPARE(container->view()->selectedItems().urlList(), QList<QUrl>({url(QStringLiteral("root/top.txt"))}));

    // Single: a deep file, with its real URL.
    select(container, {url(QStringLiteral("root/a/b/c/three.txt"))});
    QCOMPARE(container->view()->selectedItems().urlList(), QList<QUrl>({url(QStringLiteral("root/a/b/c/three.txt"))}));

    // Multi: with the selection manager, as Ctrl+click does.
    KItemListSelectionManager *selection = container->view()->itemListContainer()->controller()->selectionManager();
    KFileItemModel *model = modelOf(container->view());
    selection->setSelected(model->index(url(QStringLiteral("root/a/one.txt"))), 1, KItemListSelectionManager::Toggle);
    QList<QUrl> selected = container->view()->selectedItems().urlList();
    std::sort(selected.begin(), selected.end());
    QCOMPARE(selected, QList<QUrl>({url(QStringLiteral("root/a/b/c/three.txt")), url(QStringLiteral("root/a/one.txt"))}));
    // Select All: every file below.
    action(KStandardAction::name(KStandardAction::SelectAll))->trigger();
    QCOMPARE(container->view()->selectedItemsCount(), 4);

    // Back to the current folder: what the location lists stays selected.
    select(container, {url(QStringLiteral("root/top.txt")), url(QStringLiteral("root/a/one.txt"))});
    QVERIFY(includeSubfolders(false));
    QVERIFY(waitForListing(container, 4));
    QTRY_COMPARE(container->view()->selectedItems().urlList(), QList<QUrl>({url(QStringLiteral("root/top.txt"))}));
}

// ---------------------------------------------------------------------------------------
// Operations

void WhaleRecursiveContentTest::testOpen()
{
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("root")));
    QVERIFY(container);
    QVERIFY(includeSubfolders(true));
    QVERIFY(waitForRecursive(container, 4));
    container->viewHost()->bridge()->activateItems({url(QStringLiteral("root/a/b/c/three.txt"))});
    QTRY_COMPARE_WITH_TIMEOUT(openedFiles(), QStringList({path(QStringLiteral("root/a/b/c/three.txt"))}), 10000);
    QCOMPARE(container->url(), url(QStringLiteral("root")));
}

void WhaleRecursiveContentTest::testRename()
{
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("root")));
    QVERIFY(container);
    QVERIFY(includeSubfolders(true));
    QVERIFY(waitForRecursive(container, 4));
    select(container, {url(QStringLiteral("root/a/b/c/three.txt"))});
    action(KStandardAction::name(KStandardAction::RenameFile))->trigger();
    KIO::RenameFileDialog *dialog = nullptr;
    QTRY_VERIFY((dialog = visibleDialog<KIO::RenameFileDialog>()));
    auto edit = dialog->findChild<QLineEdit *>();
    QVERIFY(edit);
    edit->setText(QStringLiteral("renamed.txt"));
    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    // Renamed in its own folder (not the location), shown and selected under the new name.
    QTRY_VERIFY(QFile::exists(path(QStringLiteral("root/a/b/c/renamed.txt"))));
    QVERIFY(!QFile::exists(path(QStringLiteral("root/renamed.txt"))));
    QTRY_VERIFY(shownPaths(container).contains(QStringLiteral("root/a/b/c/renamed.txt")));
    QVERIFY(!shownPaths(container).contains(QStringLiteral("root/a/b/c/three.txt")));
    QCOMPARE(container->view()->itemsCount(), 4);
    QTRY_COMPARE(container->view()->selectedItems().urlList(), QList<QUrl>({url(QStringLiteral("root/a/b/c/renamed.txt"))}));
}

void WhaleRecursiveContentTest::testTrashAndDelete()
{
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("root")));
    QVERIFY(container);
    QVERIFY(includeSubfolders(true));
    QVERIFY(waitForRecursive(container, 4));

    select(container, {url(QStringLiteral("root/a/b/c/three.txt"))});
    QTRY_VERIFY(action(KStandardAction::name(KStandardAction::MoveToTrash))->isEnabled());
    action(KStandardAction::name(KStandardAction::MoveToTrash))->trigger();
    QTRY_VERIFY(!QFile::exists(path(QStringLiteral("root/a/b/c/three.txt"))));
    QVERIFY(QFile::exists(m_tmpHome.filePath(QStringLiteral("data/Trash/files/three.txt"))));
    QVERIFY(waitForRecursive(container, 3));

    select(container, {url(QStringLiteral("root/a/one.txt")), url(QStringLiteral("root/a/b/two.png"))});
    action(KStandardAction::name(KStandardAction::DeleteFile))->trigger();
    QTRY_VERIFY(!QFile::exists(path(QStringLiteral("root/a/one.txt"))));
    QTRY_VERIFY(!QFile::exists(path(QStringLiteral("root/a/b/two.png"))));
    QVERIFY(waitForRecursive(container, 1));
    QCOMPARE(shownPaths(container), QStringList({QStringLiteral("root/top.txt")}));
    // Only the selected files went: their folders and the location stay.
    QVERIFY(QFileInfo(path(QStringLiteral("root/a/b/c"))).isDir());
    QVERIFY(QFile::exists(path(QStringLiteral("root/top.txt"))));
}

void WhaleRecursiveContentTest::testCopyCut()
{
    if (QGuiApplication::platformName() != QLatin1String("offscreen")) {
        QSKIP("uses the clipboard, which is the user's own outside of the offscreen platform");
    }
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("root")));
    QVERIFY(container);
    QVERIFY(includeSubfolders(true));
    QVERIFY(waitForRecursive(container, 4));
    select(container, {url(QStringLiteral("root/a/b/c/three.txt")), url(QStringLiteral("root/a/one.txt"))});
    action(KStandardAction::name(KStandardAction::Copy))->trigger();
    QList<QUrl> copied = KUrlMimeData::urlsFromMimeData(QApplication::clipboard()->mimeData());
    std::sort(copied.begin(), copied.end());
    QCOMPARE(copied, QList<QUrl>({url(QStringLiteral("root/a/b/c/three.txt")), url(QStringLiteral("root/a/one.txt"))}));
    QVERIFY(!KIO::isClipboardDataCut(QApplication::clipboard()->mimeData()));

    select(container, {url(QStringLiteral("root/a/b/two.png"))});
    action(KStandardAction::name(KStandardAction::Cut))->trigger();
    QCOMPARE(KUrlMimeData::urlsFromMimeData(QApplication::clipboard()->mimeData()), QList<QUrl>({url(QStringLiteral("root/a/b/two.png"))}));
    QVERIFY(KIO::isClipboardDataCut(QApplication::clipboard()->mimeData()));

    // Copy Location: the real path.
    select(container, {url(QStringLiteral("root/a/b/c/three.txt"))});
    action(QStringLiteral("copy_location"))->trigger();
    QCOMPARE(QApplication::clipboard()->text(), path(QStringLiteral("root/a/b/c/three.txt")));
}

void WhaleRecursiveContentTest::testDuplicate()
{
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("root")));
    QVERIFY(container);
    QVERIFY(includeSubfolders(true));
    QVERIFY(waitForRecursive(container, 4));
    select(container, {url(QStringLiteral("root/a/b/c/three.txt"))});
    QTRY_VERIFY(action(QStringLiteral("duplicate"))->isEnabled());
    action(QStringLiteral("duplicate"))->trigger();
    // Next to the original, shown and selected.
    QTRY_VERIFY(QFile::exists(path(QStringLiteral("root/a/b/c/three copy.txt"))));
    QVERIFY(!QFile::exists(path(QStringLiteral("root/three copy.txt"))));
    QVERIFY(waitForRecursive(container, 5));
    QTRY_COMPARE(container->view()->selectedItems().urlList(), QList<QUrl>({url(QStringLiteral("root/a/b/c/three copy.txt"))}));
}

void WhaleRecursiveContentTest::testProperties()
{
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("root")));
    QVERIFY(container);
    QVERIFY(includeSubfolders(true));
    QVERIFY(waitForRecursive(container, 4));
    select(container, {url(QStringLiteral("root/a/b/two.png"))});
    action(QStringLiteral("properties"))->trigger();
    KPropertiesDialog *dialog = nullptr;
    QTRY_VERIFY((dialog = visibleDialog<KPropertiesDialog>()));
    QCOMPARE(dialog->item().url(), url(QStringLiteral("root/a/b/two.png")));
    dialog->reject();
}

void WhaleRecursiveContentTest::testCopyAndMoveToOtherPane()
{
    DolphinViewContainer *left = showMainWindow(url(QStringLiteral("root")));
    QVERIFY(left);
    QVERIFY(includeSubfolders(true));
    QVERIFY(waitForRecursive(left, 4));
    DolphinViewContainer *right = openSplit(url(QStringLiteral("target")));
    QVERIFY(right);
    left->setActive(true);
    QTRY_COMPARE(m_mainWindow->activeViewContainer(), left);

    select(left, {url(QStringLiteral("root/a/b/c/three.txt"))});
    QTRY_VERIFY(action(QStringLiteral("copy_to_inactive_split_view"))->isEnabled());
    action(QStringLiteral("copy_to_inactive_split_view"))->trigger();
    QTRY_VERIFY(QFile::exists(path(QStringLiteral("target/three.txt"))));
    QVERIFY(QFile::exists(path(QStringLiteral("root/a/b/c/three.txt"))));

    select(left, {url(QStringLiteral("root/a/one.txt"))});
    QTRY_VERIFY(action(QStringLiteral("move_to_inactive_split_view"))->isEnabled());
    action(QStringLiteral("move_to_inactive_split_view"))->trigger();
    QTRY_VERIFY(QFile::exists(path(QStringLiteral("target/one.txt"))));
    QTRY_VERIFY(!QFile::exists(path(QStringLiteral("root/a/one.txt"))));
    QVERIFY(waitForRecursive(left, 3));
    QVERIFY(!right->viewHost()->isRecursiveContentShown()); // the halves are independent
}

void WhaleRecursiveContentTest::testDragOut()
{
    DolphinViewContainer *left = showMainWindow(url(QStringLiteral("root")));
    QVERIFY(left);
    QVERIFY(includeSubfolders(true));
    QVERIFY(waitForRecursive(left, 4));
    DolphinViewContainer *right = openSplit(url(QStringLiteral("target")));
    QVERIFY(right);

    // The drag data of the file list itself (KItemListController), with the real URLs.
    std::unique_ptr<QMimeData> data(
        left->viewHost()->bridge()->createDragMimeData({url(QStringLiteral("root/a/b/c/three.txt")), url(QStringLiteral("root/a/b/two.png"))}));
    QVERIFY(data);
    QList<QUrl> dragged = KUrlMimeData::urlsFromMimeData(data.get());
    std::sort(dragged.begin(), dragged.end());
    QCOMPARE(dragged, QList<QUrl>({url(QStringLiteral("root/a/b/c/three.txt")), url(QStringLiteral("root/a/b/two.png"))}));

    // Dropped onto the other half (a real folder), copied there (Ctrl).
    QVERIFY(sendDrag(right->viewHost()->bridge()->fileListDragWidget(), backgroundPosition(right), data.get(), 2, true));
    QTRY_VERIFY(QFile::exists(path(QStringLiteral("target/three.txt"))));
    QTRY_VERIFY(QFile::exists(path(QStringLiteral("target/two.png"))));
    QVERIFY(QFile::exists(path(QStringLiteral("root/a/b/c/three.txt"))));
}

// ---------------------------------------------------------------------------------------
// Drops and Paste

void WhaleRecursiveContentTest::testDropsAreRefused()
{
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("root")));
    QVERIFY(container);
    QVERIFY(includeSubfolders(true));
    QVERIFY(waitForRecursive(container, 4));
    WhaleViewHost *host = container->viewHost();
    QSignalSpy messages(host, &WhaleViewHost::messageRequested);
    QWidget *widget = host->bridge()->fileListDragWidget();

    QMimeData data;
    data.setUrls({url(QStringLiteral("target/existing.txt"))});

    // Background: no drop action, no operation, one explanation despite many moves.
    QVERIFY(!sendDrag(widget, backgroundPosition(container), &data, 25, true));
    QCOMPARE(messages.count(), 1);
    QCOMPARE(messages.first().first().toString(), WhaleRecursiveContent::dropRefusal());
    QCOMPARE(host->recursiveContent()->lastRefusal(), WhaleRecursiveContent::dropRefusal());

    // Onto a file (also one whose folder would be a target): refused as well.
    QMimeData other;
    other.setUrls({url(QStringLiteral("target/existing.txt"))});
    const QPoint onFile = itemPosition(container, url(QStringLiteral("root/a/b/c/three.txt")));
    QVERIFY(!onFile.isNull());
    QVERIFY(!sendDrag(widget, onFile, &other, 10, true));
    QCOMPARE(messages.count(), 2);
    // The same drag entering again is not explained twice.
    QVERIFY(!sendDrag(widget, onFile, &other, 3, false));
    QCOMPARE(messages.count(), 2);

    // Nothing was copied anywhere below the location.
    QTest::qWait(300);
    QVERIFY(!QFile::exists(path(QStringLiteral("root/existing.txt"))));
    QVERIFY(!QFile::exists(path(QStringLiteral("root/a/b/c/existing.txt"))));
    QVERIFY(!QFile::exists(path(QStringLiteral("root/a/b/existing.txt"))));
    QVERIFY(waitForRecursive(container, 4));
    // The explanation is in the view area's message area.
    QTRY_VERIFY(std::ranges::any_of(container->findChildren<KMessageWidget *>(), [](const KMessageWidget *widget) {
        return widget->text() == WhaleRecursiveContent::dropRefusal();
    }));

    // A drag the view itself started (a press in it first) is refused without a word.
    QMouseEvent press(QEvent::MouseButtonPress, onFile, widget->mapToGlobal(onFile), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(widget, &press);
    std::unique_ptr<QMimeData> own(host->bridge()->createDragMimeData({url(QStringLiteral("root/a/one.txt"))}));
    QVERIFY(!sendDrag(widget, onFile, own.get(), 3, true));
    QCOMPARE(messages.count(), 2);
}

void WhaleRecursiveContentTest::testPasteIsRefused()
{
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("root")));
    QVERIFY(container);
    QVERIFY(includeSubfolders(true));
    QVERIFY(waitForRecursive(container, 4));
    WhaleViewHost *host = container->viewHost();
    QSignalSpy messages(host, &WhaleViewHost::messageRequested);
    QVERIFY(!host->recursiveContent()->pasteRefusal().isEmpty());

    if (QGuiApplication::platformName() == QLatin1String("offscreen")) {
        // Something to paste: no operation, an explanation.
        auto data = new QMimeData;
        data->setUrls({url(QStringLiteral("target/existing.txt"))});
        QApplication::clipboard()->setMimeData(data);
        QTRY_VERIFY(action(KStandardAction::name(KStandardAction::Paste))->isEnabled());
    }
    if (action(KStandardAction::name(KStandardAction::Paste))->isEnabled()) {
        action(KStandardAction::name(KStandardAction::Paste))->trigger();
    } else {
        // Outside offscreen the clipboard is the user's: Dolphin's Paste slot directly.
        QVERIFY(QMetaObject::invokeMethod(m_mainWindow.data(), "paste"));
    }
    QCOMPARE(messages.count(), 1);
    QCOMPARE(messages.first().first().toString(), host->recursiveContent()->pasteRefusal());
    QTest::qWait(300);
    QVERIFY(!QFile::exists(path(QStringLiteral("root/existing.txt"))));

    // Back in the current folder, Paste is Dolphin's again.
    QVERIFY(includeSubfolders(false));
    QVERIFY(waitForListing(container, 4));
    QVERIFY(host->recursiveContent()->pasteRefusal().isEmpty());
    if (QGuiApplication::platformName() == QLatin1String("offscreen")) {
        action(KStandardAction::name(KStandardAction::Paste))->trigger();
        QTRY_VERIFY(QFile::exists(path(QStringLiteral("root/existing.txt"))));
    }
    QCOMPARE(messages.count(), 1);
}

void WhaleRecursiveContentTest::testCurrentFolderAcceptsDropsAgain()
{
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("root")));
    QVERIFY(container);
    QVERIFY(includeSubfolders(true));
    QVERIFY(waitForRecursive(container, 4));
    QVERIFY(includeSubfolders(false));
    QVERIFY(waitForListing(container, 4));
    QSignalSpy messages(container->viewHost(), &WhaleViewHost::messageRequested);

    QMimeData data;
    data.setUrls({url(QStringLiteral("target/existing.txt"))});
    QVERIFY(sendDrag(container->viewHost()->bridge()->fileListDragWidget(), backgroundPosition(container), &data, 2, true));
    QTRY_VERIFY(QFile::exists(path(QStringLiteral("root/existing.txt"))));
    QCOMPARE(messages.count(), 0);
}

void WhaleRecursiveContentTest::testSplitDropIntoRecursive()
{
    DolphinViewContainer *left = showMainWindow(url(QStringLiteral("root")));
    QVERIFY(left);
    QVERIFY(includeSubfolders(true));
    QVERIFY(waitForRecursive(left, 4));
    DolphinViewContainer *right = openSplit(url(QStringLiteral("target")));
    QVERIFY(right);
    QSignalSpy messages(left->viewHost(), &WhaleViewHost::messageRequested);

    // From the current folder on the right into the recursive view on the left: refused.
    std::unique_ptr<QMimeData> data(right->viewHost()->bridge()->createDragMimeData({url(QStringLiteral("target/existing.txt"))}));
    QVERIFY(data);
    QVERIFY(!sendDrag(left->viewHost()->bridge()->fileListDragWidget(), backgroundPosition(left), data.get(), 5, true));
    QCOMPARE(messages.count(), 1);
    QTest::qWait(300);
    QVERIFY(!QFile::exists(path(QStringLiteral("root/existing.txt"))));
    QVERIFY(QFile::exists(path(QStringLiteral("target/existing.txt"))));
}

// ---------------------------------------------------------------------------------------
// Sorting, filter, changes

void WhaleRecursiveContentTest::testSort()
{
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("root")));
    QVERIFY(container);
    QVERIFY(includeSubfolders(true));
    QVERIFY(waitForRecursive(container, 4));
    DolphinView *view = container->view();

    // Dolphin prefers descending sizes and dates; ascending first.
    view->setSortRole("size");
    view->setSortOrder(Qt::AscendingOrder);
    QTRY_COMPARE(
        shownPaths(container),
        QStringList(
            {QStringLiteral("root/a/one.txt"), QStringLiteral("root/a/b/c/three.txt"), QStringLiteral("root/top.txt"), QStringLiteral("root/a/b/two.png")}));
    view->setSortOrder(Qt::DescendingOrder);
    QTRY_COMPARE(shownPaths(container).first(), QStringLiteral("root/a/b/two.png"));

    view->setSortRole("modificationtime");
    view->setSortOrder(Qt::AscendingOrder);
    QTRY_COMPARE(
        shownPaths(container),
        QStringList(
            {QStringLiteral("root/a/one.txt"), QStringLiteral("root/a/b/c/three.txt"), QStringLiteral("root/top.txt"), QStringLiteral("root/a/b/two.png")}));

    view->setSortRole("text");
    QTRY_COMPARE(
        shownPaths(container),
        QStringList(
            {QStringLiteral("root/a/one.txt"), QStringLiteral("root/a/b/c/three.txt"), QStringLiteral("root/top.txt"), QStringLiteral("root/a/b/two.png")}));
    view->setSortRole("type");
    QTRY_COMPARE(shownPaths(container).last(), QStringLiteral("root/a/b/two.png")); // "plain text document" before "PNG image"
}

void WhaleRecursiveContentTest::testFilter()
{
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("root")));
    QVERIFY(container);
    QVERIFY(includeSubfolders(true));
    QVERIFY(waitForRecursive(container, 4));
    // The filter bar's filter, on the files below.
    container->setFilterBarVisible(true);
    container->view()->setNameFilter(QStringLiteral("*.png"));
    QTRY_COMPARE(shownPaths(container), QStringList({QStringLiteral("root/a/b/two.png")}));
    container->view()->setNameFilter(QStringLiteral("o"));
    QTRY_COMPARE(shownPaths(container).count(), 3); // one.txt, top.txt, two.png
    // A change while filtered keeps the filter.
    m_dir->createFile(QStringLiteral("root/a/b/c/new.png"), QByteArray(10, 'n'));
    container->view()->setNameFilter(QStringLiteral("*.png"));
    QTRY_COMPARE(shownPaths(container), QStringList({QStringLiteral("root/a/b/c/new.png"), QStringLiteral("root/a/b/two.png")}));
    container->view()->setNameFilter(QString());
    QVERIFY(waitForRecursive(container, 5));
}

void WhaleRecursiveContentTest::testChangesAreShown()
{
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("root")));
    QVERIFY(container);
    QVERIFY(includeSubfolders(true));
    QVERIFY(waitForRecursive(container, 4));
    select(container, {url(QStringLiteral("root/top.txt"))});
    // A change deep below (by any program): the cache drops the tree, the view scans again
    // and keeps the selection.
    m_dir->createFile(QStringLiteral("root/a/b/c/d/new.txt"), QByteArray(10, 'n'));
    QVERIFY(waitForRecursive(container, 5));
    QVERIFY(shownPaths(container).contains(QStringLiteral("root/a/b/c/d/new.txt")));
    QCOMPARE(container->view()->selectedItems().urlList(), QList<QUrl>({url(QStringLiteral("root/top.txt"))}));
    QFile::remove(path(QStringLiteral("root/a/b/c/d/new.txt")));
    QVERIFY(waitForRecursive(container, 4));
    // F5 scans again, too.
    const int updates = container->viewHost()->recursiveContent()->timing().updates;
    action(QStringLiteral("view_redisplay"))->trigger();
    QTRY_VERIFY(container->viewHost()->recursiveContent()->timing().updates > updates);
    QVERIFY(waitForRecursive(container, 4));
}

// ---------------------------------------------------------------------------------------
// State

void WhaleRecursiveContentTest::testPreferencePerLocation()
{
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("root")));
    QVERIFY(container);
    WhaleViewHost *host = container->viewHost();
    QVERIFY(includeSubfolders(true));
    QVERIFY(waitForRecursive(container, 4));
    QCOMPARE(WhaleViewPreferences::instance()->contentScope(url(QStringLiteral("root"))), WhaleContentScope::RecursiveFiles);

    // A subfolder does not inherit it.
    container->setUrl(url(QStringLiteral("root/a")));
    QVERIFY(waitForListing(container, 2));
    QCOMPARE(host->contentScope(), WhaleContentScope::CurrentFolder);
    QCOMPARE(host->contentScopeOrigin(), QStringLiteral("default"));
    QVERIFY(!action(QStringLiteral("whale_include_subfolders"))->isChecked());

    // A new navigation to the location applies it.
    container->setUrl(url(QStringLiteral("target")));
    QVERIFY(waitForListing(container, 1));
    container->setUrl(url(QStringLiteral("root")));
    QVERIFY(waitForRecursive(container, 4));
    QCOMPARE(host->contentScopeOrigin(), QStringLiteral("preference"));
    QVERIFY(action(QStringLiteral("whale_include_subfolders"))->isChecked());

    // Turning it off forgets it.
    QVERIFY(includeSubfolders(false));
    QCOMPARE(WhaleViewPreferences::instance()->contentScopeCount(), 0);
    // The file's other group (O3) was never touched.
    QVERIFY(WhaleViewPreferences::instance()->preferredView(url(QStringLiteral("root"))).isEmpty());
}

void WhaleRecursiveContentTest::testHistory()
{
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("root")));
    QVERIFY(container);
    WhaleViewHost *host = container->viewHost();
    container->view()->setViewMode(DolphinView::DetailsView);
    QVERIFY(includeSubfolders(true));
    QVERIFY(waitForRecursive(container, 4));
    select(container, {url(QStringLiteral("root/a/b/c/three.txt"))});
    container->setUrl(url(QStringLiteral("target")));
    QVERIFY(waitForListing(container, 1));

    // The preference changes meanwhile; Back restores the scope as it was.
    WhaleViewPreferences::instance()->setContentScope(url(QStringLiteral("root")), WhaleContentScope::CurrentFolder);
    action(KStandardAction::name(KStandardAction::Back))->trigger();
    QTRY_COMPARE(container->url(), url(QStringLiteral("root")));
    QVERIFY(waitForRecursive(container, 4));
    QCOMPARE(host->contentScopeOrigin(), QStringLiteral("history"));
    QCOMPARE(container->view()->viewMode(), DolphinView::DetailsView);
    // Dolphin's own state of the entry: the current item.
    QTRY_COMPARE(container->view()->itemListContainer()->controller()->selectionManager()->currentItem(),
                 modelOf(container->view())->index(url(QStringLiteral("root/a/b/c/three.txt"))));

    // Forward: the current folder as it was there.
    action(KStandardAction::name(KStandardAction::Forward))->trigger();
    QTRY_COMPARE(container->url(), url(QStringLiteral("target")));
    QVERIFY(waitForListing(container, 1));
    QCOMPARE(host->contentScopeOrigin(), QStringLiteral("history"));

    // Back once more, then turning it off there is remembered in the entry, too.
    action(KStandardAction::name(KStandardAction::Back))->trigger();
    QVERIFY(waitForRecursive(container, 4));
    QVERIFY(includeSubfolders(false));
    QVERIFY(waitForListing(container, 4));
    action(KStandardAction::name(KStandardAction::Forward))->trigger();
    QVERIFY(waitForListing(container, 1));
    action(KStandardAction::name(KStandardAction::Back))->trigger();
    QTRY_COMPARE(container->url(), url(QStringLiteral("root")));
    QVERIFY(waitForListing(container, 4));
}

void WhaleRecursiveContentTest::testSession()
{
    DolphinViewContainer *left = showMainWindow(url(QStringLiteral("root")));
    QVERIFY(left);
    QVERIFY(includeSubfolders(true));
    QVERIFY(waitForRecursive(left, 4));
    DolphinViewContainer *right = openSplit(url(QStringLiteral("root")));
    QVERIFY(right);
    // The right half at the same location: the location's preference applies there too, the
    // user turns it off for this half.
    QVERIFY(waitForRecursive(right, 4));
    QVERIFY(includeSubfolders(false));
    QVERIFY(waitForListing(right, 4));

    KConfig config(m_tmpHome.filePath(QStringLiteral("session")), KConfig::SimpleConfig);
    KConfigGroup group = config.group(QStringLiteral("Window"));
    m_mainWindow->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"))->saveProperties(group);
    m_mainWindow.reset();
    // The preference changes before the restart; the session wins.
    WhaleViewPreferences::instance()->setContentScope(url(QStringLiteral("root")), WhaleContentScope::RecursiveFiles);

    m_mainWindow.reset(new DolphinMainWindow());
    m_mainWindow->resize(1000, 700);
    // As KMainWindow::restore(): the session first, then the window is shown. A shown window
    // without a tab must not get events (e.g. a clipboard change on Wayland: Dolphin's Paste
    // action asks the active view area, which does not exist yet).
    m_mainWindow->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"))->readProperties(group);
    m_mainWindow->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_mainWindow.data()));
    DolphinTabPage *page = m_mainWindow->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"))->currentTabPage();
    QTRY_VERIFY(page->secondaryViewContainer());
    QVERIFY(waitForRecursive(page->primaryViewContainer(), 4));
    QVERIFY(waitForListing(page->secondaryViewContainer(), 4));
    QCOMPARE(page->primaryViewContainer()->viewHost()->contentScopeOrigin(), QStringLiteral("session"));
    QCOMPARE(page->secondaryViewContainer()->viewHost()->contentScopeOrigin(), QStringLiteral("session"));
    QTest::qWait(100); // nothing pending changes it later
    QVERIFY(page->secondaryViewContainer()->viewHost()->contentScope() == WhaleContentScope::CurrentFolder);
}

void WhaleRecursiveContentTest::testPreferredViewIsSeparate()
{
    // O3 together with F1: A prefers the treemap, B the file list with subfolders.
    WhaleViewPreferences::instance()->setPreferredView(url(QStringLiteral("root/a")), treemapId);
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("root")));
    QVERIFY(container);
    WhaleViewHost *host = container->viewHost();
    QVERIFY(includeSubfolders(true));
    QVERIFY(waitForRecursive(container, 4));
    WhaleViewPreferences::instance()->setPreferredView(url(QStringLiteral("root")), fileListId);
    // Choosing the scope did not choose a view.
    QCOMPARE(WhaleViewPreferences::instance()->preferredView(url(QStringLiteral("root"))), fileListId);
    QCOMPARE(WhaleViewPreferences::instance()->count(), 2);

    container->setUrl(url(QStringLiteral("root/a")));
    QTRY_COMPARE(host->currentViewId(), treemapId);
    QVERIFY(!host->isRecursiveContentShown());
    QVERIFY(!action(QStringLiteral("whale_include_subfolders"))->isEnabled());

    container->setUrl(url(QStringLiteral("root")));
    QTRY_VERIFY(host->isDolphinViewShown());
    QVERIFY(waitForRecursive(container, 4));
    QCOMPARE(host->viewOrigin(), QStringLiteral("preference"));
    QCOMPARE(host->contentScopeOrigin(), QStringLiteral("preference"));
    QCOMPARE(WhaleViewPreferences::instance()->preferredView(url(QStringLiteral("root"))), fileListId);
    QCOMPARE(WhaleViewPreferences::instance()->preferredView(url(QStringLiteral("root/a"))), treemapId);
}

void WhaleRecursiveContentTest::testTreemapIgnoresScope()
{
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("root")));
    QVERIFY(container);
    WhaleViewHost *host = container->viewHost();
    QVERIFY(includeSubfolders(true));
    QVERIFY(waitForRecursive(container, 4));

    // The treemap sees the location's own items and scans its own tree.
    WhaleViewPlugin *plugin = m_mainWindow->findChild<WhaleViewActions *>()->registry()->plugin(treemapId);
    QVERIFY(plugin);
    QVERIFY(host->showPluginView(treemapId, plugin));
    QVERIFY(!host->isRecursiveContentShown());
    QCOMPARE(host->contentScope(), WhaleContentScope::RecursiveFiles); // kept for the file list
    QVERIFY(waitForListing(container, 4));
    QVERIFY(!action(QStringLiteral("whale_include_subfolders"))->isEnabled());
    // Back to the file list: with the subfolders again.
    host->showDolphinView();
    QVERIFY(waitForRecursive(container, 4));
    QVERIFY(action(QStringLiteral("whale_include_subfolders"))->isChecked());
}

void WhaleRecursiveContentTest::testRemoteLocation()
{
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("root")));
    QVERIFY(container);
    QVERIFY(includeSubfolders(true));
    QVERIFY(waitForRecursive(container, 4));
    // Only local folders: elsewhere the action is off, the location's items are shown.
    container->setUrl(QUrl(QStringLiteral("trash:/")));
    QTRY_COMPARE(container->url(), QUrl(QStringLiteral("trash:/")));
    QTRY_VERIFY(!container->viewHost()->isRecursiveContentShown());
    QVERIFY(!action(QStringLiteral("whale_include_subfolders"))->isEnabled());
    QVERIFY(!action(QStringLiteral("whale_include_subfolders"))->isChecked());
}

// ---------------------------------------------------------------------------------------
// Performance

void WhaleRecursiveContentTest::testPerformance_data()
{
    QTest::addColumn<int>("files");
    QTest::newRow("1k") << 1000;
    QTest::newRow("10k") << 10000;
    QTest::newRow("100k") << 100000;
}

void WhaleRecursiveContentTest::testPerformance()
{
    QFETCH(int, files);
    // Folders of 50 files in two levels (files / 50 folders), like a source tree.
    const QString root = path(QStringLiteral("big"));
    for (int i = 0; i < files; ++i) {
        const QString folder = QStringLiteral("%1/f%2/g%3").arg(root).arg(i / 2500).arg((i / 50) % 50);
        if (i % 50 == 0) {
            QVERIFY(QDir().mkpath(folder));
        }
        QFile file(QStringLiteral("%1/file-%2.txt").arg(folder).arg(i));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("x");
    }
    const int listed = (files + 2499) / 2500;
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("big")));
    QVERIFY(container);
    container->view()->setViewMode(DolphinView::DetailsView);
    WhaleRecursiveContent *content = container->viewHost()->recursiveContent();

    // The GUI thread's responsiveness: the longest gap of a 10 ms timer during each step.
    QElapsedTimer gapClock;
    qint64 longestGap = 0;
    QTimer ticker;
    ticker.setInterval(10);
    connect(&ticker, &QTimer::timeout, this, [&]() {
        longestGap = std::max(longestGap, gapClock.restart());
    });
    ticker.start();
    QElapsedTimer clock;
    QStringList report;
    auto begin = [&]() {
        QCoreApplication::processEvents();
        longestGap = 0;
        gapClock.restart();
        clock.restart();
    };
    auto end = [&](const QString &step) {
        const qint64 elapsed = clock.elapsed();
        QCoreApplication::processEvents(); // a step that blocked until its end has not ticked yet
        report.append(QStringLiteral("%1 %2 ms (GUI gap %3 ms)").arg(step).arg(elapsed).arg(longestGap));
    };

    begin();
    QVERIFY(includeSubfolders(true));
    QVERIFY(waitForRecursive(container, files));
    end(QStringLiteral("scope on"));
    const WhaleRecursiveContent::Timing first = content->timing();

    begin();
    container->view()->setSortRole("size");
    container->view()->setSortOrder(Qt::AscendingOrder);
    QTRY_VERIFY(!container->viewHost()->bridge()->isLoading());
    QCoreApplication::processEvents();
    end(QStringLiteral("sort by size"));

    begin();
    container->view()->setNameFilter(QStringLiteral("file-1"));
    QTRY_VERIFY(container->view()->itemsCount() < files);
    end(QStringLiteral("filter"));
    begin();
    container->view()->setNameFilter(QString());
    QTRY_COMPARE(container->view()->itemsCount(), files);
    end(QStringLiteral("filter cleared"));

    begin();
    container->view()->setViewMode(DolphinView::IconsView);
    QCoreApplication::processEvents();
    end(QStringLiteral("view switch to Icons"));

    begin();
    QVERIFY(includeSubfolders(false));
    QVERIFY(waitForListing(container, listed));
    end(QStringLiteral("scope off"));

    begin();
    QVERIFY(includeSubfolders(true));
    QVERIFY(waitForRecursive(container, files));
    end(QStringLiteral("scope on again (cache hit)"));
    const WhaleRecursiveContent::Timing again = content->timing();
    ticker.stop();

    qInfo().noquote() << QStringLiteral("F1 %1 files: first scan %2 ms, projection %3 ms, model %4 ms; again: scan %5, projection %6, model %7 ms")
                             .arg(files)
                             .arg(first.scanMs)
                             .arg(first.projectionMs)
                             .arg(first.modelMs)
                             .arg(again.scanMs)
                             .arg(again.projectionMs)
                             .arg(again.modelMs);
    qInfo().noquote() << QStringLiteral("F1 %1 files: ").arg(files) + report.join(QStringLiteral("; "));
}

void WhaleRecursiveContentTest::testListingBaseline_data()
{
    QTest::addColumn<int>("files");
    QTest::addColumn<int>("mode");
    QTest::newRow("10k details") << 10000 << int(DolphinView::DetailsView);
    QTest::newRow("10k icons") << 10000 << int(DolphinView::IconsView);
    QTest::newRow("100k details") << 100000 << int(DolphinView::DetailsView);
    QTest::newRow("100k icons") << 100000 << int(DolphinView::IconsView);
}

void WhaleRecursiveContentTest::testListingBaseline()
{
    // For comparison: Dolphin's own listing of the same number of files in one folder.
    QFETCH(int, files);
    QFETCH(int, mode);
    const QString flat = path(QStringLiteral("flat"));
    QVERIFY(QDir().mkpath(flat));
    for (int i = 0; i < files; ++i) {
        QFile file(QStringLiteral("%1/file-%2.txt").arg(flat).arg(i));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("x");
    }
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("root")));
    QVERIFY(container);
    container->view()->setViewMode(DolphinView::Mode(mode));
    QElapsedTimer gapClock;
    qint64 longestGap = 0;
    QTimer ticker;
    ticker.setInterval(10);
    connect(&ticker, &QTimer::timeout, this, [&]() {
        longestGap = std::max(longestGap, gapClock.restart());
    });
    QElapsedTimer clock;
    clock.start();
    gapClock.start();
    ticker.start();
    container->setUrl(url(QStringLiteral("flat")));
    QVERIFY(waitForListing(container, files));
    const qint64 shown = clock.elapsed();
    ticker.stop();
    qInfo().noquote() << QStringLiteral("Dolphin's listing of %1 files in one folder (%2): shown after %3 ms; longest GUI gap %4 ms")
                             .arg(files)
                             .arg(QString::fromLatin1(QTest::currentDataTag()))
                             .arg(shown)
                             .arg(longestGap);
}

QTEST_MAIN(WhaleRecursiveContentTest)

#include "whalerecursivecontenttest.moc"
