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
#include "dolphinviewcontainer.h"
#include "testdir.h"
#include "testhelpers.h"
#include "views/dolphinview.h"
#include "whalefileviewtoolbar.h"
#include "whalefileviewtoolbarclient.h"
#include "whaletoolbarconfigurator.h"
#include "whaletoolbarlayout.h"
#include "whaletoolbarmanager.h"
#include "whaletoolbarsettings.h"
#include "whaleviewcommandscope.h"

#include <KActionCollection>
#include <KConfig>
#include <KConfigGroup>
#include <KEditToolBar>
#include <KIconLoader>
#include <KLocalizedString>
#include <KSharedConfig>
#include <KToggleAction>
#include <KToolBar>
#include <KXMLGUIFactory>

#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QDomDocument>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QStandardPaths>
#include <QTest>
#include <QTimer>

namespace
{
const QStringList fileViewDefault = {QStringLiteral("go_back"),
                                     QStringLiteral("go_forward"),
                                     QStringLiteral("go_up"),
                                     QStringLiteral("view_redisplay"),
                                     QStringLiteral("whale_location_bar"),
                                     QStringLiteral("view_settings")};
// The main toolbar of dolphinui.rc for new users (multi-toolbar plan E6, location bar plan L8).
const QStringList mainDefault = {QStringLiteral("|"),
                                 QStringLiteral("split_view"),
                                 QStringLiteral("split_stash"),
                                 QStringLiteral("toggle_search"),
                                 QStringLiteral("hamburger_menu")};
// ... before TB7 (with the global location bars) ...
const QStringList mainBeforeTB7 = {QStringLiteral("url_navigators"),
                                   QStringLiteral("split_view"),
                                   QStringLiteral("split_stash"),
                                   QStringLiteral("toggle_search"),
                                   QStringLiteral("hamburger_menu")};
// ... and up to TB4: what the users who never changed it have in their copies.
const QStringList mainBeforeTB5 = QStringList{QStringLiteral("go_back"), QStringLiteral("go_forward"), QStringLiteral("view_settings")} + mainBeforeTB7;

// What a toolbar of the layout @p layout shows: Whale has no "url_navigators" any more, and
// KXmlGui passes over it (location bar plan, L5).
QStringList shown(QStringList layout)
{
    layout.removeAll(QStringLiteral("url_navigators"));
    return layout;
}

QString localFile(const QString &name)
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kxmlgui5/") + QStringLiteral(WHALE_COMPONENT)
        + QLatin1Char('/') + name;
}

QString localMainFile()
{
    return localFile(QStringLiteral("dolphinui.rc"));
}

QString localFileViewFile()
{
    return localFile(WhaleFileViewToolbarClient::rcFile());
}

QByteArray contentOf(const QString &file)
{
    QFile f(file);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QStringList savedLayout(const QString &file, const QString &toolBarName)
{
    QDomDocument document;
    document.setContent(contentOf(file));
    return WhaleToolbarLayout::fromDocument(document, toolBarName).toStringList();
}

/**
 * The installed dolphinui.rc with the main toolbar @p mainToolBar, as KEditToolBar saves a
 * user's copy: the whole document. @p version replaces the version of the file if given;
 * @p iconTexts are action properties.
 */
QByteArray userMainFile(const QStringList &mainToolBar, const QMap<QString, QString> &iconTexts, const QString &version = {})
{
    QDomDocument document;
    document.setContent(contentOf(QStringLiteral(":/kxmlgui5/whale/dolphinui.rc")));
    QDomElement gui = document.documentElement();
    if (!version.isEmpty()) {
        gui.setAttribute(QStringLiteral("version"), version);
    }
    QDomElement toolBar = gui.firstChildElement(QStringLiteral("ToolBar"));
    while (!toolBar.isNull() && toolBar.attribute(QStringLiteral("name")) != QLatin1String("mainToolBar")) {
        toolBar = toolBar.nextSiblingElement(QStringLiteral("ToolBar"));
    }
    Q_ASSERT(!toolBar.isNull());
    QDomElement action = toolBar.firstChildElement();
    while (!action.isNull()) {
        QDomElement next = action.nextSiblingElement();
        if (action.tagName() != QLatin1String("text")) {
            toolBar.removeChild(action);
        }
        action = next;
    }
    for (const QString &name : mainToolBar) {
        QDomElement element = document.createElement(QStringLiteral("Action"));
        element.setAttribute(QStringLiteral("name"), name);
        toolBar.appendChild(element);
    }
    QDomElement properties = gui.firstChildElement(QStringLiteral("ActionProperties"));
    for (auto it = iconTexts.cbegin(); it != iconTexts.cend(); ++it) {
        QDomElement element = document.createElement(QStringLiteral("Action"));
        element.setAttribute(QStringLiteral("name"), it.key());
        element.setAttribute(QStringLiteral("iconText"), it.value());
        properties.appendChild(element);
    }
    return document.toByteArray();
}

QStringList layoutOf(const QToolBar *toolBar)
{
    QStringList result;
    const auto actions = toolBar->actions();
    for (QAction *action : actions) {
        if (action->isSeparator()) {
            result.append(QStringLiteral("-"));
        } else if (action->objectName().isEmpty()) {
            result.append(QStringLiteral("|")); // the spacer widget
        } else {
            result.append(action->objectName());
        }
    }
    return result;
}
}

