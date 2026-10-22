/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config-dolphin.h"
#include "dolphin_generalsettings.h"
#include "dolphinmainwindow.h"
#include "testdir.h"
#include "testhelpers.h"
#include "whalemenubaraction.h"
#include "whaleviewcommandscope.h"

#include <KActionCollection>
#include <KLocalizedString>
#include <KStandardAction>
#include <KToolBar>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMenu>
#include <QMenuBar>
#include <QStandardPaths>
#include <QTest>

/**
 * The menu bar as an element of the window's toolbars (TM, .privatedoc/whale-toolbar-menu-plan.md).
 */
class WhaleMenuBarTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testOffered();
    void testSameMenus();
    void testFollowsMenuBar();
    void testIndependentOfShowMenubar();
    void testFixedWidth();
    void testMnemonicsWithBothShown();

private:
    /** Shows the window with the element in the main toolbar; @return its widget. */
    WhaleToolBarMenu *showWithElement();
    static QStringList titles(const QMenuBar *menuBar);

    QTemporaryDir m_tmpHome;
    QScopedPointer<TestDir> m_dir;
    QScopedPointer<DolphinMainWindow> m_mainWindow;
};

namespace
{
QString localMainFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kxmlgui5/") + QStringLiteral(WHALE_COMPONENT)
        + QStringLiteral("/dolphinui.rc");
}

/** The installed dolphinui.rc with the menu bar element at the start of the main toolbar, as the user would save it. */
bool writeUserMainFile()
{
    QFile installed(QStringLiteral(":/kxmlgui5/whale/dolphinui.rc"));
    if (!installed.open(QIODevice::ReadOnly)) {
        return false;
    }
    QByteArray content = installed.readAll();
    const QByteArray toolBarText = "<text context=\"@title:menu\">Main Toolbar</text>";
    if (!content.contains(toolBarText)) {
        return false;
    }
    content.replace(toolBarText, QByteArray(toolBarText + "<Action name=\"whale_menu_bar\" />"));
    QDir().mkpath(QFileInfo(localMainFile()).absolutePath());
    QFile local(localMainFile());
    return local.open(QIODevice::WriteOnly) && local.write(content) == content.size();
}
}

void WhaleMenuBarTest::initTestCase()
{
    QVERIFY(m_tmpHome.isValid());
    QCoreApplication::setApplicationName(QStringLiteral(WHALE_COMPONENT));
    QCoreApplication::setOrganizationDomain(QStringLiteral(WHALE_ORG_DOMAIN));
    qputenv("HOME", m_tmpHome.path().toUtf8());
    qputenv("XDG_DATA_HOME", m_tmpHome.filePath(QStringLiteral("data")).toUtf8());
    QStandardPaths::setTestModeEnabled(true);
    TestHelpers::disableAnimations();
    TestHelpers::openNewWindowsInSingleView();
    KLocalizedString::setApplicationDomain({"whale"});
    GeneralSettings::self()->setRememberOpenedTabs(false);
    GeneralSettings::self()->save();
}

void WhaleMenuBarTest::init()
{
    QFile::remove(localMainFile());
    m_dir.reset(new TestDir());
    m_dir->createFile(QStringLiteral("a.txt"));
}

void WhaleMenuBarTest::cleanup()
{
    m_mainWindow.reset();
    m_dir.reset();
    QFile::remove(localMainFile());
}

WhaleToolBarMenu *WhaleMenuBarTest::showWithElement()
{
    if (!writeUserMainFile()) {
        return nullptr;
    }
    m_mainWindow.reset(new DolphinMainWindow());
    m_mainWindow->openDirectories({m_dir->url()}, false);
    m_mainWindow->resize(1200, 700);
    m_mainWindow->show();
    if (!QTest::qWaitForWindowExposed(m_mainWindow.data())) {
        return nullptr;
    }
    return m_mainWindow->toolBar()->findChild<WhaleToolBarMenu *>();
}

QStringList WhaleMenuBarTest::titles(const QMenuBar *menuBar)
{
    QStringList result;
    const auto actions = menuBar->actions();
    for (const QAction *action : actions) {
        if (action->isVisible()) {
            result.append(action->text());
        }
    }
    return result;
}

