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
#include "whalefileoperations.h"
#include "whaleview.h"
#include "whaleviewactions.h"
#include "whaleviewcontextimpl.h"
#include "whaleviewhost.h"
#include "whaleviewplugin.h"
#include "whaleviewregistry.h"

#include <KActionCollection>
#include <KApplicationTrader>
#include <KConfigGroup>
#include <KIO/Paste>
#include <KIO/RenameFileDialog>
#include <KLocalizedString>
#include <KMessageWidget>
#include <KPropertiesDialog>
#include <KSharedConfig>
#include <KStandardAction>
#include <KSycoca>
#include <KUrlMimeData>

#include <QClipboard>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

// What QTest uses for mouse input on a window (see the dev MCP): unlike QTest::mouseClick()
// on a widget, the event takes the way of real input, so a right press makes a context menu event.
Q_GUI_EXPORT void qt_handleMouseEvent(QWindow *window,
                                      const QPointF &local,
                                      const QPointF &global,
                                      Qt::MouseButtons state,
                                      Qt::MouseButton button,
                                      QEvent::Type type,
                                      Qt::KeyboardModifiers mods,
                                      int timestamp);

using Operation = WhaleFileOperations::Operation;
using Result = WhaleFileOperations::Result;

namespace
{
const QString treemapId = QStringLiteral("whale_treemap");

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

QVariantList menuEntries(QWidget *view, const QUrl &url)
{
    QVariantList entries;
    QMetaObject::invokeMethod(view, "contextMenuEntries", Qt::DirectConnection, Q_RETURN_ARG(QVariantList, entries), Q_ARG(QUrl, url));
    return entries;
}

QStringList entryNames(const QVariantList &entries)
{
    QStringList names;
    for (const QVariant &entry : entries) {
        names.append(entry.toMap().value(QStringLiteral("name")).toString());
    }
    return names;
}

QWidget *canvasOf(QWidget *view)
{
    return view->findChild<QWidget *>(QStringLiteral("whaleTreemapCanvas"));
}

QUrl selectedOf(QWidget *view)
{
    return QUrl(treemapState(view).value(QStringLiteral("selected")).toString());
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

/// A visible dialog (e.g. KIO's confirmation), other than @p except.
QDialog *anyVisibleDialog(const QDialog *except = nullptr)
{
    const auto windows = QApplication::topLevelWidgets();
    for (QWidget *window : windows) {
        if (auto dialog = qobject_cast<QDialog *>(window); dialog && dialog != except && dialog->isVisible()) {
            return dialog;
        }
    }
    return nullptr;
}

/// The Menu key as Qt delivers a real one: a context menu event, then the key press, which
/// goes to the menu if one opened (it grabs the keyboard).
void pressMenuKey(QWidget *widget)
{
    QContextMenuEvent event(QContextMenuEvent::Keyboard, widget->rect().center(), widget->mapToGlobal(widget->rect().center()));
    QApplication::sendEvent(widget, &event);
    QWidget *popup = QApplication::activePopupWidget();
    QTest::keyClick(popup ? popup : widget, Qt::Key_Menu);
}

void rightClick(QWidget *widget, const QPoint &position)
{
    static int time = 0;
    QWidget *window = widget->window();
    const QPointF local = widget->mapTo(window, QPointF(position));
    const QPointF global = window->mapToGlobal(local);
    // Move there first (from one pixel off, so the move is not dropped as a repetition):
    // the window delivers the context menu event to the widget the pointer is over.
    qt_handleMouseEvent(window->windowHandle(),
                        local + QPointF(1, 1),
                        global + QPointF(1, 1),
                        Qt::NoButton,
                        Qt::NoButton,
                        QEvent::MouseMove,
                        Qt::NoModifier,
                        time += 1000);
    qt_handleMouseEvent(window->windowHandle(), local, global, Qt::NoButton, Qt::NoButton, QEvent::MouseMove, Qt::NoModifier, time += 10);
    qt_handleMouseEvent(window->windowHandle(), local, global, Qt::RightButton, Qt::RightButton, QEvent::MouseButtonPress, Qt::NoModifier, time += 10);
    qt_handleMouseEvent(window->windowHandle(), local, global, Qt::NoButton, Qt::RightButton, QEvent::MouseButtonRelease, Qt::NoModifier, time += 10);
}

/// Presses the accepting button (Yes/Accept role) of @p dialog.
bool accept(QDialog *dialog)
{
    const auto boxes = dialog->findChildren<QDialogButtonBox *>();
    for (QDialogButtonBox *box : boxes) {
        const auto buttons = box->buttons();
        for (QAbstractButton *button : buttons) {
            const QDialogButtonBox::ButtonRole role = box->buttonRole(button);
            if (role == QDialogButtonBox::YesRole || role == QDialogButtonBox::AcceptRole) {
                button->click();
                return true;
            }
        }
    }
    return false;
}

QString messageText(DolphinViewContainer *container)
{
    const auto widgets = container->findChildren<KMessageWidget *>();
    for (KMessageWidget *widget : widgets) {
        if (widget->isVisible() || widget->isShowAnimationRunning()) {
            return widget->text();
        }
    }
    return {};
}

using Finished = std::tuple<Operation, QList<QUrl>, Result, QList<QUrl>>;
}

/**
 * T1: operations on entries deep below the location, through WhaleFileOperations
 * (the host's side) and through the treemap's own context menu.
 */
class WhaleDeepOperationsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    // The host's operations (WhaleFileOperations)
    void testSupportedOperations();
    void testOpenDeepFile();
    void testOpenDeepFolder();
    void testOpenSeveral();
    void testShowInFolder();
    void testShowInFolderOfListedItem();
    void testProperties();
    void testRename();
    void testRenameConflict();
    void testTrash();
    void testDelete();
    void testMissingItem();
    void testNoPermission();
    void testCancel();
    void testLocationIsProtected();
    void testSymlink();
    void testClipboard();

    // The treemap's context menu
    void testMenuOfDeepFile();
    void testMenuOfDeepFolder();
    void testMenuEntriesFollowPermissions();
    void testMenuActsOnTheChosenEntry();
    void testMenuKey();
    void testDeepSelectionStaysOutOfWhale();
    void testGlobalActionsFollowTheMenusEntry();
    void testRescanAfterOperation();
    void testRenamedSelectionFollows();
    void testShowInFolderFromMenu();
    void testSplitView();
    void testNavigation();

private:
    DolphinViewContainer *showMainWindow();
    QWidget *showTreemap(DolphinViewContainer *container);
    WhaleFileOperations *operations(DolphinViewContainer *container) const;
    QAction *action(const QString &name) const;
    QUrl url(const QString &path) const;
    QString path(const QString &relative) const;
    QString trashedPath(const QString &fileName) const;
    QStringList openedFiles() const;
    void setConfirmations(bool confirm);
    bool select(QWidget *view, const QUrl &target);
    QMenu *openMenu(QWidget *view, const QUrl &target);
    bool waitForReady(QWidget *view, int scans);

