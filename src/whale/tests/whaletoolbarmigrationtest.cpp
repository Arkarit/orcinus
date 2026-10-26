/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaletoolbarmigration.h"
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
#include "whalelocationbar.h"
#include "whaletoolbarconfigurator.h"
#include "whaletoolbarlayout.h"
#include "whaletoolbarmanager.h"
#include "whaletoolbarsettings.h"
#include "whaleviewcommandscope.h"

#include <KActionCollection>
#include <KConfig>
#include <KConfigGroup>
#include <KEditToolBar>
#include <KFileItem>
#include <KHamburgerMenu>
#include <KIconLoader>
#include <KLocalizedString>
#include <KSharedConfig>
#include <KStandardAction>
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

#include <functional>

#include <unistd.h>

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

// The File View Toolbar file of a user of TB4 to TB6: KDE's formatting, a text of the user's.
const QByteArray fileViewBeforeTB7 =
    "<?xml version=\"1.0\"?>\n"
    "<!DOCTYPE gui SYSTEM \"kpartgui.dtd\">\n"
    "<gui name=\"whale_fileview\" version=\"1\">\n"
    "    <ToolBar noMerge=\"1\" name=\"whaleFileViewToolBar\">\n"
    "        <text context=\"@title:menu\">File View Toolbar</text>\n"
    "        <Action name=\"go_back\" />\n"
    "        <Action name=\"go_forward\" />\n"
    "        <Action name=\"go_up\" />\n"
    "        <Action name=\"view_redisplay\" />\n"
    "        <Spacer />\n"
    "        <Action name=\"view_settings\" />\n"
    "        <Action name=\"go_home\" />\n"
    "    </ToolBar>\n"
    "    <ActionProperties scheme=\"Default\">\n"
    "        <Action name=\"go_up\" iconText=\"Upwards &amp; on\"/>\n"
    "    </ActionProperties>\n"
    "</gui>\n";

// ... after the migration: the location bar after Reload, nothing else changed.
const QByteArray fileViewMigrated =
    QByteArray(fileViewBeforeTB7)
        .replace("        <Action name=\"view_redisplay\" />\n", "        <Action name=\"view_redisplay\" />\n        <Action name=\"whale_location_bar\"/>\n");

QByteArray fileViewFile(const QByteArray &toolBarContent)
{
    return "<!DOCTYPE gui SYSTEM \"kpartgui.dtd\">\n<gui name=\"whale_fileview\" version=\"1\">"
           "<ToolBar noMerge=\"1\" name=\"whaleFileViewToolBar\">"
        + toolBarContent + "</ToolBar></gui>";
}

KConfigGroup migrationGroup()
{
    return WhaleToolbarSettings::self()->config()->group(WhaleToolbarSettings::groupName());
}

// The state of the TB7 migration: done or not (a user of an older Whale).
void setMigrated(bool migrated)
{
    KConfigGroup group = migrationGroup();
    if (migrated) {
        group.writeEntry(WhaleToolbarMigration::layoutMarker(), true);
        group.writeEntry(WhaleToolbarMigration::visibilityMarker(), true);
    } else {
        group.deleteEntry(WhaleToolbarMigration::layoutMarker());
        group.deleteEntry(WhaleToolbarMigration::visibilityMarker());
    }
    group.sync();
}

void writeFile(const QString &file, const QByteArray &content)
{
    QVERIFY(QDir().mkpath(QFileInfo(file).absolutePath()));
    QFile local(file);
    QVERIFY(local.open(QIODevice::WriteOnly));
    QCOMPARE(local.write(content), content.size());
}