/**
 * Existing users, new users and the setting "Show File View Toolbars" (multi-toolbar plan,
 * TB5): the users' copies of dolphinui.rc are never changed by Whale, new users get the new
 * main toolbar, and one setting shows or hides all File View Toolbars.
 */
class WhaleToolbarMigrationTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testExistingUser_data();
    void testExistingUser();
    void testNewUser();
    void testOlderUserFile();
    void testDefaultsForExistingUser();
    void testFileViewToolBarLooks();
    void testViewMenu();
    void testToggleAll();
    void testNewContainersFollow();
    void testContextMenuHides();
    void testTwoWindows();
    void testPersistence();
    void testConfigureWhileHidden_data();
    void testConfigureWhileHidden();

private:
    DolphinViewContainer *showWindow(DolphinMainWindow *window);
    void showSplit();
    KToggleAction *toggle(DolphinMainWindow *window) const;
    static QList<WhaleFileViewToolBar *> toolBars(DolphinMainWindow *window);
    static WhaleFileViewToolBar *toolBar(DolphinViewContainer *container);
    static void writeUserMainFile(const QByteArray &content);
    QAction *global(const QString &name) const;

    QTemporaryDir m_tmpHome;
    QScopedPointer<TestDir> m_dir;
    QScopedPointer<DolphinMainWindow> m_mainWindow;
    DolphinViewContainer *m_first = nullptr;
    DolphinViewContainer *m_second = nullptr;
};

void WhaleToolbarMigrationTest::initTestCase()
{
    QVERIFY(m_tmpHome.isValid());
    QCoreApplication::setApplicationName(QStringLiteral(WHALE_COMPONENT));
    QCoreApplication::setOrganizationDomain(QStringLiteral(WHALE_ORG_DOMAIN));
    qputenv("HOME", m_tmpHome.path().toUtf8());
    qputenv("XDG_DATA_HOME", m_tmpHome.filePath(QStringLiteral("data")).toUtf8());
    qputenv("XDG_CONFIG_HOME", m_tmpHome.filePath(QStringLiteral("config")).toUtf8());
    QStandardPaths::setTestModeEnabled(true);
    TestHelpers::disableAnimations();
    KLocalizedString::setApplicationDomain({"whale"});
    QCoreApplication::addLibraryPath(QStringLiteral(WHALE_TEST_PLUGIN_DIR));
    GeneralSettings::self()->setRememberOpenedTabs(false);
    GeneralSettings::self()->save();
    QVERIFY(!QFile::exists(localMainFile()));
    QVERIFY(!QFile::exists(localFileViewFile()));
    QVERIFY(WhaleToolbarSettings::self()->fileViewToolBarsVisible());
}

void WhaleToolbarMigrationTest::init()
{
    m_dir.reset(new TestDir());
    m_dir->createFile(QStringLiteral("a.txt"), QByteArray(3000, 'a'));
    m_dir->createFile(QStringLiteral(".hidden"), QByteArray(100, 'h'));
    m_dir->createFile(QStringLiteral("sub/b.txt"), QByteArray(1000, 'b'));
}

