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
#include "kitemviews/private/kitemlistroleeditor.h"
#include "testdir.h"
#include "testhelpers.h"
#include "views/dolphinview.h"
#include "whaleselection.h"
#include "whaleview.h"
#include "whaleviewactions.h"
#include "whaleviewcontext.h"
#include "whaleviewhost.h"
#include "whaleviewplugin.h"
#include "whaleviewregistry.h"

#include <KActionCollection>
#include <KIO/RenameFileDialog>
#include <KLocalizedString>
#include <KUrlMimeData>

#include <QActionGroup>
#include <QClipboard>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QTimer>

namespace
{
const QString testListId = QStringLiteral("whale_testlist");
const QString helloId = QStringLiteral("whale_hello");
const QString capsId = QStringLiteral("whale_test_caps");

QString stdAction(KStandardAction::StandardAction id)
{
    return KStandardAction::name(id);
}

QStringList names(const KFileItemList &items)
{
    QStringList result;
    for (const KFileItem &item : items) {
        result.append(item.url().fileName());
    }
    return result;
}

/// A view with the capabilities a test asks for; records inline rename requests.
class CapsView : public WhaleView
{
public:
    CapsView(WhaleViewContext *context, WhaleViewCapabilities capabilities, QList<QUrl> *renamed)
        : WhaleView(context)
        , m_widget(new QLineEdit)
        , m_capabilities(capabilities)
        , m_renamed(renamed)
    {
    }

    ~CapsView() override
    {
        delete m_widget;
    }

    QWidget *widget() override
    {
        return m_widget;
    }

    WhaleViewCapabilities capabilities() const override
    {
        return m_capabilities;
    }

    void startRename(const QUrl &url) override
    {
        m_renamed->append(url);
    }

private:
    QPointer<QLineEdit> m_widget;
    WhaleViewCapabilities m_capabilities;
    QList<QUrl> *m_renamed;
};

class CapsPlugin : public WhaleViewPlugin
{
public:
    WhaleView *createView(WhaleViewContext *context, QObject *parent) override
    {
        Q_UNUSED(parent)
        auto view = new CapsView(context, capabilities, &renamed);
        lastContext = context;
        return view;
    }

    bool supportsUrl(const QUrl &url) const override
    {
        return !localOnly || url.isLocalFile();
    }

    WhaleViewCapabilities capabilities;
    QList<QUrl> renamed;
    bool localOnly = false;
    QPointer<WhaleViewContext> lastContext;
};

/// Opens a popup menu with @p open, returns the names (or texts) of its actions and closes it.
QStringList popupActions(const std::function<void()> &open, bool *shown)
{
    QStringList result;
    *shown = false;
    QTimer poll;
    poll.setInterval(10);
    QObject::connect(&poll, &QTimer::timeout, [&]() {
        // Plugins may spin the event loop while the menu is built; wait until it is shown.
        auto menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (menu && menu->isVisible() && !menu->actions().isEmpty()) {
            const auto actions = menu->actions();
            for (QAction *action : actions) {
                if (!action->isSeparator()) {
                    result.append(action->objectName());
                    result.append(action->text());
                }
            }
            *shown = true;
            menu->close();
        }
    });
    poll.start();
    open(); // a context menu runs its own event loop until it is closed
    // *shown tells the caller whether the menu came up in time.
    std::ignore = QTest::qWaitFor(
        [shown]() {
            return *shown;
        },
        5000);
    poll.stop();
    return result;
}
}

class WhaleActionsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testGateableActionsExist();
    void testCapabilityMatrix_data();
    void testCapabilityMatrix();
    void testGateFollowsSelection();
    void testGateRestoresDolphinState();
    void testViewModeShowsFileList();
    void testRenameWithDialog();
    void testRenameSeveralItems();
    void testRenameInline();
    void testNoActionWithoutSelection();
    void testTrashAndDeleteSelection();
    void testCopyCutPaste();
    void testContextMenuItem();
    void testContextMenuSeveralItems();
    void testContextMenuBackground();
    void testItemInfo();
    void testItemInfoOutsideListing();
    void testOpenFolder();
    void testUnsupportedLocationFallsBack();
    void testSplitView();

private:
    DolphinViewContainer *showMainWindow();
    QWidget *showView(DolphinViewContainer *container, const QString &viewId, WhaleViewPlugin *plugin = nullptr);
    QAction *action(const QString &actionName) const;
    WhaleViewActions *viewActions() const;
    QUrl url(const QString &fileName) const;
    QString trashedPath(const QString &fileName) const;
    /// The menu shows the collection's action itself, or a copy with its text.
    bool inMenu(const QStringList &entries, const QString &actionName) const;
    void click(QListView *listView, const QString &itemName, Qt::KeyboardModifiers modifiers = {});

    QTemporaryDir m_tmpHome;
    QScopedPointer<TestDir> m_dir;
    QScopedPointer<DolphinMainWindow> m_mainWindow;
    WhaleViewRegistry m_registry;
    CapsPlugin m_capsPlugin;
};