    QTemporaryDir m_tmpHome;
    QString m_openerLog;
    QScopedPointer<TestDir> m_dir;
    QScopedPointer<DolphinMainWindow> m_mainWindow;
};

void WhaleDeepOperationsTest::initTestCase()
{
    QVERIFY(m_tmpHome.isValid());
    QCoreApplication::setApplicationName(QStringLiteral(WHALE_COMPONENT));
    QCoreApplication::setOrganizationDomain(QStringLiteral(WHALE_ORG_DOMAIN));
    qputenv("HOME", m_tmpHome.path().toUtf8());
    // The trash of this test lives in the temporary home (the trash worker is another
    // process and does not know the test mode of QStandardPaths).
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
}

void WhaleDeepOperationsTest::init()
{
    // The tree of the T1 plan; sizes make every entry a cell of its own.
    m_dir.reset(new TestDir());
    m_dir->createFile(QStringLiteral("a/deep.txt"), QByteArray(40000, 'd'));
    m_dir->createFile(QStringLiteral("a/rename-me.txt"), QByteArray(30000, 'r'));
    m_dir->createFile(QStringLiteral("a/trash-me.txt"), QByteArray(30000, 't'));
    m_dir->createFile(QStringLiteral("a/delete-me.txt"), QByteArray(30000, 'x'));
    m_dir->createFile(QStringLiteral("a/sub/inner.txt"), QByteArray(30000, 'i'));
    m_dir->createFile(QStringLiteral("b/target.txt"), QByteArray(50000, 'g'));
    m_dir->createFile(QStringLiteral("top.txt"), QByteArray(20000, 'o'));
    QFile::remove(m_openerLog);
    setConfirmations(false);
    m_mainWindow.reset(new DolphinMainWindow());
}

void WhaleDeepOperationsTest::cleanup()
{
    // Dialogs of a failed test must not answer the next one.
    const auto windows = QApplication::topLevelWidgets();
    for (QWidget *window : windows) {
        if (auto dialog = qobject_cast<QDialog *>(window); dialog && dialog->isVisible()) {
            dialog->reject();
        }
    }
    while (QWidget *popup = QApplication::activePopupWidget()) {
        popup->close();
    }
    m_mainWindow.reset();
    if (m_dir) {
        QFile::setPermissions(path(QStringLiteral("a")), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    }
    m_dir.reset();
}

void WhaleDeepOperationsTest::setConfirmations(bool confirm)
{
    // KIO reads the confirmations from the shared kiorc when it asks.
    KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("kiorc"), KConfig::NoGlobals), QStringLiteral("Confirmations"));
    group.writeEntry("ConfirmDelete", confirm);
    group.writeEntry("ConfirmTrash", confirm);
    group.sync();
}

DolphinViewContainer *WhaleDeepOperationsTest::showMainWindow()
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
            [container]() {
                return container->view()->itemsCount() == 3;
            },
            5000)) {
        return nullptr;
    }
    return container;
}

QWidget *WhaleDeepOperationsTest::showTreemap(DolphinViewContainer *container)
{
    WhaleViewPlugin *plugin = m_mainWindow->findChild<WhaleViewActions *>()->registry()->plugin(treemapId);
    if (!plugin || !container->viewHost()->showPluginView(treemapId, plugin)) {
        return nullptr;
    }
    QWidget *view = container->viewHost()->currentView()->widget();
    return waitForReady(view, 1) ? view : nullptr;
}

bool WhaleDeepOperationsTest::waitForReady(QWidget *view, int scans)
{
    return QTest::qWaitFor(
        [view, scans]() {
            const QVariantMap state = treemapState(view);
            return state.value(QStringLiteral("status")).toString() == QLatin1String("ready") && state.value(QStringLiteral("finishedScans")).toInt() >= scans;
        },
        5000);
}

WhaleFileOperations *WhaleDeepOperationsTest::operations(DolphinViewContainer *container) const
{
    WhaleViewContextImpl *context = container->viewHost()->pluginContext();
    return context ? context->fileOperations() : nullptr;
}

QAction *WhaleDeepOperationsTest::action(const QString &name) const
{
    return m_mainWindow->actionCollection()->action(name);
}

QUrl WhaleDeepOperationsTest::url(const QString &relative) const
{
    return QUrl::fromLocalFile(path(relative));
}

QString WhaleDeepOperationsTest::path(const QString &relative) const
{
    return m_dir->path() + (relative.isEmpty() ? QString() : QLatin1Char('/') + relative);
}

QString WhaleDeepOperationsTest::trashedPath(const QString &fileName) const
{
    return m_tmpHome.filePath(QStringLiteral("data/Trash/files/") + fileName);
}

QStringList WhaleDeepOperationsTest::openedFiles() const
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

bool WhaleDeepOperationsTest::select(QWidget *view, const QUrl &target)
{
    const QRect rect = cellRect(view, target);
    if (rect.isEmpty()) {
        return false;
    }
    QTest::mouseClick(canvasOf(view), Qt::LeftButton, {}, rect.center());
    for (int step = 0; step < 10; ++step) {
        if (selectedOf(view) == target) {
            return true;
        }
        QTest::keyClick(canvasOf(view), Qt::Key_Up);
    }
    return false;
}

QMenu *WhaleDeepOperationsTest::openMenu(QWidget *view, const QUrl &target)
{
    const QRect rect = cellRect(view, target);
    if (rect.isEmpty()) {
        return nullptr;
    }
    // A real right click: the window turns the press into a context menu event.
    rightClick(canvasOf(view), rect.center());
    if (!QTest::qWaitFor(
            []() {
                return qobject_cast<QMenu *>(QApplication::activePopupWidget());
            },
            2000)) {
        return nullptr;
    }
    return qobject_cast<QMenu *>(QApplication::activePopupWidget());
}

// The host's operations ---------------------------------------------------------