void WhaleToolbarMigrationTest::cleanup()
{
    QList<QPointer<QWidget>> windows;
    const auto topLevelWidgets = QApplication::topLevelWidgets();
    for (QWidget *window : topLevelWidgets) {
        windows.append(window);
    }
    for (const QPointer<QWidget> &window : std::as_const(windows)) {
        if (window && qobject_cast<QDialog *>(window) && window->isVisible()) {
            window->close();
        }
    }
    m_mainWindow.reset();
    m_dir.reset();
    m_first = m_second = nullptr;
    QFile::remove(localFileViewFile());
    QFile::remove(localMainFile());
    QFile::remove(localMainFile() + QStringLiteral(".backup"));
    WhaleToolbarSettings::self()->setFileViewToolBarsVisible(true);
    // Closing a window stores the settings' version and the window's state; with them, the
    // next windows show the terminal panel, and Konsole is not part of these tests.
    GeneralSettings::self()->setVersion(0);
    GeneralSettings::self()->save();
    QFile::remove(QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + QStringLiteral("/whalestaterc"));
    KSharedConfig::openStateConfig()->reparseConfiguration();
}

DolphinViewContainer *WhaleToolbarMigrationTest::showWindow(DolphinMainWindow *window)
{
    window->openDirectories({m_dir->url()}, false);
    window->resize(1200, 700);
    window->show();
    if (!QTest::qWaitForWindowActive(window)) {
        return nullptr;
    }
    DolphinViewContainer *container = window->activeViewContainer();
    container->view()->setHiddenFilesShown(false);
    if (!QTest::qWaitFor(
            [container]() {
                return container->view()->itemsCount() == 2;
            },
            5000)) {
        return nullptr;
    }
    return container;
}

void WhaleToolbarMigrationTest::showSplit()
{
    if (!m_mainWindow) {
        m_mainWindow.reset(new DolphinMainWindow());
    }
    QVERIFY(showWindow(m_mainWindow.data()));
    global(QStringLiteral("split_view"))->trigger();
    DolphinTabPage *page = m_mainWindow->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"))->currentTabPage();
    m_first = page->primaryViewContainer();
    m_second = page->secondaryViewContainer();
    QVERIFY(m_first && m_second);
    QTRY_COMPARE(m_second->view()->itemsCount(), 2);
}

KToggleAction *WhaleToolbarMigrationTest::toggle(DolphinMainWindow *window) const
{
    return qobject_cast<KToggleAction *>(window->actionCollection()->action(WhaleToolbarManager::showFileViewToolBarsActionName()));
}

QList<WhaleFileViewToolBar *> WhaleToolbarMigrationTest::toolBars(DolphinMainWindow *window)
{
    // Of all views of all tabs.
    return window->findChildren<WhaleFileViewToolBar *>();
}

WhaleFileViewToolBar *WhaleToolbarMigrationTest::toolBar(DolphinViewContainer *container)
{
    return container->findChild<WhaleFileViewToolBar *>(QString(), Qt::FindDirectChildrenOnly);
}

void WhaleToolbarMigrationTest::writeUserMainFile(const QByteArray &content)
{
    QVERIFY(QDir().mkpath(QFileInfo(localMainFile()).absolutePath()));
    QFile local(localMainFile());
    QVERIFY(local.open(QIODevice::WriteOnly));
    local.write(content);
}

QAction *WhaleToolbarMigrationTest::global(const QString &name) const
{
    return m_mainWindow->actionCollection()->action(name);
}

void WhaleToolbarMigrationTest::testExistingUser_data()
{
    QTest::addColumn<QStringList>("mainToolBar");
    QTest::addColumn<bool>("hidden");

    // The main toolbar of up to TB4, never changed: Back, Forward and the view settings stay.
    QTest::newRow("unchanged") << mainBeforeTB5 << false;
    // Changed: other order, more and fewer buttons (also without the hamburger menu).
    QTest::newRow("changed") << QStringList({QStringLiteral("go_home"),
                                             QStringLiteral("go_back"),
                                             QStringLiteral("url_navigators"),
                                             QStringLiteral("view_settings"),
                                             QStringLiteral("show_preview")})
                             << false;
    // Hiding the File View Toolbars gives the window of before.
    QTest::newRow("hidden") << mainBeforeTB5 << true;
}

