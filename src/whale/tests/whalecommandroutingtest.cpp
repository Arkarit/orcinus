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
#include "kitemviews/private/kitemlistroleeditor.h"
#include "testdir.h"
#include "testhelpers.h"
#include "views/dolphinview.h"
#include "whalecommandrouter.h"
#include "whalefileoperations.h"
#include "whalefileoperationsimpl.h"
#include "whaleview.h"
#include "whaleviewactions.h"
#include "whaleviewcontext.h"
#include "whaleviewhost.h"
#include "whaleviewplugin.h"
#include "whaleviewregistry.h"

#include <KActionCollection>
#include <KApplicationTrader>
#include <KConfigGroup>
#include <KIO/Paste>
#include <KIO/RenameFileDialog>
#include <KLocalizedString>
#include <KPropertiesDialog>
#include <KSharedConfig>
#include <KStandardAction>
#include <KSycoca>
#include <KUrlMimeData>

#include <QClipboard>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QMimeData>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

using Source = WhaleCommandRouter::Source;

namespace
{
const QString treemapId = QStringLiteral("orcinus_treemap");
const QString testListId = QStringLiteral("whale_testlist");
const QString ownId = QStringLiteral("own_selection");

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

QWidget *canvasOf(QWidget *view)
{
    return view->findChild<QWidget *>(QStringLiteral("whaleTreemapCanvas"));
}

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

/**
 * A view with a selection of its own that the test sets (like the treemap's, but any URLs,
 * also the location or several items).
 */
class OwnSelectionView : public WhaleView
{
public:
    explicit OwnSelectionView(WhaleViewContext *context)
        : WhaleView(context)
        , m_widget(new QLineEdit)
    {
        m_widget->setObjectName(QStringLiteral("ownSelectionView"));
    }
    ~OwnSelectionView() override
    {
        delete m_widget;
    }
    QWidget *widget() override
    {
        return m_widget;
    }
    WhaleViewCapabilities capabilities() const override
    {
        return WhaleViewCapability::Selection | WhaleViewCapability::RecursiveContent;
    }
    QList<QUrl> selectedItems() const override
    {
        return m_selected;
    }
    void select(const QList<QUrl> &urls)
    {
        m_selected = urls;
        Q_EMIT selectedItemsChanged();
    }

private:
    QPointer<QLineEdit> m_widget;
    QList<QUrl> m_selected;
};

class OwnSelectionPlugin : public WhaleViewPlugin
{
public:
    WhaleView *createView(WhaleViewContext *context, QObject *parent) override
    {
        Q_UNUSED(parent)
        view = new OwnSelectionView(context);
        return view;
    }
    QPointer<OwnSelectionView> view;
};
}

/**
 * T2: Whale's actions on selected items (menus, toolbar, shortcuts) act on the selection of
 * the active view area's source: the DolphinView's, or a plugin view's own (routed).
 */
class WhaleCommandRoutingTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    // Router / command context
    void testFileListIsTheSource();
    void testPluginViewIsTheSource();
    void testActivePaneDecides();
    void testViewSwitchAndHistory();
    void testEmptySelectionDisables();
    void testDeepSelectionEnables();
    void testReadOnlySelection();
    void testLocationIsProtected();
    void testNoFallbackToTheHiddenSelection();

    // Global actions and shortcuts
    void testEnterOpens();
    void testF2Renames();
    void testDeleteTrashes();
    void testShiftDeleteDeletes();
    void testCopyCutPaste();
    void testProperties();
    void testCopyLocation();
    void testDuplicate();
    void testSeveralItems();

    // Split view
    void testCopyAndMoveToOtherPane();

    // The file list stays Dolphin's
    void testFileListUnchanged();

private:
    DolphinViewContainer *showMainWindow();
    QWidget *showTreemap(DolphinViewContainer *container);
    OwnSelectionView *showOwnSelectionView(DolphinViewContainer *container);
    DolphinViewContainer *openSplit(const QUrl &url);
    QAction *action(const QString &name) const;
    WhaleViewActions *viewActions() const;
    QUrl url(const QString &path) const;
    QString path(const QString &relative) const;
    QString trashedPath(const QString &fileName) const;
    QStringList openedFiles() const;
    bool select(QWidget *view, const QUrl &target);
    void setConfirmations(bool confirm);

    QTemporaryDir m_tmpHome;
    QString m_openerLog;
    QScopedPointer<TestDir> m_dir;
    QList<WhaleFileOperations::Operation> m_confirmed;
    QScopedPointer<DolphinMainWindow> m_mainWindow;
    OwnSelectionPlugin m_ownPlugin;
};

void WhaleCommandRoutingTest::initTestCase()
{
    QVERIFY(m_tmpHome.isValid());
    QCoreApplication::setApplicationName(QStringLiteral(WHALE_COMPONENT));
    QCoreApplication::setOrganizationDomain(QStringLiteral(WHALE_ORG_DOMAIN));
    qputenv("HOME", m_tmpHome.path().toUtf8());
    qputenv("XDG_DATA_HOME", m_tmpHome.filePath(QStringLiteral("data")).toUtf8());
    QStandardPaths::setTestModeEnabled(true);
    TestHelpers::disableAnimations();
    TestHelpers::openNewWindowsInSingleView();
    KLocalizedString::setApplicationDomain(TRANSLATION_DOMAIN);
    QCoreApplication::addLibraryPath(QStringLiteral(WHALE_TEST_PLUGIN_DIR));
    QCoreApplication::addLibraryPath(QStringLiteral(WHALE_TEST_VIEW_PLUGIN_DIR));

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
}