void WhaleDeepOperationsTest::testSupportedOperations()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QVERIFY(showTreemap(container));
    WhaleFileOperations *ops = operations(container);
    QVERIFY(ops);

    const auto all = Operation::Open | Operation::OpenWith | Operation::ShowInFolder | Operation::Properties | Operation::Rename | Operation::Trash
        | Operation::Delete | Operation::Copy | Operation::Cut | Operation::CopyLocation | Operation::Duplicate;
    // Deep and listed entries alike: everything the file list offers for them.
    QCOMPARE(ops->supportedOperations({url(QStringLiteral("a/deep.txt"))}), WhaleFileOperations::Operations(all));
    QCOMPARE(ops->supportedOperations({url(QStringLiteral("a/sub"))}), WhaleFileOperations::Operations(all));
    QCOMPARE(ops->supportedOperations({url(QStringLiteral("top.txt"))}), WhaleFileOperations::Operations(all));
    // Several items: no "Show in Folder" (one folder to go to).
    QCOMPARE(ops->supportedOperations({url(QStringLiteral("a/deep.txt")), url(QStringLiteral("b/target.txt"))}),
             WhaleFileOperations::Operations(all & ~WhaleFileOperations::Operations(Operation::ShowInFolder)));
    QCOMPARE(ops->supportedOperations({}), WhaleFileOperations::Operations());
    QCOMPARE(ops->supportedOperations({QUrl()}), WhaleFileOperations::Operations());
}

void WhaleDeepOperationsTest::testOpenDeepFile()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QVERIFY(showTreemap(container));
    // The usual way of the file list (KIO::OpenUrlJob with the file's application).
    operations(container)->openItems({url(QStringLiteral("a/deep.txt"))});
    QTRY_COMPARE_WITH_TIMEOUT(openedFiles(), QStringList({path(QStringLiteral("a/deep.txt"))}), 10000);
    // A file does not change the location.
    QCOMPARE(container->url(), m_dir->url());
    QCOMPARE(container->viewHost()->currentViewId(), treemapId);
}

void WhaleDeepOperationsTest::testOpenDeepFolder()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QVERIFY(showTreemap(container));
    operations(container)->openItems({url(QStringLiteral("a/sub"))});
    QTRY_COMPARE(container->url(), url(QStringLiteral("a/sub")));
    // With a history entry, as entering a folder in the file list.
    QVERIFY(action(KStandardAction::name(KStandardAction::Back))->isEnabled());
    QCOMPARE(container->viewHost()->currentViewId(), treemapId);
    QVERIFY(openedFiles().isEmpty());
}

void WhaleDeepOperationsTest::testOpenSeveral()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QVERIFY(showTreemap(container));
    auto tabs = m_mainWindow->findChild<DolphinTabWidget *>();
    QCOMPARE(tabs->count(), 1);
    // As several items in the file list: folders in new tabs, the files with their applications.
    operations(container)->openItems({url(QStringLiteral("a/sub")), url(QStringLiteral("a/deep.txt")), url(QStringLiteral("b/target.txt"))});
    QTRY_COMPARE(tabs->count(), 2);
    QTRY_COMPARE_WITH_TIMEOUT(openedFiles().count(), 2, 10000);
    QStringList opened = openedFiles();
    opened.sort();
    QCOMPARE(opened, QStringList({path(QStringLiteral("a/deep.txt")), path(QStringLiteral("b/target.txt"))}));
    // This view area stays where it is.
    QCOMPARE(container->url(), m_dir->url());
}

void WhaleDeepOperationsTest::testShowInFolder()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QWidget *view = showTreemap(container);
    QVERIFY(view);
    DolphinViewBridge *bridge = container->viewHost()->bridge();

    // A deep file: its folder becomes the location, and the file Whale's selection there.
    const QUrl deep = url(QStringLiteral("a/deep.txt"));
    operations(container)->showItemInFolder(deep);
    QTRY_COMPARE(container->url(), url(QStringLiteral("a")));
    QTRY_COMPARE(bridge->selection().selected(), QList<QUrl>({deep}));
    QCOMPARE(bridge->selection().current(), deep);
    QCOMPARE(container->view()->selectedItems().count(), 1);
    // The treemap (still shown) shows it as its selection, now an entry of the listing.
    view = container->viewHost()->currentView()->widget();
    QTRY_COMPARE(selectedOf(view), deep);
    QTRY_VERIFY(!treemapState(view).value(QStringLiteral("selectedDeep")).toBool());

    // A deep folder: its parent folder, with the folder selected.
    const QUrl sub = url(QStringLiteral("a/sub"));
    container->setUrl(m_dir->url());
    QTRY_COMPARE(container->view()->itemsCount(), 3);
    operations(container)->showItemInFolder(sub);
    QTRY_COMPARE(container->url(), url(QStringLiteral("a")));
    QTRY_COMPARE(bridge->selection().selected(), QList<QUrl>({sub}));

    // Going elsewhere before the folder is shown drops the pending selection.
    container->setUrl(m_dir->url());
    QTRY_COMPARE(container->view()->itemsCount(), 3);
    operations(container)->showItemInFolder(url(QStringLiteral("b/target.txt")));
    container->setUrl(url(QStringLiteral("a")));
    QTRY_COMPARE(container->url(), url(QStringLiteral("a")));
    QTRY_COMPARE(container->view()->itemsCount(), 5);
    QTest::qWait(200);
    QVERIFY(bridge->selection().isEmpty());
}

void WhaleDeepOperationsTest::testShowInFolderOfListedItem()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QWidget *view = showTreemap(container);
    QVERIFY(view);
    // The folder is the location already: the item is selected at once, nothing navigates.
    operations(container)->showItemInFolder(url(QStringLiteral("b")));
    QCOMPARE(container->viewHost()->bridge()->selection().selected(), QList<QUrl>({url(QStringLiteral("b"))}));
    QCOMPARE(container->url(), m_dir->url());
    QVERIFY(!action(KStandardAction::name(KStandardAction::Back))->isEnabled());
    QTRY_COMPARE(selectedOf(view), url(QStringLiteral("b")));
}

void WhaleDeepOperationsTest::testProperties()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QVERIFY(showTreemap(container));
    const QUrl deep = url(QStringLiteral("a/deep.txt"));
    operations(container)->showProperties({deep});
    KPropertiesDialog *dialog = nullptr;
    QTRY_VERIFY((dialog = visibleDialog<KPropertiesDialog>()));
    QCOMPARE(dialog->items().count(), 1);
    QCOMPARE(dialog->item().url(), deep);
    QVERIFY(dialog->testAttribute(Qt::WA_DeleteOnClose));
    QPointer<KPropertiesDialog> guard(dialog);
    dialog->reject();
    QTRY_VERIFY(!guard);
}