void WhaleToolbarMigrationTest::testExistingUser()
{
    QFETCH(QStringList, mainToolBar);
    QFETCH(bool, hidden);
    // A user who has run Whale before: Dolphin's own migration of older settings (it adds
    // the hamburger menu to a toolbar without it) is done.
    GeneralSettings::self()->setVersion(202);
    const QByteArray userFile = userMainFile(mainToolBar, {{QStringLiteral("split_stash"), QStringLiteral("Halves")}});
    writeUserMainFile(userFile);
    WhaleToolbarSettings::self()->setFileViewToolBarsVisible(!hidden);

    m_mainWindow.reset(new DolphinMainWindow());
    QVERIFY(showWindow(m_mainWindow.data()));
    // The main toolbar is the user's, with the user's text ...
    QCOMPARE(layoutOf(m_mainWindow->toolBar(QStringLiteral("mainToolBar"))), shown(mainToolBar));
    QCOMPARE(global(QStringLiteral("split_stash"))->iconText(), QStringLiteral("Halves"));
    // ... and the File View Toolbar has its default, Back twice included: nothing is cleaned up.
    WhaleFileViewToolBar *bar = toolBar(m_mainWindow->activeViewContainer());
    QCOMPARE(bar->isHidden(), hidden);
    if (!hidden) {
        QTRY_VERIFY(bar->scope());
        QCOMPARE(layoutOf(bar), fileViewDefault);
    } else {
        QVERIFY(!bar->scope()); // never shown: no actions created
    }
    QVERIFY(!QFile::exists(localFileViewFile()));

    // Working with the window, closing it and starting again: the user's file is the same.
    global(QStringLiteral("split_view"))->trigger();
    QTRY_VERIFY(m_mainWindow->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"))->currentTabPage()->secondaryViewContainer());
    m_mainWindow->close();
    m_mainWindow.reset();
    QCOMPARE(contentOf(localMainFile()), userFile);
    m_mainWindow.reset(new DolphinMainWindow());
    QCOMPARE(layoutOf(m_mainWindow->toolBar(QStringLiteral("mainToolBar"))), shown(mainToolBar));
    m_mainWindow.reset();
    QCOMPARE(contentOf(localMainFile()), userFile);
    QVERIFY(!QFile::exists(localMainFile() + QStringLiteral(".backup")));
}

void WhaleToolbarMigrationTest::testNewUser()
{
    m_mainWindow.reset(new DolphinMainWindow());
    QVERIFY(showWindow(m_mainWindow.data()));
    QCOMPARE(layoutOf(m_mainWindow->toolBar(QStringLiteral("mainToolBar"))), mainDefault);
    WhaleFileViewToolBar *bar = toolBar(m_mainWindow->activeViewContainer());
    QTRY_VERIFY(bar->scope());
    QVERIFY(bar->isVisible());
    QCOMPARE(layoutOf(bar), fileViewDefault);
    // Back, Forward and the view settings are in the File View Toolbar only.
    for (const QString &name : {QStringLiteral("go_back"), QStringLiteral("go_forward"), QStringLiteral("view_settings")}) {
        QVERIFY2(!m_mainWindow->toolBar(QStringLiteral("mainToolBar"))->actions().contains(global(name)), qPrintable(name));
    }
    m_mainWindow->close();
    m_mainWindow.reset();
    // Starting writes no copies of the files.
    QVERIFY(!QFile::exists(localMainFile()));
    QVERIFY(!QFile::exists(localFileViewFile()));
}

void WhaleToolbarMigrationTest::testOlderUserFile()
{
    // A copy of an older dolphinui.rc (a lower version, an older View menu): KDE's version
    // handler takes the installed file and puts the user's toolbars and action properties
    // into it. That is KXmlGui, as for every version step of the file; Whale does not.
    const QStringList userToolBar = {QStringLiteral("go_back"), QStringLiteral("go_up"), QStringLiteral("url_navigators"), QStringLiteral("hamburger_menu")};
    QByteArray userFile = userMainFile(userToolBar, {{QStringLiteral("go_up"), QStringLiteral("Upwards")}}, QStringLiteral("40"));
    userFile.replace("<Action name=\"whale_include_subfolders\"/>", "");
    QVERIFY(!userFile.contains("whale_include_subfolders"));
    writeUserMainFile(userFile);

    m_mainWindow.reset(new DolphinMainWindow());
    QVERIFY(showWindow(m_mainWindow.data()));
    QCOMPARE(layoutOf(m_mainWindow->toolBar(QStringLiteral("mainToolBar"))), shown(userToolBar));
    QCOMPARE(global(QStringLiteral("go_up"))->iconText(), QStringLiteral("Upwards"));
    // The menus are the installed ones.
    auto viewMenu = qobject_cast<QMenu *>(m_mainWindow->guiFactory()->container(QStringLiteral("view"), m_mainWindow.data()));
    QVERIFY(viewMenu);
    QVERIFY(viewMenu->actions().contains(global(QStringLiteral("whale_include_subfolders"))));
    QVERIFY(viewMenu->actions().contains(toggle(m_mainWindow.data())));
    WhaleFileViewToolBar *bar = toolBar(m_mainWindow->activeViewContainer());
    QTRY_VERIFY(bar->scope());
    QCOMPARE(layoutOf(bar), fileViewDefault);
    // The text of the main toolbar's action is not the File View Toolbar's.
    QVERIFY(bar->scope()->action(QStringLiteral("go_up"))->iconText() != QStringLiteral("Upwards"));
    m_mainWindow.reset();

    // KXmlGui has saved the merged document: the user's toolbar and texts are still in it.
    QDomDocument saved;
    QVERIFY(saved.setContent(contentOf(localMainFile())));
    QCOMPARE(saved.documentElement().attribute(QStringLiteral("version")), QStringLiteral("52"));
    QCOMPARE(savedLayout(localMainFile(), QStringLiteral("mainToolBar")), userToolBar);
    QVERIFY(contentOf(localMainFile()).contains("Upwards"));
    m_mainWindow.reset(new DolphinMainWindow());
    QCOMPARE(layoutOf(m_mainWindow->toolBar(QStringLiteral("mainToolBar"))), shown(userToolBar));
    QCOMPARE(global(QStringLiteral("go_up"))->iconText(), QStringLiteral("Upwards"));
}

void WhaleToolbarMigrationTest::testDefaultsForExistingUser()
{
    // "Defaults" in Configure Toolbars, as in KDE: the new main toolbar for everybody.
    writeUserMainFile(userMainFile(mainBeforeTB5, {}));
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    QCOMPARE(layoutOf(m_mainWindow->toolBar(QStringLiteral("mainToolBar"))), shown(mainBeforeTB5));
    WhaleToolbarConfigurator::of(m_mainWindow.data())->configure(WhaleToolbarDescriptor::globalId());
    KEditToolBar *dialog = WhaleToolbarConfigurator::of(m_mainWindow.data())->dialog();
    QVERIFY(dialog);
    QTimer::singleShot(100, dialog, []() {
        auto box = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        auto buttons = box ? box->findChild<QDialogButtonBox *>() : nullptr;
        if (QAbstractButton *yes = buttons ? buttons->button(QDialogButtonBox::Yes) : nullptr) {
            yes->click();
        } else if (box) {
            box->reject();
        }
    });
    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::RestoreDefaults)->click();
    QVERIFY(!QFile::exists(localMainFile()));
    QCOMPARE(layoutOf(m_mainWindow->toolBar(QStringLiteral("mainToolBar"))), mainDefault);
    QCOMPARE(layoutOf(toolBar(m_first)), fileViewDefault);
    QCOMPARE(layoutOf(toolBar(m_second)), fileViewDefault);
    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->click();
}

