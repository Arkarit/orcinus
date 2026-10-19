/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaledevautomation.h"

#include "config-dolphin.h"
#include "dolphin_generalsettings.h"
#include "dolphinmainwindow.h"
#include "dolphinviewcontainer.h"
#include "testdir.h"
#include "testhelpers.h"
#include "views/dolphinview.h"
#include "whaletoolbarsettings.h"
#include "whaleview.h"
#include "whaleviewhost.h"

#include <KLocalizedString>

#include <QDBusConnection>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

namespace
{
QJsonObject expect(std::initializer_list<std::pair<QString, QJsonValue>> conditions)
{
    QJsonObject object;
    for (const auto &[path, value] : conditions) {
        object.insert(path, value);
    }
    return object;
}

QStringList strings(const QJsonValue &value)
{
    QStringList result;
    const QJsonArray array = value.toArray();
    for (const QJsonValue &entry : array) {
        result.append(entry.toString());
    }
    return result;
}
}

class WhaleDevAutomationTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testOnlyStartedWhenRequested();
    void testUiState();
    void testSetView();
    void testPreferredViews();
    void testContentScope();
    void testSplitPanes();
    void testHiddenFiles();
    void testFilter();
    void testSort();
    void testLocation();
    void testFocusMatchesVisibleWidget();
    void testOnlyNonDestructiveActions();
    void testSetSelection();
    void testWaitForState();
    void testScreenshot();
    void testToolbarState();
    void testToolbarErrors();
    void testToolbarsOfTwoWindows();
    void testLocationBarState();
    void testLocationBarInput();
    void testLocationBarErrors();

private:
    /// Waits until the conditions hold; prints the mismatches otherwise.
    bool reaches(const QJsonObject &conditions);

    QTemporaryDir m_tmpHome;
    QScopedPointer<TestDir> m_dir;
    QScopedPointer<DolphinMainWindow> m_window;
    WhaleDevAutomation *m_automation = nullptr;
};

void WhaleDevAutomationTest::initTestCase()
{
    QVERIFY(m_tmpHome.isValid());
    QCoreApplication::setApplicationName(QStringLiteral(WHALE_COMPONENT));
    QCoreApplication::setOrganizationDomain(QStringLiteral(WHALE_ORG_DOMAIN));
    qputenv("HOME", m_tmpHome.path().toUtf8());
    QStandardPaths::setTestModeEnabled(true);
    TestHelpers::disableAnimations();
    KLocalizedString::setApplicationDomain({"whale"});
    QCoreApplication::addLibraryPath(QStringLiteral(WHALE_TEST_PLUGIN_DIR));
    QCoreApplication::addLibraryPath(QStringLiteral(WHALE_TEST_VIEW_PLUGIN_DIR));
    qunsetenv("WHALE_DEV_MCP");
}

void WhaleDevAutomationTest::init()
{
    m_dir.reset(new TestDir());
    m_dir->createFile(QStringLiteral("a.txt"), QByteArray(300, 'a'));
    m_dir->createFile(QStringLiteral("b.txt"), QByteArray(100, 'b'));
    m_dir->createFile(QStringLiteral(".hidden"));
    m_dir->createFile(QStringLiteral("sub/inner.txt"));

    m_window.reset(new DolphinMainWindow());
    m_window->openDirectories({m_dir->url()}, false);
    m_window->resize(1200, 800);
    m_window->show();
    QVERIFY(QTest::qWaitForWindowActive(m_window.data()));
    m_automation = new WhaleDevAutomation(m_window.data());

    // Whale shares the view properties of all folders by default; start from the defaults.
    m_automation->setHiddenFiles(-1, false);
    m_automation->setSort(-1, QStringLiteral("name"), QStringLiteral("ascending"), true);
    QVERIFY(reaches(expect({{QStringLiteral("panes.0.itemCount"), 3}, {QStringLiteral("panes.0.loading"), false}})));
}

void WhaleDevAutomationTest::cleanup()
{
    WhaleToolbarSettings::self()->setFileViewToolBarsVisible(true);
    m_automation = nullptr;
    m_window.reset();
    m_dir.reset();
}

bool WhaleDevAutomationTest::reaches(const QJsonObject &conditions)
{
    const bool reached = QTest::qWaitFor(
        [this, &conditions]() {
            return m_automation->evaluate(conditions).value(QStringLiteral("matched")).toBool();
        },
        5000);
    if (!reached) {
        qWarning().noquote() << QJsonDocument(m_automation->evaluate(conditions)).toJson();
    }
    return reached;
}

void WhaleDevAutomationTest::testOnlyStartedWhenRequested()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        QSKIP("No D-Bus session bus");
    }

    // Without WHALE_DEV_MCP=1 nothing is created or offered on D-Bus.
    QVERIFY(!WhaleDevAutomation::isRequested());
    const int before = m_window->findChildren<WhaleDevAutomation *>().count();
    WhaleDevAutomation::startIfRequested(m_window.data());
    QCOMPARE(m_window->findChildren<WhaleDevAutomation *>().count(), before);
    QVERIFY(!bus.objectRegisteredAt(WhaleDevAutomation::objectPath()));

    qputenv("WHALE_DEV_MCP", "1");
    WhaleDevAutomation::startIfRequested(m_window.data());
    qunsetenv("WHALE_DEV_MCP");
    QCOMPARE(m_window->findChildren<WhaleDevAutomation *>().count(), before + 1);
    QVERIFY(bus.objectRegisteredAt(WhaleDevAutomation::objectPath()));
    bus.unregisterObject(WhaleDevAutomation::objectPath());
}