void WhaleCommandRoutingTest::init()
{
    // The tree of the T2 plan: root with deep entries, target beside it.
    m_dir.reset(new TestDir());
    for (const QString &name : {QStringLiteral("open"),
                                QStringLiteral("rename-me"),
                                QStringLiteral("trash-me"),
                                QStringLiteral("delete-me"),
                                QStringLiteral("copy-me"),
                                QStringLiteral("move-me"),
                                QStringLiteral("dup-me")}) {
        m_dir->createFile(QStringLiteral("root/deep/%1.txt").arg(name), QByteArray(20000, 'x'));
    }
    m_dir->createFile(QStringLiteral("root/deep/sub/inner.txt"), QByteArray(20000, 'i'));
    m_dir->createFile(QStringLiteral("root/ro/locked.txt"), QByteArray(20000, 'l'));
    m_dir->createFile(QStringLiteral("root/top.txt"), QByteArray(20000, 't'));
    m_dir->createFile(QStringLiteral("target/existing.txt"), QByteArray(100, 'e'));
    QFile::remove(m_openerLog);
    setConfirmations(false);
    GeneralSettings::setRenameInline(true);
    // Removing and cutting in a treemap are always confirmed: confirmed here, recorded.
    m_confirmed.clear();
    WhaleFileOperationsImpl::setConfirmationRunner([this](WhaleFileOperations::Operation operation, const QList<QUrl> &) {
        m_confirmed.append(operation);
        return true;
    });
    m_mainWindow.reset(new DolphinMainWindow());
}

void WhaleCommandRoutingTest::cleanup()
{
    WhaleFileOperationsImpl::setConfirmationRunner({});
    const auto windows = QApplication::topLevelWidgets();
    for (QWidget *window : windows) {
        if (auto dialog = qobject_cast<QDialog *>(window); dialog && dialog->isVisible()) {
            dialog->reject();
        }
    }
    m_mainWindow.reset();
    if (m_dir) {
        QFile::setPermissions(path(QStringLiteral("root/ro")), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    }
    m_dir.reset();
}

void WhaleCommandRoutingTest::setConfirmations(bool confirm)
{
    KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("kiorc"), KConfig::NoGlobals), QStringLiteral("Confirmations"));
    group.writeEntry("ConfirmDelete", confirm);
    group.writeEntry("ConfirmTrash", confirm);
    group.sync();
}

DolphinViewContainer *WhaleCommandRoutingTest::showMainWindow()
{
    m_mainWindow->openDirectories({url(QStringLiteral("root"))}, false);
    m_mainWindow->resize(1000, 700);
    m_mainWindow->show();
    if (!QTest::qWaitForWindowActive(m_mainWindow.data())) {
        return nullptr;
    }
    DolphinViewContainer *container = m_mainWindow->activeViewContainer();
    container->view()->setHiddenFilesShown(false);
    if (!QTest::qWaitFor(
            [container]() {
                return container->view()->itemsCount() == 3;
            },
            5000)) {
        return nullptr;
    }
    return container;
}

QWidget *WhaleCommandRoutingTest::showTreemap(DolphinViewContainer *container)
{
    WhaleViewPlugin *plugin = viewActions()->registry()->plugin(treemapId);
    if (!plugin || !container->viewHost()->showPluginView(treemapId, plugin)) {
        return nullptr;
    }
    QWidget *view = container->viewHost()->currentView()->widget();
    if (!QTest::qWaitFor(
            [view]() {
                return treemapState(view).value(QStringLiteral("status")).toString() == QLatin1String("ready");
            },
            5000)) {
        return nullptr;
    }
    return view;
}

OwnSelectionView *WhaleCommandRoutingTest::showOwnSelectionView(DolphinViewContainer *container)
{
    if (!container->viewHost()->showPluginView(ownId, &m_ownPlugin)) {
        return nullptr;
    }
    return m_ownPlugin.view;
}

DolphinViewContainer *WhaleCommandRoutingTest::openSplit(const QUrl &secondUrl)
{
    m_mainWindow->actionCollection()->action(QStringLiteral("split_view"))->trigger();
    DolphinTabPage *page = m_mainWindow->findChild<DolphinTabWidget *>()->currentTabPage();
    if (!QTest::qWaitFor(
            [page]() {
                return page->splitViewEnabled() && page->secondaryViewContainer();
            },
            5000)) {
        return nullptr;
    }
    DolphinViewContainer *right = page->secondaryViewContainer();
    right->setUrl(secondUrl);
    if (!QTest::qWaitFor(
            [right, secondUrl]() {
                return right->url() == secondUrl && !right->view()->items().isEmpty();
            },
            5000)) {
        return nullptr;
    }
    return right;
}

QAction *WhaleCommandRoutingTest::action(const QString &name) const
{
    return m_mainWindow->actionCollection()->action(name);
}

WhaleViewActions *WhaleCommandRoutingTest::viewActions() const
{
    return m_mainWindow->findChild<WhaleViewActions *>();
}

QUrl WhaleCommandRoutingTest::url(const QString &relative) const
{
    return QUrl::fromLocalFile(path(relative));
}