void WhaleToolbarMigrationTest::testFileViewToolBarLooks()
{
    // Small icons without text since TB2, for every File View Toolbar, also after KDE's
    // settings of toolbars or icons changed.
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    const int small = KIconLoader::global()->currentSize(KIconLoader::Small);
    for (DolphinViewContainer *container : {m_first, m_second}) {
        QCOMPARE(toolBar(container)->toolButtonStyle(), Qt::ToolButtonIconOnly);
        QCOMPARE(toolBar(container)->iconSize(), QSize(small, small));
    }
    Q_EMIT KIconLoader::global()->iconLoaderSettingsChanged();
    for (DolphinViewContainer *container : {m_first, m_second}) {
        QCOMPARE(toolBar(container)->toolButtonStyle(), Qt::ToolButtonIconOnly);
        QCOMPARE(toolBar(container)->iconSize(), QSize(small, small));
    }
}

void WhaleToolbarMigrationTest::testViewMenu()
{
    m_mainWindow.reset(new DolphinMainWindow());
    QVERIFY(showWindow(m_mainWindow.data()));
    KToggleAction *action = toggle(m_mainWindow.data());
    QVERIFY(action);
    QVERIFY(action->isCheckable() && action->isChecked());
    QCOMPARE(KLocalizedString::removeAcceleratorMarker(action->text()), QStringLiteral("Show File View Toolbars"));
    // Not part of dolphinui.rc (the users' copies keep their menus) ...
    QVERIFY(!contentOf(QStringLiteral(":/kxmlgui5/whale/dolphinui.rc")).contains(WhaleToolbarManager::showFileViewToolBarsActionName().toUtf8()));

    // ... but in the View menu, once, after "Location Bar"; also after the GUI was built again.
    const auto checkViewMenu = [this]() {
        QAction *action = toggle(m_mainWindow.data());
        KXMLGUIFactory *factory = m_mainWindow->guiFactory();
        auto viewMenu = qobject_cast<QMenu *>(factory->container(QStringLiteral("view"), m_mainWindow.data()));
        auto locationBar = qobject_cast<QMenu *>(factory->container(QStringLiteral("location_bar"), m_mainWindow.data()));
        QVERIFY(viewMenu && locationBar);
        const QList<QAction *> actions = viewMenu->actions();
        QCOMPARE(actions.count(action), 1);
        QCOMPARE(actions.indexOf(action), actions.indexOf(locationBar->menuAction()) + 1);
    };
    checkViewMenu();
    if (QTest::currentTestFailed()) {
        return;
    }
    WhaleToolbarConfigurator::of(m_mainWindow.data())->configure(WhaleToolbarDescriptor::globalId());
    KEditToolBar *dialog = WhaleToolbarConfigurator::of(m_mainWindow.data())->dialog();
    QVERIFY(dialog);
    // Apply with a change of the main toolbar: the window builds its GUI again.
    const auto lists = dialog->findChildren<QListWidget *>();
    QListWidget *current = lists.value(1);
    QVERIFY(current);
    const auto search = current->findItems(KLocalizedString::removeAcceleratorMarker(global(QStringLiteral("toggle_search"))->iconText()), Qt::MatchExactly);
    QCOMPARE(search.count(), 1);
    current->setCurrentItem(search.first());
    Q_EMIT current->itemDoubleClicked(search.first());
    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click();
    QVERIFY(QFile::exists(localMainFile()));
    checkViewMenu();
    if (QTest::currentTestFailed()) {
        return;
    }
    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    // The user's copy written by KEditToolBar does not contain it either.
    QVERIFY(!contentOf(localMainFile()).contains(WhaleToolbarManager::showFileViewToolBarsActionName().toUtf8()));
    m_mainWindow.reset(new DolphinMainWindow());
    checkViewMenu();
}