void WhaleDeepOperationsTest::testRename()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QVERIFY(showTreemap(container));
    WhaleFileOperations *ops = operations(container);
    QSignalSpy finished(ops, &WhaleFileOperations::operationFinished);
    const QUrl old = url(QStringLiteral("a/rename-me.txt"));
    ops->renameItems({old});

    // The rename dialog of the file list (never inline).
    KIO::RenameFileDialog *dialog = nullptr;
    QTRY_VERIFY((dialog = visibleDialog<KIO::RenameFileDialog>()));
    auto edit = dialog->findChild<QLineEdit *>();
    QVERIFY(edit);
    QCOMPARE(edit->text(), QStringLiteral("rename-me.txt"));
    edit->setText(QStringLiteral("renamed.txt"));
    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();

    QTRY_VERIFY(QFile::exists(path(QStringLiteral("a/renamed.txt"))));
    QVERIFY(!QFile::exists(path(QStringLiteral("a/rename-me.txt"))));
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(finished.first().at(0).value<Operation>(), Operation::Rename);
    QCOMPARE(finished.first().at(1).value<QList<QUrl>>(), QList<QUrl>({old}));
    QCOMPARE(finished.first().at(2).value<Result>(), Result::Done);
    QCOMPARE(finished.first().at(3).value<QList<QUrl>>(), QList<QUrl>({url(QStringLiteral("a/renamed.txt"))}));
    // Whale's selection is not touched by an operation on a deep entry.
    QVERIFY(container->viewHost()->bridge()->selection().isEmpty());
}

void WhaleDeepOperationsTest::testRenameConflict()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QVERIFY(showTreemap(container));
    WhaleFileOperations *ops = operations(container);
    QSignalSpy finished(ops, &WhaleFileOperations::operationFinished);
    ops->renameItems({url(QStringLiteral("a/rename-me.txt"))});
    KIO::RenameFileDialog *dialog = nullptr;
    QTRY_VERIFY((dialog = visibleDialog<KIO::RenameFileDialog>()));
    dialog->findChild<QLineEdit *>()->setText(QStringLiteral("deep.txt"));
    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();

    // The dialog of the file list refuses an existing name itself and stays open with a
    // message, so the user can choose another name; canceling then keeps both files.
    auto dialogMessage = [dialog]() {
        const auto widgets = dialog->findChildren<KMessageWidget *>();
        for (const KMessageWidget *widget : widgets) {
            if (widget->isVisible()) {
                return widget->text();
            }
        }
        return QString();
    };
    QTRY_VERIFY(dialogMessage().contains(QStringLiteral("already exists")));
    QVERIFY(dialog->isVisible());
    QVERIFY(finished.isEmpty());
    dialog->reject();
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(finished.first().at(2).value<Result>(), Result::Canceled);
    QCOMPARE(QFileInfo(path(QStringLiteral("a/deep.txt"))).size(), 40000);
    QCOMPARE(QFileInfo(path(QStringLiteral("a/rename-me.txt"))).size(), 30000);
}

void WhaleDeepOperationsTest::testTrash()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QVERIFY(showTreemap(container));
    WhaleFileOperations *ops = operations(container);
    QSignalSpy finished(ops, &WhaleFileOperations::operationFinished);
    const QUrl trashMe = url(QStringLiteral("a/trash-me.txt"));
    ops->trashItems({trashMe});
    QTRY_VERIFY(!QFile::exists(path(QStringLiteral("a/trash-me.txt"))));
    QTRY_VERIFY(QFile::exists(trashedPath(QStringLiteral("trash-me.txt"))));
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(finished.first().at(0).value<Operation>(), Operation::Trash);
    QCOMPARE(finished.first().at(2).value<Result>(), Result::Done);
    // Nothing else.
    for (const QString &other :
         {QStringLiteral("a/deep.txt"), QStringLiteral("a/rename-me.txt"), QStringLiteral("a/delete-me.txt"), QStringLiteral("b/target.txt")}) {
        QVERIFY2(QFile::exists(path(other)), qPrintable(other));
    }
}

void WhaleDeepOperationsTest::testDelete()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QVERIFY(showTreemap(container));
    WhaleFileOperations *ops = operations(container);
    QSignalSpy finished(ops, &WhaleFileOperations::operationFinished);
    ops->deleteItems({url(QStringLiteral("a/delete-me.txt"))});
    QTRY_VERIFY(!QFile::exists(path(QStringLiteral("a/delete-me.txt"))));
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(finished.first().at(0).value<Operation>(), Operation::Delete);
    QCOMPARE(finished.first().at(2).value<Result>(), Result::Done);
    QVERIFY(!QFile::exists(trashedPath(QStringLiteral("delete-me.txt"))));
    QVERIFY(QFile::exists(path(QStringLiteral("a/deep.txt"))));
}

void WhaleDeepOperationsTest::testMissingItem()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QVERIFY(showTreemap(container));
    WhaleFileOperations *ops = operations(container);
    const QUrl gone = url(QStringLiteral("a/deep.txt"));
    QVERIFY(QFile::remove(path(QStringLiteral("a/deep.txt"))));

    // Only "Show in Folder" is left; the operations report the item as missing.
    QCOMPARE(ops->supportedOperations({gone}), WhaleFileOperations::Operations(Operation::ShowInFolder));
    QSignalSpy finished(ops, &WhaleFileOperations::operationFinished);
    ops->trashItems({gone});
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.first().at(2).value<Result>(), Result::Failed);
    QTRY_VERIFY(messageText(container).contains(QStringLiteral("deep.txt")));
    ops->renameItems({gone});
    QCOMPARE(finished.count(), 2);
    QCOMPARE(finished.last().at(2).value<Result>(), Result::Failed);
    ops->openItems({gone});
    ops->showProperties({gone});
    QTest::qWait(200);
    QVERIFY(!anyVisibleDialog());
    QVERIFY(openedFiles().isEmpty());

    // A missing folder: not even "Show in Folder".
    QVERIFY(QDir(path(QStringLiteral("a/sub"))).removeRecursively());
    QCOMPARE(ops->supportedOperations({url(QStringLiteral("a/sub/inner.txt"))}), WhaleFileOperations::Operations());
}

