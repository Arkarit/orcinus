/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaicanvasmedia.h"
#include "whaleaicanvaspanel.h"
#include "whaleaiqmlguard.h"
#include "whaleaiquickwidget.h"
#include "whaleaitoolapi.h"
#include "whaleaitoolstore.h"
#include "whaleaitooltab.h"

#include "config-dolphin.h"

#include <KConfigGroup>
#include <KIO/FileUndoManager>

#include <QDir>
#include <QFile>
#include <QMainWindow>
#include <QQmlEngine>
#include <QQuickItem>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <functional>

/**
 * The tools of the AI canvas (CV2): the store and the trust step, the "orcinus" object, the import
 * guard, and the example tool Sound Triage working end to end.
 */
class WhaleAiToolTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();
    void testStore();
    void testGuard();
    void testApiReading();
    void testApiChanging();
    void testApiSettingsAndContext();
    void testSoundTriage();
    void testKeepAndTrust();
    void testBrokenTool();

private:
    QString writeFile(const QString &path, const QByteArray &data);
    void writeWav(const QString &path);
    void writeTool(const QString &id, const QByteArray &qml, const QByteArray &manifest = QByteArray());
    /** Runs @p script with "orcinus" set; it must set "result" when done. @return result. */
    QJSValue run(const QString &script);
    QQuickItem *findItem(QQuickItem *root, const QString &objectName) const;

    QTemporaryDir m_dir;
    QString m_tools;
    QString m_files;
    KSharedConfigPtr m_config;
    WhaleAiToolStore m_store;
    QList<QUrl> m_revealed;
    WhaleAiToolHost m_host;
    QQmlEngine *m_engine = nullptr;
    WhaleAiToolApi *m_api = nullptr;
};

QString WhaleAiToolTest::writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    file.open(QIODevice::WriteOnly);
    file.write(data);
    return path;
}

void WhaleAiToolTest::writeWav(const QString &path)
{
    // One second of 8 kHz, 8 bit mono silence.
    const quint32 dataSize = 8000;
    QByteArray wav("RIFF");
    auto u32 = [&wav](quint32 v) {
        wav.append(reinterpret_cast<const char *>(&v), 4);
    };
    auto u16 = [&wav](quint16 v) {
        wav.append(reinterpret_cast<const char *>(&v), 2);
    };
    u32(36 + dataSize);
    wav.append("WAVEfmt ");
    u32(16);
    u16(1);
    u16(1);
    u32(8000);
    u32(8000);
    u16(1);
    u16(8);
    wav.append("data");
    u32(dataSize);
    wav.append(QByteArray(dataSize, char(128)));
    writeFile(path, wav);
}

void WhaleAiToolTest::writeTool(const QString &id, const QByteArray &qml, const QByteArray &manifest)
{
    const QString folder = m_tools + QLatin1Char('/') + id;
    writeFile(folder + QStringLiteral("/tool.json"), manifest.isEmpty() ? QByteArray("{\"name\": \"") + id.toUtf8() + "\"}" : manifest);
    writeFile(folder + QStringLiteral("/main.qml"), qml);
}

QQuickItem *WhaleAiToolTest::findItem(QQuickItem *root, const QString &objectName) const
{
    if (!root) {
        return nullptr;
    }
    if (root->objectName() == objectName) {
        return root;
    }
    const auto children = root->childItems();
    for (QQuickItem *child : children) {
        if (QQuickItem *found = findItem(child, objectName)) {
            return found;
        }
    }
    return nullptr;
}

void WhaleAiToolTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QVERIFY(m_dir.isValid());
    m_tools = m_dir.filePath(QStringLiteral("tools"));
    m_files = m_dir.filePath(QStringLiteral("files"));
    // The example tool, as it is in the source tree.
    const QDir source(QStringLiteral(WHALE_AI_TEST_TOOLS "/sound-triage"));
    for (const QString &name : source.entryList(QDir::Files)) {
        QDir().mkpath(m_tools + QStringLiteral("/sound-triage"));
        QVERIFY(QFile::copy(source.filePath(name), m_tools + QStringLiteral("/sound-triage/") + name));
    }
    writeWav(m_files + QStringLiteral("/b.wav"));
    writeWav(m_files + QStringLiteral("/a.wav"));
    writeWav(m_files + QStringLiteral("/sub/c.wav"));
    writeWav(m_files + QStringLiteral("/.hidden.wav"));
    writeFile(m_files + QStringLiteral("/notes.txt"), "hello");
    QDir().mkpath(m_dir.filePath(QStringLiteral("target")));

    m_config = KSharedConfig::openConfig(m_dir.filePath(QStringLiteral("toolsrc")), KConfig::SimpleConfig);
    m_store = WhaleAiToolStore(m_tools, m_config);
    m_host = {
        [this] {
            return QUrl::fromLocalFile(m_files);
        },
        [this] {
            return QList<QUrl>{QUrl::fromLocalFile(m_files + QStringLiteral("/a.wav"))};
        },
        [] {
            return QUrl();
        },
        [this](const QUrl &url) {
            m_revealed.append(url);
        },
    };
}

void WhaleAiToolTest::init()
{
    m_revealed.clear();
    const auto tool = m_store.tool(QStringLiteral("sound-triage"));
    QVERIFY(tool);
    m_engine = new QQmlEngine;
    m_api = new WhaleAiToolApi(*tool, m_store, m_host, nullptr);
    m_api->setEngine(m_engine);
    m_engine->globalObject().setProperty(QStringLiteral("orcinus"), m_engine->newQObject(m_api));
    QQmlEngine::setObjectOwnership(m_api, QQmlEngine::CppOwnership);
}

void WhaleAiToolTest::cleanup()
{
    delete m_engine;
    m_engine = nullptr;
    delete m_api;
    m_api = nullptr;
}

QJSValue WhaleAiToolTest::run(const QString &script)
{
    m_engine->globalObject().setProperty(QStringLiteral("result"), QJSValue());
    const QJSValue value = m_engine->evaluate(script);
    if (value.isError()) {
        qWarning() << value.toString();
        return value;
    }
    QTest::qWaitFor(
        [this] {
            return !m_engine->globalObject().property(QStringLiteral("result")).isUndefined();
        },
        5000);
    return m_engine->globalObject().property(QStringLiteral("result"));
}

void WhaleAiToolTest::testStore()
{
    QCOMPARE(m_store.ids(), QStringList{QStringLiteral("sound-triage")});
    const auto tool = m_store.tool(QStringLiteral("sound-triage"));
    QCOMPARE(tool->name, QStringLiteral("Sound Triage"));
    QCOMPARE(tool->settings.value(QLatin1String("target")).toString(), QString());
    QCOMPARE(tool->writes.size(), 1);
    QCOMPARE(m_store.files(QStringLiteral("sound-triage")), QStringList({QStringLiteral("main.qml"), QStringLiteral("tool.json")}));

    // Manifests that are refused.
    QString error;
    QVERIFY(!m_store.tool(QStringLiteral("../etc"), &error));
    QVERIFY(!m_store.tool(QStringLiteral("Upper"), &error));
    writeTool(QStringLiteral("no-name"), "import QtQuick\nItem {}", "{}");
    QVERIFY(!m_store.tool(QStringLiteral("no-name"), &error));
    QVERIFY(error.contains(QLatin1String("name")));
    writeTool(QStringLiteral("outside"), "import QtQuick\nItem {}", "{\"name\": \"x\", \"entry\": \"../sound-triage/main.qml\"}");
    QVERIFY(!m_store.tool(QStringLiteral("outside"), &error));
    writeTool(QStringLiteral("not-qml"), "import QtQuick\nItem {}", "{\"name\": \"x\", \"entry\": \"tool.json\"}");
    QVERIFY(!m_store.tool(QStringLiteral("not-qml"), &error));
    writeFile(m_tools + QStringLiteral("/broken-json/tool.json"), "{name");
    QVERIFY(!m_store.tool(QStringLiteral("broken-json"), &error));
    QCOMPARE(m_store.ids(), QStringList{QStringLiteral("sound-triage")});
    for (const QString &id : {QStringLiteral("no-name"), QStringLiteral("outside"), QStringLiteral("not-qml"), QStringLiteral("broken-json")}) {
        QDir(m_tools + QLatin1Char('/') + id).removeRecursively();
    }

    // The fingerprint follows the files, but not hidden ones.
    const QString before = m_store.fingerprint(QStringLiteral("sound-triage"));
    writeFile(m_tools + QStringLiteral("/sound-triage/.cache/x"), "cache");
    QCOMPARE(m_store.fingerprint(QStringLiteral("sound-triage")), before);
    writeFile(m_tools + QStringLiteral("/sound-triage/extra.qml"), "import QtQuick\nItem {}");
    QVERIFY(m_store.fingerprint(QStringLiteral("sound-triage")) != before);
    QFile::remove(m_tools + QStringLiteral("/sound-triage/extra.qml"));
    QDir(m_tools + QStringLiteral("/sound-triage/.cache")).removeRecursively();
    QCOMPARE(m_store.fingerprint(QStringLiteral("sound-triage")), before);
}

