/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whalestyleelements.h"
#include "config-dolphin.h"
#include "dolphin_generalsettings.h"
#include "dolphinmainwindow.h"
#include "dolphintabpage.h"
#include "dolphintabwidget.h"
#include "dolphinviewcontainer.h"
#include "filterbar/filterbar.h"
#include "settings/viewmodes/viewmodesettings.h"
#include "testdir.h"
#include "testhelpers.h"
#include "views/dolphinview.h"
#include "views/viewproperties.h"
#include "views/zoomlevelinfo.h"
#include "whalesplitviewstartup.h"
#include "whalestyleelement.h"
#include "whaletoolbarsettings.h"

#include <KLocalizedString>
#include <KToolBar>

#include <QApplication>
#include <QDockWidget>
#include <QMenuBar>
#include <QStandardPaths>
#include <QTest>

namespace
{
const WhaleStyleElement *element(const char *key)
{
    return WhaleStyleElements::self().element(QLatin1String(key));
}
}

/**
 * The element types of styles at a main window (ST2): capture, change, apply, as before.
 */
class WhaleStyleElementsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testOrder();
    void testSplit();
    void testViewStyleLeft();
    void testViewStyleRightWithoutSplit();
    void testViewStyleRightWithSplit();
    void testViewStyleInFolderMode();
    void testFilterBar();
    void testFilterBarRight();
    void testZoom();
    void testMenuBar();
    void testToolBars();
    void testFileViewToolBars();
    void testStatusBar();
    void testPanels();
    void testWholeWindow();

private:
    DolphinViewContainer *showMainWindow();
    DolphinTabPage *tabPage() const;

    QTemporaryDir m_tmpHome;
    QScopedPointer<TestDir> m_dir;
    QScopedPointer<DolphinMainWindow> m_mainWindow;
};

void WhaleStyleElementsTest::initTestCase()
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
    GeneralSettings::self()->setGlobalViewProps(true);
    GeneralSettings::self()->save();
}

void WhaleStyleElementsTest::init()
{
    m_dir.reset(new TestDir());
    m_dir->createFile(QStringLiteral("a.txt"));
    m_dir->createFile(QStringLiteral("b.txt"));
    m_mainWindow.reset(new DolphinMainWindow());
}

void WhaleStyleElementsTest::cleanup()
{
    m_mainWindow.reset();
    m_dir.reset();
    WhaleToolbarSettings::self()->setFileViewToolBarsVisible(true);
    GeneralSettings::self()->setGlobalViewProps(true);
    TestHelpers::openNewWindowsInSingleView();
    GeneralSettings::self()->save();
}

DolphinViewContainer *WhaleStyleElementsTest::showMainWindow()
{
    m_mainWindow->openDirectories({m_dir->url()}, false);
    m_mainWindow->resize(1000, 700);
    m_mainWindow->show();
    if (!QTest::qWaitForWindowExposed(m_mainWindow.data())) {
        return nullptr;
    }
    DolphinViewContainer *container = m_mainWindow->activeViewContainer();
    if (!QTest::qWaitFor(
            [container]() {
                return container->view()->itemsCount() == 2;
            },
            5000)) {
        return nullptr;
    }
    return container;
}

DolphinTabPage *WhaleStyleElementsTest::tabPage() const
{
    return m_mainWindow->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"))->currentTabPage();
}

void WhaleStyleElementsTest::testOrder()
{
    QStringList keys;
    const auto elements = WhaleStyleElements::self().elements();
    for (const WhaleStyleElement *e : elements) {
        keys.append(e->key());
        QVERIFY(!e->title().isEmpty());
    }
    QCOMPARE(keys,
             (QStringList{QStringLiteral("split"),
                          QStringLiteral("viewStyle.left"),
                          QStringLiteral("viewStyle.right"),
                          QStringLiteral("filterBar.left"),
                          QStringLiteral("filterBar.right"),
                          QStringLiteral("zoom"),
                          QStringLiteral("menuBar"),
                          QStringLiteral("toolBars"),
                          QStringLiteral("fileViewToolBars"),
                          QStringLiteral("statusBar"),
                          QStringLiteral("panels")}));
}