void WhaleDeepOperationsTest::testNoPermission()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QVERIFY(showTreemap(container));
    WhaleFileOperations *ops = operations(container);
    // A folder the user may not change: its entries cannot be renamed, trashed, deleted or cut.
    QVERIFY(QFile::setPermissions(path(QStringLiteral("a")), QFile::ReadOwner | QFile::ExeOwner));
    const QUrl deep = url(QStringLiteral("a/deep.txt"));
    const WhaleFileOperations::Operations supported = ops->supportedOperations({deep});
    QVERIFY(supported & Operation::Open);
    QVERIFY(supported & Operation::Copy);
    QVERIFY(supported & Operation::Properties);
    QVERIFY(!(supported & Operation::Rename));
    QVERIFY(!(supported & Operation::Trash));
    QVERIFY(!(supported & Operation::Delete));
    QVERIFY(!(supported & Operation::Cut));

    // Called anyway: KIO fails, the view area says why, the file stays.
    QSignalSpy finished(ops, &WhaleFileOperations::operationFinished);
    ops->deleteItems({deep});
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
    QCOMPARE(finished.first().at(2).value<Result>(), Result::Failed);
    QVERIFY(QFile::exists(path(QStringLiteral("a/deep.txt"))));
    QTRY_VERIFY(!messageText(container).isEmpty());
}

void WhaleDeepOperationsTest::testCancel()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QVERIFY(showTreemap(container));
    WhaleFileOperations *ops = operations(container);
    QSignalSpy finished(ops, &WhaleFileOperations::operationFinished);
    setConfirmations(true);

    // KIO's confirmation, as for Shift+Delete in the file list: canceled, nothing happens.
    ops->deleteItems({url(QStringLiteral("a/delete-me.txt"))});
    QDialog *confirmation = nullptr;
    QTRY_VERIFY((confirmation = anyVisibleDialog()));
    confirmation->reject();
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(finished.first().at(2).value<Result>(), Result::Canceled);
    QVERIFY(QFile::exists(path(QStringLiteral("a/delete-me.txt"))));
    QVERIFY(messageText(container).isEmpty());

    // Confirmed: deleted.
    ops->deleteItems({url(QStringLiteral("a/delete-me.txt"))});
    QTRY_VERIFY((confirmation = anyVisibleDialog()));
    QVERIFY(accept(confirmation));
    QTRY_COMPARE(finished.count(), 2);
    QCOMPARE(finished.last().at(2).value<Result>(), Result::Done);
    QVERIFY(!QFile::exists(path(QStringLiteral("a/delete-me.txt"))));

    // The trash asks too when the user wants that; canceled.
    ops->trashItems({url(QStringLiteral("a/trash-me.txt"))});
    QTRY_VERIFY((confirmation = anyVisibleDialog()));
    confirmation->reject();
    QTRY_COMPARE(finished.count(), 3);
    QCOMPARE(finished.last().at(2).value<Result>(), Result::Canceled);
    QVERIFY(QFile::exists(path(QStringLiteral("a/trash-me.txt"))));

    // The rename dialog, canceled.
    ops->renameItems({url(QStringLiteral("a/rename-me.txt"))});
    KIO::RenameFileDialog *rename = nullptr;
    QTRY_VERIFY((rename = visibleDialog<KIO::RenameFileDialog>()));
    rename->reject();
    QTRY_COMPARE(finished.count(), 4);
    QCOMPARE(finished.last().at(0).value<Operation>(), Operation::Rename);
    QCOMPARE(finished.last().at(2).value<Result>(), Result::Canceled);
    QVERIFY(QFile::exists(path(QStringLiteral("a/rename-me.txt"))));
}

void WhaleDeepOperationsTest::testLocationIsProtected()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QVERIFY(showTreemap(container));
    WhaleFileOperations *ops = operations(container);
    // Never remove the shown folder or one above it.
    for (const QUrl &folder : {m_dir->url(), url(QString()).adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash)}) {
        const WhaleFileOperations::Operations supported = ops->supportedOperations({folder});
        QVERIFY(!(supported & Operation::Rename));
        QVERIFY(!(supported & Operation::Trash));
        QVERIFY(!(supported & Operation::Delete));
        QVERIFY(!(supported & Operation::Cut));
    }
    QSignalSpy finished(ops, &WhaleFileOperations::operationFinished);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Refusing to remove the location")));
    ops->deleteItems({m_dir->url()});
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.first().at(2).value<Result>(), Result::Failed);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Refusing to rename the location")));
    ops->renameItems({m_dir->url()});
    QCOMPARE(finished.count(), 2);
    QTest::qWait(200);
    QVERIFY(QFile::exists(path(QStringLiteral("a/deep.txt"))));
    QVERIFY(!anyVisibleDialog());
}

void WhaleDeepOperationsTest::testSymlink()
{
    QVERIFY(QFile::link(path(QStringLiteral("b/target.txt")), path(QStringLiteral("a/link.txt"))));
    QVERIFY(QFile::link(path(QStringLiteral("nowhere")), path(QStringLiteral("a/broken.txt"))));
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QVERIFY(showTreemap(container));
    WhaleFileOperations *ops = operations(container);
    // The link itself goes, never its target; a broken link can be removed too.
    QVERIFY(ops->supportedOperations({url(QStringLiteral("a/broken.txt"))}) & Operation::Delete);
    ops->deleteItems({url(QStringLiteral("a/link.txt")), url(QStringLiteral("a/broken.txt"))});
    QTRY_VERIFY(!QFileInfo(path(QStringLiteral("a/link.txt"))).isSymLink());
    QTRY_VERIFY(!QFileInfo(path(QStringLiteral("a/broken.txt"))).isSymLink());
    QCOMPARE(QFileInfo(path(QStringLiteral("b/target.txt"))).size(), 50000);
}

void WhaleDeepOperationsTest::testClipboard()
{
    if (QGuiApplication::platformName() != QLatin1String("offscreen")) {
        QSKIP("uses the clipboard, which is the user's own outside of the offscreen platform");
    }
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QVERIFY(showTreemap(container));
    WhaleFileOperations *ops = operations(container);
    const QUrl deep = url(QStringLiteral("a/deep.txt"));

    ops->copyItems({deep});
    const QMimeData *data = QApplication::clipboard()->mimeData();
    QCOMPARE(KUrlMimeData::urlsFromMimeData(data), QList<QUrl>({deep}));
    QVERIFY(!KIO::isClipboardDataCut(data));

    ops->cutItems({deep});
    data = QApplication::clipboard()->mimeData();
    QCOMPARE(KUrlMimeData::urlsFromMimeData(data), QList<QUrl>({deep}));
    QVERIFY(KIO::isClipboardDataCut(data));

    // Whale's own Paste (into the location) moves the cut file.
    QTRY_VERIFY(action(KStandardAction::name(KStandardAction::Paste))->isEnabled());
    action(KStandardAction::name(KStandardAction::Paste))->trigger();
    QTRY_VERIFY(QFile::exists(path(QStringLiteral("deep.txt"))));
    QTRY_VERIFY(!QFile::exists(path(QStringLiteral("a/deep.txt"))));

    ops->copyLocations({url(QStringLiteral("b/target.txt"))});
    QCOMPARE(QApplication::clipboard()->text(), path(QStringLiteral("b/target.txt")));
}

