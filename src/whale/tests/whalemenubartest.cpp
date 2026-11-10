/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
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
#include <KHamburgerMenu>
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
#include <QToolButton>

/**
 * The menu bar as an element of the window's toolbars (TM, .privatedoc/design-notes.md).
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

    void testBurgerWhenNarrow_data();
    void testBurgerWhenNarrow();
    void testBurgerMenu();
    void testBurgerWhenVertical();
    void testHamburgerLeftOut();
    void testHamburgerLeftOutAfterShowMenubar();
    void testNoChangesOnHover();
    void testMnemonicsOnlyElement();

private:
    /** Shows the window with the element in the main toolbar; @return its widget. */
    WhaleToolBarMenu *showWithElement(bool atEnd = false);
    static QStringList titles(const QMenuBar *menuBar);
    /** @return The button of the KHamburgerMenu in the main toolbar. */
    QWidget *hamburgerButton() const;

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

enum class ElementPlace {
    Start,
    End,
    None,
};

/** The installed dolphinui.rc with the menu bar element at @p place of the main toolbar, as the user would save it. */
bool writeUserMainFile(ElementPlace place)
{
    QFile installed(QStringLiteral(":/kxmlgui5/orcinus/dolphinui.rc"));
    if (!installed.open(QIODevice::ReadOnly)) {
        return false;
    }
    QByteArray content = installed.readAll();
    const QByteArray toolBarText = "<text context=\"@title:menu\">Main Toolbar</text>";
    if (!content.contains(toolBarText)) {
        return false;
    }
    // The default main toolbar has the element (0.1.0); placed anew here.
    content.replace("<Action name=\"whale_menu_bar\" />", "");
    if (place == ElementPlace::End) {
        content.replace("<Action name=\"hamburger_menu\" />", "<Action name=\"hamburger_menu\" /><Action name=\"whale_menu_bar\" />");
    } else if (place == ElementPlace::Start) {
        content.replace(toolBarText, QByteArray(toolBarText + "<Action name=\"whale_menu_bar\" />"));
    }
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
    KLocalizedString::setApplicationDomain(TRANSLATION_DOMAIN);
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

WhaleToolBarMenu *WhaleMenuBarTest::showWithElement(bool atEnd)
{
    if (!writeUserMainFile(atEnd ? ElementPlace::End : ElementPlace::Start)) {
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
    // In the main toolbar by default (since 0.1.0; F3 had it left out), and never in the File View Toolbars.
    QVERIFY(m_mainWindow->toolBar()->actions().contains(action));
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

QWidget *WhaleMenuBarTest::hamburgerButton() const
{
    QAction *hamburger = m_mainWindow->actionCollection()->action(KStandardAction::name(KStandardAction::HamburgerMenu));
    return hamburger ? m_mainWindow->toolBar()->widgetForAction(hamburger) : nullptr;
}

void WhaleMenuBarTest::testBurgerWhenNarrow_data()
{
    QTest::addColumn<bool>("atEnd");
    QTest::newRow("first") << false;
    // The toolbar moves the last elements to its overflow menu first, hiding them.
    QTest::newRow("at the end") << true;
}

void WhaleMenuBarTest::testBurgerWhenNarrow()
{
    QFETCH(bool, atEnd);
    WhaleToolBarMenu *element = showWithElement(atEnd);
    QVERIFY(element);
    QCOMPARE(m_mainWindow->toolBar()->actions().constLast()->objectName() == WhaleMenuBarAction::actionName(), atEnd);
    const int fullWidth = element->menuBar()->sizeHint().width();
    QTRY_VERIFY(!element->isBurger());
    QVERIFY(element->menuBar()->isVisible());
    QVERIFY(!element->burgerButton()->isVisible());

    m_mainWindow->resize(fullWidth + 60, 700);
    QTRY_VERIFY(element->isBurger());
    QVERIFY(!element->menuBar()->isVisible());
    QVERIFY(element->burgerButton()->isVisible());
    QTRY_VERIFY(element->width() < fullWidth / 2);
    // In the toolbar itself, not in its overflow menu.
    QVERIFY(element->isVisible());
    QVERIFY(m_mainWindow->toolBar()->rect().contains(element->geometry()));

    m_mainWindow->resize(1200, 700);
    QTRY_VERIFY(!element->isBurger());
    QVERIFY(element->menuBar()->isVisible());
    QTRY_COMPARE(element->width(), fullWidth);
    QVERIFY(m_mainWindow->toolBar()->rect().contains(element->geometry()));
}

void WhaleMenuBarTest::testBurgerMenu()
{
    WhaleToolBarMenu *element = showWithElement();
    QVERIFY(element);
    m_mainWindow->resize(element->menuBar()->sizeHint().width() + 60, 700);
    QTRY_VERIFY(element->isBurger());

    // The menus of the menu bar as submenus (F1).
    QMenu *menu = element->burgerButton()->menu();
    QVERIFY(menu);
    Q_EMIT menu->aboutToShow();
    QList<QAction *> visibleMenus;
    const auto menus = m_mainWindow->menuBar()->actions();
    for (QAction *action : menus) {
        if (action->isVisible()) {
            visibleMenus.append(action);
        }
    }
    QCOMPARE(menu->actions(), visibleMenus);
    QVERIFY(menu->actions().constFirst()->menu());
    // Again: not twice.
    Q_EMIT menu->aboutToShow();
    QCOMPARE(menu->actions(), visibleMenus);
}

void WhaleMenuBarTest::testBurgerWhenVertical()
{
    WhaleToolBarMenu *element = showWithElement();
    QVERIFY(element);
    QTRY_VERIFY(!element->isBurger());
    m_mainWindow->addToolBar(Qt::LeftToolBarArea, m_mainWindow->toolBar());
    QTRY_VERIFY(element->isBurger());
    m_mainWindow->addToolBar(Qt::TopToolBarArea, m_mainWindow->toolBar());
    QTRY_VERIFY(!element->isBurger());
}

void WhaleMenuBarTest::testHamburgerLeftOut()
{
    // Without the element: the button of the hamburger menu as before.
    QVERIFY(writeUserMainFile(ElementPlace::None));
    m_mainWindow.reset(new DolphinMainWindow());
    m_mainWindow->openDirectories({m_dir->url()}, false);
    m_mainWindow->resize(1200, 700);
    m_mainWindow->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_mainWindow.data()));
    QVERIFY(!m_mainWindow->menuBar()->isVisible());
    QPointer<QWidget> hamburger = hamburgerButton();
    QVERIFY(hamburger);
    QTRY_VERIFY(hamburger->isVisible() && hamburger->width() > 0);
    m_mainWindow.reset();

    // F2: with the element in the same toolbar, it takes no room, in both modes.
    WhaleToolBarMenu *element = showWithElement();
    QVERIFY(element);
    hamburger = hamburgerButton();
    QVERIFY(hamburger);
    QTRY_COMPARE(hamburger->width(), 0);
    QCOMPARE(hamburger->focusPolicy(), Qt::NoFocus);
    m_mainWindow->resize(element->menuBar()->sizeHint().width() + 60, 700);
    QTRY_VERIFY(element->isBurger());
    QCOMPARE(hamburger->width(), 0);

    // Taken out of the toolbar: the button comes back.
    QAction *action = m_mainWindow->actionCollection()->action(WhaleMenuBarAction::actionName());
    m_mainWindow->toolBar()->removeAction(action);
    QTRY_VERIFY(!element || !m_mainWindow->toolBar()->findChild<WhaleToolBarMenu *>());
    m_mainWindow->resize(1200, 700);
    QTRY_VERIFY(hamburger->width() > 0);
}

void WhaleMenuBarTest::testHamburgerLeftOutAfterShowMenubar()
{
    // KHamburgerMenu hides its button while the classic menu bar is shown, and shows it again.
    WhaleToolBarMenu *element = showWithElement(true);
    QVERIFY(element);
    QAction *showMenuBar = m_mainWindow->actionCollection()->action(KStandardAction::name(KStandardAction::ShowMenubar));
    showMenuBar->trigger();
    QTRY_VERIFY(m_mainWindow->menuBar()->isVisible());
    showMenuBar->trigger();
    QTRY_VERIFY(!m_mainWindow->menuBar()->isVisible());
    QWidget *hamburger = hamburgerButton();
    QVERIFY(hamburger);
    QTest::qWait(50);
    QCOMPARE(hamburger->width(), 0);
}

namespace
{
class EventCounter : public QObject
{
public:
    QMap<int, int> counts;
    bool eventFilter(QObject *, QEvent *event) override
    {
        counts[event->type()]++;
        return false;
    }
};
}

void WhaleMenuBarTest::testNoChangesOnHover()
{
    WhaleToolBarMenu *element = showWithElement();
    QVERIFY(element);
    QAction *showMenuBar = m_mainWindow->actionCollection()->action(KStandardAction::name(KStandardAction::ShowMenubar));
    showMenuBar->trigger();
    QTRY_VERIFY(m_mainWindow->menuBar()->isVisible());

    // Both menu bars show the same actions: hovering must not change them (it flickered when
    // KAcceleratorManager gave both the same title different accelerators, again and again).
    EventCounter source, toolBar;
    m_mainWindow->menuBar()->installEventFilter(&source);
    m_mainWindow->toolBar()->installEventFilter(&toolBar);
    // Counted only once showing the menu bar has settled: under load the toolbar may still be
    // laid out after a fixed wait.
    const auto changes = [&] {
        return source.counts.value(QEvent::ActionChanged) + toolBar.counts.value(QEvent::LayoutRequest);
    };
    bool settled = false;
    for (int attempt = 0; attempt < 25 && !settled; ++attempt) {
        source.counts.clear();
        toolBar.counts.clear();
        QTest::qWait(200);
        settled = changes() == 0;
    }
    QVERIFY(settled);
    for (QMenuBar *bar : {element->menuBar(), m_mainWindow->menuBar()}) {
        const auto actions = bar->actions();
        for (QAction *action : actions) {
            if (!action->isSeparator()) {
                QTest::mouseMove(bar, bar->actionGeometry(action).center());
                QTest::qWait(20);
            }
        }
    }
    QTest::qWait(50);
    QCOMPARE(source.counts.value(QEvent::ActionChanged), 0);
    QCOMPARE(toolBar.counts.value(QEvent::LayoutRequest), 0);
}

void WhaleMenuBarTest::testMnemonicsOnlyElement()
{
    WhaleToolBarMenu *element = showWithElement();
    QVERIFY(element);
    QVERIFY(!m_mainWindow->menuBar()->isVisible());
    QMenu *fileMenu = element->menuBar()->actions().constFirst()->menu();
    QVERIFY(fileMenu);
    m_mainWindow->QWidget::activateWindow();
    QTest::keyClick(m_mainWindow.data(), Qt::Key_F, Qt::AltModifier);
    QTRY_VERIFY(fileMenu->isVisible());
    fileMenu->close();
}

QTEST_MAIN(WhaleMenuBarTest)

#include "whalemenubartest.moc"