void WhaleStyleElementsTest::testSplit()
{
    QVERIFY(showMainWindow());
    GeneralSettings::setSplitViewAtStartup(GeneralSettings::EnumSplitViewAtStartup::LastState);
    const WhaleStyleElement *split = element("split");

    const QString single = split->capture(m_mainWindow.data());
    QCOMPARE(single, QStringLiteral("false"));

    split->apply(m_mainWindow.data(), QStringLiteral("true"));
    QVERIFY(tabPage()->splitViewEnabled());
    QCOMPARE(split->capture(m_mainWindow.data()), QStringLiteral("true"));
    // Remembered for new windows and tabs (E3).
    QVERIFY(WhaleSplitViewStartup::openInSplitView());

    // Closing keeps the left view, even when the right one is active.
    QVERIFY(tabPage()->secondaryViewContainer()->isActive());
    DolphinViewContainer *left = tabPage()->primaryViewContainer();
    split->apply(m_mainWindow.data(), single);
    QVERIFY(!tabPage()->splitViewEnabled());
    QCOMPARE(tabPage()->primaryViewContainer(), left);
    QVERIFY(!WhaleSplitViewStartup::openInSplitView());

    // A value it cannot read changes nothing.
    split->apply(m_mainWindow.data(), QStringLiteral("maybe"));
    QVERIFY(!tabPage()->splitViewEnabled());
    QVERIFY(!split->describe(QStringLiteral("true")).isEmpty());
}

void WhaleStyleElementsTest::testViewStyleLeft()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    DolphinView *view = container->view();
    const WhaleStyleElement *style = element("viewStyle.left");
    QVERIFY(style->isAvailable(m_mainWindow.data()));

    view->setViewMode(DolphinView::CompactView);
    view->setHiddenFilesShown(true);
    const QString compact = style->capture(m_mainWindow.data());
    QVERIFY(!compact.isEmpty());
    QVERIFY(style->describe(compact).contains(i18nc("@item:intext view mode", "Compact")));
    QVERIFY(style->describe(compact).contains(i18nc("@item:intext style element value", "hidden files")));

    view->setViewMode(DolphinView::DetailsView);
    view->setHiddenFilesShown(false);
    style->apply(m_mainWindow.data(), compact);
    QCOMPARE(view->viewMode(), DolphinView::CompactView);
    QVERIFY(view->hiddenFilesShown());
    QCOMPARE(style->capture(m_mainWindow.data()), compact);

    // Written down for the left position (E3).
    QCOMPARE(ViewProperties(QUrl(), DolphinView::PanelRole::Left).viewMode(), DolphinView::CompactView);

    // Text without view properties changes nothing.
    style->apply(m_mainWindow.data(), QStringLiteral("nonsense"));
    QCOMPARE(view->viewMode(), DolphinView::CompactView);
}

void WhaleStyleElementsTest::testViewStyleRightWithoutSplit()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    DolphinView *left = container->view();
    left->setViewMode(DolphinView::DetailsView);
    const WhaleStyleElement *left_ = element("viewStyle.left");
    const WhaleStyleElement *right = element("viewStyle.right");
    QVERIFY(right->isAvailable(m_mainWindow.data()));

    // A style of Icons for the right view, taken from the left one.
    left->setViewMode(DolphinView::IconsView);
    const QString icons = left_->capture(m_mainWindow.data());
    left->setViewMode(DolphinView::DetailsView);

    right->apply(m_mainWindow.data(), icons);
    QCOMPARE(left->viewMode(), DolphinView::DetailsView);
    QCOMPARE(ViewProperties(QUrl(), DolphinView::PanelRole::Right).viewMode(), DolphinView::IconsView);
    QCOMPARE(ViewProperties(QUrl(), DolphinView::PanelRole::Left).viewMode(), DolphinView::DetailsView);
    // Without a right view it says what the right view starts from.
    QCOMPARE(right->capture(m_mainWindow.data()), icons);

    tabPage()->setSplitViewEnabled(true, WithoutAnimation);
    QCOMPARE(tabPage()->secondaryViewContainer()->view()->viewMode(), DolphinView::IconsView);
    QCOMPARE(left->viewMode(), DolphinView::DetailsView);
}

void WhaleStyleElementsTest::testViewStyleRightWithSplit()
{
    QVERIFY(showMainWindow());
    tabPage()->setSplitViewEnabled(true, WithoutAnimation);
    DolphinView *left = tabPage()->primaryViewContainer()->view();
    DolphinView *rightView = tabPage()->secondaryViewContainer()->view();
    const WhaleStyleElement *right = element("viewStyle.right");

    left->setViewMode(DolphinView::CompactView);
    rightView->setViewMode(DolphinView::DetailsView);
    const QString details = right->capture(m_mainWindow.data());

    rightView->setViewMode(DolphinView::IconsView);
    right->apply(m_mainWindow.data(), details);
    QCOMPARE(rightView->viewMode(), DolphinView::DetailsView);
    QCOMPARE(left->viewMode(), DolphinView::CompactView);
}