// The treemap's context menu -----------------------------------------------------

void WhaleDeepOperationsTest::testMenuOfDeepFile()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QWidget *view = showTreemap(container);
    QVERIFY(view);
    const QUrl deep = url(QStringLiteral("a/deep.txt"));
    QMenu *menu = openMenu(view, deep);
    QVERIFY(menu);
    // The right click selected the entry, as in the file list.
    QCOMPARE(selectedOf(view), deep);
    QStringList names;
    const auto actions = menu->actions();
    for (QAction *entry : actions) {
        if (!entry->isSeparator()) {
            names.append(entry->objectName());
            QVERIFY2(entry->isEnabled(), qPrintable(entry->objectName()));
            // Only the treemap's own entries: no action of Whale's (with their shortcuts).
            QVERIFY2(entry->shortcut().isEmpty(), qPrintable(entry->objectName()));
            QVERIFY(!m_mainWindow->actionCollection()->actions().contains(entry));
        }
    }
    QCOMPARE(names,
             QStringList({QStringLiteral("open"),
                          QStringLiteral("open_with"),
                          QStringLiteral("show_in_folder"),
                          QStringLiteral("copy"),
                          QStringLiteral("cut"),
                          QStringLiteral("copy_location"),
                          QStringLiteral("rename"),
                          QStringLiteral("trash"),
                          QStringLiteral("delete"),
                          QStringLiteral("properties")}));
    QCOMPARE(menu->findChild<QAction *>(QStringLiteral("show_in_folder"))->text(), QStringLiteral("Show in Folder"));
    // The same as the treemap describes for development tools.
    QCOMPARE(entryNames(menuEntries(view, deep)), names);
    menu->close();
    QTRY_VERIFY(!QApplication::activePopupWidget());
}

void WhaleDeepOperationsTest::testMenuOfDeepFolder()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QWidget *view = showTreemap(container);
    QVERIFY(view);
    const QUrl sub = url(QStringLiteral("a/sub"));
    // A folder is covered by its entries: select it, then the Menu key opens its menu.
    QVERIFY(select(view, sub));
    pressMenuKey(canvasOf(view));
    QTRY_VERIFY(qobject_cast<QMenu *>(QApplication::activePopupWidget()));
    auto menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
    QStringList names;
    const auto actions = menu->actions();
    for (QAction *entry : actions) {
        if (!entry->isSeparator()) {
            names.append(entry->objectName());
        }
    }
    // No "Open With" for folders.
    QCOMPARE(names,
             QStringList({QStringLiteral("open"),
                          QStringLiteral("show_in_folder"),
                          QStringLiteral("copy"),
                          QStringLiteral("cut"),
                          QStringLiteral("copy_location"),
                          QStringLiteral("rename"),
                          QStringLiteral("trash"),
                          QStringLiteral("delete"),
                          QStringLiteral("properties")}));
    QCOMPARE(menu->findChild<QAction *>(QStringLiteral("show_in_folder"))->text(), QStringLiteral("Show in Parent Folder"));
    menu->close();
}

void WhaleDeepOperationsTest::testMenuEntriesFollowPermissions()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QWidget *view = showTreemap(container);
    QVERIFY(view);
    // Read-only folder: nothing that would remove the entry from it is offered.
    QVERIFY(QFile::setPermissions(path(QStringLiteral("a")), QFile::ReadOwner | QFile::ExeOwner));
    QCOMPARE(entryNames(menuEntries(view, url(QStringLiteral("a/deep.txt")))),
             QStringList({QStringLiteral("open"),
                          QStringLiteral("open_with"),
                          QStringLiteral("show_in_folder"),
                          QStringLiteral("copy"),
                          QStringLiteral("copy_location"),
                          QStringLiteral("properties")}));
    QVERIFY(QFile::setPermissions(path(QStringLiteral("a")), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));

    // Gone meanwhile (the treemap still shows it): only "Show in Folder".
    QVERIFY(QFile::remove(path(QStringLiteral("a/deep.txt"))));
    QCOMPARE(entryNames(menuEntries(view, url(QStringLiteral("a/deep.txt")))), QStringList({QStringLiteral("show_in_folder")}));

    // No menu for the location itself, nor for anything outside it.
    QVERIFY(menuEntries(view, m_dir->url()).isEmpty());
    QVERIFY(menuEntries(view, QUrl::fromLocalFile(QDir::tempPath())).isEmpty());
    QVERIFY(menuEntries(view, QUrl()).isEmpty());
}

void WhaleDeepOperationsTest::testMenuActsOnTheChosenEntry()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QWidget *view = showTreemap(container);
    QVERIFY(view);
    // Another entry was selected before; the right click chooses this one.
    QVERIFY(select(view, url(QStringLiteral("a/deep.txt"))));
    QMenu *menu = openMenu(view, url(QStringLiteral("a/trash-me.txt")));
    QVERIFY(menu);
    QAction *trash = menu->findChild<QAction *>(QStringLiteral("trash"));
    QVERIFY(trash);
    menu->hide();
    trash->trigger();
    QTRY_VERIFY(!QFile::exists(path(QStringLiteral("a/trash-me.txt"))));
    QTRY_VERIFY(QFile::exists(trashedPath(QStringLiteral("trash-me.txt"))));
    for (const QString &other :
         {QStringLiteral("a/deep.txt"), QStringLiteral("a/rename-me.txt"), QStringLiteral("a/delete-me.txt"), QStringLiteral("top.txt")}) {
        QVERIFY2(QFile::exists(path(other)), qPrintable(other));
    }
}

