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

void WhaleDevAutomationTest::testSplitPanes()
{
    m_automation->setSplitView(true);
    QVERIFY(reaches(expect({{QStringLiteral("paneCount"), 2}, {QStringLiteral("activePane"), 1}, {QStringLiteral("panes.1.loading"), false}})));

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

QTEST_MAIN(WhaleDevAutomationTest)

#include "whaledevautomationtest.moc"