void WhaleDevAutomationTest::testUiState()
{
    const QJsonObject state = m_automation->uiState();
    QCOMPARE(state.value(QStringLiteral("schema")).toInt(), 1);
    QCOMPARE(state.value(QStringLiteral("paneCount")).toInt(), 1);
    QCOMPARE(state.value(QStringLiteral("splitView")).toBool(), false);
    QCOMPARE(state.value(QStringLiteral("activePane")).toInt(), 0);
    QCOMPARE(QUrl(state.value(QStringLiteral("url")).toString()).adjusted(QUrl::StripTrailingSlash), m_dir->url().adjusted(QUrl::StripTrailingSlash));
    const QStringList views = strings(state.value(QStringLiteral("availableViews")));
    QVERIFY(views.contains(QStringLiteral("dolphin")));
    QVERIFY(views.contains(QStringLiteral("whale_hello")));
    QVERIFY(views.contains(QStringLiteral("whale_testlist")));

    const QJsonObject pane = state.value(QStringLiteral("panes")).toArray().at(0).toObject();
    QCOMPARE(pane.value(QStringLiteral("viewId")).toString(), QStringLiteral("dolphin"));
    QCOMPARE(pane.value(QStringLiteral("active")).toBool(), true);
    QCOMPARE(pane.value(QStringLiteral("dolphinViewVisible")).toBool(), true);
    QVERIFY(pane.value(QStringLiteral("pluginWidgetVisible")).isNull());
    QCOMPARE(strings(pane.value(QStringLiteral("items"))), QStringList({QStringLiteral("sub"), QStringLiteral("a.txt"), QStringLiteral("b.txt")}));
    QCOMPARE(pane.value(QStringLiteral("hiddenFiles")).toBool(), false);
    QCOMPARE(pane.value(QStringLiteral("sort")).toObject().value(QStringLiteral("field")).toString(), QStringLiteral("name"));
    QCOMPARE(pane.value(QStringLiteral("focusProxy")).toString(), QStringLiteral("dolphin-item-view"));
    QCOMPARE(pane.value(QStringLiteral("selectionCount")).toInt(), 0);

    const QJsonObject focus = state.value(QStringLiteral("focus")).toObject();
    QCOMPARE(focus.value(QStringLiteral("role")).toString(), QStringLiteral("dolphin-item-view"));
    QCOMPARE(focus.value(QStringLiteral("pane")).toInt(), 0);
    QCOMPARE(focus.value(QStringLiteral("windowActive")).toBool(), true);
    QCOMPARE(focus.value(QStringLiteral("keyboardFocus")).toBool(), true);
    QVERIFY(focus.value(QStringLiteral("anomalies")).toArray().isEmpty());

    // No pointers or addresses anywhere in the state.
    const QByteArray json = QJsonDocument(state).toJson();
    QVERIFY(!json.contains("0x"));
}

void WhaleDevAutomationTest::testSetView()
{
    QVERIFY(m_automation->setView(0, QStringLiteral("whale_testlist")).value(QStringLiteral("ok")).toBool());
    QVERIFY(reaches(expect({
        {QStringLiteral("panes.0.viewId"), QStringLiteral("whale_testlist")},
        {QStringLiteral("panes.0.dolphinViewVisible"), false},
        {QStringLiteral("panes.0.pluginWidgetVisible"), true},
        {QStringLiteral("panes.0.focusProxy"), QStringLiteral("plugin-widget")},
        {QStringLiteral("focus.role"), QStringLiteral("plugin:whale_testlist")},
    })));
    const QJsonObject pane = m_automation->uiState().value(QStringLiteral("panes")).toArray().at(0).toObject();
    QCOMPARE(pane.value(QStringLiteral("pluginRows")), pane.value(QStringLiteral("items")));

    // The View menu: exactly the shown view's entry is checked; the view options follow it.
    auto checkedModes = [this]() {
        QStringList ids;
        const QJsonArray modes = m_automation->uiState()[QStringLiteral("viewMenu")][QStringLiteral("modes")].toArray();
        for (const QJsonValue &mode : modes) {
            if (mode[QStringLiteral("checked")].toBool()) {
                ids.append(mode[QStringLiteral("id")].toString());
            }
        }
        return ids;
    };
    QCOMPARE(checkedModes(), QStringList{QStringLiteral("whale_testlist")});
    const QJsonValue viewMenu = m_automation->uiState()[QStringLiteral("viewMenu")];
    QCOMPARE(viewMenu[QStringLiteral("options")][QStringLiteral("flat")][QStringLiteral("enabled")].toBool(), false);
    QCOMPARE(viewMenu[QStringLiteral("options")][QStringLiteral("previews")][QStringLiteral("enabled")].toBool(), true);
    QCOMPARE(viewMenu[QStringLiteral("options")][QStringLiteral("hidden")][QStringLiteral("enabled")].toBool(), true);
    QVERIFY(!strings(viewMenu[QStringLiteral("viewSettings")]).isEmpty());

    QVERIFY(m_automation->setView(0, QStringLiteral("dolphin")).value(QStringLiteral("ok")).toBool());
    QVERIFY(reaches(expect({
        {QStringLiteral("panes.0.viewId"), QStringLiteral("dolphin")},
        {QStringLiteral("panes.0.dolphinViewVisible"), true},
        {QStringLiteral("panes.0.focusProxy"), QStringLiteral("dolphin-item-view")},
        {QStringLiteral("focus.role"), QStringLiteral("dolphin-item-view")},
    })));

    const QJsonObject error = m_automation->setView(0, QStringLiteral("no_such_view"));
    QVERIFY(!error.value(QStringLiteral("ok")).toBool());
    QVERIFY(error.value(QStringLiteral("error")).toString().contains(QStringLiteral("no_such_view")));
    QVERIFY(!m_automation->setView(5, QStringLiteral("dolphin")).value(QStringLiteral("ok")).toBool());
}