void WhaleDeepOperationsTest::testMenuKey()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QWidget *view = showTreemap(container);
    QVERIFY(view);
    // Without a selection, the Menu key shows nothing.
    canvasOf(view)->setFocus();
    pressMenuKey(canvasOf(view));
    QTest::qWait(100);
    QVERIFY(!QApplication::activePopupWidget());

    // With one: exactly one menu, for the selected entry.
    QVERIFY(select(view, url(QStringLiteral("a/deep.txt"))));
    int menus = 0;
    const auto connection = connect(qApp, &QApplication::focusChanged, this, [&menus](QWidget *, QWidget *now) {
        menus += qobject_cast<QMenu *>(now) ? 1 : 0;
    });
    pressMenuKey(canvasOf(view));
    QTRY_VERIFY(qobject_cast<QMenu *>(QApplication::activePopupWidget()));
    QTest::qWait(100);
    disconnect(connection);
    // Synthesized popups only stay open offscreen; on Wayland they need real input (the
    // acceptance test checks one open menu there, with the real Menu key).
    if (QGuiApplication::platformName() == QLatin1String("offscreen")) {
        const auto popups = canvasOf(view)->findChildren<QMenu *>();
        QCOMPARE(std::count_if(popups.cbegin(),
                               popups.cend(),
                               [](const QMenu *menu) {
                                   return menu->isVisible();
                               }),
                 1);
    }
    QVERIFY(menus <= 1);
    if (QWidget *popup = QApplication::activePopupWidget()) {
        popup->close();
    }
}

void WhaleDeepOperationsTest::testDeepSelectionStaysOutOfWhale()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QWidget *view = showTreemap(container);
    QVERIFY(view);
    DolphinViewBridge *bridge = container->viewHost()->bridge();
    // A listed entry first, so a change is visible.
    QVERIFY(select(view, url(QStringLiteral("top.txt"))));
    QTRY_COMPARE(bridge->selection().selected(), QList<QUrl>({url(QStringLiteral("top.txt"))}));
    QMenu *menu = openMenu(view, url(QStringLiteral("a/deep.txt")));
    QVERIFY(menu);
    QVERIFY(treemapState(view).value(QStringLiteral("selectedDeep")).toBool());
    QTRY_VERIFY(bridge->selection().isEmpty());
    QVERIFY(container->view()->selectedItems().isEmpty());
    QTest::keyClick(menu, Qt::Key_Escape);
}

void WhaleDeepOperationsTest::testGlobalActionsFollowTheMenusEntry()
{
    // Since T2 Whale's actions act on the treemap's selection; the menu does not change that.
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QWidget *view = showTreemap(container);
    QVERIFY(view);
    QMenu *menu = openMenu(view, url(QStringLiteral("a/deep.txt")));
    QVERIFY(menu);
    for (const QString &name : {KStandardAction::name(KStandardAction::MoveToTrash),
                                KStandardAction::name(KStandardAction::DeleteFile),
                                KStandardAction::name(KStandardAction::RenameFile),
                                KStandardAction::name(KStandardAction::Cut),
                                KStandardAction::name(KStandardAction::Copy),
                                QStringLiteral("duplicate"),
                                QStringLiteral("copy_location")}) {
        QVERIFY2(action(name)->isEnabled(), qPrintable(name));
    }
    QVERIFY(!action(QStringLiteral("compare_files"))->isEnabled());
    QVERIFY(!action(QStringLiteral("copy_to_inactive_split_view"))->isEnabled()); // no split view
    menu->close();
    QVERIFY(container->viewHost()->bridge()->selection().isEmpty());
}

void WhaleDeepOperationsTest::testRescanAfterOperation()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QWidget *view = showTreemap(container);
    QVERIFY(view);
    const QUrl trashMe = url(QStringLiteral("a/trash-me.txt"));
    QVERIFY(select(view, trashMe));
    const int scans = treemapState(view).value(QStringLiteral("finishedScans")).toInt();
    const qint64 total = treemapState(view).value(QStringLiteral("totalSize")).toLongLong();

    operations(container)->trashItems({trashMe});
    // The treemap scans again by itself, and the cache does not answer with the old tree.
    QVERIFY(waitForReady(view, scans + 1));
    const QVariantMap state = treemapState(view);
    QCOMPARE(state.value(QStringLiteral("operationScans")).toInt(), 1);
    QCOMPARE(state.value(QStringLiteral("lastOperation")).toString(), QStringLiteral("Trash:Done"));
    QCOMPARE(state.value(QStringLiteral("totalSize")).toLongLong(), total - 30000);
    QVERIFY(cellRect(view, trashMe).isEmpty());
    // The removed selection went away; Whale's selection stays empty.
    QVERIFY(state.value(QStringLiteral("selected")).toString().isEmpty());
    QVERIFY(container->viewHost()->bridge()->selection().isEmpty());

    // Canceled operations change nothing and scan nothing.
    setConfirmations(true);
    operations(container)->deleteItems({url(QStringLiteral("a/delete-me.txt"))});
    QDialog *confirmation = nullptr;
    QTRY_VERIFY((confirmation = anyVisibleDialog()));
    confirmation->reject();
    QTRY_COMPARE(treemapState(view).value(QStringLiteral("lastOperation")).toString(), QStringLiteral("Delete:Canceled"));
    QCOMPARE(treemapState(view).value(QStringLiteral("operationScans")).toInt(), 1);
    QVERIFY(!cellRect(view, url(QStringLiteral("a/delete-me.txt"))).isEmpty());
}

void WhaleDeepOperationsTest::testRenamedSelectionFollows()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QWidget *view = showTreemap(container);
    QVERIFY(view);
    // Zoomed into a, the renamed entry selected.
    QVERIFY(select(view, url(QStringLiteral("a/rename-me.txt"))));
    QTest::keyClick(canvasOf(view), Qt::Key_Plus);
    QTRY_COMPARE(treemapState(view).value(QStringLiteral("zoomPath")).toStringList(), QStringList({QStringLiteral("a")}));
    QMenu *menu = openMenu(view, url(QStringLiteral("a/rename-me.txt")));
    QVERIFY(menu);
    QAction *rename = menu->findChild<QAction *>(QStringLiteral("rename"));
    menu->hide();
    rename->trigger();
    KIO::RenameFileDialog *dialog = nullptr;
    QTRY_VERIFY((dialog = visibleDialog<KIO::RenameFileDialog>()));
    dialog->findChild<QLineEdit *>()->setText(QStringLiteral("renamed.txt"));
    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    QTRY_VERIFY(QFile::exists(path(QStringLiteral("a/renamed.txt"))));
    QTRY_COMPARE(treemapState(view).value(QStringLiteral("operationScans")).toInt(), 1);
    QVERIFY(waitForReady(view, 2));
    // Zoom and selection are kept, under the new name.
    QCOMPARE(selectedOf(view), url(QStringLiteral("a/renamed.txt")));
    QCOMPARE(treemapState(view).value(QStringLiteral("zoomPath")).toStringList(), QStringList({QStringLiteral("a")}));
    QVERIFY(!cellRect(view, url(QStringLiteral("a/renamed.txt"))).isEmpty());
    QVERIFY(cellRect(view, url(QStringLiteral("a/rename-me.txt"))).isEmpty());
}