void WhaleActionsTest::initTestCase()
{
    QVERIFY(m_tmpHome.isValid());
    QCoreApplication::setApplicationName(QStringLiteral(WHALE_COMPONENT));
    QCoreApplication::setOrganizationDomain(QStringLiteral(WHALE_ORG_DOMAIN));
    qputenv("HOME", m_tmpHome.path().toUtf8());
    // The trash of this test lives in the temporary home (the trash worker is another
    // process and does not know QStandardPaths' test mode).
    qputenv("XDG_DATA_HOME", m_tmpHome.filePath(QStringLiteral("data")).toUtf8());
    QStandardPaths::setTestModeEnabled(true);
    TestHelpers::disableAnimations();
    TestHelpers::openNewWindowsInSingleView();
    KLocalizedString::setApplicationDomain(TRANSLATION_DOMAIN);
    QCoreApplication::addLibraryPath(QStringLiteral(WHALE_TEST_PLUGIN_DIR));
    QCoreApplication::addLibraryPath(QStringLiteral(WHALE_TEST_VIEW_PLUGIN_DIR));

    // Deleting asks for confirmation by default; the test confirms by configuration.
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    QVERIFY(QDir().mkpath(config));
    QFile kiorc(config + QStringLiteral("/kiorc"));
    QVERIFY(kiorc.open(QIODevice::WriteOnly));
    kiorc.write("[Confirmations]\nConfirmDelete=false\nConfirmTrash=false\n");
    kiorc.close();

    GeneralSettings::self()->setRememberOpenedTabs(false);
    GeneralSettings::self()->setRenameInline(true);
    GeneralSettings::self()->save();
}

void WhaleActionsTest::init()
{
    m_dir.reset(new TestDir());
    m_dir->createFile(QStringLiteral("a.txt"), QByteArray(300, 'a'));
    m_dir->createFile(QStringLiteral("b.txt"), QByteArray(100, 'b'));
    m_dir->createFile(QStringLiteral("c.txt"), QByteArray(200, 'c'));
    m_dir->createFile(QStringLiteral("sub/inner.txt"));
    m_capsPlugin.capabilities = {};
    m_capsPlugin.renamed.clear();
    m_capsPlugin.localOnly = false;
    GeneralSettings::self()->setRenameInline(true);
    m_mainWindow.reset(new DolphinMainWindow());
}

void WhaleActionsTest::cleanup()
{
    // No rename dialog or other window may stay behind.
    const auto windows = QApplication::topLevelWidgets();
    for (QWidget *window : windows) {
        if (qobject_cast<QDialog *>(window) && window->isVisible()) {
            window->close();
        }
    }
    m_mainWindow.reset();
    m_dir.reset();
}

DolphinViewContainer *WhaleActionsTest::showMainWindow()
{
    m_mainWindow->openDirectories({m_dir->url()}, false);
    m_mainWindow->resize(1200, 800);
    m_mainWindow->show();
    if (!QTest::qWaitForWindowActive(m_mainWindow.data())) {
        return nullptr;
    }
    DolphinViewContainer *container = m_mainWindow->activeViewContainer();
    container->view()->setHiddenFilesShown(false);
    container->view()->setSortRole("text");
    container->view()->setSortOrder(Qt::AscendingOrder);
    container->view()->setSortFoldersFirst(true);
    if (!QTest::qWaitFor(
            [container]() {
                return container->view()->itemsCount() == 4;
            },
            5000)) {
        return nullptr;
    }
    return container;
}

QWidget *WhaleActionsTest::showView(DolphinViewContainer *container, const QString &viewId, WhaleViewPlugin *plugin)
{
    WhaleViewHost *host = container->viewHost();
    if (!plugin) {
        plugin = m_registry.plugin(viewId);
    }
    if (!plugin || !host->showPluginView(viewId, plugin)) {
        return nullptr;
    }
    return host->currentView()->widget();
}

QAction *WhaleActionsTest::action(const QString &actionName) const
{
    return m_mainWindow->actionCollection()->action(actionName);
}

WhaleViewActions *WhaleActionsTest::viewActions() const
{
    return m_mainWindow->findChild<WhaleViewActions *>();
}

QUrl WhaleActionsTest::url(const QString &fileName) const
{
    return QUrl::fromLocalFile(m_dir->path() + QLatin1Char('/') + fileName);
}

QString WhaleActionsTest::trashedPath(const QString &fileName) const
{
    return m_tmpHome.filePath(QStringLiteral("data/Trash/files/") + fileName);
}

bool WhaleActionsTest::inMenu(const QStringList &entries, const QString &actionName) const
{
    return entries.contains(actionName) || entries.contains(action(actionName)->text());
}

void WhaleActionsTest::click(QListView *listView, const QString &itemName, Qt::KeyboardModifiers modifiers)
{
    const QAbstractItemModel *model = listView->model();
    for (int row = 0; row < model->rowCount(); ++row) {
        if (model->index(row, 0).data().toString() == itemName) {
            QTest::mouseClick(listView->viewport(), Qt::LeftButton, modifiers, listView->visualRect(model->index(row, 0)).center());
            return;
        }
    }
    QFAIL(qPrintable(QStringLiteral("no row ") + itemName));
}