void WhaleDevAutomationTest::testPreferredViews()
{
    const QString url = m_automation->uiState().value(QStringLiteral("url")).toString();
    QVERIFY(reaches(expect({{QStringLiteral("panes.0.preferredView"), QJsonValue::Null}})));

    // An automatic switch is not remembered; the user's choice (the switcher's entry) is.
    QVERIFY(m_automation->setView(0, QStringLiteral("whale_testlist")).value(QStringLiteral("ok")).toBool());
    QVERIFY(reaches(expect({{QStringLiteral("panes.0.preferredView"), QJsonValue::Null}})));
    QVERIFY(m_automation->setView(0, QStringLiteral("dolphin"), true).value(QStringLiteral("ok")).toBool());
    QVERIFY(m_automation->setView(0, QStringLiteral("whale_testlist"), true).value(QStringLiteral("ok")).toBool());
    QVERIFY(reaches(expect({
        {QStringLiteral("panes.0.viewId"), QStringLiteral("whale_testlist")},
        {QStringLiteral("panes.0.preferredView"), QStringLiteral("whale_testlist")},
        {QStringLiteral("panes.0.viewOrigin"), QStringLiteral("user")},
    })));
    QCOMPARE(m_automation->preferredView(url).value(QStringLiteral("view")).toString(), QStringLiteral("whale_testlist"));
    QVERIFY(!m_automation->setView(0, QStringLiteral("no_such_view"), true).value(QStringLiteral("ok")).toBool());

    // Set and clear from outside, as another window would.
    QCOMPARE(m_automation->setPreferredView(url + QLatin1Char('/'), QStringLiteral("dolphin")).value(QStringLiteral("view")).toString(),
             QStringLiteral("dolphin"));
    QVERIFY(m_automation->setPreferredView(url, QString()).value(QStringLiteral("view")).isNull());
    QVERIFY(!m_automation->setPreferredView(QString(), QStringLiteral("dolphin")).value(QStringLiteral("ok")).toBool());

    // The plugin hook: the switcher does not offer a missing plugin.
    QVERIFY(m_automation->setViewPluginAvailable(QStringLiteral("whale_hello"), false).value(QStringLiteral("ok")).toBool());
    QVERIFY(!m_automation->setView(0, QStringLiteral("whale_hello"), false).value(QStringLiteral("ok")).toBool());
    QVERIFY(m_automation->setViewPluginAvailable(QStringLiteral("whale_hello"), true).value(QStringLiteral("ok")).toBool());
    QVERIFY(m_automation->setView(0, QStringLiteral("dolphin")).value(QStringLiteral("ok")).toBool());
}

void WhaleDevAutomationTest::testContentScope()
{
    QVERIFY(reaches(expect({
        {QStringLiteral("panes.0.contentScope.scope"), QStringLiteral("current-folder")},
        {QStringLiteral("panes.0.contentScope.shown"), false},
        {QStringLiteral("panes.0.contentScope.supported"), true},
        {QStringLiteral("panes.0.contentScope.pasteRefusal"), QJsonValue::Null},
    })));
    // "Show flat (no folders)", as the user chooses it: the files below, relative to the location.
    QVERIFY(m_automation->setContentScope(0, QStringLiteral("recursive-files")).value(QStringLiteral("ok")).toBool());
    QVERIFY(reaches(expect({
        {QStringLiteral("panes.0.contentScope.shown"), true},
        {QStringLiteral("panes.0.contentScope.origin"), QStringLiteral("user")},
        {QStringLiteral("panes.0.contentScope.preferred"), QStringLiteral("recursive-files")},
        {QStringLiteral("panes.0.loading"), false},
        {QStringLiteral("panes.0.contentScope.relativeItems"), QJsonArray{QStringLiteral("a.txt"), QStringLiteral("b.txt"), QStringLiteral("sub/inner.txt")}},
        {QStringLiteral("panes.0.contentScope.fileCount"), 3},
    })));
    QVERIFY(m_automation->uiState()
                .value(QStringLiteral("panes"))
                .toArray()
                .at(0)
                .toObject()
                .value(QStringLiteral("contentScope"))
                .toObject()
                .value(QStringLiteral("pasteRefusal"))
                .isString());
    QVERIFY(!m_automation->setContentScope(0, QStringLiteral("everything")).value(QStringLiteral("ok")).toBool());

    // Plugin views ignore it; the file list keeps it.
    QVERIFY(m_automation->setView(0, QStringLiteral("whale_testlist")).value(QStringLiteral("ok")).toBool());
    QVERIFY(reaches(expect({
        {QStringLiteral("panes.0.contentScope.shown"), false},
        {QStringLiteral("panes.0.contentScope.supported"), false},
        {QStringLiteral("panes.0.contentScope.scope"), QStringLiteral("recursive-files")},
    })));
    QVERIFY(!m_automation->setContentScope(0, QStringLiteral("current-folder")).value(QStringLiteral("ok")).toBool());
    QVERIFY(m_automation->setView(0, QStringLiteral("dolphin")).value(QStringLiteral("ok")).toBool());
    QVERIFY(reaches(expect({{QStringLiteral("panes.0.contentScope.shown"), true}, {QStringLiteral("panes.0.loading"), false}})));

    QVERIFY(m_automation->setContentScope(0, QStringLiteral("current-folder")).value(QStringLiteral("ok")).toBool());
    QVERIFY(reaches(expect({
        {QStringLiteral("panes.0.contentScope.shown"), false},
        {QStringLiteral("panes.0.contentScope.preferred"), QStringLiteral("current-folder")},
        {QStringLiteral("panes.0.itemCount"), 3},
        {QStringLiteral("panes.0.loading"), false},
    })));
}

void WhaleDevAutomationTest::testSplitPanes()
{
    m_automation->setSplitView(true);
    // The new right view opens animated; until the animation ends, it may be 1 px wide.
    QVERIFY(m_automation->uiState().value(QStringLiteral("splitAnimating")).toBool());
    QVERIFY(reaches(expect({{QStringLiteral("paneCount"), 2},
                            {QStringLiteral("activePane"), 1},
                            {QStringLiteral("panes.1.loading"), false},
                            {QStringLiteral("splitAnimating"), false}})));

    // Dolphin left, test list right.
    QVERIFY(m_automation->setView(1, QStringLiteral("whale_testlist")).value(QStringLiteral("ok")).toBool());
    QVERIFY(reaches(expect({
        {QStringLiteral("panes.0.viewId"), QStringLiteral("dolphin")},
        {QStringLiteral("panes.1.viewId"), QStringLiteral("whale_testlist")},
        {QStringLiteral("focus.role"), QStringLiteral("plugin:whale_testlist")},
        {QStringLiteral("focus.pane"), 1},
    })));

    const int activations = m_automation->uiState().value(QStringLiteral("activations")).toObject().value(QStringLiteral("count")).toInt();
    QVERIFY(m_automation->activatePane(0).value(QStringLiteral("ok")).toBool());
    QVERIFY(reaches(expect({
        {QStringLiteral("activePane"), 0},
        {QStringLiteral("panes.0.active"), true},
        {QStringLiteral("panes.1.active"), false},
        {QStringLiteral("focus.role"), QStringLiteral("dolphin-item-view")},
        {QStringLiteral("focus.pane"), 0},
        {QStringLiteral("focus.anomalies"), QJsonArray()},
    })));
    QCOMPARE(m_automation->uiState().value(QStringLiteral("activations")).toObject().value(QStringLiteral("count")).toInt(), activations + 1);

    QVERIFY(m_automation->triggerAction(QStringLiteral("focus_other_view")).value(QStringLiteral("ok")).toBool());
    QVERIFY(reaches(expect({
        {QStringLiteral("activePane"), 1},
        {QStringLiteral("focus.role"), QStringLiteral("plugin:whale_testlist")},
        {QStringLiteral("focus.pane"), 1},
        {QStringLiteral("focus.anomalies"), QJsonArray()},
    })));

    m_automation->setSplitView(false);
    QVERIFY(reaches(expect({{QStringLiteral("paneCount"), 1}})));
}