void WhaleStyleElementsTest::testViewStyleInFolderMode()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    const WhaleStyleElement *style = element("viewStyle.left");
    container->view()->setViewMode(DolphinView::CompactView);
    const QString compact = style->capture(m_mainWindow.data());

    // A style for each folder: the style belongs to the folder (F1).
    GeneralSettings::self()->setGlobalViewProps(false);
    QVERIFY(!style->isAvailable(m_mainWindow.data()));
    QVERIFY(style->capture(m_mainWindow.data()).isEmpty());
    container->view()->setViewMode(DolphinView::DetailsView);
    style->apply(m_mainWindow.data(), compact);
    QCOMPARE(container->view()->viewMode(), DolphinView::DetailsView);
}

void WhaleStyleElementsTest::testFilterBar()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    FilterBar *bar = container->findChild<FilterBar *>();
    const WhaleStyleElement *filter = element("filterBar.left");
    QVERIFY(filter->isAvailable(m_mainWindow.data()));

    // Hidden: no text, whatever was typed before.
    const QString hidden = filter->capture(m_mainWindow.data());
    QVERIFY(hidden.startsWith(QLatin1String("visible=false,")));
    QVERIFY(hidden.endsWith(QLatin1String(",text=")));

    // A regular expression with commas and "=" in it, for files only.
    const QString media = QStringLiteral("visible=true,lock=true,case=true,mode=Regex,scope=Files,text=\\.(jpg|txt){1,2}$=x,text=");
    QWidget *focusBefore = QApplication::focusWidget();
    filter->apply(m_mainWindow.data(), media);
    QVERIFY(container->isFilterBarVisible());
    QCOMPARE(bar->text(), QStringLiteral("\\.(jpg|txt){1,2}$=x,text="));
    QCOMPARE(bar->filterMode(), KFileItemModelFilter::Regex);
    QVERIFY(bar->isCaseSensitive());
    QCOMPARE(bar->filterScope(), KFileItemModelFilter::Scope::Files);
    QCOMPARE(container->view()->nameFilter(), bar->text());
    // A style leaves the focus where it was.
    QCOMPARE(QApplication::focusWidget(), focusBefore);
    QCOMPARE(filter->capture(m_mainWindow.data()), media);
    QVERIFY(filter->describe(media).contains(bar->text()));

    // Back to hidden: the filter is gone, lock and scope as the style says.
    filter->apply(m_mainWindow.data(), QStringLiteral("visible=false,lock=false,case=false,mode=Glob,scope=FilesAndFolders,text=ignored"));
    QVERIFY(!container->isFilterBarVisible());
    QVERIFY(container->view()->nameFilter().isEmpty());
    QVERIFY(!bar->isLocked());
    QCOMPARE(bar->filterScope(), KFileItemModelFilter::Scope::FilesAndFolders);
    QCOMPARE(filter->capture(m_mainWindow.data()), QStringLiteral("visible=false,lock=false,case=false,mode=Glob,scope=FilesAndFolders,text="));

    // Text it cannot read changes nothing.
    filter->apply(m_mainWindow.data(), QStringLiteral("visible=maybe,text=x"));
    filter->apply(m_mainWindow.data(), QStringLiteral("nonsense"));
    QVERIFY(!container->isFilterBarVisible());
}

void WhaleStyleElementsTest::testFilterBarRight()
{
    QVERIFY(showMainWindow());
    const WhaleStyleElement *right = element("filterBar.right");
    // No right view, no filter bar to set.
    QVERIFY(!right->isAvailable(m_mainWindow.data()));
    QVERIFY(right->capture(m_mainWindow.data()).isEmpty());

    tabPage()->setSplitViewEnabled(true, WithoutAnimation);
    QVERIFY(right->isAvailable(m_mainWindow.data()));
    right->apply(m_mainWindow.data(), QStringLiteral("visible=true,lock=true,case=false,mode=Glob,scope=Files,text=*.txt"));
    QCOMPARE(tabPage()->secondaryViewContainer()->view()->nameFilter(), QStringLiteral("*.txt"));
    QVERIFY(tabPage()->primaryViewContainer()->view()->nameFilter().isEmpty());
}