void WhaleAiToolTest::testGuard()
{
    QVERIFY(WhaleAiQmlGuard::allows(QStringLiteral("/usr/lib/qt6/qml/QtQuick/Controls/qmldir")));
    QVERIFY(WhaleAiQmlGuard::allows(QStringLiteral("/usr/lib/qt6/qml/org/kde/desktop/qmldir")));
    QVERIFY(!WhaleAiQmlGuard::allows(QStringLiteral("/usr/lib/qt6/qml/Qt/labs/folderlistmodel/qmldir")));
    QVERIFY(!WhaleAiQmlGuard::allows(QStringLiteral("/usr/lib/qt6/qml/QtQuick/LocalStorage/qmldir")));
    QVERIFY(!WhaleAiQmlGuard::allows(QStringLiteral("/usr/lib/qt6/qml/QtCore/qmldir")));

    // A tool that imports a module of its own for files does not load, and says why.
    writeTool(QStringLiteral("lister"), "import QtQuick\nimport Qt.labs.folderlistmodel\nItem { FolderListModel {} }");
    const auto tool = m_store.tool(QStringLiteral("lister"));
    QVERIFY(tool);
    WhaleAiToolTab tab(*tool, m_store, m_host);
    tab.load();
    QVERIFY(!tab.isLoaded());
    QVERIFY2(tab.log().join(QLatin1Char('\n')).contains(QLatin1String("folderlistmodel")), qPrintable(tab.log().join(QLatin1Char('\n'))));
    QDir(m_tools + QStringLiteral("/lister")).removeRecursively();
}

void WhaleAiToolTest::testApiReading()
{
    const QString folder = QUrl::fromLocalFile(m_files).toString();
    // Recursive, filtered by MIME type, hidden files left out.
    QJSValue result = run(QStringLiteral("orcinus.list('%1', {recursive: true, mime: 'audio/*'}, function(items, error) {"
                                         " result = {names: items.map(i => i.name).sort(), error: error, item: items[0]} })")
                              .arg(folder));
    QCOMPARE(result.property(QStringLiteral("error")).isNull(), true);
    QCOMPARE(result.property(QStringLiteral("names")).toVariant().toStringList(),
             QStringList({QStringLiteral("a.wav"), QStringLiteral("b.wav"), QStringLiteral("c.wav")}));
    const QJSValue item = result.property(QStringLiteral("item"));
    QVERIFY(item.property(QStringLiteral("url")).toString().startsWith(QLatin1String("file:///")));
    QCOMPARE(item.property(QStringLiteral("size")).toInt(), 8044);
    QVERIFY(item.property(QStringLiteral("mime")).toString().startsWith(QLatin1String("audio/")));

    // Not recursive, everything, with hidden ones; a limit.
    result = run(QStringLiteral("orcinus.list('%1', {hidden: true}, function(items, error) { result = items.map(i => i.name).sort() })").arg(m_files));
    QCOMPARE(
        result.toVariant().toStringList(),
        QStringList({QStringLiteral(".hidden.wav"), QStringLiteral("a.wav"), QStringLiteral("b.wav"), QStringLiteral("notes.txt"), QStringLiteral("sub")}));
    result = run(QStringLiteral("orcinus.list('%1', {limit: 2}, function(items, error) { result = items.length })").arg(m_files));
    QCOMPARE(result.toInt(), 2);
    result = run(QStringLiteral("orcinus.list('relative/path', {}, function(items, error) { result = error })"));
    QVERIFY(result.isString());

    // One item; a text file; a size limit.
    result = run(QStringLiteral("orcinus.item('%1/notes.txt', function(item, error) { result = item })").arg(m_files));
    QCOMPARE(result.property(QStringLiteral("name")).toString(), QStringLiteral("notes.txt"));
    QCOMPARE(result.property(QStringLiteral("isDir")).toBool(), false);
    result = run(QStringLiteral("orcinus.readText('%1/notes.txt', function(text, error) { result = text })").arg(m_files));
    QCOMPARE(result.toString(), QStringLiteral("hello"));
    result = run(QStringLiteral("orcinus.readText('%1/a.wav', function(text, error) { result = error }, 100)").arg(m_files));
    QVERIFY(result.toString().contains(QLatin1String("larger")));
}