void WhaleDevAutomationTest::testHiddenFiles()
{
    QVERIFY(m_automation->setView(0, QStringLiteral("whale_testlist")).value(QStringLiteral("ok")).toBool());
    m_automation->setHiddenFiles(0, true);
    QVERIFY(reaches(expect({{QStringLiteral("panes.0.hiddenFiles"), true}, {QStringLiteral("panes.0.itemCount"), 4}})));
    QVERIFY(strings(m_automation->uiState()[QStringLiteral("panes")][0][QStringLiteral("pluginRows")]).contains(QStringLiteral(".hidden")));

    // A setter, not a toggle: setting it twice keeps it.
    m_automation->setHiddenFiles(0, false);
    m_automation->setHiddenFiles(0, false);
    QVERIFY(reaches(expect({{QStringLiteral("panes.0.hiddenFiles"), false}, {QStringLiteral("panes.0.itemCount"), 3}})));
}

void WhaleDevAutomationTest::testFilter()
{
    QVERIFY(m_automation->setView(0, QStringLiteral("whale_testlist")).value(QStringLiteral("ok")).toBool());
    QVERIFY(m_automation->setFilterText(0, QStringLiteral("b.")).value(QStringLiteral("ok")).toBool());
    QVERIFY(reaches(expect({
        {QStringLiteral("panes.0.filter.text"), QStringLiteral("b.")},
        {QStringLiteral("panes.0.filter.barVisible"), true},
        {QStringLiteral("panes.0.items"), QJsonArray{QStringLiteral("b.txt")}},
        {QStringLiteral("panes.0.pluginRows"), QJsonArray{QStringLiteral("b.txt")}},
        {QStringLiteral("focus.role"), QStringLiteral("filter-bar")},
    })));

    QVERIFY(m_automation->clearFilter(0).value(QStringLiteral("ok")).toBool());
    QVERIFY(reaches(expect({
        {QStringLiteral("panes.0.filter.text"), QString()},
        {QStringLiteral("panes.0.filter.barVisible"), false},
        {QStringLiteral("panes.0.itemCount"), 3},
        {QStringLiteral("focus.role"), QStringLiteral("plugin:whale_testlist")},
    })));
}

void WhaleDevAutomationTest::testSort()
{
    QVERIFY(m_automation->setSort(0, QStringLiteral("size"), QStringLiteral("ascending"), false).value(QStringLiteral("ok")).toBool());
    QVERIFY(reaches(expect({
        {QStringLiteral("panes.0.sort.field"), QStringLiteral("size")},
        {QStringLiteral("panes.0.sort.order"), QStringLiteral("ascending")},
        {QStringLiteral("panes.0.sort.foldersFirst"), false},
    })));
    const QStringList files = strings(m_automation->uiState()[QStringLiteral("panes")][0][QStringLiteral("items")]);
    QVERIFY(files.indexOf(QStringLiteral("b.txt")) < files.indexOf(QStringLiteral("a.txt")));

    QVERIFY(m_automation->setSort(0, QStringLiteral("name"), QStringLiteral("descending"), true).value(QStringLiteral("ok")).toBool());
    QVERIFY(reaches(expect({{QStringLiteral("panes.0.items"), QJsonArray{QStringLiteral("sub"), QStringLiteral("b.txt"), QStringLiteral("a.txt")}}})));

    QVERIFY(!m_automation->setSort(0, QStringLiteral("text"), QString(), std::nullopt).value(QStringLiteral("ok")).toBool());
    QVERIFY(!m_automation->setSort(0, QString(), QStringLiteral("up"), std::nullopt).value(QStringLiteral("ok")).toBool());
}

void WhaleDevAutomationTest::testLocation()
{
    const QString subUrl = QUrl::fromLocalFile(m_dir->path() + QStringLiteral("/sub")).toString();
    QVERIFY(m_automation->setView(0, QStringLiteral("whale_testlist")).value(QStringLiteral("ok")).toBool());
    QVERIFY(m_automation->setLocation(0, subUrl).value(QStringLiteral("ok")).toBool());
    QVERIFY(reaches(expect({
        {QStringLiteral("url"), subUrl},
        {QStringLiteral("panes.0.loading"), false},
        {QStringLiteral("panes.0.pluginRows"), QJsonArray{QStringLiteral("inner.txt")}},
        {QStringLiteral("panes.0.viewId"), QStringLiteral("whale_testlist")},
        {QStringLiteral("focus.role"), QStringLiteral("plugin:whale_testlist")},
    })));

    QVERIFY(m_automation->triggerAction(QStringLiteral("go_back")).value(QStringLiteral("ok")).toBool());
    QVERIFY(reaches(expect({{QStringLiteral("panes.0.loading"), false}, {QStringLiteral("panes.0.itemCount"), 3}})));
    QCOMPARE(QUrl(m_automation->uiState().value(QStringLiteral("url")).toString()).adjusted(QUrl::StripTrailingSlash),
             m_dir->url().adjusted(QUrl::StripTrailingSlash));
}