void WhaleMenuBarTest::testOffered()
{
    m_mainWindow.reset(new DolphinMainWindow());
    QAction *action = m_mainWindow->actionCollection()->action(WhaleMenuBarAction::actionName());
    QVERIFY(qobject_cast<WhaleMenuBarAction *>(action));
    QVERIFY(!action->text().isEmpty());
    QVERIFY(!action->icon().isNull());
    // Not in the main toolbar by default (F3), and never in the File View Toolbars.
    QVERIFY(!m_mainWindow->toolBar()->actions().contains(action));
    QVERIFY(!WhaleViewCommandScope::allowedActions().contains(WhaleMenuBarAction::actionName()));
}

void WhaleMenuBarTest::testSameMenus()
{
    WhaleToolBarMenu *element = showWithElement();
    QVERIFY(element);
    QVERIFY(element->isVisible());
    QVERIFY(!element->menuBar()->isNativeMenuBar());
    QVERIFY(titles(m_mainWindow->menuBar()).size() >= 5);
    QCOMPARE(element->menuBar()->actions(), m_mainWindow->menuBar()->actions());

    // The same menus: opening one from the toolbar shows the window's menu.
    QAction *first = element->menuBar()->actions().constFirst();
    QVERIFY(first->menu());
    QCOMPARE(first, m_mainWindow->menuBar()->actions().constFirst());
}

void WhaleMenuBarTest::testFollowsMenuBar()
{
    WhaleToolBarMenu *element = showWithElement();
    QVERIFY(element);
    const int widthBefore = element->sizeHint().width();

    auto extra = new QMenu(QStringLiteral("Extra Menu With A Long Title"), m_mainWindow.data());
    m_mainWindow->menuBar()->addMenu(extra);
    QTRY_VERIFY(element->menuBar()->actions().contains(extra->menuAction()));
    QTRY_VERIFY(element->sizeHint().width() > widthBefore);

    m_mainWindow->menuBar()->removeAction(extra->menuAction());
    QTRY_VERIFY(!element->menuBar()->actions().contains(extra->menuAction()));
    QCOMPARE(element->menuBar()->actions(), m_mainWindow->menuBar()->actions());
}

void WhaleMenuBarTest::testIndependentOfShowMenubar()
{
    WhaleToolBarMenu *element = showWithElement();
    QVERIFY(element);
    QAction *showMenuBar = m_mainWindow->actionCollection()->action(KStandardAction::name(KStandardAction::ShowMenubar));
    QVERIFY(showMenuBar);

    // E1: "Show Menubar" changes only the classic menu bar.
    showMenuBar->setChecked(false);
    showMenuBar->trigger();
    QTRY_VERIFY(m_mainWindow->menuBar()->isVisible());
    QVERIFY(element->isVisible());
    showMenuBar->trigger();
    QTRY_VERIFY(!m_mainWindow->menuBar()->isVisible());
    QVERIFY(element->isVisible());
    QCOMPARE(titles(element->menuBar()), titles(m_mainWindow->menuBar()));
}

void WhaleMenuBarTest::testFixedWidth()
{
    WhaleToolBarMenu *element = showWithElement();
    QVERIFY(element);
    QCOMPARE(element->sizePolicy().horizontalPolicy(), QSizePolicy::Fixed);
    // As wide as all titles, not more: the window is wide enough.
    QTRY_COMPARE(element->width(), element->sizeHint().width());
    QVERIFY(element->width() >= element->menuBar()->sizeHint().width());
    m_mainWindow->resize(1600, 700);
    QTRY_COMPARE(m_mainWindow->width(), 1600);
    QCOMPARE(element->width(), element->sizeHint().width());
}

void WhaleMenuBarTest::testMnemonicsWithBothShown()
{
    WhaleToolBarMenu *element = showWithElement();
    QVERIFY(element);
    QAction *showMenuBar = m_mainWindow->actionCollection()->action(KStandardAction::name(KStandardAction::ShowMenubar));
    if (!showMenuBar->isChecked()) {
        showMenuBar->trigger();
    }
    QTRY_VERIFY(m_mainWindow->menuBar()->isVisible());

    // Two menu bars with the same titles: Alt+F must still open the File menu.
    QMenu *fileMenu = m_mainWindow->menuBar()->actions().constFirst()->menu();
    QVERIFY(fileMenu);
    QVERIFY(m_mainWindow->menuBar()->actions().constFirst()->text().contains(QLatin1String("&F")));
    m_mainWindow->QWidget::activateWindow();
    QTest::keyClick(m_mainWindow.data(), Qt::Key_F, Qt::AltModifier);
    QTRY_VERIFY(fileMenu->isVisible());
    fileMenu->close();
}

QTEST_MAIN(WhaleMenuBarTest)

#include "whalemenubartest.moc"