void WhaleAiToolTest::testApiChanging()
{
    const QString target = m_dir.filePath(QStringLiteral("target"));
    // Copy, with undo.
    QJSValue result = run(QStringLiteral("orcinus.copy(['%1/a.wav'], '%2', function(ok, error) { result = ok })").arg(m_files, target));
    QVERIFY(result.toBool());
    QVERIFY(QFile::exists(target + QStringLiteral("/a.wav")));
    QVERIFY(KIO::FileUndoManager::self()->isUndoAvailable());

    // Rename, a new folder, move into it.
    result = run(QStringLiteral("orcinus.rename('%1/a.wav', 'renamed.wav', function(ok, error) { result = ok })").arg(target));
    QVERIFY(result.toBool());
    QVERIFY(QFile::exists(target + QStringLiteral("/renamed.wav")));
    result = run(QStringLiteral("orcinus.rename('%1/renamed.wav', '../out.wav', function(ok, error) { result = [ok, error] })").arg(target));
    QCOMPARE(result.property(0).toBool(), false);
    result = run(QStringLiteral("orcinus.mkdir('%1/new', function(ok, error) { result = ok })").arg(target));
    QVERIFY(result.toBool());
    result = run(QStringLiteral("orcinus.move(['%1/renamed.wav'], '%1/new', function(ok, error) { result = ok })").arg(target));
    QVERIFY(result.toBool());
    QVERIFY(QFile::exists(target + QStringLiteral("/new/renamed.wav")));

    // A new text file; an existing one only with overwrite.
    result = run(QStringLiteral("orcinus.writeText('%1/list.txt', 'one', false, function(ok, error) { result = ok })").arg(target));
    QVERIFY(result.toBool());
    result = run(QStringLiteral("orcinus.writeText('%1/list.txt', 'two', false, function(ok, error) { result = [ok, error] })").arg(target));
    QCOMPARE(result.property(0).toBool(), false);
    QVERIFY(result.property(1).toString().contains(QLatin1String("overwrite")));
    QFile file(target + QStringLiteral("/list.txt"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("one"));

    // Wrong arguments come back as (false, error), later.
    result = run(QStringLiteral("orcinus.copy([], '%1', function(ok, error) { result = [ok, error] })").arg(target));
    QCOMPARE(result.property(0).toBool(), false);
    QVERIFY(result.property(1).isString());
}

void WhaleAiToolTest::testApiSettingsAndContext()
{
    QCOMPARE(m_api->currentFolder(), QUrl::fromLocalFile(m_files).toString());
    QCOMPARE(m_api->selection(), QStringList{QUrl::fromLocalFile(m_files + QStringLiteral("/a.wav")).toString()});
    QCOMPARE(m_api->otherFolder(), QString());
    m_api->reveal(m_files + QStringLiteral("/b.wav"));
    QCOMPARE(m_revealed, QList<QUrl>{QUrl::fromLocalFile(m_files + QStringLiteral("/b.wav"))});
    m_api->reveal(QStringLiteral("not a url"));
    QCOMPARE(m_revealed.size(), 1);

    // Settings: the manifest's default, then what the tool kept, in its own type.
    QCOMPARE(m_api->setting(QStringLiteral("target")).toString(), QString());
    QCOMPARE(m_api->setting(QStringLiteral("missing"), 7).toInt(), 7);
    m_api->setSetting(QStringLiteral("target"), QStringLiteral("file:///tmp/x"));
    m_api->setSetting(QStringLiteral("count"), 3);
    m_api->setSetting(QStringLiteral("list"), QVariantList{1, QStringLiteral("two")});
    QCOMPARE(m_api->setting(QStringLiteral("target")).toString(), QStringLiteral("file:///tmp/x"));
    QCOMPARE(m_api->setting(QStringLiteral("count")).toInt(), 3);
    QCOMPARE(m_api->setting(QStringLiteral("list")).toList().size(), 2);
    // Kept per tool, apart from the others.
    QVERIFY(m_store.settingsGroup(QStringLiteral("other")).keyList().isEmpty());
    m_store.settingsGroup(QStringLiteral("sound-triage")).deleteGroup();
}

void WhaleAiToolTest::testSoundTriage()
{
    // The example prompt's tool, end to end: collected sorted by name, the first plays, Keep
    // copies to Target and goes on, keys work while the tool has the focus.
    const QString target = m_dir.filePath(QStringLiteral("triage-target"));
    QDir().mkpath(target);
    const QByteArray targetSetting = QByteArray("\"") + QUrl::fromLocalFile(target).toString().toUtf8() + QByteArray("\"");
    m_store.settingsGroup(QStringLiteral("sound-triage")).writeEntry("target", targetSetting);

    QMainWindow window;
    auto panel = new WhaleAiCanvasPanel;
    panel->setToolStore(m_store);
    panel->setToolHost(m_host);
    window.setCentralWidget(panel);
    window.resize(700, 600);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    QCOMPARE(panel->openTool(QStringLiteral("nope")).isEmpty(), false);
    QCOMPARE(panel->openTool(QStringLiteral("sound-triage")), QString());
    WhaleAiToolTab *tab = panel->toolTab(QStringLiteral("sound-triage"));
    QVERIFY(tab);
    QTRY_VERIFY(tab->isLoaded());
    QQuickItem *root = tab->quickWidget()->rootObject();
    QTRY_COMPARE(findItem(root, QStringLiteral("name"))->property("text").toString(), QStringLiteral("a.wav"));
    QCOMPARE(findItem(root, QStringLiteral("position"))->property("text").toString(), QStringLiteral("1 of 3"));
    QCOMPARE(m_revealed.last(), QUrl::fromLocalFile(m_files + QStringLiteral("/a.wav")));
#if HAVE_MPV
    QCOMPARE(tab->api()->player()->source(), QUrl::fromLocalFile(m_files + QStringLiteral("/a.wav")).toString());
#endif

    // Keep: a click on the button.
    QQuickItem *keep = findItem(root, QStringLiteral("keep"));
    QTRY_VERIFY(keep->isEnabled());
    const QPointF centre = keep->mapToScene(QPointF(keep->width() / 2, keep->height() / 2));
    QTest::mouseClick(tab->quickWidget(), Qt::LeftButton, {}, centre.toPoint());
    QTRY_VERIFY(QFile::exists(target + QStringLiteral("/a.wav")));
    QTRY_COMPARE(findItem(root, QStringLiteral("name"))->property("text").toString(), QStringLiteral("b.wav"));

    // Keys: Right, then K for the last one, Left back.
    tab->quickWidget()->setFocus();
    QTRY_VERIFY(tab->quickWidget()->hasFocus());
    QTest::keyClick(tab->quickWidget(), Qt::Key_Right);
    QTRY_COMPARE(findItem(root, QStringLiteral("name"))->property("text").toString(), QStringLiteral("c.wav"));
    QVERIFY(findItem(root, QStringLiteral("folder"))->property("text").toString().endsWith(QLatin1String("/sub")));
    QTest::keyClick(tab->quickWidget(), Qt::Key_K);
    QTRY_VERIFY(QFile::exists(target + QStringLiteral("/c.wav")));
    QTest::keyClick(tab->quickWidget(), Qt::Key_Left);
    QTRY_COMPARE(findItem(root, QStringLiteral("name"))->property("text").toString(), QStringLiteral("b.wav"));
    QVERIFY(!QFile::exists(target + QStringLiteral("/b.wav")));

    QVERIFY2(tab->log().isEmpty(), qPrintable(tab->log().join(QLatin1Char('\n'))));
    QVERIFY(panel->closeTool(QStringLiteral("sound-triage")));
    QVERIFY(panel->openTools().isEmpty());
    m_store.settingsGroup(QStringLiteral("sound-triage")).deleteGroup();
}

void WhaleAiToolTest::testKeepAndTrust()
{
    writeTool(QStringLiteral("hello"), "import QtQuick\nItem { objectName: \"hello\" }");
    int asked = 0;
    bool answer = false;
    QStringList shownDetails;
    auto makePanel = [&](QMainWindow &window) {
        auto panel = new WhaleAiCanvasPanel;
        panel->setToolStore(m_store);
        panel->setToolHost(m_host);
        panel->setKeepConfirmation([&](const WhaleAiTool &, const QStringList &details) {
            ++asked;
            shownDetails = details;
            return answer;
        });
        window.setCentralWidget(panel);
        window.show();
        return panel;
    };

    {
        QMainWindow window;
        WhaleAiCanvasPanel *panel = makePanel(window);
        QCOMPARE(panel->openTool(QStringLiteral("hello")), QString());
        // Declined: not kept.
        QVERIFY(!panel->keepTool(QStringLiteral("hello")));
        QCOMPARE(asked, 1);
        QVERIFY(m_store.pinned().isEmpty());
        // The trust step shows the files.
        QVERIFY(shownDetails.join(QLatin1Char('\n')).contains(QLatin1String("main.qml")));
        answer = true;
        QVERIFY(panel->keepTool(QStringLiteral("hello")));
        QCOMPARE(m_store.pinned(), QStringList{QStringLiteral("hello")});
        QVERIFY(m_store.isTrusted(QStringLiteral("hello")));
    }

    // Next start: the kept tool is back and loads when its tab is shown.
    {
        QMainWindow window;
        WhaleAiCanvasPanel *panel = makePanel(window);
        panel->restoreKeptTools();
        QCOMPARE(panel->openTools(), QStringList{QStringLiteral("hello")});
        WhaleAiToolTab *tab = panel->toolTab(QStringLiteral("hello"));
        QVERIFY(!tab->isBlocked());
        QVERIFY(!tab->isLoaded());
        panel->openTool(QStringLiteral("hello"));
        QTRY_VERIFY(tab->isLoaded());
    }

    // Changed since: blocked until the user keeps it again.
    writeTool(QStringLiteral("hello"), "import QtQuick\nItem { objectName: \"changed\" }");
    QVERIFY(!m_store.isTrusted(QStringLiteral("hello")));
    {
        QMainWindow window;
        WhaleAiCanvasPanel *panel = makePanel(window);
        panel->restoreKeptTools();
        WhaleAiToolTab *tab = panel->toolTab(QStringLiteral("hello"));
        QVERIFY(tab->isBlocked());
        panel->openTool(QStringLiteral("hello"));
        QTest::qWait(50);
        QVERIFY(!tab->isLoaded());
        Q_EMIT tab->trustRequested();
        QTRY_VERIFY(tab->isLoaded());
        QVERIFY(m_store.isTrusted(QStringLiteral("hello")));
        // Removed: not kept, tab gone.
        Q_EMIT tab->removeRequested();
        QVERIFY(m_store.pinned().isEmpty());
        QVERIFY(panel->openTools().isEmpty());
    }
    QDir(m_tools + QStringLiteral("/hello")).removeRecursively();
}

void WhaleAiToolTest::testBrokenTool()
{
    writeTool(QStringLiteral("broken"), "import QtQuick\nItem { this is not QML }");
    const auto tool = m_store.tool(QStringLiteral("broken"));
    QVERIFY(tool);
    WhaleAiToolTab tab(*tool, m_store, m_host);
    tab.load();
    QVERIFY(!tab.isLoaded());
    QVERIFY(!tab.log().isEmpty());
    QVERIFY(tab.log().constFirst().contains(QLatin1String("main.qml")));

    // Fixed and loaded again; a failing callback lands in the log.
    writeTool(QStringLiteral("broken"),
              "import QtQuick\nItem { Component.onCompleted: orcinus.item(orcinus.currentFolder(), function(item, error) { undefinedFunction() }) }");
    tab.load();
    QVERIFY(tab.isLoaded());
    QTRY_VERIFY(tab.log().join(QLatin1Char('\n')).contains(QLatin1String("undefinedFunction")));
    QDir(m_tools + QStringLiteral("/broken")).removeRecursively();
}

QTEST_MAIN(WhaleAiToolTest)

#include "whaleaitooltest.moc"