void WhaleDevAutomationTest::testFocusMatchesVisibleWidget()
{
    DolphinViewContainer *container = m_window->activeViewContainer();
    const QStringList views = {QStringLiteral("whale_testlist"), QStringLiteral("whale_hello"), QStringLiteral("dolphin")};
    for (const QString &viewId : views) {
        QVERIFY(m_automation->setView(0, viewId).value(QStringLiteral("ok")).toBool());
        const QString role = viewId == QLatin1String("dolphin") ? QStringLiteral("dolphin-item-view") : QStringLiteral("plugin:") + viewId;
        QVERIFY(reaches(expect({{QStringLiteral("focus.role"), role}})));

        // The reported role belongs to the widget that really has the focus and is visible.
        QWidget *focus = QApplication::focusWidget();
        QVERIFY(focus && focus->isVisible());
        WhaleViewHost *host = container->viewHost();
        QWidget *visibleView = host->isDolphinViewShown() ? static_cast<QWidget *>(container->view()) : host->currentView()->widget();
        QVERIFY(visibleView == focus || visibleView->isAncestorOf(focus));
    }

    QVERIFY(m_automation->triggerAction(QStringLiteral("edit_location")).value(QStringLiteral("ok")).toBool());
    QVERIFY(reaches(expect({{QStringLiteral("focus.role"), QStringLiteral("location-bar")}, {QStringLiteral("focus.pane"), 0}})));
}

void WhaleDevAutomationTest::testOnlyNonDestructiveActions()
{
    const QStringList ids = WhaleDevAutomation::actionIds();
    QVERIFY(ids.contains(QStringLiteral("focus_other_view")));
    QVERIFY(ids.contains(QStringLiteral("go_up")));
    QVERIFY(ids.contains(QStringLiteral("rename")));

    // File operations are offered, but refused unless Whale runs for a test profile.
    qunsetenv("WHALE_DEV_MCP_FILE_OPERATIONS");
    for (const QString &id : {QStringLiteral("copy"),
                              QStringLiteral("cut"),
                              QStringLiteral("paste"),
                              QStringLiteral("trash"),
                              QStringLiteral("delete"),
                              QStringLiteral("duplicate"),
                              QStringLiteral("copy_location"),
                              QStringLiteral("copy_to_other_pane"),
                              QStringLiteral("move_to_other_pane")}) {
        QVERIFY(ids.contains(id));
        const QJsonObject result = m_automation->triggerAction(id);
        QVERIFY(!result.value(QStringLiteral("ok")).toBool());
        QVERIFY2(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("WHALE_DEV_MCP_FILE_OPERATIONS")), qPrintable(id));
    }
    QVERIFY(!m_automation->answerRenameDialog(QStringLiteral("x")).value(QStringLiteral("ok")).toBool());
    // Drag and drop and popup entries may change files too (M7).
    for (const QJsonObject &result :
         {m_automation->performDragDrop(0, 0, QString(), QStringLiteral("copy")), m_automation->choosePopupEntry(QStringLiteral("Copy Here"))}) {
        QVERIFY(!result.value(QStringLiteral("ok")).toBool());
        QVERIFY(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("WHALE_DEV_MCP_FILE_OPERATIONS")));
    }

    // Whale's own action names are never IDs (the IDs above are refused without a test profile).
    const QStringList forbidden = {QStringLiteral("move_to_trash"),
                                   QStringLiteral("create_dir"),
                                   QStringLiteral("edit_cut"),
                                   QStringLiteral("edit_paste"),
                                   QStringLiteral("deletefile"),
                                   QStringLiteral("compress")};
    for (const QString &id : forbidden) {
        QVERIFY2(!ids.contains(id), qPrintable(id));
        const QJsonObject result = m_automation->triggerAction(id);
        QVERIFY(!result.value(QStringLiteral("ok")).toBool());
        QVERIFY(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("not allowed")));
    }
}

void WhaleDevAutomationTest::testSetSelection()
{
    const QString a = QUrl::fromLocalFile(m_dir->path() + QStringLiteral("/a.txt")).toString();
    const QString b = QUrl::fromLocalFile(m_dir->path() + QStringLiteral("/b.txt")).toString();
    const QString inner = QUrl::fromLocalFile(m_dir->path() + QStringLiteral("/sub/inner.txt")).toString();
    QVERIFY(m_automation->setView(0, QStringLiteral("whale_testlist")).value(QStringLiteral("ok")).toBool());

    const QJsonObject reply = m_automation->setSelection(0, {a, inner, b}, b);
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QCOMPARE(strings(reply.value(QStringLiteral("selection"))), QStringList({a, b}));
    QCOMPARE(strings(reply.value(QStringLiteral("ignored"))), QStringList({inner}));
    QVERIFY(reaches(expect({
        {QStringLiteral("panes.0.selectedNames"), QJsonArray{QStringLiteral("a.txt"), QStringLiteral("b.txt")}},
        {QStringLiteral("panes.0.currentName"), QStringLiteral("b.txt")},
        {QStringLiteral("panes.0.pluginSelection"), QJsonArray{QStringLiteral("a.txt"), QStringLiteral("b.txt")}},
        {QStringLiteral("panes.0.pluginCurrent"), QStringLiteral("b.txt")},
        {QStringLiteral("shell.selectedNames"), QJsonArray{QStringLiteral("a.txt"), QStringLiteral("b.txt")}},
        {QStringLiteral("panes.0.statusText"), QJsonObject{{QStringLiteral("$contains"), QStringLiteral("2")}}},
        {QStringLiteral("panes.0.selection"), QJsonObject{{QStringLiteral("$contains"), a}}},
    })));
    QVERIFY(!m_automation->evaluate(expect({{QStringLiteral("panes.0.selection"), QJsonObject{{QStringLiteral("$contains"), inner}}}}))
                 .value(QStringLiteral("matched"))
                 .toBool());

    // Activation through the same host API: entering a folder.
    const QString sub = QUrl::fromLocalFile(m_dir->path() + QStringLiteral("/sub")).toString();
    const QString typed = m_dir->path() + QStringLiteral("/sub");
    QVERIFY(m_automation->activateItems(0, {sub}).value(QStringLiteral("ok")).toBool());
    QVERIFY(reaches(expect({{QStringLiteral("url"), sub}, {QStringLiteral("panes.0.viewId"), QStringLiteral("whale_testlist")}})));
}