QString WhaleCommandRoutingTest::path(const QString &relative) const
{
    return m_dir->path() + (relative.isEmpty() ? QString() : QLatin1Char('/') + relative);
}

QString WhaleCommandRoutingTest::trashedPath(const QString &fileName) const
{
    return m_tmpHome.filePath(QStringLiteral("data/Trash/files/") + fileName);
}

QStringList WhaleCommandRoutingTest::openedFiles() const
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

bool WhaleCommandRoutingTest::select(QWidget *view, const QUrl &target)
{
    const QRect rect = cellRect(view, target);
    if (rect.isEmpty()) {
        return false;
    }
    QTest::mouseClick(canvasOf(view), Qt::LeftButton, {}, rect.center());
    for (int step = 0; step < 10; ++step) {
        if (QUrl(treemapState(view).value(QStringLiteral("selected")).toString()) == target) {
            return true;
        }
        QTest::keyClick(canvasOf(view), Qt::Key_Up);
    }
    return false;
}

// Router / command context -------------------------------------------------------

void WhaleCommandRoutingTest::testFileListIsTheSource()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    DolphinView *view = container->view();
    QCOMPARE(WhaleCommandRouter::source(view), Source::FileList);
    QVERIFY(!WhaleCommandRouter::routes(view));
    container->viewHost()->bridge()->setSelection(WhaleSelection({url(QStringLiteral("root/top.txt"))}, url(QStringLiteral("root/top.txt"))));
    QCOMPARE(WhaleCommandRouter::selectedUrls(view), QList<QUrl>({url(QStringLiteral("root/top.txt"))}));
    // Not routed: Dolphin's slots do their own work.
    QVERIFY(!viewActions()->routeAction(view, KStandardAction::name(KStandardAction::MoveToTrash)));

    // A plugin view that shows Whale's selection: the same selection, Dolphin's path.
    WhaleViewPlugin *testList = viewActions()->registry()->plugin(testListId);
    QVERIFY(testList && container->viewHost()->showPluginView(testListId, testList));
    QCOMPARE(WhaleCommandRouter::source(view), Source::MirroredPluginView);
    QCOMPARE(WhaleCommandRouter::selectedUrls(view), QList<QUrl>({url(QStringLiteral("root/top.txt"))}));
    QVERIFY(!viewActions()->routeAction(view, KStandardAction::name(KStandardAction::MoveToTrash)));
}

void WhaleCommandRoutingTest::testPluginViewIsTheSource()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QWidget *treemap = showTreemap(container);
    QVERIFY(treemap);
    DolphinView *view = container->view();
    QCOMPARE(WhaleCommandRouter::source(view), Source::PluginView);
    QVERIFY(WhaleCommandRouter::selectedUrls(view).isEmpty());
    const QUrl deep = url(QStringLiteral("root/deep/open.txt"));
    QVERIFY(select(treemap, deep));
    QCOMPARE(WhaleCommandRouter::selectedUrls(view), QList<QUrl>({deep}));
    QCOMPARE(WhaleCommandRouter::selectedItems(view).count(), 1);
    QCOMPARE(WhaleCommandRouter::selectedItems(view).first().url(), deep);
    // The DolphinView's selection stays empty: nothing is mirrored into it.
    QTRY_VERIFY(view->selectedItems().isEmpty());
}

void WhaleCommandRoutingTest::testActivePaneDecides()
{
    if (QGuiApplication::platformName() != QLatin1String("offscreen")) {
        QSKIP("uses the clipboard, which is the user's own outside of the offscreen platform");
    }
    DolphinViewContainer *left = showMainWindow();
    QVERIFY(left);
    QWidget *treemap = showTreemap(left);
    QVERIFY(treemap);
    DolphinViewContainer *right = openSplit(url(QStringLiteral("target")));
    QVERIFY(right);
    right->viewHost()->bridge()->setSelection(WhaleSelection({url(QStringLiteral("target/existing.txt"))}, url(QStringLiteral("target/existing.txt"))));

    // Left: the treemap's deep selection.
    const QUrl deep = url(QStringLiteral("root/deep/copy-me.txt"));
    QVERIFY(select(treemap, deep));
    QTRY_VERIFY(left->isActive());
    QTest::keySequence(canvasOf(treemap), QKeySequence::Copy);
    QCOMPARE(KUrlMimeData::urlsFromMimeData(QApplication::clipboard()->mimeData()), QList<QUrl>({deep}));

    // Right: its own selection, through Dolphin's own path.
    right->setActive(true);
    QTRY_VERIFY(right->isActive());
    QTRY_COMPARE(m_mainWindow->activeViewContainer(), right);
    action(KStandardAction::name(KStandardAction::Copy))->trigger();
    QCOMPARE(KUrlMimeData::urlsFromMimeData(QApplication::clipboard()->mimeData()), QList<QUrl>({url(QStringLiteral("target/existing.txt"))}));

    // Back to the left half: the treemap's again (its selection was kept).
    left->setActive(true);
    QTRY_COMPARE(m_mainWindow->activeViewContainer(), left);
    action(KStandardAction::name(KStandardAction::Copy))->trigger();
    QCOMPARE(KUrlMimeData::urlsFromMimeData(QApplication::clipboard()->mimeData()), QList<QUrl>({deep}));
}