void WhaleDeepOperationsTest::testShowInFolderFromMenu()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QWidget *view = showTreemap(container);
    QVERIFY(view);
    const QUrl deep = url(QStringLiteral("a/deep.txt"));
    QMenu *menu = openMenu(view, deep);
    QVERIFY(menu);
    QAction *show = menu->findChild<QAction *>(QStringLiteral("show_in_folder"));
    menu->hide();
    show->trigger();
    QTRY_COMPARE(container->url(), url(QStringLiteral("a")));
    QTRY_COMPARE(container->viewHost()->bridge()->selection().selected(), QList<QUrl>({deep}));
    // The file list shows it selected after switching views.
    container->viewHost()->showDolphinView();
    QCOMPARE(container->view()->selectedItems().count(), 1);
    QCOMPARE(container->view()->selectedItems().first().url(), deep);
}

void WhaleDeepOperationsTest::testSplitView()
{
    DolphinViewContainer *left = showMainWindow();
    QVERIFY(left);
    QWidget *view = showTreemap(left);
    QVERIFY(view);
    m_mainWindow->actionCollection()->action(QStringLiteral("split_view"))->trigger();
    DolphinTabPage *page = m_mainWindow->findChild<DolphinTabWidget *>()->currentTabPage();
    QTRY_VERIFY(page->splitViewEnabled());
    DolphinViewContainer *right = page->secondaryViewContainer();
    QTRY_COMPARE(right->view()->itemsCount(), 3);
    right->setUrl(url(QStringLiteral("b")));
    QTRY_COMPARE(right->view()->itemsCount(), 1);
    // The file list on the right has its own selection, and is the active half.
    right->viewHost()->bridge()->setSelection(WhaleSelection({url(QStringLiteral("b/target.txt"))}, url(QStringLiteral("b/target.txt"))));
    right->setActive(true);
    QTRY_VERIFY(right->isActive());

    // An operation of the left treemap: only the chosen entry, the right half untouched.
    QMenu *menu = openMenu(view, url(QStringLiteral("a/trash-me.txt")));
    QVERIFY(menu);
    QAction *trash = menu->findChild<QAction *>(QStringLiteral("trash"));
    menu->hide();
    trash->trigger();
    QTRY_VERIFY(!QFile::exists(path(QStringLiteral("a/trash-me.txt"))));
    QCOMPARE(right->viewHost()->bridge()->selection().selected(), QList<QUrl>({url(QStringLiteral("b/target.txt"))}));
    QCOMPARE(right->url(), url(QStringLiteral("b")));
    QVERIFY(QFile::exists(path(QStringLiteral("b/target.txt"))));

    // "Show in Folder" goes to the left half (the treemap's), which becomes active.
    menu = openMenu(view, url(QStringLiteral("a/deep.txt")));
    QVERIFY(menu);
    QAction *show = menu->findChild<QAction *>(QStringLiteral("show_in_folder"));
    menu->hide();
    show->trigger();
    QTRY_COMPARE(left->url(), url(QStringLiteral("a")));
    QTRY_COMPARE(left->viewHost()->bridge()->selection().selected(), QList<QUrl>({url(QStringLiteral("a/deep.txt"))}));
    QTRY_VERIFY(left->isActive());
    QVERIFY(!right->isActive());
    QCOMPARE(right->url(), url(QStringLiteral("b")));
    QCOMPARE(right->viewHost()->bridge()->selection().selected(), QList<QUrl>({url(QStringLiteral("b/target.txt"))}));
    // The window's focus (offscreen, the window is not activated again after the menu).
    QTRY_VERIFY(left->viewHost()->currentView()->widget()->isAncestorOf(m_mainWindow->focusWidget()));
}

void WhaleDeepOperationsTest::testNavigation()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QWidget *view = showTreemap(container);
    QVERIFY(view);
    // T2: Enter opens the selected entry as Whale's Open does: a deep file with its
    // application (the location stays), a deep folder as the location.
    QVERIFY(select(view, url(QStringLiteral("a/deep.txt"))));
    QTest::keyClick(canvasOf(view), Qt::Key_Return);
    QTRY_COMPARE_WITH_TIMEOUT(openedFiles(), QStringList({path(QStringLiteral("a/deep.txt"))}), 10000);
    QCOMPARE(container->url(), m_dir->url());
    QVERIFY(select(view, url(QStringLiteral("a/sub"))));
    QTest::keyClick(canvasOf(view), Qt::Key_Return);
    QTRY_COMPARE(container->url(), url(QStringLiteral("a/sub")));
    // A double click still opens the folder under the pointer (M9).
    container->setUrl(m_dir->url());
    view = container->viewHost()->currentView()->widget();
    QVERIFY(waitForReady(view, 1));
    const QRect deepCell = cellRect(view, url(QStringLiteral("a/deep.txt")));
    QTest::mouseDClick(canvasOf(view), Qt::LeftButton, {}, deepCell.center());
    QTRY_COMPARE(container->url(), url(QStringLiteral("a")));
    QCOMPARE(openedFiles().count(), 1);
    // The wheel still zooms within the treemap.
    container->setUrl(m_dir->url());
    view = container->viewHost()->currentView()->widget();
    QVERIFY(waitForReady(view, 1));
    QTRY_VERIFY(!cellRect(view, url(QStringLiteral("a/sub/inner.txt"))).isEmpty());
    const QRect inner = cellRect(view, url(QStringLiteral("a/sub/inner.txt")));
    QWheelEvent wheel(QPointF(inner.center()),
                      QPointF(canvasOf(view)->mapToGlobal(inner.center())),
                      QPoint(),
                      QPoint(0, 120),
                      Qt::NoButton,
                      Qt::NoModifier,
                      Qt::NoScrollPhase,
                      false);
    QApplication::sendEvent(canvasOf(view), &wheel);
    QCOMPARE(treemapState(view).value(QStringLiteral("zoomPath")).toStringList(), QStringList({QStringLiteral("a")}));
    QCOMPARE(container->url(), m_dir->url());
}

QTEST_MAIN(WhaleDeepOperationsTest)

#include "whaledeepoperationstest.moc"