void WhaleActionsTest::testGateableActionsExist()
{
    // The gating finds Dolphin's actions by name. If upstream renames or removes one, its
    // gate would silently do nothing; this turns that into a failure. Same list as production.
    QVERIFY(showMainWindow());
    const QStringList names = WhaleViewActions::gateableActions();
    QVERIFY(!names.isEmpty());
    // The actions routed to a plugin view's own selection (T2): Dolphin's, all but
    // "properties" gateable.
    const auto routed = WhaleViewActions::routedActions();
    QVERIFY(!routed.isEmpty());
    for (auto it = routed.cbegin(); it != routed.cend(); ++it) {
        QVERIFY2(action(it.key()), qPrintable(QStringLiteral("Dolphin has no action \"%1\" any more; update WhaleViewActions::routedActions()").arg(it.key())));
        QVERIFY2(names.contains(it.key()) || it.key() == QLatin1String("properties"), qPrintable(it.key()));
    }
    for (const QString &actionName : names) {
        QAction *gated = action(actionName);
        QVERIFY2(gated, qPrintable(QStringLiteral("Dolphin has no action \"%1\" any more; update WhaleViewActions::gateableActions()").arg(actionName)));
        // The gate is an action group of its own; an action in another group cannot be gated.
        QVERIFY2(gated->actionGroup() && gated->actionGroup()->actions() == QList<QAction *>({gated}),
                 qPrintable(QStringLiteral("\"%1\" is not gated by its own action group").arg(actionName)));
    }
}

void WhaleActionsTest::testCapabilityMatrix_data()
{
    using C = WhaleViewCapability;
    QTest::addColumn<QString>("viewId");
    QTest::addColumn<int>("capabilities"); // for the caps view only
    QTest::addColumn<QStringList>("gated"); // with one item selected
    QTest::addColumn<QStringList>("open");

    const QStringList zoom = {stdAction(KStandardAction::ZoomIn), stdAction(KStandardAction::ZoomOut), QStringLiteral("view_zoom_reset")};
    const QStringList order = {QStringLiteral("sort"), QStringLiteral("folders_first"), QStringLiteral("additional_info")};
    const QStringList selecting = {stdAction(KStandardAction::SelectAll), QStringLiteral("invert_selection")};
    const QStringList onSelection = {stdAction(KStandardAction::RenameFile),
                                     stdAction(KStandardAction::MoveToTrash),
                                     stdAction(KStandardAction::DeleteFile),
                                     stdAction(KStandardAction::Cut),
                                     stdAction(KStandardAction::Copy),
                                     QStringLiteral("duplicate"),
                                     QStringLiteral("copy_location")};
    const QStringList selectionMode = {QStringLiteral("toggle_selection_mode")};
    // (Paste is gated only without a destination, but Dolphin disables it while the clipboard is empty.)
    const QStringList always = {QStringLiteral("properties")};
    // Without a destination (items from many folders) nothing is created; Duplicate as well.
    const QStringList creating = {QStringLiteral("new_menu"), QStringLiteral("create_dir"), QStringLiteral("create_file"), stdAction(KStandardAction::Paste)};
    QStringList onSelectionWithoutDuplicate = onSelection;
    onSelectionWithoutDuplicate.removeOne(QStringLiteral("duplicate"));
    // Only views that follow the setting (HiddenFiles) keep "Show Hidden Files".
    const QStringList hidden = {QStringLiteral("show_hidden_files")};

    QTest::newRow("file list") << QStringLiteral("dolphin") << 0 << QStringList() << (zoom + order + selecting + onSelection + selectionMode + always + hidden);
    QTest::newRow("test list") << testListId << 0 << (zoom + QStringList{QStringLiteral("group_by")} + selectionMode)
                               << (order + selecting + onSelection + always + hidden + QStringList{QStringLiteral("show_preview")});
    QTest::newRow("hello: no capabilities") << helloId << 0 << (zoom + order + selecting + onSelection + selectionMode + hidden) << always;
    QTest::newRow("zoom, previews, grouping") << capsId << int(C::Selection | C::MultiSelection | C::HostOrder | C::Zoom | C::Previews | C::Grouping)
                                              << (selectionMode + hidden)
                                              << (zoom + order + selecting + onSelection
                                                  + QStringList{QStringLiteral("show_preview"), QStringLiteral("group_by")});
    QTest::newRow("single selection") << capsId << int(C::Selection | C::HostOrder) << (selecting + selectionMode) << (onSelection + order);
    QTest::newRow("hidden files") << capsId << int(C::Selection | C::HostOrder | C::HiddenFiles) << (selecting + selectionMode)
                                  << (onSelection + order + hidden);
    // T2: the view's own selection (by default Whale's, a.txt) is routed; nothing removes the location.
    QTest::newRow("recursive content") << capsId << int(C::Selection | C::MultiSelection | C::RecursiveContent)
                                       << (order + selectionMode + hidden + creating + QStringList{QStringLiteral("duplicate")})
                                       << (selecting + always + onSelectionWithoutDuplicate);
}