void WhaleCommandRoutingTest::testViewSwitchAndHistory()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QWidget *treemap = showTreemap(container);
    QVERIFY(treemap);
    QAction *trash = action(KStandardAction::name(KStandardAction::MoveToTrash));
    QVERIFY(select(treemap, url(QStringLiteral("root/deep/trash-me.txt"))));
    QVERIFY(trash->isEnabled());
    QVERIFY(viewActions()->gateReason(trash->objectName()).isEmpty());

    // To the file list: at once Dolphin's selection (empty here), no event loop in between.
    container->viewHost()->showDolphinView();
    QCOMPARE(WhaleCommandRouter::source(container->view()), Source::FileList);
    QVERIFY(WhaleCommandRouter::selectedUrls(container->view()).isEmpty());
    QVERIFY(viewActions()->gateReason(trash->objectName()).isEmpty()); // Dolphin's: starts the selection mode

    // Back to the treemap: its own selection again (restored with its state), at once.
    QVERIFY(container->viewHost()->showPluginView(treemapId, viewActions()->registry()->plugin(treemapId)));
    QCOMPARE(WhaleCommandRouter::source(container->view()), Source::PluginView);
    treemap = container->viewHost()->currentView()->widget();
    QTRY_COMPARE(WhaleCommandRouter::selectedUrls(container->view()), QList<QUrl>({url(QStringLiteral("root/deep/trash-me.txt"))}));
    QTRY_VERIFY(trash->isEnabled());

    // History: into the deep folder (Enter) and back.
    QVERIFY(select(treemap, url(QStringLiteral("root/deep"))));
    QTest::keyClick(canvasOf(treemap), Qt::Key_Return);
    QTRY_COMPARE(container->url(), url(QStringLiteral("root/deep")));
    // A new location: nothing selected there, the actions are off at once.
    QVERIFY(WhaleCommandRouter::selectedUrls(container->view()).isEmpty());
    QCOMPARE(viewActions()->gateReason(trash->objectName()), QStringLiteral("nothing selected"));
    QVERIFY(!trash->isEnabled());
    action(KStandardAction::name(KStandardAction::Back))->trigger();
    QTRY_COMPARE(container->url(), url(QStringLiteral("root")));
    QCOMPARE(WhaleCommandRouter::source(container->view()), Source::PluginView);
    // The stored state brings the selection back once the tree is there.
    QTRY_COMPARE(WhaleCommandRouter::selectedUrls(container->view()), QList<QUrl>({url(QStringLiteral("root/deep"))}));
    QTRY_VERIFY(trash->isEnabled());
}

void WhaleCommandRoutingTest::testEmptySelectionDisables()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QVERIFY(showTreemap(container));
    for (const QString &name :
         WhaleViewActions::gateableActions().mid(WhaleViewActions::gateableActions().indexOf(KStandardAction::name(KStandardAction::RenameFile)))) {
        if (QAction *gated = action(name)) {
            QVERIFY2(!gated->isEnabled(), qPrintable(name));
            // Duplicate creates an item: never in a view without a destination folder.
            QCOMPARE(viewActions()->gateReason(name),
                     name == QLatin1String("duplicate") ? QStringLiteral("no destination folder") : QStringLiteral("nothing selected"));
        }
    }
    // Triggered anyway (e.g. through D-Bus): nothing happens, no selection mode either.
    QVERIFY(viewActions()->routeAction(container->view(), KStandardAction::name(KStandardAction::MoveToTrash)));
    QVERIFY(!container->isSelectionModeEnabled());
    // Properties without a selection: the location's, Dolphin's path.
    QVERIFY(!viewActions()->routeAction(container->view(), QStringLiteral("properties")));
}

void WhaleCommandRoutingTest::testDeepSelectionEnables()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QWidget *treemap = showTreemap(container);
    QVERIFY(treemap);
    QVERIFY(select(treemap, url(QStringLiteral("root/deep/open.txt"))));
    for (const QString &name : {KStandardAction::name(KStandardAction::RenameFile),
                                KStandardAction::name(KStandardAction::MoveToTrash),
                                KStandardAction::name(KStandardAction::DeleteFile),
                                KStandardAction::name(KStandardAction::Cut),
                                KStandardAction::name(KStandardAction::Copy),
                                QStringLiteral("copy_location"),
                                QStringLiteral("properties")}) {
        QVERIFY2(action(name)->isEnabled(), qPrintable(name));
        QVERIFY2(viewActions()->gateReason(name).isEmpty(), qPrintable(name));
    }
    QCOMPARE(viewActions()->gateReason(QStringLiteral("duplicate")), QStringLiteral("no destination folder"));
    // Not routed (yet): show the target of a link, compare.
    QCOMPARE(viewActions()->gateReason(QStringLiteral("show_target")), QStringLiteral("not routed to a view's own selection"));
    QCOMPARE(viewActions()->gateReason(QStringLiteral("compare_files")), QStringLiteral("not routed to a view's own selection"));
    // Local items can be trashed: the "delete with the trash's shortcut" stays off (Dolphin's rule).
    QVERIFY(!action(QStringLiteral("delete_shortcut"))->isEnabled());
    // One item: no "Select All".
    QVERIFY(!action(KStandardAction::name(KStandardAction::SelectAll))->isEnabled());
}