QStringList layoutOf(const QToolBar *toolBar)
{
    QStringList result;
    const auto actions = toolBar->actions();
    for (QAction *action : actions) {
        if (WhaleFileViewToolBar::isLeadingPlaceholder(action)) {
            continue; // not part of the layout, as the lock of a split view on it
        }
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
    void testWaysBack();
    void testTwoWindows();
    void testPersistence();
    void testConfigureWhileHidden_data();
    void testConfigureWhileHidden();

    // TB7 (location bar plan, LB3)
    void testInsertLocationBar_data();
    void testInsertLocationBar();
    void testLayoutMigration();
    void testLayoutMigrationOnce();
    void testLayoutMigrationFailure();
    void testUnreadableFile();
    void testVisibilityMigration();
    void testNewUserTB7();
    void testMainFileNotMigrated();
    void testDowngradeFiles();

private:
    DolphinViewContainer *showWindow(DolphinMainWindow *window);
    void showSplit();
    KToggleAction *toggle(DolphinMainWindow *window) const;
    static QList<WhaleFileViewToolBar *> toolBars(DolphinMainWindow *window);
    static WhaleFileViewToolBar *toolBar(DolphinViewContainer *container);
    static void writeUserMainFile(const QByteArray &content);
    QAction *global(const QString &name) const;
    static QList<QAction *> actionsOfMenu(const std::function<void()> &open);

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
    TestHelpers::openNewWindowsInSingleView();
    KLocalizedString::setApplicationDomain(TRANSLATION_DOMAIN);
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
    // The tests of the TB7 migration start from a user of an older Whale; all others from one
    // who has run TB7.
    setMigrated(true);
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
    const QString backup = localFileViewFile() + WhaleToolbarMigration::backupSuffix();
    QFile::setPermissions(QFileInfo(localFileViewFile()).absolutePath(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QDir(backup).removeRecursively();
    QFile::remove(backup);
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
    QCOMPARE(saved.documentElement().attribute(QStringLiteral("version")), QStringLiteral("54"));
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

void WhaleToolbarMigrationTest::testWaysBack()
{
    // Without the menu bar (the default), the View menu cannot show hidden File View Toolbars
    // again: the hamburger menu, the main toolbar's context menu and, while they are hidden,
    // the menu of a view's background can.
    m_mainWindow.reset(new DolphinMainWindow());
    DolphinViewContainer *container = showWindow(m_mainWindow.data());
    QVERIFY(container);
    KToggleAction *action = toggle(m_mainWindow.data());
    QVERIFY(action && action->isChecked());
    const auto hamburgerMenu = [this]() {
        KActionCollection *ac = m_mainWindow->actionCollection();
        auto hamburger = qobject_cast<KHamburgerMenu *>(ac->action(KStandardAction::name(KStandardAction::HamburgerMenu)));
        return QMetaObject::invokeMethod(m_mainWindow.data(), "updateHamburgerMenu") && hamburger->menu() ? hamburger->menu()->actions() : QList<QAction *>();
    };
    const auto toolBarMenu = [this]() {
        // The main toolbar is built again with the GUI: always the current one.
        KToolBar *mainToolBar = m_mainWindow->toolBar();
        return actionsOfMenu([mainToolBar]() {
            const QPoint position(mainToolBar->width() / 3, mainToolBar->height() / 2); // the spacer
            QContextMenuEvent event(QContextMenuEvent::Mouse, position, mainToolBar->mapToGlobal(position));
            QApplication::sendEvent(mainToolBar, &event);
        });
    };
    const auto backgroundMenu = [this, container]() {
        return actionsOfMenu([this, container]() {
            QMetaObject::invokeMethod(m_mainWindow.data(),
                                      "openContextMenu",
                                      Q_ARG(QPoint, container->mapToGlobal(QPoint(50, 200))),
                                      Q_ARG(KFileItem, KFileItem()),
                                      Q_ARG(KFileItemList, KFileItemList()),
                                      Q_ARG(QUrl, container->url()));
        });
    };
    const QAction *configure = m_mainWindow->actionCollection()->action(KStandardAction::name(KStandardAction::ConfigureToolbars));

    QVERIFY(hamburgerMenu().contains(action));
    QList<QAction *> actions = toolBarMenu();
    QCOMPARE(actions.count(action), 1);
    QVERIFY(actions.indexOf(action) < actions.indexOf(configure));
    QVERIFY(!backgroundMenu().contains(action));

    action->trigger();
    QVERIFY(!WhaleToolbarSettings::self()->fileViewToolBarsVisible());
    QVERIFY(hamburgerMenu().contains(action));
    // The toolbar keeps its menu: still once.
    actions = toolBarMenu();
    QCOMPARE(actions.count(action), 1);
    QVERIFY(actions.indexOf(action) < actions.indexOf(configure));
    actions = backgroundMenu();
    QCOMPARE(actions.count(action), 1);

    // Also after the window built its GUI again (as after Configure Toolbars).
    QVERIFY(QMetaObject::invokeMethod(m_mainWindow.data(), "saveNewToolbarConfig"));
    QCOMPARE(toolBarMenu().count(action), 1);

    action->trigger();
    QVERIFY(WhaleToolbarSettings::self()->fileViewToolBarsVisible());
    QVERIFY(toolBar(container)->isVisible());
    QVERIFY(!backgroundMenu().contains(action));
}

QList<QAction *> WhaleToolbarMigrationTest::actionsOfMenu(const std::function<void()> &open)
{
    // The actions of the menu that @p open shows with exec(); the menu is closed then.
    QList<QAction *> actions;
    QTimer timer;
    timer.setInterval(10);
    connect(&timer, &QTimer::timeout, [&actions, &timer]() {
        if (auto menu = qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
            actions = menu->actions();
            timer.stop();
            menu->close();
        }
    });
    timer.start();
    open();
    return actions;
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

void WhaleToolbarMigrationTest::testInsertLocationBar_data()
{
    QTest::addColumn<QByteArray>("before");
    QTest::addColumn<QByteArray>("after");

    QTest::newRow("KDE's formatting, after Reload") << fileViewBeforeTB7 << fileViewMigrated;
    // One line, an order of the user's: after the last of Back, Forward, Up and Reload.
    QTest::newRow("hand-edited order")
        << fileViewFile(
               "<Action name=\"view_settings\"/><Action name=\"go_up\"/><Separator/><Action name=\"renamefile\"/><Action name=\"go_back\"/>"
               "<Action name=\"go_home\"/>")
        << fileViewFile(
               "<Action name=\"view_settings\"/><Action name=\"go_up\"/><Separator/><Action name=\"renamefile\"/><Action name=\"go_back\"/>"
               "<Action name=\"whale_location_bar\"/><Action name=\"go_home\"/>");
    // None of them: in front of the first entry.
    QTest::newRow("no navigation")
        << fileViewFile("<text>File View Toolbar</text>\n  <Separator/>\n  <Action name=\"renamefile\"/>\n")
        << fileViewFile("<text>File View Toolbar</text>\n  <Action name=\"whale_location_bar\"/>\n  <Separator/>\n  <Action name=\"renamefile\"/>\n");
    QTest::newRow("nothing but the text") << fileViewFile("<text>File View Toolbar</text>")
                                          << fileViewFile("<text>File View Toolbar</text><Action name=\"whale_location_bar\"/>");
    // Comments, entities and other characters stay as they are.
    QTest::newRow("comments and umlauts") << fileViewFile(
        "<!-- mine: Größe -->\n<text>Datei&amp;Ansicht</text>\n<Action name=\"go_up\"/><!-- end -->\n<Action name=\"view_settings\"/>")
                                          << fileViewFile(
                                                 "<!-- mine: Größe -->\n<text>Datei&amp;Ansicht</text>\n<Action name=\"go_up\"/>\n<Action "
                                                 "name=\"whale_location_bar\"/><!-- end -->\n<Action name=\"view_settings\"/>");
    // Nothing to do.
    QTest::newRow("has the location bar") << fileViewFile("<Action name=\"go_up\"/><Action name=\"view_settings\"/><Action name=\"whale_location_bar\"/>")
                                          << fileViewFile("<Action name=\"go_up\"/><Action name=\"view_settings\"/><Action name=\"whale_location_bar\"/>");
    QTest::newRow("other toolbar only")
        << QByteArray("<gui name=\"whale_fileview\" version=\"1\"><ToolBar name=\"other\"><Action name=\"go_up\"/></ToolBar></gui>")
        << QByteArray("<gui name=\"whale_fileview\" version=\"1\"><ToolBar name=\"other\"><Action name=\"go_up\"/></ToolBar></gui>");
    QTest::newRow("empty toolbar element") << QByteArray("<gui name=\"whale_fileview\" version=\"1\"><ToolBar name=\"whaleFileViewToolBar\"/></gui>")
                                           << QByteArray("<gui name=\"whale_fileview\" version=\"1\"><ToolBar name=\"whaleFileViewToolBar\"/></gui>");
}

void WhaleToolbarMigrationTest::testInsertLocationBar()
{
    QFETCH(QByteArray, before);
    QFETCH(QByteArray, after);
    QString error;
    const QByteArray result = WhaleToolbarMigration::withLocationBar(before, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(result, after);
    // Once only, whatever happens.
    QCOMPARE(WhaleToolbarMigration::withLocationBar(result, &error), result);
    QVERIFY(result.count("whale_location_bar") <= 1);
}

void WhaleToolbarMigrationTest::testLayoutMigration()
{
    // A user of TB5/TB6 with a File View Toolbar of their own, starting TB7.
    writeFile(localFileViewFile(), fileViewBeforeTB7);
    setMigrated(false);
    m_mainWindow.reset(new DolphinMainWindow());
    QVERIFY(showWindow(m_mainWindow.data()));
    // The location bar is in the file, after Reload; everything else as it was, also the
    // user's text for Up ...
    QCOMPARE(contentOf(localFileViewFile()), fileViewMigrated);
    QCOMPARE(contentOf(localFileViewFile() + WhaleToolbarMigration::backupSuffix()), fileViewBeforeTB7);
    QVERIFY(migrationGroup().readEntry(WhaleToolbarMigration::layoutMarker(), false));
    // ... and in the toolbars.
    WhaleFileViewToolBar *bar = toolBar(m_mainWindow->activeViewContainer());
    QTRY_VERIFY(bar->scope());
    QCOMPARE(layoutOf(bar),
             QStringList({QStringLiteral("go_back"),
                          QStringLiteral("go_forward"),
                          QStringLiteral("go_up"),
                          QStringLiteral("view_redisplay"),
                          QStringLiteral("whale_location_bar"),
                          QStringLiteral("|"),
                          QStringLiteral("view_settings"),
                          QStringLiteral("go_home")}));
    QCOMPARE(bar->scope()->action(QStringLiteral("go_up"))->iconText(), QStringLiteral("Upwards & on"));
    QTRY_VERIFY(bar->locationBar()->isVisible());
    // The old spacer does not take the location bar's room.
    QVERIFY(bar->locationBar()->width() > bar->width() / 2);
}

void WhaleToolbarMigrationTest::testLayoutMigrationOnce()
{
    writeFile(localFileViewFile(), fileViewBeforeTB7);
    setMigrated(false);
    m_mainWindow.reset(new DolphinMainWindow());
    QCOMPARE(contentOf(localFileViewFile()), fileViewMigrated);
    const QString backup = localFileViewFile() + WhaleToolbarMigration::backupSuffix();
    const QDateTime backupTime = QFileInfo(backup).lastModified();

    // The user takes the location bar out again: the next start leaves it out.
    writeFile(localFileViewFile(), fileViewBeforeTB7);
    m_mainWindow.reset(new DolphinMainWindow());
    QCOMPARE(contentOf(localFileViewFile()), fileViewBeforeTB7);

    // Without the marker (e.g. it could not be saved): the location bar is not added twice,
    // and the backup is the first one.
    writeFile(localFileViewFile(), fileViewMigrated);
    setMigrated(false);
    m_mainWindow.reset(new DolphinMainWindow());
    QCOMPARE(contentOf(localFileViewFile()), fileViewMigrated);
    QCOMPARE(contentOf(backup), fileViewBeforeTB7);
    QCOMPARE(QFileInfo(backup).lastModified(), backupTime);
    QVERIFY(migrationGroup().readEntry(WhaleToolbarMigration::layoutMarker(), false));

    // A backup of an earlier attempt is never overwritten.
    writeFile(backup, "earlier");
    writeFile(localFileViewFile(), fileViewBeforeTB7);
    setMigrated(false);
    m_mainWindow.reset(new DolphinMainWindow());
    QCOMPARE(contentOf(localFileViewFile()), fileViewMigrated);
    QCOMPARE(contentOf(backup), QByteArray("earlier"));
}

void WhaleToolbarMigrationTest::testLayoutMigrationFailure()
{
    const QString backup = localFileViewFile() + WhaleToolbarMigration::backupSuffix();
    // No backup possible (something else has its name): the file stays, no marker.
    writeFile(localFileViewFile(), fileViewBeforeTB7);
    QVERIFY(QDir().mkpath(backup));
    setMigrated(false);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Could not add the location bar.*is not a file")));
    m_mainWindow.reset(new DolphinMainWindow());
    QCOMPARE(contentOf(localFileViewFile()), fileViewBeforeTB7);
    QVERIFY(!migrationGroup().readEntry(WhaleToolbarMigration::layoutMarker(), false));
    // The other part is done all the same.
    QVERIFY(migrationGroup().readEntry(WhaleToolbarMigration::visibilityMarker(), false));
    m_mainWindow.reset();

    // Backed up, but the file cannot be written: the file stays, no marker.
    QVERIFY(QDir(backup).removeRecursively());
    writeFile(backup, fileViewBeforeTB7);
    const QString folder = QFileInfo(localFileViewFile()).absolutePath();
    QVERIFY(QFile::setPermissions(folder, QFile::ReadOwner | QFile::ExeOwner));
    if (::access(qPrintable(folder), W_OK) == 0) {
        QSKIP("the folder stays writable (root?)");
    }
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Could not add the location bar.*cannot write")));
    m_mainWindow.reset(new DolphinMainWindow());
    QCOMPARE(contentOf(localFileViewFile()), fileViewBeforeTB7);
    QVERIFY(!migrationGroup().readEntry(WhaleToolbarMigration::layoutMarker(), false));
    m_mainWindow.reset();

    // The next start tries again.
    QVERIFY(QFile::setPermissions(folder, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    m_mainWindow.reset(new DolphinMainWindow());
    QCOMPARE(contentOf(localFileViewFile()), fileViewMigrated);
    QCOMPARE(contentOf(backup), fileViewBeforeTB7);
    QVERIFY(migrationGroup().readEntry(WhaleToolbarMigration::layoutMarker(), false));
}

void WhaleToolbarMigrationTest::testUnreadableFile()
{
    // Not XML: left alone, tried again.
    const QByteArray broken = fileViewFile("<Action name=\"go_up\">");
    writeFile(localFileViewFile(), broken);
    setMigrated(false);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Could not add the location bar.*cannot read")));
    m_mainWindow.reset(new DolphinMainWindow());
    QCOMPARE(contentOf(localFileViewFile()), broken);
    QVERIFY(!QFile::exists(localFileViewFile() + WhaleToolbarMigration::backupSuffix()));
    QVERIFY(!migrationGroup().readEntry(WhaleToolbarMigration::layoutMarker(), false));
}

void WhaleToolbarMigrationTest::testVisibilityMigration()
{
    // Hidden File View Toolbars of TB5: shown on the first start of TB7 (L15) ...
    WhaleToolbarSettings::self()->setFileViewToolBarsVisible(false);
    setMigrated(false);
    m_mainWindow.reset(new DolphinMainWindow());
    QVERIFY(WhaleToolbarSettings::self()->fileViewToolBarsVisible());
    QVERIFY(migrationGroup().readEntry(WhaleToolbarMigration::visibilityMarker(), false));
    QVERIFY(toggle(m_mainWindow.data())->isChecked());
    QVERIFY(showWindow(m_mainWindow.data()));
    WhaleFileViewToolBar *bar = toolBar(m_mainWindow->activeViewContainer());
    QTRY_VERIFY(bar->isVisible() && bar->locationBar()->isVisible());

    // ... and hidden again by the user: they stay hidden.
    toggle(m_mainWindow.data())->trigger();
    QVERIFY(!WhaleToolbarSettings::self()->fileViewToolBarsVisible());
    m_mainWindow.reset();
    m_mainWindow.reset(new DolphinMainWindow());
    QVERIFY(!WhaleToolbarSettings::self()->fileViewToolBarsVisible());
    QVERIFY(!toggle(m_mainWindow.data())->isChecked());
    QVERIFY(showWindow(m_mainWindow.data()));
    QVERIFY(toolBar(m_mainWindow->activeViewContainer())->isHidden());
}

void WhaleToolbarMigrationTest::testNewUserTB7()
{
    // Nothing of an older Whale: nothing to migrate, nothing written but the markers.
    setMigrated(false);
    m_mainWindow.reset(new DolphinMainWindow());
    QVERIFY(showWindow(m_mainWindow.data()));
    QVERIFY(!QFile::exists(localFileViewFile()));
    QVERIFY(!QFile::exists(localFileViewFile() + WhaleToolbarMigration::backupSuffix()));
    QVERIFY(!QFile::exists(localMainFile()));
    QVERIFY(migrationGroup().readEntry(WhaleToolbarMigration::layoutMarker(), false));
    QVERIFY(migrationGroup().readEntry(WhaleToolbarMigration::visibilityMarker(), false));
    QVERIFY(WhaleToolbarSettings::self()->fileViewToolBarsVisible());
    QCOMPARE(layoutOf(m_mainWindow->toolBar(QStringLiteral("mainToolBar"))), mainDefault);
    WhaleFileViewToolBar *bar = toolBar(m_mainWindow->activeViewContainer());
    QTRY_VERIFY(bar->scope());
    QCOMPARE(layoutOf(bar), fileViewDefault);

    // A layout made with TB7 without the location bar is the user's choice.
    writeFile(localFileViewFile(), fileViewFile("<Action name=\"go_up\"/>"));
    m_mainWindow.reset(new DolphinMainWindow());
    QCOMPARE(contentOf(localFileViewFile()), fileViewFile("<Action name=\"go_up\"/>"));
}

void WhaleToolbarMigrationTest::testMainFileNotMigrated()
{
    // The user's dolphinui.rc with the global location bars: never touched (L5); KXmlGui
    // passes over "url_navigators".
    const QByteArray userFile = userMainFile(mainBeforeTB5, {});
    writeUserMainFile(userFile);
    writeFile(localFileViewFile(), fileViewBeforeTB7);
    WhaleToolbarSettings::self()->setFileViewToolBarsVisible(false);
    setMigrated(false);
    m_mainWindow.reset(new DolphinMainWindow());
    QVERIFY(showWindow(m_mainWindow.data()));
    QCOMPARE(layoutOf(m_mainWindow->toolBar(QStringLiteral("mainToolBar"))), shown(mainBeforeTB5));
    QCOMPARE(contentOf(localFileViewFile()), fileViewMigrated);
    m_mainWindow->close();
    m_mainWindow.reset();
    QCOMPARE(contentOf(localMainFile()), userFile);
    QVERIFY(!QFile::exists(localMainFile() + WhaleToolbarMigration::backupSuffix()));
}

void WhaleToolbarMigrationTest::testDowngradeFiles()
{
    writeFile(localFileViewFile(), fileViewBeforeTB7);
    setMigrated(false);
    m_mainWindow.reset(new DolphinMainWindow());
    m_mainWindow.reset();
    // An older Whale reads the migrated file as before: the same toolbar, one entry more that
    // it does not know (and passes over, as Whale does with unknown IDs), the same texts.
    QDomDocument migrated;
    QVERIFY(migrated.setContent(contentOf(localFileViewFile())));
    QDomDocument before;
    QVERIFY(before.setContent(fileViewBeforeTB7));
    QStringList withoutLocationBar = WhaleToolbarLayout::fromDocument(migrated, WhaleFileViewToolbarClient::toolBarName()).toStringList();
    QCOMPARE(withoutLocationBar.removeAll(QStringLiteral("whale_location_bar")), 1);
    QCOMPARE(withoutLocationBar, WhaleToolbarLayout::fromDocument(before, WhaleFileViewToolbarClient::toolBarName()).toStringList());
    QCOMPARE(WhaleToolbarLayout::fromDocument(migrated, WhaleFileViewToolbarClient::toolBarName()).actionProperties(),
             WhaleToolbarLayout::fromDocument(before, WhaleFileViewToolbarClient::toolBarName()).actionProperties());

    // Back to the layout of before: the backup, copied over the file; it is not migrated again.
    QVERIFY(QFile::remove(localFileViewFile()));
    QVERIFY(QFile::copy(localFileViewFile() + WhaleToolbarMigration::backupSuffix(), localFileViewFile()));
    m_mainWindow.reset(new DolphinMainWindow());
    QVERIFY(showWindow(m_mainWindow.data()));
    QCOMPARE(contentOf(localFileViewFile()), fileViewBeforeTB7);
    WhaleFileViewToolBar *bar = toolBar(m_mainWindow->activeViewContainer());
    QTRY_VERIFY(bar->scope());
    QVERIFY(!layoutOf(bar).contains(QStringLiteral("whale_location_bar")));
}

QTEST_MAIN(WhaleToolbarMigrationTest)

#include "whaletoolbarmigrationtest.moc"