void WhaleActionsTest::testCapabilityMatrix()
{
    QFETCH(QString, viewId);
    QFETCH(int, capabilities);
    QFETCH(QStringList, gated);
    QFETCH(QStringList, open);

    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    DolphinViewBridge *bridge = container->viewHost()->bridge();
    bridge->setSelection(WhaleSelection({url(QStringLiteral("a.txt"))}, url(QStringLiteral("a.txt"))));
    m_capsPlugin.capabilities = WhaleViewCapabilities(capabilities);
    if (viewId != QLatin1String("dolphin")) {
        QVERIFY(showView(container, viewId, viewId == capsId ? &m_capsPlugin : nullptr));
    }

    for (const QString &actionName : std::as_const(gated)) {
        QVERIFY2(action(actionName), qPrintable(actionName));
        QVERIFY2(!action(actionName)->isEnabled(), qPrintable(actionName));
        QVERIFY2(!viewActions()->gateReason(actionName).isEmpty(), qPrintable(actionName));
    }
    if (!(capabilities & int(WhaleViewCapability::RecursiveContent))) {
        QVERIFY(viewActions()->gateReason(stdAction(KStandardAction::Paste)).isEmpty());
    }
    for (const QString &actionName : std::as_const(open)) {
        QVERIFY2(action(actionName), qPrintable(actionName));
        QVERIFY2(viewActions()->gateReason(actionName).isEmpty(), qPrintable(actionName + QLatin1String(": ") + viewActions()->gateReason(actionName)));
        QTRY_VERIFY2(action(actionName)->isEnabled(), qPrintable(actionName));
    }
}

void WhaleActionsTest::testGateFollowsSelection()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    auto listView = qobject_cast<QListView *>(showView(container, testListId));
    QVERIFY(listView);
    const QStringList onSelection = {stdAction(KStandardAction::RenameFile),
                                     stdAction(KStandardAction::MoveToTrash),
                                     stdAction(KStandardAction::DeleteFile),
                                     stdAction(KStandardAction::Cut),
                                     stdAction(KStandardAction::Copy)};
    for (const QString &actionName : onSelection) {
        QVERIFY2(!action(actionName)->isEnabled(), qPrintable(actionName));
        QCOMPARE(viewActions()->gateReason(actionName), QStringLiteral("nothing selected"));
    }

    // Enabled at once, before any event is processed (a shortcut right after the click).
    click(listView, QStringLiteral("b.txt"));
    for (const QString &actionName : onSelection) {
        QVERIFY2(viewActions()->gateReason(actionName).isEmpty(), qPrintable(actionName));
    }
    QTRY_VERIFY(action(stdAction(KStandardAction::Copy))->isEnabled());
    QVERIFY(action(stdAction(KStandardAction::MoveToTrash))->isEnabled());

    listView->selectionModel()->clearSelection();
    QVERIFY(!action(stdAction(KStandardAction::MoveToTrash))->isEnabled());

    // Back in the file list, Dolphin's own state: without a selection these start the
    // selection mode there.
    container->viewHost()->showDolphinView();
    QTRY_VERIFY(action(stdAction(KStandardAction::MoveToTrash))->isEnabled());
    QVERIFY(action(stdAction(KStandardAction::RenameFile))->isEnabled());
    QVERIFY(action(QStringLiteral("toggle_selection_mode"))->isEnabled());
}

void WhaleActionsTest::testGateRestoresDolphinState()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    DolphinView *view = container->view();
    QAction *zoomIn = action(stdAction(KStandardAction::ZoomIn));
    QVERIFY(zoomIn->isEnabled());

    // While the gate is closed, Dolphin disables zoom in (largest zoom level) ...
    QVERIFY(showView(container, testListId));
    QVERIFY(!zoomIn->isEnabled());
    view->setZoomLevel(16);
    // ... and opening the gate keeps Dolphin's latest wish, not the old state.
    container->viewHost()->showDolphinView();
    QVERIFY(!zoomIn->isEnabled());
    view->setZoomLevel(1);
    QTRY_VERIFY(zoomIn->isEnabled());
}

void WhaleActionsTest::testViewModeShowsFileList()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QVERIFY(showView(container, testListId));
    action(QStringLiteral("details"))->trigger();
    QVERIFY(container->viewHost()->isDolphinViewShown());
    QCOMPARE(container->view()->viewMode(), DolphinView::DetailsView);
}

void WhaleActionsTest::testRenameWithDialog()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    auto listView = qobject_cast<QListView *>(showView(container, testListId));
    QVERIFY(listView);
    click(listView, QStringLiteral("a.txt"));
    QTRY_VERIFY(action(stdAction(KStandardAction::RenameFile))->isEnabled());

    // F2 in the test list (no InlineRename): Dolphin's rename dialog, never its inline editor.
    QTest::keyClick(listView, Qt::Key_F2);
    QTRY_VERIFY(m_mainWindow->findChild<KIO::RenameFileDialog *>());
    QVERIFY(!container->view()->findChild<KItemListRoleEditor *>());
    QVERIFY(!container->isSelectionModeEnabled());
    auto dialog = m_mainWindow->findChild<KIO::RenameFileDialog *>();
    QTRY_VERIFY(dialog->isVisible());
    auto edit = dialog->findChild<QLineEdit *>();
    QVERIFY(edit);
    edit->setText(QStringLiteral("renamed.txt"));
    QTest::keyClick(edit, Qt::Key_Return);

    QTRY_VERIFY(QFile::exists(m_dir->path() + QStringLiteral("/renamed.txt")));
    QVERIFY(!QFile::exists(m_dir->path() + QStringLiteral("/a.txt")));
    // Dolphin selects the renamed item; the test list shows it.
    QTRY_COMPARE(names(container->view()->selectedItems()), QStringList({QStringLiteral("renamed.txt")}));
    QTRY_COMPARE(listView->selectionModel()->selectedIndexes().count(), 1);
    QCOMPARE(listView->selectionModel()->selectedIndexes().first().data().toString(), QStringLiteral("renamed.txt"));
    // The setting was only bent for the dialog.
    QVERIFY(GeneralSettings::renameInline());
}