void WhaleCommandRoutingTest::testReadOnlySelection()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QWidget *treemap = showTreemap(container);
    QVERIFY(treemap);
    QVERIFY(QFile::setPermissions(path(QStringLiteral("root/ro")), QFile::ReadOwner | QFile::ExeOwner));
    QVERIFY(select(treemap, url(QStringLiteral("root/ro/locked.txt"))));
    for (const QString &name : {KStandardAction::name(KStandardAction::RenameFile),
                                KStandardAction::name(KStandardAction::MoveToTrash),
                                KStandardAction::name(KStandardAction::DeleteFile),
                                KStandardAction::name(KStandardAction::Cut)}) {
        QVERIFY2(!action(name)->isEnabled(), qPrintable(name));
    }
    for (const QString &name : {KStandardAction::name(KStandardAction::Copy), QStringLiteral("copy_location"), QStringLiteral("properties")}) {
        QVERIFY2(action(name)->isEnabled(), qPrintable(name));
    }
    // Shortcuts reach nothing that removes it.
    QTest::keySequence(canvasOf(treemap), QKeySequence(Qt::Key_Delete));
    QTest::keySequence(canvasOf(treemap), QKeySequence(Qt::SHIFT | Qt::Key_Delete));
    QTest::keySequence(canvasOf(treemap), QKeySequence(Qt::Key_F2));
    QTest::qWait(300);
    QVERIFY(QFile::exists(path(QStringLiteral("root/ro/locked.txt"))));
    QVERIFY(!visibleDialog<KIO::RenameFileDialog>());
}

void WhaleCommandRoutingTest::testLocationIsProtected()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    OwnSelectionView *view = showOwnSelectionView(container);
    QVERIFY(view);
    // The location itself, and the folder above it, as the view's selection.
    for (const QUrl &protectedUrl : {url(QStringLiteral("root")), url(QString())}) {
        view->select({protectedUrl});
        for (const QString &name : {KStandardAction::name(KStandardAction::RenameFile),
                                    KStandardAction::name(KStandardAction::MoveToTrash),
                                    KStandardAction::name(KStandardAction::DeleteFile),
                                    KStandardAction::name(KStandardAction::Cut)}) {
            QVERIFY2(!action(name)->isEnabled(), qPrintable(name));
            QCOMPARE(viewActions()->gateReason(name), QStringLiteral("not supported for the selection"));
        }
        // Triggered anyway: refused.
        QVERIFY(viewActions()->routeAction(container->view(), KStandardAction::name(KStandardAction::DeleteFile)));
        QTest::qWait(200);
        QVERIFY(QFile::exists(path(QStringLiteral("root/top.txt"))));
    }
    // An item that is gone meanwhile: nothing but (not routed) "show in folder".
    view->select({url(QStringLiteral("root/deep/gone.txt"))});
    QVERIFY(!action(KStandardAction::name(KStandardAction::Copy))->isEnabled());
    QVERIFY(!action(KStandardAction::name(KStandardAction::MoveToTrash))->isEnabled());
}

void WhaleCommandRoutingTest::testNoFallbackToTheHiddenSelection()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    OwnSelectionView *view = showOwnSelectionView(container);
    QVERIFY(view);
    // The hidden DolphinView has a selection of its own (top.txt), the view another one.
    container->viewHost()->bridge()->setSelection(WhaleSelection({url(QStringLiteral("root/top.txt"))}, url(QStringLiteral("root/top.txt"))));
    view->select({url(QStringLiteral("root/deep/trash-me.txt"))});
    action(KStandardAction::name(KStandardAction::MoveToTrash))->trigger();
    QTRY_VERIFY(!QFile::exists(path(QStringLiteral("root/deep/trash-me.txt"))));
    QVERIFY(QFile::exists(path(QStringLiteral("root/top.txt"))));
    // An empty own selection never falls back to the hidden one either.
    view->select({});
    QVERIFY(!action(KStandardAction::name(KStandardAction::DeleteFile))->isEnabled());
    QVERIFY(viewActions()->routeAction(container->view(), KStandardAction::name(KStandardAction::DeleteFile)));
    QTest::qWait(200);
    QVERIFY(QFile::exists(path(QStringLiteral("root/top.txt"))));
}

// Global actions and shortcuts -----------------------------------------------------

void WhaleCommandRoutingTest::testEnterOpens()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QWidget *treemap = showTreemap(container);
    QVERIFY(treemap);
    QVERIFY(select(treemap, url(QStringLiteral("root/deep/open.txt"))));
    QTest::keyClick(canvasOf(treemap), Qt::Key_Return);
    QTRY_COMPARE_WITH_TIMEOUT(openedFiles(), QStringList({path(QStringLiteral("root/deep/open.txt"))}), 10000);
    QCOMPARE(container->url(), url(QStringLiteral("root")));
    QVERIFY(select(treemap, url(QStringLiteral("root/deep/sub"))));
    QTest::keyClick(canvasOf(treemap), Qt::Key_Enter);
    QTRY_COMPARE(container->url(), url(QStringLiteral("root/deep/sub")));
}