void WhaleDevAutomationTest::testWaitForState()
{
    QJsonObject result;
    bool done = false;
    m_automation->waitForState(expect({{QStringLiteral("panes.0.viewId"), QStringLiteral("whale_testlist")}}), 5000, [&](const QJsonObject &r) {
        result = r;
        done = true;
    });
    QVERIFY(!done); // never synchronous
    m_automation->setView(0, QStringLiteral("whale_testlist"));
    QTRY_VERIFY(done);
    QCOMPARE(result.value(QStringLiteral("matched")).toBool(), true);
    QCOMPARE(result.value(QStringLiteral("timedOut")).toBool(), false);
    QVERIFY(result.value(QStringLiteral("state")).isObject());

    done = false;
    m_automation->waitForState(expect({{QStringLiteral("panes.0.viewId"), QStringLiteral("never")}}), 200, [&](const QJsonObject &r) {
        result = r;
        done = true;
    });
    QTRY_VERIFY(done);
    QCOMPARE(result.value(QStringLiteral("matched")).toBool(), false);
    QCOMPARE(result.value(QStringLiteral("timedOut")).toBool(), true);
    const QJsonObject mismatch = result.value(QStringLiteral("mismatches")).toArray().at(0).toObject();
    QCOMPARE(mismatch.value(QStringLiteral("path")).toString(), QStringLiteral("panes.0.viewId"));
    QCOMPARE(mismatch.value(QStringLiteral("actual")).toString(), QStringLiteral("whale_testlist"));
}

void WhaleDevAutomationTest::testScreenshot()
{
    const QImage image = QImage::fromData(m_automation->screenshot(), "PNG");
    QVERIFY(!image.isNull());
    QCOMPARE(image.size(), m_window->size() * m_window->devicePixelRatio());
}

namespace
{
QJsonObject paneToolbar(const QJsonObject &state, int pane)
{
    return state.value(QStringLiteral("panes")).toArray().at(pane).toObject().value(QStringLiteral("toolbar")).toObject();
}

QStringList actionKeys(const QJsonObject &toolbar)
{
    QStringList keys;
    const QJsonArray actions = toolbar.value(QStringLiteral("actions")).toArray();
    for (const QJsonValue &action : actions) {
        keys.append(action.toObject().value(QStringLiteral("actionKey")).toString());
    }
    return keys;
}

QString errorOf(const QJsonObject &result)
{
    return result.value(QStringLiteral("ok")).toBool() ? QString() : result.value(QStringLiteral("error")).toString();
}
}

void WhaleDevAutomationTest::testToolbarState()
{
    QVERIFY(reaches(expect({{QStringLiteral("panes.0.toolbar.initialized"), true}})));
    const QJsonObject state = m_automation->uiState();
    const QJsonObject toolbar = paneToolbar(state, 0);
    const QString id = toolbar.value(QStringLiteral("instanceId")).toString();
    QVERIFY(id.startsWith(QLatin1String("view-")));
    QCOMPARE(toolbar.value(QStringLiteral("ownerView")).toString(), QStringLiteral("dolphin"));
    QCOMPARE(toolbar.value(QStringLiteral("visible")).toBool(), true);
    QCOMPARE(strings(toolbar.value(QStringLiteral("layout"))),
             QStringList({QStringLiteral("go_back"),
                          QStringLiteral("go_forward"),
                          QStringLiteral("go_up"),
                          QStringLiteral("view_redisplay"),
                          QStringLiteral("whale_location_bar"),
                          QStringLiteral("view_settings")}));
    // The same ID and the same objects as long as the view lives.
    const QJsonObject again = paneToolbar(m_automation->uiState(), 0);
    QCOMPARE(again.value(QStringLiteral("instanceId")).toString(), id);
    QCOMPARE(actionKeys(again), actionKeys(toolbar));
    const QJsonObject toolbars = state.value(QStringLiteral("toolbars")).toObject();
    QCOMPARE(toolbars.value(QStringLiteral("global")).toObject().value(QStringLiteral("visible")).toBool(), true);
    QCOMPARE(toolbars.value(QStringLiteral("fileViewsVisible")).toBool(), true);
    QCOMPARE(toolbars.value(QStringLiteral("instances")).toArray().count(), 1);
    QCOMPARE(state.value(QStringLiteral("toolbarDialog")).toObject(), QJsonObject({{QStringLiteral("open"), false}}));

    // A view that is created hidden has no actions; reading the state does not create them.
    WhaleToolbarSettings::self()->setFileViewToolBarsVisible(false);
    QVERIFY(m_automation->setSplitView(true).value(QStringLiteral("ok")).toBool());
    QVERIFY(reaches(expect({{QStringLiteral("paneCount"), 2}, {QStringLiteral("panes.1.toolbar.visible"), false}})));
    for (int i = 0; i < 3; ++i) {
        QCOMPARE(paneToolbar(m_automation->uiState(), 1).value(QStringLiteral("initialized")).toBool(), false);
    }
    QVERIFY(paneToolbar(m_automation->uiState(), 1).value(QStringLiteral("actions")).toArray().isEmpty());
    QVERIFY(paneToolbar(m_automation->uiState(), 1).value(QStringLiteral("instanceId")).toString() != id);
}