void WhaleActionsTest::testRenameSeveralItems()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    auto listView = qobject_cast<QListView *>(showView(container, testListId));
    click(listView, QStringLiteral("a.txt"));
    click(listView, QStringLiteral("b.txt"), Qt::ControlModifier);
    QTRY_COMPARE(container->view()->selectedItemsCount(), 2);

    // As in the file list: several items are renamed with the dialog (numbered names).
    action(stdAction(KStandardAction::RenameFile))->trigger();
    QTRY_VERIFY(m_mainWindow->findChild<KIO::RenameFileDialog *>());
    QVERIFY(!container->view()->findChild<KItemListRoleEditor *>());
    m_mainWindow->findChild<KIO::RenameFileDialog *>()->reject();
}

void WhaleActionsTest::testRenameInline()
{
    using C = WhaleViewCapability;
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    m_capsPlugin.capabilities = C::Selection | C::MultiSelection | C::HostOrder | C::InlineRename;
    QVERIFY(showView(container, capsId, &m_capsPlugin));
    DolphinViewBridge *bridge = container->viewHost()->bridge();

    // One item: the view renames it itself.
    bridge->setSelection(WhaleSelection({url(QStringLiteral("b.txt"))}));
    QTRY_VERIFY(action(stdAction(KStandardAction::RenameFile))->isEnabled());
    action(stdAction(KStandardAction::RenameFile))->trigger();
    QCOMPARE(m_capsPlugin.renamed, QList<QUrl>({url(QStringLiteral("b.txt"))}));
    QVERIFY(!m_mainWindow->findChild<KIO::RenameFileDialog *>());
    QVERIFY(!container->view()->findChild<KItemListRoleEditor *>());

    // Several items, or the user does not want inline renaming: the dialog.
    bridge->setSelection(WhaleSelection({url(QStringLiteral("a.txt")), url(QStringLiteral("b.txt"))}));
    action(stdAction(KStandardAction::RenameFile))->trigger();
    QTRY_VERIFY(m_mainWindow->findChild<KIO::RenameFileDialog *>());
    m_mainWindow->findChild<KIO::RenameFileDialog *>()->reject();
    delete m_mainWindow->findChild<KIO::RenameFileDialog *>();

    GeneralSettings::self()->setRenameInline(false);
    bridge->setSelection(WhaleSelection({url(QStringLiteral("c.txt"))}));
    action(stdAction(KStandardAction::RenameFile))->trigger();
    QTRY_VERIFY(m_mainWindow->findChild<KIO::RenameFileDialog *>());
    QCOMPARE(m_capsPlugin.renamed.count(), 1);
}

void WhaleActionsTest::testNoActionWithoutSelection()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    auto listView = qobject_cast<QListView *>(showView(container, testListId));
    QVERIFY(listView);
    QVERIFY(container->view()->selectedItems().isEmpty());
    const QStringList before = QDir(m_dir->path()).entryList(QDir::Files);

    // Keys in the test list: nothing happens, in particular no selection mode of the
    // hidden file list and no rename editor.
    QTest::keyClick(listView, Qt::Key_Delete);
    QTest::keyClick(listView, Qt::Key_Delete, Qt::ShiftModifier);
    QTest::keyClick(listView, Qt::Key_F2);
    QTest::keyClick(listView, Qt::Key_X, Qt::ControlModifier);
    QCoreApplication::processEvents();
    QVERIFY(!container->isSelectionModeEnabled());
    QVERIFY(!container->view()->findChild<KItemListRoleEditor *>());
    QVERIFY(!m_mainWindow->findChild<KIO::RenameFileDialog *>());
    QCOMPARE(QDir(m_dir->path()).entryList(QDir::Files), before);
    // The actions themselves, too (a disabled action does not trigger).
    for (const QString &actionName :
         {stdAction(KStandardAction::MoveToTrash), stdAction(KStandardAction::DeleteFile), stdAction(KStandardAction::RenameFile)}) {
        action(actionName)->trigger();
    }
    QCoreApplication::processEvents();
    QVERIFY(!container->isSelectionModeEnabled());
    QCOMPARE(QDir(m_dir->path()).entryList(QDir::Files), before);

    // Properties without a selection are those of the folder, as in the file list.
    QVERIFY(action(QStringLiteral("properties"))->isEnabled());
}