void WhaleCommandRoutingTest::testF2Renames()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QWidget *treemap = showTreemap(container);
    QVERIFY(treemap);
    QVERIFY(select(treemap, url(QStringLiteral("root/deep/rename-me.txt"))));
    // Inline renaming is the user's choice here, but a view's own selection gets the dialog.
    QVERIFY(GeneralSettings::renameInline());
    QTest::keySequence(canvasOf(treemap), QKeySequence(Qt::Key_F2));
    KIO::RenameFileDialog *dialog = nullptr;
    QTRY_VERIFY((dialog = visibleDialog<KIO::RenameFileDialog>()));
    QCOMPARE(dialog->findChild<QLineEdit *>()->text(), QStringLiteral("rename-me.txt"));
    dialog->findChild<QLineEdit *>()->setText(QStringLiteral("renamed.txt"));
    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    QTRY_VERIFY(QFile::exists(path(QStringLiteral("root/deep/renamed.txt"))));
    QVERIFY(!QFile::exists(path(QStringLiteral("root/deep/rename-me.txt"))));
    // No inline editor in the hidden file list.
    QVERIFY(!container->view()->findChild<KItemListRoleEditor *>() || !container->view()->findChild<KItemListRoleEditor *>()->isVisible());
}

void WhaleCommandRoutingTest::testDeleteTrashes()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QWidget *treemap = showTreemap(container);
    QVERIFY(treemap);
    QVERIFY(select(treemap, url(QStringLiteral("root/deep/trash-me.txt"))));
    QTest::keySequence(canvasOf(treemap), QKeySequence(Qt::Key_Delete));
    QTRY_VERIFY(!QFile::exists(path(QStringLiteral("root/deep/trash-me.txt"))));
    QTRY_VERIFY(QFile::exists(trashedPath(QStringLiteral("trash-me.txt"))));
    QVERIFY(QFile::exists(path(QStringLiteral("root/deep/delete-me.txt"))));
    QVERIFY(QFile::exists(path(QStringLiteral("root/top.txt"))));
    // The treemap scans again; the selection is gone with the file, the actions are off.
    QTRY_VERIFY(treemapState(treemap).value(QStringLiteral("selected")).toString().isEmpty());
    QTRY_VERIFY(!action(KStandardAction::name(KStandardAction::MoveToTrash))->isEnabled());
    QVERIFY(!container->isSelectionModeEnabled());
}

void WhaleCommandRoutingTest::testShiftDeleteDeletes()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QWidget *treemap = showTreemap(container);
    QVERIFY(treemap);
    setConfirmations(true);
    QVERIFY(select(treemap, url(QStringLiteral("root/deep/delete-me.txt"))));
    // KIO asks; canceled, nothing happens.
    QTest::keySequence(canvasOf(treemap), QKeySequence(Qt::SHIFT | Qt::Key_Delete));
    QDialog *confirmation = nullptr;
    QTRY_VERIFY((confirmation = visibleDialog<QDialog>()));
    confirmation->reject();
    QTest::qWait(200);
    QVERIFY(QFile::exists(path(QStringLiteral("root/deep/delete-me.txt"))));
    // (Offscreen nothing activates the window again after a dialog; shortcuts need that.)
    m_mainWindow->QWidget::activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(m_mainWindow.data()));
    // Confirmed: deleted for good, nothing else.
    QTest::keySequence(canvasOf(treemap), QKeySequence(Qt::SHIFT | Qt::Key_Delete));
    QTRY_VERIFY((confirmation = visibleDialog<QDialog>()));
    const auto buttons = confirmation->findChild<QDialogButtonBox *>()->buttons();
    for (QAbstractButton *button : buttons) {
        if (confirmation->findChild<QDialogButtonBox *>()->buttonRole(button) == QDialogButtonBox::YesRole
            || confirmation->findChild<QDialogButtonBox *>()->buttonRole(button) == QDialogButtonBox::AcceptRole) {
            button->click();
            break;
        }
    }
    QTRY_VERIFY(!QFile::exists(path(QStringLiteral("root/deep/delete-me.txt"))));
    QVERIFY(!QFile::exists(trashedPath(QStringLiteral("delete-me.txt"))));
    QVERIFY(QFile::exists(path(QStringLiteral("root/deep/trash-me.txt"))));
}

void WhaleCommandRoutingTest::testCopyCutPaste()
{
    if (QGuiApplication::platformName() != QLatin1String("offscreen")) {
        QSKIP("uses the clipboard, which is the user's own outside of the offscreen platform");
    }
    DolphinViewContainer *left = showMainWindow();
    QVERIFY(left);
    QWidget *treemap = showTreemap(left);
    QVERIFY(treemap);
    DolphinViewContainer *right = openSplit(url(QStringLiteral("target")));
    QVERIFY(right);

    // Ctrl+C on a deep file, paste in the other half.
    QVERIFY(select(treemap, url(QStringLiteral("root/deep/copy-me.txt"))));
    QTRY_COMPARE(m_mainWindow->activeViewContainer(), left);
    QTest::keySequence(canvasOf(treemap), QKeySequence::Copy);
    const QMimeData *data = QApplication::clipboard()->mimeData();
    QCOMPARE(KUrlMimeData::urlsFromMimeData(data), QList<QUrl>({url(QStringLiteral("root/deep/copy-me.txt"))}));
    QVERIFY(!KIO::isClipboardDataCut(data));
    right->setActive(true);
    QTRY_COMPARE(m_mainWindow->activeViewContainer(), right);
    QTRY_VERIFY(action(KStandardAction::name(KStandardAction::Paste))->isEnabled());
    action(KStandardAction::name(KStandardAction::Paste))->trigger();
    QTRY_VERIFY(QFile::exists(path(QStringLiteral("target/copy-me.txt"))));
    QVERIFY(QFile::exists(path(QStringLiteral("root/deep/copy-me.txt"))));

    // Ctrl+X, paste: moved.
    left->setActive(true);
    QTRY_COMPARE(m_mainWindow->activeViewContainer(), left);
    QVERIFY(select(treemap, url(QStringLiteral("root/deep/move-me.txt"))));
    // (The paste's job window can take the activation; offscreen nothing gives it back.)
    m_mainWindow->QWidget::activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(m_mainWindow.data()));
    QTest::keySequence(canvasOf(treemap), QKeySequence::Cut);
    QVERIFY(KIO::isClipboardDataCut(QApplication::clipboard()->mimeData()));
    right->setActive(true);
    QTRY_COMPARE(m_mainWindow->activeViewContainer(), right);
    action(KStandardAction::name(KStandardAction::Paste))->trigger();
    QTRY_VERIFY(QFile::exists(path(QStringLiteral("target/move-me.txt"))));
    QTRY_VERIFY(!QFile::exists(path(QStringLiteral("root/deep/move-me.txt"))));
}