void WhaleDevAutomationTest::testToolbarErrors()
{
    QVERIFY(reaches(expect({{QStringLiteral("panes.0.toolbar.initialized"), true}})));
    QVERIFY(
        errorOf(m_automation->triggerToolbarAction(QStringLiteral("view-999999"), QStringLiteral("go_up"), QString())).contains(QStringLiteral("instanceId")));
    QVERIFY(errorOf(m_automation->triggerToolbarAction(QStringLiteral("3"), QStringLiteral("go_up"), QString())).contains(QStringLiteral("no pane")));
    QVERIFY(
        errorOf(m_automation->triggerToolbarAction(QStringLiteral("0"), QStringLiteral("split_view"), QString())).contains(QStringLiteral("unknown action")));
    QVERIFY(
        errorOf(m_automation->triggerToolbarAction(QStringLiteral("0"), QStringLiteral("go_home"), QString())).contains(QStringLiteral("not in this toolbar")));
    // Nothing to go forward to.
    QVERIFY(errorOf(m_automation->triggerToolbarAction(QStringLiteral("0"), QStringLiteral("go_forward"), QString())).contains(QStringLiteral("disabled")));
    QVERIFY(
        errorOf(m_automation->triggerToolbarAction(QStringLiteral("0"), QStringLiteral("whale_location_bar"), QString())).contains(QStringLiteral("widget")));
    QVERIFY(errorOf(m_automation->triggerToolbarAction(QStringLiteral("0"), QStringLiteral("go_up"), QStringLiteral("x"))).contains(QStringLiteral("no menu")));
    QVERIFY(errorOf(m_automation->configureToolbars({{QStringLiteral("apply"), true}})).contains(QStringLiteral("not open")));
    QVERIFY(errorOf(m_automation->configureToolbars({{QStringLiteral("select"), QStringLiteral("left")}})).contains(QStringLiteral("unknown toolbar")));
    // Up works: the view still reacts after all of them.
    QVERIFY(errorOf(m_automation->triggerToolbarAction(QStringLiteral("0"), QStringLiteral("go_up"), QString())).isEmpty());
    QVERIFY(reaches(expect({{QStringLiteral("panes.0.toolbar.actions.1.enabled"), false}, {QStringLiteral("panes.0.toolbar.actions.0.enabled"), true}})));
}

void WhaleDevAutomationTest::testToolbarsOfTwoWindows()
{
    // Two main windows of one process (Whale's "New Window" starts a new process).
    QScopedPointer<DolphinMainWindow> other(new DolphinMainWindow());
    other->openDirectories({QUrl::fromLocalFile(m_dir->path() + QStringLiteral("/sub"))}, false);
    other->resize(1000, 700);
    other->show();
    QVERIFY(QTest::qWaitForWindowActive(other.data()));
    auto otherAutomation = new WhaleDevAutomation(other.data());
    QVERIFY(QTest::qWaitFor([otherAutomation]() {
        return otherAutomation->evaluate(QJsonObject{{QStringLiteral("panes.0.toolbar.initialized"), true}, {QStringLiteral("panes.0.loading"), false}})
            .value(QStringLiteral("matched"))
            .toBool();
    }));
    QVERIFY(reaches(expect({{QStringLiteral("panes.0.toolbar.initialized"), true}})));
    const QJsonObject mine = paneToolbar(m_automation->uiState(), 0);
    const QJsonObject theirs = paneToolbar(otherAutomation->uiState(), 0);
    const QString myId = mine.value(QStringLiteral("instanceId")).toString();
    const QString theirId = theirs.value(QStringLiteral("instanceId")).toString();
    QVERIFY(myId != theirId);
    for (const QString &key : actionKeys(mine)) {
        QVERIFY(!actionKeys(theirs).contains(key));
    }
    // Each window knows its own views only.
    QVERIFY(errorOf(m_automation->triggerToolbarAction(theirId, QStringLiteral("go_up"), QString())).contains(QStringLiteral("another window")));
    QCOMPARE(otherAutomation->uiState().value(QStringLiteral("toolbars")).toObject().value(QStringLiteral("instances")).toArray().count(), 1);

    // Up in the other window's toolbar: only that window's view goes up.
    const QString myUrl = m_automation->uiState().value(QStringLiteral("url")).toString();
    QVERIFY(errorOf(otherAutomation->triggerToolbarAction(theirId, QStringLiteral("go_up"), QString())).isEmpty());
    QVERIFY(QTest::qWaitFor([otherAutomation, this]() {
        const QString url = otherAutomation->uiState().value(QStringLiteral("url")).toString();
        return QUrl(url).adjusted(QUrl::StripTrailingSlash) == m_dir->url().adjusted(QUrl::StripTrailingSlash);
    }));
    QCOMPARE(m_automation->uiState().value(QStringLiteral("url")).toString(), myUrl);

    // "Show File View Toolbars" is one setting for both windows.
    QVERIFY(m_automation->triggerAction(QStringLiteral("whale_show_file_view_toolbars")).value(QStringLiteral("ok")).toBool());
    QCOMPARE(paneToolbar(otherAutomation->uiState(), 0).value(QStringLiteral("visible")).toBool(), false);
    QCOMPARE(otherAutomation->uiState().value(QStringLiteral("toolbars")).toObject().value(QStringLiteral("fileViewsVisible")).toBool(), false);
    QVERIFY(errorOf(otherAutomation->triggerToolbarAction(theirId, QStringLiteral("go_back"), QString())).contains(QStringLiteral("not shown")));
}