void WhaleActionsTest::testTrashAndDeleteSelection()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    auto listView = qobject_cast<QListView *>(showView(container, testListId));
    QVERIFY(listView);

    // Delete: the selected item only.
    click(listView, QStringLiteral("a.txt"));
    QTRY_VERIFY(action(stdAction(KStandardAction::MoveToTrash))->isEnabled());
    QTest::keyClick(listView, Qt::Key_Delete);
    QTRY_VERIFY(!QFile::exists(m_dir->path() + QStringLiteral("/a.txt")));
    QTRY_VERIFY(QFile::exists(trashedPath(QStringLiteral("a.txt"))));
    QVERIFY(QFile::exists(m_dir->path() + QStringLiteral("/b.txt")));
    QVERIFY(QFile::exists(m_dir->path() + QStringLiteral("/c.txt")));

    // Shift+Delete: deleted for good, exactly the two selected items.
    QTRY_VERIFY(listView->model()->rowCount() == 3);
    click(listView, QStringLiteral("b.txt"));
    click(listView, QStringLiteral("c.txt"), Qt::ControlModifier);
    QTRY_COMPARE(names(container->view()->selectedItems()), QStringList({QStringLiteral("b.txt"), QStringLiteral("c.txt")}));
    QTest::keyClick(listView, Qt::Key_Delete, Qt::ShiftModifier);
    QTRY_VERIFY(!QFile::exists(m_dir->path() + QStringLiteral("/b.txt")) && !QFile::exists(m_dir->path() + QStringLiteral("/c.txt")));
    QVERIFY(!QFile::exists(trashedPath(QStringLiteral("b.txt"))));
    QVERIFY(QFile::exists(m_dir->path() + QStringLiteral("/sub/inner.txt")));
    QVERIFY(!container->isSelectionModeEnabled());
}

void WhaleActionsTest::testCopyCutPaste()
{
    if (QGuiApplication::platformName() != QLatin1String("offscreen")) {
        QSKIP("uses the clipboard, which is the user's own outside of the offscreen platform");
    }
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    auto listView = qobject_cast<QListView *>(showView(container, testListId));
    QVERIFY(listView);
    const QUrl sub = url(QStringLiteral("sub"));

    // Copy a.txt into sub.
    click(listView, QStringLiteral("a.txt"));
    QTest::keyClick(listView, Qt::Key_C, Qt::ControlModifier);
    QTRY_COMPARE(KUrlMimeData::urlsFromMimeData(QApplication::clipboard()->mimeData()), QList<QUrl>({url(QStringLiteral("a.txt"))}));
    container->setUrl(sub);
    QTRY_COMPARE(listView->model()->rowCount(), 1);
    QTRY_VERIFY(action(stdAction(KStandardAction::Paste))->isEnabled());
    QTest::keyClick(listView, Qt::Key_V, Qt::ControlModifier);
    QTRY_VERIFY(QFile::exists(m_dir->path() + QStringLiteral("/sub/a.txt")));
    QVERIFY(QFile::exists(m_dir->path() + QStringLiteral("/a.txt")));

    // Cut b.txt and paste it into sub.
    container->setUrl(m_dir->url());
    QTRY_COMPARE(listView->model()->rowCount(), 4);
    click(listView, QStringLiteral("b.txt"));
    QTest::keyClick(listView, Qt::Key_X, Qt::ControlModifier);
    QTRY_COMPARE(KUrlMimeData::urlsFromMimeData(QApplication::clipboard()->mimeData()), QList<QUrl>({url(QStringLiteral("b.txt"))}));
    container->setUrl(sub);
    QTRY_COMPARE(listView->model()->rowCount(), 2);
    QTest::keyClick(listView, Qt::Key_V, Qt::ControlModifier);
    QTRY_VERIFY(QFile::exists(m_dir->path() + QStringLiteral("/sub/b.txt")));
    QTRY_VERIFY(!QFile::exists(m_dir->path() + QStringLiteral("/b.txt")));
    QApplication::clipboard()->clear();
}

void WhaleActionsTest::testContextMenuItem()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    auto listView = qobject_cast<QListView *>(showView(container, testListId));
    QVERIFY(listView);
    const QModelIndex a = listView->model()->index(1, 0);
    QCOMPARE(a.data().toString(), QStringLiteral("a.txt"));

    // Right click on an item that is not selected: it becomes the selection, then the
    // file list's context menu for it opens.
    bool shown = false;
    const QStringList entries = popupActions(
        [listView, a]() {
            QTest::mouseClick(listView->viewport(), Qt::RightButton, {}, listView->visualRect(a).center());
            QContextMenuEvent event(QContextMenuEvent::Mouse,
                                    listView->visualRect(a).center(),
                                    listView->viewport()->mapToGlobal(listView->visualRect(a).center()));
            QCoreApplication::sendEvent(listView->viewport(), &event);
        },
        &shown);
    QVERIFY(shown);
    QCOMPARE(names(container->view()->selectedItems()), QStringList({QStringLiteral("a.txt")}));
    for (const QString &entry : {stdAction(KStandardAction::RenameFile),
                                 stdAction(KStandardAction::Copy),
                                 stdAction(KStandardAction::Cut),
                                 stdAction(KStandardAction::MoveToTrash),
                                 QStringLiteral("properties")}) {
        QVERIFY2(inMenu(entries, entry), qPrintable(entry + QLatin1String(": ") + entries.join(QLatin1Char(','))));
    }
    QVERIFY2(entries.contains(QStringLiteral("openwith")) || entries.contains(QStringLiteral("openWith_submenu")), qPrintable(entries.join(QLatin1Char(','))));
}