void WhaleToolbarMigrationTest::testToggleAll()
{
    // A split view in one tab, a second tab: all toolbars at once.
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    global(QStringLiteral("new_tab"))->trigger();
    auto tabs = m_mainWindow->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"));
    QTRY_COMPARE(tabs->count(), 2);
    QTRY_COMPARE(toolBars(m_mainWindow.data()).count(), 3);
    KToggleAction *action = toggle(m_mainWindow.data());

    action->trigger();
    QVERIFY(!action->isChecked());
    QVERIFY(!WhaleToolbarSettings::self()->fileViewToolBarsVisible());
    for (WhaleFileViewToolBar *bar : toolBars(m_mainWindow.data())) {
        QVERIFY(bar->isHidden());
    }
    // The main toolbar stays.
    QVERIFY(m_mainWindow->toolBar(QStringLiteral("mainToolBar"))->isVisible());

    action->trigger();
    QVERIFY(action->isChecked());
    QVERIFY(WhaleToolbarSettings::self()->fileViewToolBarsVisible());
    for (WhaleFileViewToolBar *bar : toolBars(m_mainWindow.data())) {
        QVERIFY(!bar->isHidden());
    }
    // The shown tab (the new one) shows its toolbar; the split view's when it is shown again.
    QVERIFY(toolBar(tabs->currentTabPage()->activeViewContainer())->isVisible());
    tabs->setCurrentIndex(0);
    QTRY_VERIFY(toolBar(m_first)->isVisible());
    QVERIFY(toolBar(m_second)->isVisible());
    QCOMPARE(layoutOf(toolBar(m_first)), fileViewDefault);
    QCOMPARE(layoutOf(toolBar(m_second)), fileViewDefault);
}

