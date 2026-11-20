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
#include "whaleaitoolbackend.h"
#include "whaleaitoolstore.h"
#include "whaleaitooltab.h"
#include "whaleaitooltools.h"

#include "config-dolphin.h"

#include <KConfigGroup>
#include <KIO/FileUndoManager>
#include <KMessageWidget>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QMainWindow>
#include <QMenu>
#include <QPointer>
#include <QProcess>
#include <QQmlEngine>
#include <QQuickItem>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>

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
    void testToolsMenu();
    void testBrokenTool();
    void testDevelopmentLoop();
    void testBackend();
    void testBackendForAgent();
    void testSoundCheck();

private:
    QString writeFile(const QString &path, const QByteArray &data);
    void writeWav(const QString &path);
    /** A 16 bit mono WAV at 8 kHz from @p samples (-1..1, clipped to full scale). */
    void writeWav16(const QString &path, const QList<double> &samples);
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

void WhaleAiToolTest::writeWav16(const QString &path, const QList<double> &samples)
{
    const quint32 dataSize = quint32(samples.size() * 2);
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
    u16(1); // PCM
    u16(1); // mono
    u32(8000);
    u32(16000);
    u16(2);
    u16(16);
    wav.append("data");
    u32(dataSize);
    for (const double sample : samples) {
        u16(quint16(qint16(std::clamp(sample, -1.0, 1.0) * 32767.0)));
    }
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
    // The example tools, as they are in the source tree.
    for (const QString &id : {QStringLiteral("sound-triage"), QStringLiteral("sound-check")}) {
        const QDir source(QStringLiteral(WHALE_AI_TEST_TOOLS "/") + id);
        for (const QString &name : source.entryList(QDir::Files)) {
            QDir().mkpath(m_tools + QLatin1Char('/') + id);
            QVERIFY(QFile::copy(source.filePath(name), m_tools + QLatin1Char('/') + id + QLatin1Char('/') + name));
        }
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
    QCOMPARE(m_store.ids(), QStringList({QStringLiteral("sound-check"), QStringLiteral("sound-triage")}));
    const auto tool = m_store.tool(QStringLiteral("sound-triage"));
    QCOMPARE(tool->name, QStringLiteral("Sound Triage"));
    QCOMPARE(tool->settings.value(QLatin1String("target")).toString(), QString());
    QCOMPARE(tool->writes.size(), 1);
    QCOMPARE(m_store.files(QStringLiteral("sound-triage")), QStringList({QStringLiteral("main.qml"), QStringLiteral("tool.json")}));
    const auto check = m_store.tool(QStringLiteral("sound-check"));
    QVERIFY(check);
    QCOMPARE(check->backend.value(QLatin1String("command")).toArray(), (QJsonArray{QStringLiteral("python3"), QStringLiteral("backend.py")}));

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
    QCOMPARE(m_store.ids(), QStringList({QStringLiteral("sound-check"), QStringLiteral("sound-triage")}));
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

void WhaleAiToolTest::testToolsMenu()
{
    writeTool(QStringLiteral("hello"), "import QtQuick\nItem { objectName: \"hello\" }");
    writeTool(QStringLiteral("second"), "import QtQuick\nItem { objectName: \"second\" }");
    QMainWindow window;
    auto panel = new WhaleAiCanvasPanel;
    panel->setToolStore(m_store);
    panel->setToolHost(m_host);
    window.setCentralWidget(panel);
    window.show();
    // Many tabs: titles shorten, then arrows.
    auto *tabs = panel->findChild<QTabWidget *>();
    QCOMPARE(tabs->elideMode(), Qt::ElideRight);
    QVERIFY(tabs->usesScrollButtons());

    QMenu *menu = panel->findChild<QToolButton *>(QStringLiteral("aiCanvasTools"))->menu();
    auto texts = [menu](const QString &name) {
        Q_EMIT menu->aboutToShow();
        QStringList list;
        for (const QAction *action : menu->actions()) {
            if (action->objectName() == name) {
                list.append(action->text());
            }
        }
        return list;
    };
    auto action = [menu](const QString &name, const QString &text) -> QAction * {
        Q_EMIT menu->aboutToShow();
        for (QAction *action : menu->actions()) {
            if (action->objectName() == name && action->text() == text) {
                return action;
            }
        }
        return nullptr;
    };
    // Only the canvas is open; the tools come after it.
    QCOMPARE(texts(QStringLiteral("openTab")), QStringList{QStringLiteral("Canvas")});
    QVERIFY(texts(QStringLiteral("tool")).contains(QStringLiteral("hello")));
    QVERIFY(action(QStringLiteral("openTab"), QStringLiteral("Canvas"))->isChecked());

    // Opened from the menu: listed with the open ones, shown.
    action(QStringLiteral("tool"), QStringLiteral("hello"))->trigger();
    QVERIFY(panel->toolTab(QStringLiteral("hello")));
    QCOMPARE(tabs->currentWidget(), panel->toolTab(QStringLiteral("hello")));
    QCOMPARE(texts(QStringLiteral("openTab")), (QStringList{QStringLiteral("Canvas"), QStringLiteral("hello")}));
    QVERIFY(!texts(QStringLiteral("tool")).contains(QStringLiteral("hello")));
    QVERIFY(texts(QStringLiteral("tool")).contains(QStringLiteral("second")));
    // The current one in bold.
    QVERIFY(action(QStringLiteral("openTab"), QStringLiteral("hello"))->font().bold());
    QVERIFY(!action(QStringLiteral("openTab"), QStringLiteral("Canvas"))->font().bold());

    // An open one: shown, not opened again.
    action(QStringLiteral("openTab"), QStringLiteral("Canvas"))->trigger();
    QCOMPARE(tabs->currentIndex(), 0);
    action(QStringLiteral("openTab"), QStringLiteral("hello"))->trigger();
    QCOMPARE(tabs->currentWidget(), panel->toolTab(QStringLiteral("hello")));
    QCOMPARE(tabs->count(), 2);
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
    // With the line it points to, so the agent need not count lines.
    QVERIFY2(tab.log().constFirst().contains(QLatin1String("\n    2 | Item { this is not QML }")), qPrintable(tab.log().constFirst()));

    // Fixed and loaded again; a failing callback lands in the log.
    writeTool(QStringLiteral("broken"),
              "import QtQuick\nItem { Component.onCompleted: orcinus.item(orcinus.currentFolder(), function(item, error) { undefinedFunction() }) }");
    tab.load();
    QVERIFY(tab.isLoaded());
    QTRY_VERIFY(tab.log().join(QLatin1Char('\n')).contains(QLatin1String("undefinedFunction")));
    QDir(m_tools + QStringLiteral("/broken")).removeRecursively();
}

void WhaleAiToolTest::testDevelopmentLoop()
{
    // The agent's loop (CV3): write, open, read the errors, fix, inspect, act, look.
    QMainWindow window;
    auto panel = new WhaleAiCanvasPanel;
    panel->setToolStore(m_store);
    panel->setToolHost(m_host);
    panel->setKeepConfirmation([](const WhaleAiTool &, const QStringList &) {
        return true;
    });
    window.setCentralWidget(panel);
    window.resize(600, 500);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    int shown = 0;
    QHash<QString, WhaleAiWindowTool> tools;
    for (const WhaleAiWindowTool &tool : WhaleAiToolTools::create(panel, [&shown] {
             ++shown;
         })) {
        tools.insert(tool.name, tool);
    }
    auto call = [&tools](const QString &name, const QJsonObject &arguments, bool *error, QString *notice = nullptr) {
        QString text;
        *error = false;
        const QJsonObject result = tools.value(name).call(arguments, error, &text);
        if (notice) {
            *notice = text;
        }
        return result;
    };
    bool error = false;
    QString notice;
    const QString id = QStringLiteral("counter");

    QVERIFY(call(QStringLiteral("tool_guide"), {}, &error).value(QLatin1String("guide")).toString().contains(QLatin1String("orcinus.list(")));
    QVERIFY(tools.value(QStringLiteral("tool_guide")).readOnly && tools.value(QStringLiteral("tool_inspect")).readOnly);
    QVERIFY(!tools.value(QStringLiteral("tool_write_file")).readOnly && !tools.value(QStringLiteral("tool_invoke")).readOnly);
    for (const QString &name : WhaleAiToolTools::changingTools()) {
        QVERIFY(tools.contains(name));
    }

    // Wrong ids and paths are refused.
    auto write = [&](const QString &tool, const QString &path, const QString &content) {
        return call(QStringLiteral("tool_write_file"),
                    {{QStringLiteral("tool"), tool}, {QStringLiteral("path"), path}, {QStringLiteral("content"), content}},
                    &error,
                    &notice);
    };
    write(QStringLiteral("../evil"), QStringLiteral("main.qml"), QStringLiteral("x"));
    QVERIFY(error);
    write(id, QStringLiteral("../sound-triage/main.qml"), QStringLiteral("x"));
    QVERIFY(error);
    write(id, QStringLiteral(".hidden.qml"), QStringLiteral("x"));
    QVERIFY(error);
    write(id, QStringLiteral("lib.so"), QStringLiteral("x"));
    QVERIFY(error);
    write(id, QStringLiteral("/etc/passwd.txt"), QStringLiteral("x"));
    QVERIFY(error);
    QVERIFY(!QFile::exists(m_tools + QStringLiteral("/counter")));

    // A manifest alone is not a tool yet; with a broken QML it does not load, and says why.
    QJsonObject result = write(id, QStringLiteral("tool.json"), QStringLiteral(R"({"name": "Counter", "icon": "accessories-calculator"})"));
    QVERIFY(!error);
    QVERIFY(!notice.isEmpty());
    QCOMPARE(result.value(QLatin1String("toolValid")).toBool(), false);
    result = call(QStringLiteral("tool_list"), {}, &error);
    QVERIFY(result.value(QLatin1String("invalid")).toArray().first().toObject().value(QLatin1String("id")).toString() == id);
    result = write(id, QStringLiteral("main.qml"), QStringLiteral("import QtQuick\nItem { property int count: 0\n broken here }"));
    QCOMPARE(result.value(QLatin1String("toolValid")).toBool(), true);
    result = call(QStringLiteral("tool_open"), {{QStringLiteral("tool"), id}}, &error);
    QVERIFY(!error);
    QCOMPARE(shown, 1);
    QCOMPARE(result.value(QLatin1String("loaded")).toBool(), false);
    QVERIFY(!result.value(QLatin1String("log")).toArray().isEmpty());

    // Fixed: loads; console.log goes into the log.
    write(id, QStringLiteral("main.qml"), QStringLiteral(R"(import QtQuick
import QtQuick.Controls
FocusScope {
    id: root
    focus: true
    property int count: 0
    property var names: ["a", "b"]
    function add(n) { count += n; return count }
    Component.onCompleted: console.log("counter ready")
    Keys.onPressed: (event) => { if (event.key === Qt.Key_Right) { count += 10; event.accepted = true } }
    Button { objectName: "inc"; text: "Count " + root.count; onClicked: root.count++ }
}
)"));
    result = call(QStringLiteral("tool_open"), {{QStringLiteral("tool"), id}}, &error);
    QVERIFY(!error);
    QCOMPARE(result.value(QLatin1String("loaded")).toBool(), true);
    QTRY_VERIFY(call(QStringLiteral("tool_log"), {{QStringLiteral("tool"), id}}, &error)
                    .value(QLatin1String("log"))
                    .toArray()
                    .contains(QJsonValue(QStringLiteral("console: counter ready (main.qml:9)"))));

    // Inspect, act, inspect.
    result = call(QStringLiteral("tool_inspect"), {{QStringLiteral("tool"), id}}, &error);
    QCOMPARE(result.value(QLatin1String("properties")).toObject().value(QLatin1String("count")).toInt(), 0);
    QCOMPARE(result.value(QLatin1String("properties")).toObject().value(QLatin1String("names")).toArray().size(), 2);
    QCOMPARE(result.value(QLatin1String("items")).toArray().first().toObject().value(QLatin1String("text")).toString(), QStringLiteral("Count 0"));
    call(QStringLiteral("tool_invoke"), {{QStringLiteral("tool"), id}, {QStringLiteral("click"), QStringLiteral("inc")}}, &error, &notice);
    QVERIFY(!error);
    QVERIFY(!notice.isEmpty());
    call(QStringLiteral("tool_invoke"), {{QStringLiteral("tool"), id}, {QStringLiteral("keys"), QStringLiteral("Right")}}, &error);
    QVERIFY(!error);
    result = call(QStringLiteral("tool_invoke"),
                  {{QStringLiteral("tool"), id}, {QStringLiteral("function"), QStringLiteral("add")}, {QStringLiteral("arguments"), QJsonArray{5}}},
                  &error);
    QVERIFY(!error);
    QCOMPARE(result.value(QLatin1String("returned")).toInt(), 16);
    call(QStringLiteral("tool_invoke"), {{QStringLiteral("tool"), id}, {QStringLiteral("click"), QStringLiteral("missing")}}, &error);
    QVERIFY(error);
    call(QStringLiteral("tool_invoke"), {{QStringLiteral("tool"), id}, {QStringLiteral("function"), QStringLiteral("nope")}}, &error);
    QVERIFY(error);

    // A look at it.
    result = call(QStringLiteral("tool_screenshot"), {{QStringLiteral("tool"), id}}, &error);
    QVERIFY(!error);
    QVERIFY(result.value(QLatin1String("width")).toInt() > 0);
    QVERIFY(
        QByteArray::fromBase64(result.value(QLatin1String("_mcpImage")).toObject().value(QLatin1String("data")).toString().toLatin1()).startsWith("\x89PNG"));

    // Reading back; a kept tool the agent changes is no longer kept.
    QVERIFY(call(QStringLiteral("tool_read_file"), {{QStringLiteral("tool"), id}, {QStringLiteral("path"), QStringLiteral("main.qml")}}, &error)
                .value(QLatin1String("content"))
                .toString()
                .contains(QLatin1String("counter ready")));
    QVERIFY(panel->keepTool(id));
    result = write(id, QStringLiteral("notes.md"), QStringLiteral("# Counter"));
    QVERIFY(!error);
    QVERIFY(result.contains(QLatin1String("note")));
    QVERIFY(!m_store.pinned().contains(id));
    call(QStringLiteral("tool_delete_file"), {{QStringLiteral("tool"), id}, {QStringLiteral("path"), QStringLiteral("notes.md")}}, &error);
    QVERIFY(!error);
    QVERIFY(!QFile::exists(m_tools + QStringLiteral("/counter/notes.md")));

    result = call(QStringLiteral("tool_list"), {}, &error);
    bool listed = false;
    for (const QJsonValue &entry : result.value(QLatin1String("tools")).toArray()) {
        if (entry.toObject().value(QLatin1String("id")).toString() == id) {
            listed = entry.toObject().value(QLatin1String("loaded")).toBool();
        }
    }
    QVERIFY(listed);
    QVERIFY(call(QStringLiteral("tool_close"), {{QStringLiteral("tool"), id}}, &error).value(QLatin1String("closed")).toBool());
    call(QStringLiteral("tool_log"), {{QStringLiteral("tool"), id}}, &error);
    QVERIFY(error);
    QDir(m_tools + QStringLiteral("/counter")).removeRecursively();
}

void WhaleAiToolTest::testBackend()
{
    // CV4: a tool's backend starts after the user confirms it, talks JSON lines with the QML and
    // says when it ends.
    if (QStandardPaths::findExecutable(QStringLiteral("python3")).isEmpty()) {
        QSKIP("python3 is needed for the backend");
    }
    const QString folder = m_tools + QStringLiteral("/echo");
    const QByteArray backendScript =
        "import json, sys\n"
        "for line in sys.stdin:\n"
        "    message = json.loads(line)\n"
        "    if message == {'ping': 'exit'}:\n"
        "        print('oops', file=sys.stderr, flush=True)\n"
        "        sys.exit(2)\n"
        "    print(json.dumps({'got': message}), flush=True)\n";
    writeTool(QStringLiteral("echo"),
              "import QtQuick\n"
              "Item {\n"
              "    property var received: []\n"
              "    property string backendState: orcinus.backend.state\n"
              "    Component.onCompleted: orcinus.backend.send({hello: 'early'})\n"
              "    Connections { target: orcinus.backend; function onMessage(value) { received = received.concat([JSON.stringify(value)]) } }\n"
              "    function ping(x) { return orcinus.backend.send({ping: x}) }\n"
              "}\n",
              R"({"name": "Echo", "writes": ["nowhere"], "backend": {"command": ["python3", "backend.py"]}})");
    writeFile(folder + QStringLiteral("/backend.py"), backendScript);
    const auto tool = m_store.tool(QStringLiteral("echo"));
    QVERIFY(tool);
    auto tab = std::make_unique<WhaleAiToolTab>(*tool, m_store, m_host);
    auto *bar = tab->findChild<KMessageWidget *>(QStringLiteral("backendMessage"));
    QVERIFY(bar);
    auto received = [&tab] {
        return tab->quickWidget()->rootObject()->property("received").toStringList();
    };
    auto ping = [&tab](const QString &text) {
        QVariant sent;
        QMetaObject::invokeMethod(tab->quickWidget()->rootObject(), "ping", Q_RETURN_ARG(QVariant, sent), Q_ARG(QVariant, text));
        return sent.toBool();
    };

    // Not confirmed yet: it waits and asks; what the QML sends meanwhile is kept.
    tab->load();
    QVERIFY(tab->isLoaded());
    QCOMPARE(tab->backend()->state(), WhaleAiToolBackend::State::Waiting);
    QCOMPARE(tab->quickWidget()->rootObject()->property("backendState").toString(), QStringLiteral("waiting"));
    QVERIFY(!bar->isHidden());
    QVERIFY(bar->text().contains(QLatin1String("python3 backend.py")));
    QVERIFY(bar->text().contains(QLatin1String("nowhere")));
    QCOMPARE(bar->actions().size(), 2);
    QVERIFY(!m_store.isBackendAllowed(QStringLiteral("echo")));
    QVERIFY(tab->confirmBackend());
    QVERIFY(m_store.isBackendAllowed(QStringLiteral("echo")));
    QCOMPARE(tab->backend()->state(), WhaleAiToolBackend::State::Running);
    QTRY_COMPARE(received(), QStringList{QStringLiteral(R"({"got":{"hello":"early"}})")});
    QVERIFY(ping(QStringLiteral("one")));
    QTRY_COMPARE(received().size(), 2);
    QCOMPARE(received().last(), QStringLiteral(R"({"got":{"ping":"one"}})"));
    QTRY_VERIFY(bar->isHidden());

    // Confirmed: a reload starts it at once, also after a change of the QML only.
    tab->load();
    QCOMPARE(tab->backend()->state(), WhaleAiToolBackend::State::Running);
    writeTool(QStringLiteral("echo"),
              "import QtQuick\n"
              "Item {\n"
              "    property var received: []\n"
              "    property string backendState: orcinus.backend.state\n"
              "    Connections { target: orcinus.backend; function onMessage(value) { received = received.concat([JSON.stringify(value)]) } }\n"
              "    function ping(x) { return orcinus.backend.send({ping: x}) }\n"
              "}\n",
              R"({"name": "Echo", "writes": ["nowhere"], "backend": {"command": ["python3", "backend.py"]}})");
    tab->load();
    QCOMPARE(tab->backend()->state(), WhaleAiToolBackend::State::Running);

    // A changed backend asks again.
    writeFile(folder + QStringLiteral("/backend.py"), backendScript + "# changed\n");
    tab->load();
    QCOMPARE(tab->backend()->state(), WhaleAiToolBackend::State::Waiting);
    QVERIFY(!bar->isHidden());
    QVERIFY(tab->confirmBackend());
    QVERIFY(!tab->confirmBackend()); // nothing waits any more

    // It ends: the bar says so with its stderr, and Restart starts it again.
    QVERIFY(ping(QStringLiteral("exit")));
    QTRY_COMPARE(tab->backend()->state(), WhaleAiToolBackend::State::Exited);
    QCOMPARE(tab->backend()->exitCode(), 2);
    QCOMPARE(tab->quickWidget()->rootObject()->property("backendState").toString(), QStringLiteral("exited"));
    QTRY_VERIFY(!bar->isHidden());
    QVERIFY(bar->text().contains(QLatin1String("exit code 2")));
    QVERIFY(bar->text().contains(QLatin1String("oops")));
    QVERIFY(tab->log().join(QLatin1Char('\n')).contains(QLatin1String("backend stderr: oops")));
    QVERIFY(!ping(QStringLiteral("lost")));
    QCOMPARE(bar->actions().size(), 1);
    bar->actions().constFirst()->trigger();
    QCOMPARE(tab->backend()->state(), WhaleAiToolBackend::State::Running);
    QVERIFY(ping(QStringLiteral("two")));
    QTRY_VERIFY(!received().isEmpty() && received().last() == QStringLiteral(R"({"got":{"ping":"two"}})"));

    // Keeping the tool trusts its backend too.
    writeFile(folder + QStringLiteral("/backend.py"), backendScript + "# changed again\n");
    QVERIFY(!m_store.isBackendAllowed(QStringLiteral("echo")));
    m_store.pin(QStringLiteral("echo"));
    QVERIFY(m_store.isBackendAllowed(QStringLiteral("echo")));
    m_store.unpin(QStringLiteral("echo"));

    // Closing the tab ends the backend.
    QPointer<QObject> process = tab->backend()->findChild<QProcess *>();
    QVERIFY(process);
    tab.reset();
    QVERIFY(!process);
    QDir(folder).removeRecursively();
}

void WhaleAiToolTest::testBackendForAgent()
{
    // CV4c: the agent writes a backend, sees that it waits for the user, and reads its output.
    if (QStandardPaths::findExecutable(QStringLiteral("python3")).isEmpty()) {
        QSKIP("python3 is needed for the backend");
    }
    QMainWindow window;
    auto panel = new WhaleAiCanvasPanel;
    panel->setToolStore(m_store);
    panel->setToolHost(m_host);
    window.setCentralWidget(panel);
    window.resize(600, 500);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QHash<QString, WhaleAiWindowTool> tools;
    for (const WhaleAiWindowTool &tool : WhaleAiToolTools::create(panel, {})) {
        tools.insert(tool.name, tool);
    }
    bool error = false;
    auto call = [&tools, &error](const QString &name, const QJsonObject &arguments) {
        QString notice;
        error = false;
        return tools.value(name).call(arguments, &error, &notice);
    };
    const QString id = QStringLiteral("agent-backend");
    auto write = [&](const QString &path, const QString &content) {
        call(QStringLiteral("tool_write_file"), {{QStringLiteral("tool"), id}, {QStringLiteral("path"), path}, {QStringLiteral("content"), content}});
        QVERIFY(!error);
    };
    const QString guide = call(QStringLiteral("tool_guide"), {}).value(QLatin1String("guide")).toString();
    QVERIFY(guide.contains(QLatin1String("orcinus.backend.send")));
    QVERIFY(guide.contains(QLatin1String("\"backend\": {\"command\"")));

    write(QStringLiteral("tool.json"), QStringLiteral(R"({"name": "Agent Backend", "backend": {"command": ["python3", "backend.py"]}})"));
    write(QStringLiteral("backend.py"),
          QStringLiteral("import json, sys\nprint('ready', file=sys.stderr, flush=True)\nfor line in sys.stdin:\n"
                         "    print(json.dumps({'got': json.loads(line)}), flush=True)\n"));
    write(QStringLiteral("main.qml"),
          QStringLiteral("import QtQuick\nItem { property string answer\n"
                         "  Connections { target: orcinus.backend; function onMessage(value) { answer = JSON.stringify(value) } }\n"
                         "  function ask() { orcinus.backend.send('hi') } }\n"));
    const QString tab = QStringLiteral("tool");
    QJsonObject result = call(QStringLiteral("tool_open"), {{tab, id}});
    QVERIFY(!error);
    QCOMPARE(result.value(QLatin1String("backend")).toObject().value(QLatin1String("state")).toString(), QStringLiteral("waiting"));
    QVERIFY(result.value(QLatin1String("backend")).toObject().value(QLatin1String("note")).toString().contains(QLatin1String("Start")));
    result = call(QStringLiteral("tool_inspect"), {{tab, id}});
    QCOMPARE(result.value(QLatin1String("backend")).toObject().value(QLatin1String("state")).toString(), QStringLiteral("waiting"));
    // The agent cannot confirm it; the user does.
    QVERIFY(panel->toolTab(id)->confirmBackend());
    result = call(QStringLiteral("tool_inspect"), {{tab, id}});
    QCOMPARE(result.value(QLatin1String("backend")).toObject().value(QLatin1String("state")).toString(), QStringLiteral("running"));
    call(QStringLiteral("tool_invoke"), {{tab, id}, {QStringLiteral("function"), QStringLiteral("ask")}});
    QVERIFY(!error);
    QTRY_COMPARE(call(QStringLiteral("tool_inspect"), {{tab, id}}).value(QLatin1String("properties")).toObject().value(QLatin1String("answer")).toString(),
                 QStringLiteral(R"({"got":"hi"})"));
    QTRY_VERIFY(
        call(QStringLiteral("tool_log"), {{tab, id}}).value(QLatin1String("log")).toVariant().toStringList().contains(QStringLiteral("backend stderr: ready")));

    // A change of the QML keeps the confirmation, one of the backend does not.
    write(QStringLiteral("main.qml"), QStringLiteral("import QtQuick\nItem { property int version: 2 }\n"));
    result = call(QStringLiteral("tool_open"), {{tab, id}});
    QCOMPARE(result.value(QLatin1String("backend")).toObject().value(QLatin1String("state")).toString(), QStringLiteral("running"));
    write(QStringLiteral("backend.py"), QStringLiteral("import sys\nprint('broken', file=sys.stderr)\nsys.exit(4)\n"));
    result = call(QStringLiteral("tool_open"), {{tab, id}});
    QCOMPARE(result.value(QLatin1String("backend")).toObject().value(QLatin1String("state")).toString(), QStringLiteral("waiting"));
    QVERIFY(panel->toolTab(id)->confirmBackend());
    // It ends: the agent reads the exit code and stderr.
    QTRY_COMPARE(call(QStringLiteral("tool_inspect"), {{tab, id}}).value(QLatin1String("backend")).toObject().value(QLatin1String("state")).toString(),
                 QStringLiteral("exited"));
    const QJsonObject backend = call(QStringLiteral("tool_inspect"), {{tab, id}}).value(QLatin1String("backend")).toObject();
    QCOMPARE(backend.value(QLatin1String("exitCode")).toInt(), 4);
    QCOMPARE(backend.value(QLatin1String("stderr")).toArray(), QJsonArray{QStringLiteral("broken")});
    call(QStringLiteral("tool_close"), {{tab, id}});
    QDir(m_tools + QLatin1Char('/') + id).removeRecursively();
}

void WhaleAiToolTest::testSoundCheck()
{
    // The example tool with a backend (CV4), end to end: measured, flagged, trimmed.
    if (QStandardPaths::findExecutable(QStringLiteral("python3")).isEmpty()) {
        QSKIP("python3 is needed for the backend");
    }
    const QString folder = m_dir.filePath(QStringLiteral("check"));
    auto tone = [](int count, double amplitude) {
        QList<double> samples;
        for (int i = 0; i < count; ++i) {
            samples.append(amplitude * std::sin(2 * M_PI * 440.0 * i / 8000.0));
        }
        return samples;
    };
    writeWav16(folder + QStringLiteral("/tone.wav"), tone(4000, 0.5));
    writeWav16(folder + QStringLiteral("/clipped.wav"), tone(4000, 1.5));
    writeWav16(folder + QStringLiteral("/padded.wav"), QList<double>(4000, 0.0) + tone(4000, 0.5) + QList<double>(4000, 0.0));

    QMainWindow window;
    auto panel = new WhaleAiCanvasPanel;
    panel->setToolStore(m_store);
    panel->setToolHost(m_host);
    window.setCentralWidget(panel);
    window.resize(700, 600);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QCOMPARE(panel->openTool(QStringLiteral("sound-check")), QString());
    WhaleAiToolTab *tab = panel->toolTab(QStringLiteral("sound-check"));
    QTRY_VERIFY(tab->isLoaded());
    QQuickItem *root = tab->quickWidget()->rootObject();
    QCOMPARE(tab->backend()->state(), WhaleAiToolBackend::State::Waiting);
    QCOMPARE(findItem(root, QStringLiteral("status"))->property("text").toString(), QStringLiteral("Waiting for Start above…"));
    QVERIFY(tab->confirmBackend());
    // The scan of the current folder it asked for while waiting runs first.
    QTRY_VERIFY(!root->property("scanning").toBool());

    QMetaObject::invokeMethod(root, "scan", Q_ARG(QVariant, QUrl::fromLocalFile(folder).toString()));
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("scanning").toBool(), 15000);
    QVERIFY2(tab->log().filter(QStringLiteral("stderr")).isEmpty(), qPrintable(tab->log().join(QLatin1Char('\n'))));
    const QVariantList files = root->property("files").toList();
    QCOMPARE(files.size(), 3);
    QStringList names;
    for (const QVariant &file : files) {
        names.append(file.toMap().value(QStringLiteral("name")).toString());
    }
    QCOMPARE(names, QStringList({QStringLiteral("clipped.wav"), QStringLiteral("padded.wav"), QStringLiteral("tone.wav")}));
    const QVariantMap clipped = files.at(0).toMap();
    const QVariantMap padded = files.at(1).toMap();
    const QVariantMap plain = files.at(2).toMap();
    QVERIFY(clipped.value(QStringLiteral("clipped")).toInt() > 0);
    QCOMPARE(plain.value(QStringLiteral("clipped")).toInt(), 0);
    QCOMPARE(plain.value(QStringLiteral("duration")).toDouble(), 0.5);
    QCOMPARE(padded.value(QStringLiteral("duration")).toDouble(), 1.5);
    QVERIFY(qAbs(plain.value(QStringLiteral("peakDb")).toDouble() + 6.0) < 0.2);
    QVERIFY(padded.value(QStringLiteral("longSilence")).toBool());
    QVERIFY(!plain.value(QStringLiteral("longSilence")).toBool());
    QVERIFY(!clipped.value(QStringLiteral("longSilence")).toBool());
    QVERIFY(padded.value(QStringLiteral("waveform")).toList().size() > 100);

    // Trim the padded one: a click on its row, then on the button.
    QQuickItem *trim = findItem(root, QStringLiteral("trim"));
    QMetaObject::invokeMethod(root, "select", Q_ARG(QVariant, 1));
    QTRY_VERIFY(trim->isEnabled());
    const QPointF centre = trim->mapToScene(QPointF(trim->width() / 2, trim->height() / 2));
    QTest::mouseClick(tab->quickWidget(), Qt::LeftButton, {}, centre.toPoint());
    const QString trimmed = folder + QStringLiteral("/trimmed/padded.wav");
    QTRY_VERIFY(QFile::exists(trimmed));
    QTRY_VERIFY(findItem(root, QStringLiteral("status"))->property("text").toString().startsWith(QLatin1String("Trimmed:")));
    // About the tone alone is left (0.5 s at 8 kHz, 16 bit).
    const qint64 frames = (QFileInfo(trimmed).size() - 44) / 2;
    QVERIFY2(frames > 3900 && frames <= 4000, qPrintable(QString::number(frames)));
    QCOMPARE(QFileInfo(folder + QStringLiteral("/padded.wav")).size(), qint64(44 + 2 * 12000));
    QCOMPARE(m_revealed.last(), QUrl::fromLocalFile(trimmed));
    // Twice is refused, the copy is not overwritten.
    QTest::mouseClick(tab->quickWidget(), Qt::LeftButton, {}, centre.toPoint());
    QTRY_VERIFY(findItem(root, QStringLiteral("status"))->property("text").toString().contains(QLatin1String("exists already")));

    QVERIFY(panel->closeTool(QStringLiteral("sound-check")));
    QDir(folder).removeRecursively();
}

QTEST_MAIN(WhaleAiToolTest)

#include "whaleaitooltest.moc"