void WhaleActionsTest::testContextMenuSeveralItems()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    auto listView = qobject_cast<QListView *>(showView(container, testListId));
    QVERIFY(listView);
    click(listView, QStringLiteral("a.txt"));
    click(listView, QStringLiteral("c.txt"), Qt::ControlModifier);
    QTRY_COMPARE(container->view()->selectedItemsCount(), 2);

    // A right click on one of the selected items keeps the whole selection.
    bool shown = false;
    const QStringList entries = popupActions(
        [container, this]() {
            container->viewHost()->bridge()->requestContextMenu(m_mainWindow->mapToGlobal(QPoint(300, 300)), url(QStringLiteral("c.txt")));
        },
        &shown);
    QVERIFY(shown);
    QCOMPARE(names(container->view()->selectedItems()), QStringList({QStringLiteral("a.txt"), QStringLiteral("c.txt")}));
    QVERIFY2(inMenu(entries, stdAction(KStandardAction::MoveToTrash)), qPrintable(entries.join(QLatin1Char(','))));
    QVERIFY2(inMenu(entries, stdAction(KStandardAction::Copy)), qPrintable(entries.join(QLatin1Char(','))));
}

void WhaleActionsTest::testContextMenuBackground()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    auto listView = qobject_cast<QListView *>(showView(container, testListId));
    QVERIFY(listView);
    click(listView, QStringLiteral("a.txt"));
    QTRY_COMPARE(container->view()->selectedItemsCount(), 1);

    // A right click on the background clears the selection and shows the folder's menu.
    const QPoint background(listView->viewport()->width() / 2, listView->viewport()->height() - 10);
    QVERIFY(!listView->indexAt(background).isValid());
    bool shown = false;
    const QStringList entries = popupActions(
        [listView, background]() {
            QContextMenuEvent event(QContextMenuEvent::Mouse, background, listView->viewport()->mapToGlobal(background));
            QCoreApplication::sendEvent(listView->viewport(), &event);
        },
        &shown);
    QVERIFY(shown);
    QCOMPARE(container->view()->selectedItemsCount(), 0);
    QVERIFY2(!inMenu(entries, stdAction(KStandardAction::MoveToTrash)), qPrintable(entries.join(QLatin1Char(','))));
    QVERIFY2(!inMenu(entries, stdAction(KStandardAction::RenameFile)), qPrintable(entries.join(QLatin1Char(','))));
    QVERIFY2(inMenu(entries, QStringLiteral("properties")), qPrintable(entries.join(QLatin1Char(','))));
}

void WhaleActionsTest::testItemInfo()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    auto listView = qobject_cast<QListView *>(showView(container, testListId));
    QVERIFY(listView);
    // The information panel gets the main window's requestItemInfo().
    QSignalSpy info(m_mainWindow.data(), &DolphinMainWindow::requestItemInfo);

    const QAbstractItemModel *model = listView->model();
    QTest::mouseMove(listView->viewport(), listView->visualRect(model->index(2, 0)).center());
    QTRY_VERIFY(!info.isEmpty() && info.last().at(0).value<KFileItem>().url() == url(QStringLiteral("b.txt")));
    QTest::mouseMove(listView->viewport(), listView->visualRect(model->index(3, 0)).center());
    QTRY_COMPARE(info.last().at(0).value<KFileItem>().url(), url(QStringLiteral("c.txt")));

    // Off the items: no item any more.
    QTest::mouseMove(listView->viewport(), QPoint(listView->viewport()->width() / 2, listView->viewport()->height() - 5));
    QTRY_VERIFY(info.last().at(0).value<KFileItem>().isNull());
    QCOMPARE(container->viewHost()->currentViewId(), testListId);
}

void WhaleActionsTest::testItemInfoOutsideListing()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QVERIFY(showView(container, capsId, &m_capsPlugin));
    WhaleViewContext *context = m_capsPlugin.lastContext;
    QVERIFY(context);
    QSignalSpy info(m_mainWindow.data(), &DolphinMainWindow::requestItemInfo);

    // An entry below the listing is described from the file system (view API 6).
    context->requestItemInfo(url(QStringLiteral("sub/inner.txt")));
    QVERIFY(!info.isEmpty());
    KFileItem item = info.last().at(0).value<KFileItem>();
    QCOMPARE(item.url(), url(QStringLiteral("sub/inner.txt")));
    QVERIFY(!item.isDir());
    QCOMPARE(item.size(), KIO::filesize_t(4)); // "test"
    // A listed one is the listing's item.
    context->requestItemInfo(url(QStringLiteral("sub")));
    QVERIFY(info.last().at(0).value<KFileItem>().isDir());
    // Missing and non-local URLs describe nothing.
    context->requestItemInfo(url(QStringLiteral("sub/missing.txt")));
    QVERIFY(info.last().at(0).value<KFileItem>().isNull());
    context->requestItemInfo(url(QStringLiteral("sub/inner.txt")));
    context->requestItemInfo(QUrl(QStringLiteral("sftp://host/file")));
    QVERIFY(info.last().at(0).value<KFileItem>().isNull());
}