void WhaleStyleElementsTest::testZoom()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    DolphinView *view = container->view();
    view->setViewMode(DolphinView::IconsView);
    view->setPreviewsShown(false);
    const WhaleStyleElement *zoom = element("zoom");

    ViewModeSettings(DolphinView::IconsView).setIconSize(64);
    ViewModeSettings(DolphinView::IconsView).save();
    const QString captured = zoom->capture(m_mainWindow.data());
    QVERIFY(captured.contains(QLatin1String("Icons=64,")));
    QVERIFY(!zoom->describe(captured).isEmpty());

    view->setZoomLevel(ZoomLevelInfo::zoomLevelForIconSize(QSize(32, 32)));
    QCOMPARE(ViewModeSettings(DolphinView::IconsView).iconSize(), 32);

    zoom->apply(m_mainWindow.data(), captured);
    QCOMPARE(ViewModeSettings(DolphinView::IconsView).iconSize(), 64);
    QCOMPARE(view->zoomLevel(), ZoomLevelInfo::zoomLevelForIconSize(QSize(64, 64)));
    QCOMPARE(zoom->capture(m_mainWindow.data()), captured);

    // Parts it cannot read are left out.
    zoom->apply(m_mainWindow.data(), QStringLiteral("Icons=x;Nope=1,2;Compact"));
    QCOMPARE(ViewModeSettings(DolphinView::IconsView).iconSize(), 64);
}

void WhaleStyleElementsTest::testMenuBar()
{
    QVERIFY(showMainWindow());
    const WhaleStyleElement *menuBar = element("menuBar");
    const QString before = menuBar->capture(m_mainWindow.data());

    // DolphinMainWindow shows and hides the menu bar a moment later (queued, bug 286822).
    menuBar->apply(m_mainWindow.data(), QStringLiteral("false"));
    QCOMPARE(menuBar->capture(m_mainWindow.data()), QStringLiteral("false"));
    QTRY_VERIFY(m_mainWindow->menuBar()->isHidden());
    menuBar->apply(m_mainWindow.data(), QStringLiteral("true"));
    QCOMPARE(menuBar->capture(m_mainWindow.data()), QStringLiteral("true"));
    QTRY_VERIFY(!m_mainWindow->menuBar()->isHidden());

    menuBar->apply(m_mainWindow.data(), before);
    QCOMPARE(menuBar->capture(m_mainWindow.data()), before);
}

void WhaleStyleElementsTest::testToolBars()
{
    QVERIFY(showMainWindow());
    const WhaleStyleElement *toolBars = element("toolBars");
    KToolBar *main = m_mainWindow->toolBar(QStringLiteral("mainToolBar"));
    QVERIFY(main);
    main->show();

    const QString shown = toolBars->capture(m_mainWindow.data());
    QVERIFY(shown.contains(QLatin1String("mainToolBar=true")));

    toolBars->apply(m_mainWindow.data(), QStringLiteral("mainToolBar=false,noSuchToolBar=true"));
    QVERIFY(main->isHidden());
    // Not created by asking for it.
    QVERIFY(!m_mainWindow->findChild<KToolBar *>(QStringLiteral("noSuchToolBar")));
    QVERIFY(toolBars->capture(m_mainWindow.data()).contains(QLatin1String("mainToolBar=false")));

    toolBars->apply(m_mainWindow.data(), shown);
    QVERIFY(!main->isHidden());
    QCOMPARE(toolBars->capture(m_mainWindow.data()), shown);
}

void WhaleStyleElementsTest::testFileViewToolBars()
{
    QVERIFY(showMainWindow());
    const WhaleStyleElement *fileViewToolBars = element("fileViewToolBars");
    QCOMPARE(fileViewToolBars->capture(m_mainWindow.data()), QStringLiteral("true"));
    fileViewToolBars->apply(m_mainWindow.data(), QStringLiteral("false"));
    QVERIFY(!WhaleToolbarSettings::self()->fileViewToolBarsVisible());
    QCOMPARE(fileViewToolBars->capture(m_mainWindow.data()), QStringLiteral("false"));
    fileViewToolBars->apply(m_mainWindow.data(), QStringLiteral("true"));
    QVERIFY(WhaleToolbarSettings::self()->fileViewToolBarsVisible());
}