void WhaleCommandRoutingTest::testProperties()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QWidget *treemap = showTreemap(container);
    QVERIFY(treemap);
    // Nothing selected: the location's properties, as in the file list.
    action(QStringLiteral("properties"))->trigger();
    KPropertiesDialog *dialog = nullptr;
    QTRY_VERIFY((dialog = visibleDialog<KPropertiesDialog>()));
    QCOMPARE(dialog->item().url().adjusted(QUrl::StripTrailingSlash), url(QStringLiteral("root")));
    dialog->reject();
    QTRY_VERIFY(!visibleDialog<KPropertiesDialog>());
    // A deep entry (Alt+Return).
    QVERIFY(select(treemap, url(QStringLiteral("root/deep/open.txt"))));
    QTest::keySequence(canvasOf(treemap), QKeySequence(Qt::ALT | Qt::Key_Return));
    QTRY_VERIFY((dialog = visibleDialog<KPropertiesDialog>()));
    QCOMPARE(dialog->items().count(), 1);
    QCOMPARE(dialog->item().url(), url(QStringLiteral("root/deep/open.txt")));
    dialog->reject();
}

void WhaleCommandRoutingTest::testCopyLocation()
{
    if (QGuiApplication::platformName() != QLatin1String("offscreen")) {
        QSKIP("uses the clipboard, which is the user's own outside of the offscreen platform");
    }
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QWidget *treemap = showTreemap(container);
    QVERIFY(treemap);
    QVERIFY(select(treemap, url(QStringLiteral("root/deep/open.txt"))));
    action(QStringLiteral("copy_location"))->trigger();
    QCOMPARE(QApplication::clipboard()->text(), path(QStringLiteral("root/deep/open.txt")));
    QVERIFY(!container->isSelectionModeEnabled());
}

void WhaleCommandRoutingTest::testDuplicate()
{
    // A treemap has no destination folder: Duplicate is disabled, the shortcut explains it.
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QWidget *treemap = showTreemap(container);
    QVERIFY(treemap);
    QVERIFY(select(treemap, url(QStringLiteral("root/deep/dup-me.txt"))));
    QVERIFY(!action(QStringLiteral("duplicate"))->isEnabled());
    QTest::keySequence(canvasOf(treemap), QKeySequence(Qt::CTRL | Qt::Key_D));
    QTest::qWait(300);
    QCOMPARE(QDir(path(QStringLiteral("root/deep"))).entryList({QStringLiteral("dup-me*")}).count(), 1);
    // Triggered anyway (e.g. through D-Bus): the router carries nothing out either.
    QVERIFY(viewActions()->routeAction(container->view(), QStringLiteral("duplicate")));
    QTest::qWait(300);
    QCOMPARE(QDir(path(QStringLiteral("root/deep"))).entryList({QStringLiteral("dup-me*")}).count(), 1);
}

void WhaleCommandRoutingTest::testSeveralItems()
{
    // The router works on lists (no multiple selection UI yet): a view with two selected items.
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    OwnSelectionView *view = showOwnSelectionView(container);
    QVERIFY(view);
    view->select({url(QStringLiteral("root/deep/trash-me.txt")), url(QStringLiteral("root/deep/delete-me.txt"))});
    QVERIFY(action(KStandardAction::name(KStandardAction::MoveToTrash))->isEnabled());
    QVERIFY(!action(QStringLiteral("copy_location"))->isEnabled()); // Dolphin: one item only
    action(KStandardAction::name(KStandardAction::MoveToTrash))->trigger();
    QTRY_VERIFY(!QFile::exists(path(QStringLiteral("root/deep/trash-me.txt"))));
    QTRY_VERIFY(!QFile::exists(path(QStringLiteral("root/deep/delete-me.txt"))));
    QVERIFY(QFile::exists(path(QStringLiteral("root/deep/open.txt"))));
}

// Split view -------------------------------------------------------------------