void WhaleDevAutomationTest::testLocationBarState()
{
    QVERIFY(reaches(expect({{QStringLiteral("panes.0.toolbar.locationBar.visible"), true}})));
    QVERIFY(reaches(expect({{QStringLiteral("panes.0.toolbar.locationBar.exists"), true},
                            {QStringLiteral("panes.0.toolbar.locationBar.connected"), true},
                            {QStringLiteral("panes.0.toolbar.locationBar.present"), true},
                            {QStringLiteral("panes.0.toolbar.locationBar.editable"), false},
                            {QStringLiteral("panes.0.toolbar.locationBar.active"), true},
                            {QStringLiteral("panes.0.toolbar.locationBar.text"), QString()}})));
    const QJsonObject first = paneToolbar(m_automation->uiState(), 0).value(QStringLiteral("locationBar")).toObject();
    QCOMPARE(QUrl(first.value(QStringLiteral("url")).toString()).adjusted(QUrl::StripTrailingSlash), m_dir->url());
    const QString key = first.value(QStringLiteral("widgetKey")).toString();
    QVERIFY(key.startsWith(QLatin1String("location-bar-")));

    // Ctrl+L through the window's action: editable, focused.
    QVERIFY(errorOf(m_automation->triggerAction(QStringLiteral("edit_location"))).isEmpty());
    QVERIFY(reaches(expect({{QStringLiteral("panes.0.toolbar.locationBar.editable"), true},
                            {QStringLiteral("panes.0.toolbar.locationBar.focused"), true},
                            {QStringLiteral("focus.role"), QStringLiteral("location-bar")}})));

    // A tab never shown: its location bar exists and is connected, not placed; nothing built.
    WhaleToolbarSettings::self()->setFileViewToolBarsVisible(false);
    QVERIFY(errorOf(m_automation->triggerAction(QStringLiteral("new_tab"))).isEmpty());
    QVERIFY(reaches(expect({{QStringLiteral("tabs.count"), 2}, {QStringLiteral("panes.0.toolbar.locationBar.visible"), false}})));
    const QJsonArray instances = m_automation->uiState().value(QStringLiteral("toolbars")).toObject().value(QStringLiteral("instances")).toArray();
    QCOMPARE(instances.count(), 2);
    QStringList keys;
    for (const QJsonValue &value : instances) {
        const QJsonObject instance = value.toObject();
        const QJsonObject locationBar = instance.value(QStringLiteral("locationBar")).toObject();
        QVERIFY(locationBar.value(QStringLiteral("exists")).toBool());
        QVERIFY(locationBar.value(QStringLiteral("connected")).toBool());
        keys.append(locationBar.value(QStringLiteral("widgetKey")).toString());
        if (instance.value(QStringLiteral("tab")).toInt() == 1) {
            QVERIFY(!instance.value(QStringLiteral("initialized")).toBool());
            QVERIFY(!locationBar.value(QStringLiteral("present")).toBool());
        }
    }
    QCOMPARE(keys.count(), 2);
    QVERIFY(keys.at(0) != keys.at(1));
    QVERIFY(keys.contains(key));
    // Reading the state built nothing.
    QCOMPARE(paneToolbar(m_automation->uiState(), 0).value(QStringLiteral("initialized")).toBool(), false);
}

void WhaleDevAutomationTest::testLocationBarInput()
{
    QVERIFY(reaches(expect({{QStringLiteral("panes.0.toolbar.locationBar.visible"), true}})));
    const QString sub = QUrl::fromLocalFile(m_dir->path() + QStringLiteral("/sub")).toString();
    const QString typed = m_dir->path() + QStringLiteral("/sub");
    // Typed, not submitted: editable with the text, the view stays.
    const QJsonObject typing = m_automation->locationBarInput(QStringLiteral("0"), typed, false);
    QVERIFY2(errorOf(typing).isEmpty(), qPrintable(errorOf(typing)));
    QVERIFY(typing.value(QStringLiteral("text")).toString().startsWith(typed));
    QVERIFY(reaches(expect({{QStringLiteral("panes.0.toolbar.locationBar.editable"), true},
                            {QStringLiteral("panes.0.toolbar.locationBar.text"), QJsonObject{{QStringLiteral("$contains"), typed}}},
                            {QStringLiteral("panes.0.toolbar.locationBar.focused"), true},
                            {QStringLiteral("url"), m_dir->url().toString()}})));
    // Submitted: the view goes there; the location bar shows it.
    QVERIFY(errorOf(m_automation->locationBarInput(QStringLiteral("active"), typed, true)).isEmpty());
    // (with the "/" KDE's completion added to the folder's name)
    const QString folder = sub + QLatin1Char('/');
    QVERIFY(reaches(expect({{QStringLiteral("url"), folder}, {QStringLiteral("panes.0.toolbar.locationBar.url"), folder}})));
    // Back in the breadcrumb after Return (Whale's default).
    QVERIFY(reaches(expect({{QStringLiteral("panes.0.toolbar.locationBar.editable"), false}})));
    // From the breadcrumb (a click on its toggle button), not submitted: the view stays. (Not
    // a location of the editor's list: an editor that loses the focus with such a text goes
    // there, KUrlComboBox/QComboBox behaviour.)
    const QString other = m_dir->path() + QStringLiteral("/a.txt");
    const QJsonObject again = m_automation->locationBarInput(QStringLiteral("0"), other, false);
    QVERIFY2(errorOf(again).isEmpty(), qPrintable(errorOf(again)));
    QVERIFY(reaches(expect({{QStringLiteral("url"), folder},
                            {QStringLiteral("panes.0.toolbar.locationBar.editable"), true},
                            {QStringLiteral("panes.0.toolbar.locationBar.text"), QJsonObject{{QStringLiteral("$contains"), other}}}})));
}

void WhaleDevAutomationTest::testLocationBarErrors()
{
    QVERIFY(reaches(expect({{QStringLiteral("panes.0.toolbar.locationBar.visible"), true}})));
    QVERIFY(errorOf(m_automation->locationBarInput(QStringLiteral("3"), QStringLiteral("/"), true)).contains(QStringLiteral("no pane")));
    QVERIFY(errorOf(m_automation->locationBarInput(QStringLiteral("view-999999"), QStringLiteral("/"), true)).contains(QStringLiteral("instanceId")));
    QVERIFY(errorOf(m_automation->locationBarInput(QStringLiteral("global"), QStringLiteral("/"), true)).contains(QStringLiteral("main toolbar")));
    // Hidden toolbars: not shown, and nothing changes.
    WhaleToolbarSettings::self()->setFileViewToolBarsVisible(false);
    QVERIFY(errorOf(m_automation->locationBarInput(QStringLiteral("0"), QStringLiteral("/"), true)).contains(QStringLiteral("not shown")));
    QVERIFY(reaches(expect({{QStringLiteral("url"), m_dir->url().toString()}, {QStringLiteral("panes.0.toolbar.locationBar.editable"), false}})));
    WhaleToolbarSettings::self()->setFileViewToolBarsVisible(true);
    // The location bar is a widget, no command of the toolbar.
    QVERIFY(
        errorOf(m_automation->triggerToolbarAction(QStringLiteral("0"), QStringLiteral("whale_location_bar"), QString())).contains(QStringLiteral("widget")));
}

QTEST_MAIN(WhaleDevAutomationTest)

#include "whaledevautomationtest.moc"