void WhaleActionsTest::testOpenFolder()
{
    m_dir->createFile(QStringLiteral("sub/deeper/x.txt"));
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QVERIFY(showView(container, capsId, &m_capsPlugin));
    WhaleViewContext *context = m_capsPlugin.lastContext;
    QVERIFY(context);

    // A folder below the listing becomes the location, with a history entry.
    context->openFolder(url(QStringLiteral("sub/deeper")));
    QTRY_COMPARE(container->url(), url(QStringLiteral("sub/deeper")));
    m_mainWindow->actionCollection()->action(KStandardAction::name(KStandardAction::Back))->trigger();
    QTRY_COMPARE(container->url(), m_dir->url());

    // Files and missing folders are not opened (no application starts).
    context->openFolder(url(QStringLiteral("a.txt")));
    context->openFolder(url(QStringLiteral("missing")));
    QTest::qWait(200);
    QCOMPARE(container->url(), m_dir->url());
}

void WhaleActionsTest::testUnsupportedLocationFallsBack()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    m_capsPlugin.localOnly = true;
    WhaleViewHost *host = container->viewHost();
    QVERIFY(showView(container, capsId, &m_capsPlugin));

    // A location the plugin does not support: the file list shows it.
    container->setUrl(QUrl(QStringLiteral("trash:/")));
    QTRY_COMPARE(container->url(), QUrl(QStringLiteral("trash:/")));
    QTRY_VERIFY(host->isDolphinViewShown());
    QCOMPARE(host->lastRestore().source, QStringLiteral("location"));
    QCOMPARE(host->lastRestore().requestedViewId, capsId);
    QVERIFY(host->lastRestore().fallback);
    QVERIFY(!host->showPluginView(capsId, &m_capsPlugin));
    QVERIFY(host->isDolphinViewShown());

    // The history brings the view back where it is supported.
    m_mainWindow->actionCollection()->action(KStandardAction::name(KStandardAction::Back))->trigger();
    QTRY_COMPARE(container->url(), m_dir->url());
    // (The test plugin is not in the registry, so the history cannot restore it; a real
    // plugin comes back, see whaletreemaptest.) It can be shown again here.
    QVERIFY(host->showPluginView(capsId, &m_capsPlugin));
}

void WhaleActionsTest::testSplitView()
{
    QVERIFY(showMainWindow());
    action(QStringLiteral("split_view"))->trigger();
    DolphinTabPage *page = m_mainWindow->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"))->currentTabPage();
    DolphinViewContainer *left = page->primaryViewContainer();
    DolphinViewContainer *right = page->secondaryViewContainer();
    QVERIFY(left && right);
    QTRY_COMPARE(right->view()->itemsCount(), 4);

    // Left: test list with a.txt selected. Right: file list without a selection.
    auto leftList = qobject_cast<QListView *>(showView(left, testListId));
    QVERIFY(leftList);
    left->setActive(true);
    QTRY_VERIFY(left->isActive());
    click(leftList, QStringLiteral("a.txt"));
    QTRY_COMPARE(names(left->view()->selectedItems()), QStringList({QStringLiteral("a.txt")}));
    QVERIFY(right->view()->selectedItems().isEmpty());

    // The gates follow the active half at once.
    QVERIFY(!action(stdAction(KStandardAction::ZoomIn))->isEnabled());
    QTRY_VERIFY(action(stdAction(KStandardAction::Copy))->isEnabled());
    right->setActive(true);
    QTRY_VERIFY(right->isActive());
    QVERIFY(action(stdAction(KStandardAction::ZoomIn))->isEnabled());
    QVERIFY(viewActions()->gateReason(stdAction(KStandardAction::MoveToTrash)).isEmpty());
    // In the file list, Delete without a selection starts its selection mode, as always.
    QVERIFY(action(stdAction(KStandardAction::MoveToTrash))->isEnabled());

    // Test lists on both sides, different selections: each action works on the active half.
    auto rightList = qobject_cast<QListView *>(showView(right, testListId));
    QVERIFY(rightList);
    click(rightList, QStringLiteral("c.txt"));
    QTRY_COMPARE(names(right->view()->selectedItems()), QStringList({QStringLiteral("c.txt")}));
    QTest::keyClick(rightList, Qt::Key_Delete);
    QTRY_VERIFY(!QFile::exists(m_dir->path() + QStringLiteral("/c.txt")));
    QVERIFY(QFile::exists(m_dir->path() + QStringLiteral("/a.txt"))); // the inactive half's selection

    left->setActive(true);
    QTRY_VERIFY(left->isActive());
    QTRY_COMPARE(names(left->view()->selectedItems()), QStringList({QStringLiteral("a.txt")}));
    QTRY_VERIFY(action(stdAction(KStandardAction::MoveToTrash))->isEnabled());
    action(stdAction(KStandardAction::MoveToTrash))->trigger();
    QTRY_VERIFY(!QFile::exists(m_dir->path() + QStringLiteral("/a.txt")));
    QVERIFY(QFile::exists(m_dir->path() + QStringLiteral("/b.txt")));
}

QTEST_MAIN(WhaleActionsTest)

#include "whaleactionstest.moc"