void WhaleToolbarMigrationTest::testNewContainersFollow()
{
    m_mainWindow.reset(new DolphinMainWindow());
    QVERIFY(showWindow(m_mainWindow.data()));
    toggle(m_mainWindow.data())->trigger();
    QVERIFY(!WhaleToolbarSettings::self()->fileViewToolBarsVisible());

    // Views created while hidden start hidden, without actions.
    global(QStringLiteral("split_view"))->trigger();
    DolphinTabPage *page = m_mainWindow->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"))->currentTabPage();
    QTRY_VERIFY(page->secondaryViewContainer());
    WhaleFileViewToolBar *splitBar = toolBar(page->secondaryViewContainer());
    QVERIFY(splitBar->isHidden());
    QVERIFY(!splitBar->scope());
    global(QStringLiteral("new_tab"))->trigger();
    QTRY_COMPARE(toolBars(m_mainWindow.data()).count(), 3);
    WhaleFileViewToolBar *tabBar = toolBar(m_mainWindow->activeViewContainer());
    QVERIFY(tabBar != splitBar);
    QVERIFY(tabBar->isHidden());

    // Shown: they create their actions now, with the current layout.
    toggle(m_mainWindow.data())->trigger();
    QTRY_VERIFY(tabBar->isVisible() && tabBar->scope());
    QCOMPARE(layoutOf(tabBar), fileViewDefault);

    // And views created while shown start shown.
    global(QStringLiteral("new_tab"))->trigger();
    QTRY_COMPARE(toolBars(m_mainWindow.data()).count(), 4);
    QTRY_VERIFY(toolBar(m_mainWindow->activeViewContainer())->isVisible());
}

void WhaleToolbarMigrationTest::testContextMenuHides()
{
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    QTRY_VERIFY(toolBar(m_second)->scope());
    WhaleFileViewToolBar *bar = toolBar(m_second);
    const QPoint position(bar->width() / 2, bar->height() / 2);
    QContextMenuEvent event(QContextMenuEvent::Mouse, position, bar->mapToGlobal(position));
    QApplication::sendEvent(bar, &event);
    QPointer<QMenu> menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
    QVERIFY(menu);
    QCOMPARE(menu->actions().count(), 2);
    QAction *hide = menu->actions().at(1);
    QCOMPARE(hide->objectName(), QStringLiteral("whale_hide_file_view_toolbars"));
    QCOMPARE(KLocalizedString::removeAcceleratorMarker(hide->text()), QStringLiteral("Hide File View Toolbars"));
    hide->trigger();
    menu->close();
    QTRY_VERIFY(!menu);

    // All of them, and the View menu's entry follows: it is the way back.
    QVERIFY(toolBar(m_first)->isHidden());
    QVERIFY(toolBar(m_second)->isHidden());
    QVERIFY(!toggle(m_mainWindow.data())->isChecked());
    toggle(m_mainWindow.data())->trigger();
    QVERIFY(toolBar(m_first)->isVisible());
    QVERIFY(toolBar(m_second)->isVisible());
}

void WhaleToolbarMigrationTest::testTwoWindows()
{
    // Two windows of one process: one setting.
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    QScopedPointer<DolphinMainWindow> other(new DolphinMainWindow());
    QVERIFY(showWindow(other.data()));
    QCOMPARE(toolBars(other.data()).count(), 1);

    toggle(other.data())->trigger();
    for (DolphinMainWindow *window : {m_mainWindow.data(), other.data()}) {
        QVERIFY(!toggle(window)->isChecked());
        for (WhaleFileViewToolBar *bar : toolBars(window)) {
            QVERIFY(bar->isHidden());
        }
    }
    toggle(m_mainWindow.data())->trigger();
    for (DolphinMainWindow *window : {m_mainWindow.data(), other.data()}) {
        QVERIFY(toggle(window)->isChecked());
        for (WhaleFileViewToolBar *bar : toolBars(window)) {
            QVERIFY(bar->isVisible());
        }
    }
    // A window opened while hidden starts hidden.
    toggle(other.data())->trigger();
    other.reset(new DolphinMainWindow());
    QVERIFY(!toggle(other.data())->isChecked());
    QVERIFY(showWindow(other.data()));
    QVERIFY(toolBar(other->activeViewContainer())->isHidden());
}