void WhaleStyleElementsTest::testStatusBar()
{
    QVERIFY(showMainWindow());
    const WhaleStyleElement *statusBar = element("statusBar");
    const QString before = statusBar->capture(m_mainWindow.data());

    statusBar->apply(m_mainWindow.data(), QStringLiteral("Disabled"));
    QCOMPARE(GeneralSettings::showStatusBar(), GeneralSettings::EnumShowStatusBar::Disabled);
    QCOMPARE(statusBar->capture(m_mainWindow.data()), QStringLiteral("Disabled"));
    statusBar->apply(m_mainWindow.data(), QStringLiteral("FullWidth"));
    QCOMPARE(GeneralSettings::showStatusBar(), GeneralSettings::EnumShowStatusBar::FullWidth);
    statusBar->apply(m_mainWindow.data(), QStringLiteral("Nonsense"));
    QCOMPARE(GeneralSettings::showStatusBar(), GeneralSettings::EnumShowStatusBar::FullWidth);

    statusBar->apply(m_mainWindow.data(), before);
    QCOMPARE(statusBar->capture(m_mainWindow.data()), before);
    QVERIFY(!statusBar->describe(before).isEmpty());
}

void WhaleStyleElementsTest::testPanels()
{
    QVERIFY(showMainWindow());
    const WhaleStyleElement *panels = element("panels");
    auto infoDock = m_mainWindow->findChild<QDockWidget *>(QStringLiteral("infoDock"));
    QVERIFY(infoDock);

    panels->apply(m_mainWindow.data(), QStringLiteral("information=true,places=false"));
    QVERIFY(!infoDock->isHidden());
    QVERIFY(m_mainWindow->findChild<QDockWidget *>(QStringLiteral("placesDock"))->isHidden());
    const QString captured = panels->capture(m_mainWindow.data());
    QVERIFY(captured.contains(QLatin1String("information=true")));
    QVERIFY(captured.contains(QLatin1String("places=false")));
    QVERIFY(panels->describe(captured).contains(i18nc("@item:intext panel", "Information")));

    panels->apply(m_mainWindow.data(), QStringLiteral("information=false"));
    QVERIFY(infoDock->isHidden());
    // Not listed: left as it is.
    QVERIFY(m_mainWindow->findChild<QDockWidget *>(QStringLiteral("placesDock"))->isHidden());
}

void WhaleStyleElementsTest::testWholeWindow()
{
    QVERIFY(showMainWindow());
    m_mainWindow->toolBar(QStringLiteral("mainToolBar"))->show();
    const auto elements = WhaleStyleElements::self().elements();

    // "Bilder": one view, Icons with previews, the Information panel.
    DolphinView *view = tabPage()->primaryViewContainer()->view();
    view->setViewMode(DolphinView::IconsView);
    view->setPreviewsShown(true);
    element("panels")->apply(m_mainWindow.data(), QStringLiteral("information=true"));
    QMap<QString, QString> bilder;
    for (const WhaleStyleElement *e : elements) {
        bilder.insert(e->key(), e->capture(m_mainWindow.data()));
    }

    // Something else entirely, "Schlicht" and more.
    tabPage()->setSplitViewEnabled(true, WithoutAnimation);
    view->setViewMode(DolphinView::CompactView);
    view->setPreviewsShown(false);
    tabPage()->secondaryViewContainer()->view()->setViewMode(DolphinView::DetailsView);
    element("menuBar")->apply(m_mainWindow.data(), QStringLiteral("false"));
    element("toolBars")->apply(m_mainWindow.data(), QStringLiteral("mainToolBar=false"));
    element("fileViewToolBars")->apply(m_mainWindow.data(), QStringLiteral("false"));
    element("statusBar")->apply(m_mainWindow.data(), QStringLiteral("FullWidth"));
    element("panels")->apply(m_mainWindow.data(), QStringLiteral("information=false,folders=true"));

    for (const WhaleStyleElement *e : elements) {
        e->apply(m_mainWindow.data(), bilder.value(e->key()));
    }

    QVERIFY(!tabPage()->splitViewEnabled());
    QCOMPARE(tabPage()->primaryViewContainer()->view(), view);
    QCOMPARE(view->viewMode(), DolphinView::IconsView);
    QVERIFY(view->previewsShown());
    for (const WhaleStyleElement *e : elements) {
        QCOMPARE(e->capture(m_mainWindow.data()), bilder.value(e->key()));
    }
}

QTEST_MAIN(WhaleStyleElementsTest)

#include "whalestyleelementstest.moc"