void WhaleCommandRoutingTest::testCopyAndMoveToOtherPane()
{
    DolphinViewContainer *left = showMainWindow();
    QVERIFY(left);
    QWidget *treemap = showTreemap(left);
    QVERIFY(treemap);
    DolphinViewContainer *right = openSplit(url(QStringLiteral("target")));
    QVERIFY(right);
    right->viewHost()->bridge()->setSelection(WhaleSelection({url(QStringLiteral("target/existing.txt"))}, url(QStringLiteral("target/existing.txt"))));
    QAction *copyTo = action(QStringLiteral("copy_to_inactive_split_view"));
    QAction *moveTo = action(QStringLiteral("move_to_inactive_split_view"));

    // Left active with a deep selection: source = the treemap's entry, target = the right half.
    QVERIFY(select(treemap, url(QStringLiteral("root/deep/copy-me.txt"))));
    QTRY_COMPARE(m_mainWindow->activeViewContainer(), left);
    QTRY_VERIFY(copyTo->isEnabled());
    QVERIFY(moveTo->isEnabled());
    copyTo->trigger();
    QTRY_VERIFY(QFile::exists(path(QStringLiteral("target/copy-me.txt"))));
    QVERIFY(QFile::exists(path(QStringLiteral("root/deep/copy-me.txt"))));
    // Nothing of the right half's own selection went anywhere.
    QVERIFY(!QFile::exists(path(QStringLiteral("root/existing.txt"))));
    QVERIFY(!QFile::exists(path(QStringLiteral("root/deep/existing.txt"))));

    QVERIFY(select(treemap, url(QStringLiteral("root/deep/move-me.txt"))));
    QTRY_VERIFY(moveTo->isEnabled());
    moveTo->trigger();
    QTRY_VERIFY(QFile::exists(path(QStringLiteral("target/move-me.txt"))));
    QTRY_VERIFY(!QFile::exists(path(QStringLiteral("root/deep/move-me.txt"))));
    QCOMPARE(left->url(), url(QStringLiteral("root")));
    QCOMPARE(m_mainWindow->activeViewContainer(), left);

    // Right active: nothing goes into the treemap on the left, it has no destination folder.
    right->setActive(true);
    QTRY_COMPARE(m_mainWindow->activeViewContainer(), right);
    right->viewHost()->bridge()->setSelection(WhaleSelection({url(QStringLiteral("target/existing.txt"))}, url(QStringLiteral("target/existing.txt"))));
    QTRY_VERIFY(!copyTo->isEnabled());
    QVERIFY(!moveTo->isEnabled());
    QSignalSpy messages(left->viewHost(), &WhaleViewHost::messageRequested);
    QVERIFY(QMetaObject::invokeMethod(m_mainWindow->findChild<DolphinTabWidget *>(), "copyToInactiveSplitView"));
    QCOMPARE(messages.count(), 1);
    QCOMPARE(messages.first().first().toString(), left->viewHost()->refusal(WhaleViewHost::Refusal::Transfer));
    QTest::qWait(300);
    QVERIFY(!QFile::exists(path(QStringLiteral("root/existing.txt"))));
    // The treemap's deep selection was not copied this time.
    QVERIFY(!QFile::exists(path(QStringLiteral("target/open.txt"))));

    // An entry already in the other half's folder: Dolphin's rule disables copying.
    left->setActive(true);
    QTRY_COMPARE(m_mainWindow->activeViewContainer(), left);
    right->setUrl(url(QStringLiteral("root/deep")));
    QTRY_COMPARE(right->url(), url(QStringLiteral("root/deep")));
    QTRY_VERIFY(!right->view()->items().isEmpty());
    QVERIFY(select(treemap, url(QStringLiteral("root/deep/open.txt"))));
    QTRY_VERIFY(!copyTo->isEnabled());
}

// The file list stays Dolphin's ----------------------------------------------------

void WhaleCommandRoutingTest::testFileListUnchanged()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    DolphinView *view = container->view();
    // Delete without a selection: Dolphin's selection mode.
    view->setFocus();
    QVERIFY(view->selectedItems().isEmpty());
    action(KStandardAction::name(KStandardAction::MoveToTrash))->trigger();
    QTRY_VERIFY(container->isSelectionModeEnabled());
    container->setSelectionModeEnabled(false);
    QTRY_VERIFY(!container->isSelectionModeEnabled());
    // F2 with a selection: Dolphin's inline editor.
    container->viewHost()->bridge()->setSelection(WhaleSelection({url(QStringLiteral("root/top.txt"))}, url(QStringLiteral("root/top.txt"))));
    QTRY_COMPARE(view->selectedItems().count(), 1);
    action(KStandardAction::name(KStandardAction::RenameFile))->trigger();
    QTRY_VERIFY(view->findChild<KItemListRoleEditor *>() && view->findChild<KItemListRoleEditor *>()->isVisible());
    QTest::keyClick(view->findChild<KItemListRoleEditor *>(), Qt::Key_Escape);
    // Delete with a selection: trashed by Dolphin.
    QTRY_COMPARE(view->selectedItems().count(), 1);
    action(KStandardAction::name(KStandardAction::MoveToTrash))->trigger();
    QTRY_VERIFY(!QFile::exists(path(QStringLiteral("root/top.txt"))));
    QTRY_VERIFY(QFile::exists(trashedPath(QStringLiteral("top.txt"))));
}

QTEST_MAIN(WhaleCommandRoutingTest)

#include "whalecommandroutingtest.moc"