void WhaleToolbarMigrationTest::testPersistence()
{
    const QString configFile = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/whalerc");
    m_mainWindow.reset(new DolphinMainWindow());
    QVERIFY(showWindow(m_mainWindow.data()));
    toggle(m_mainWindow.data())->trigger();

    // Written at once, in the group "WhaleToolbars" of whalerc.
    {
        KConfig written(configFile, KConfig::SimpleConfig);
        QVERIFY(written.hasGroup(WhaleToolbarSettings::groupName()));
        QCOMPARE(written.group(WhaleToolbarSettings::groupName()).readEntry("ShowFileViewToolBars", true), false);
    }
    // Not in the XML GUI files and not in Dolphin's settings.
    QVERIFY(!QFile::exists(localMainFile()));
    QVERIFY(!QFile::exists(localFileViewFile()));

    // As after a restart.
    m_mainWindow.reset();
    m_mainWindow.reset(new DolphinMainWindow());
    QVERIFY(showWindow(m_mainWindow.data()));
    QVERIFY(!toggle(m_mainWindow.data())->isChecked());
    QVERIFY(toolBar(m_mainWindow->activeViewContainer())->isHidden());
    m_mainWindow.reset();

    // Another process has shown them (its file, read at the start of this one).
    {
        KConfig other(configFile, KConfig::SimpleConfig);
        other.group(WhaleToolbarSettings::groupName()).writeEntry("ShowFileViewToolBars", true);
    }
    KSharedConfig::openConfig()->reparseConfiguration();
    m_mainWindow.reset(new DolphinMainWindow());
    QVERIFY(showWindow(m_mainWindow.data()));
    QVERIFY(toggle(m_mainWindow.data())->isChecked());
    QTRY_VERIFY(toolBar(m_mainWindow->activeViewContainer())->isVisible());
}

void WhaleToolbarMigrationTest::testConfigureWhileHidden_data()
{
    QTest::addColumn<bool>("shownBefore");
    // Hidden from the start: the toolbars have never created their actions.
    QTest::newRow("never shown") << false;
    // Shown, then hidden: they have.
    QTest::newRow("shown before") << true;
}

void WhaleToolbarMigrationTest::testConfigureWhileHidden()
{
    QFETCH(bool, shownBefore);
    WhaleToolbarSettings::self()->setFileViewToolBarsVisible(shownBefore);
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    if (shownBefore) {
        QTRY_VERIFY(toolBar(m_first)->scope() && toolBar(m_second)->scope());
        toggle(m_mainWindow.data())->trigger();
    }
    QVERIFY(toolBar(m_first)->isHidden() && toolBar(m_second)->isHidden());
    QCOMPARE(toolBar(m_first)->scope() != nullptr, shownBefore);

    // The File View Toolbar is in Configure Toolbars all the same.
    WhaleToolbarConfigurator::of(m_mainWindow.data())->configure(WhaleToolbarDescriptor::fileViewId());
    KEditToolBar *dialog = WhaleToolbarConfigurator::of(m_mainWindow.data())->dialog();
    QVERIFY(dialog);
    auto combo = dialog->findChild<QComboBox *>();
    QCOMPARE(combo->currentText(), QStringLiteral("File View Toolbar"));
    const auto lists = dialog->findChildren<QListWidget *>();
    QListWidget *available = lists.value(0);
    QListWidget *current = lists.value(1);
    QCOMPARE(current->count(), fileViewDefault.count());
    const QString home = KLocalizedString::removeAcceleratorMarker(global(QStringLiteral("go_home"))->text());
    const auto items = available->findItems(home, Qt::MatchExactly);
    QCOMPARE(items.count(), 1);
    available->setCurrentItem(items.first());
    Q_EMIT available->itemDoubleClicked(items.first());
    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    const QStringList expected = fileViewDefault + QStringList({QStringLiteral("go_home")});
    QCOMPARE(savedLayout(localFileViewFile(), WhaleFileViewToolbarClient::toolBarName()), expected);

    // Still hidden: configuring does not show them ...
    QVERIFY(toolBar(m_first)->isHidden() && toolBar(m_second)->isHidden());
    QVERIFY(!toggle(m_mainWindow.data())->isChecked());
    // ... and shown again, they have the new layout.
    toggle(m_mainWindow.data())->trigger();
    QTRY_VERIFY(toolBar(m_first)->scope() && toolBar(m_second)->scope());
    QCOMPARE(layoutOf(toolBar(m_first)), expected);
    QCOMPARE(layoutOf(toolBar(m_second)), expected);
    QCOMPARE(layoutOf(m_mainWindow->toolBar(QStringLiteral("mainToolBar"))), mainDefault);
}

QTEST_MAIN(WhaleToolbarMigrationTest)

#include "whaletoolbarmigrationtest.moc"
