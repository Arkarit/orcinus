/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaicanvaspanel.h"
#include "whaleaicanvasmedia.h"
#include "whaleaicanvasmodel.h"
#include "whaleaicanvastools.h"
#include "whaleaiquickwidget.h"

#include "config-dolphin.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QMainWindow>
#include <QQuickItem>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <functional>

class WhaleAiCanvasPanelTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();
    void testLoads();
    void testAllBlocks();
    void testClickShowsInView();
    void testKeysStayInCanvas();
    void testPlaysSound();
    void testTools();

private:
    QString writeImage(const QString &name, const QColor &color);
    QString writeWav(const QString &name, int seconds);
    QQuickItem *findItem(const QString &objectName) const;

    QTemporaryDir m_dir;
    QMainWindow *m_window = nullptr;
    WhaleAiCanvasPanel *m_panel = nullptr;
    QStringList m_qmlWarnings;
};

namespace
{
QtMessageHandler s_previousHandler = nullptr;
QStringList *s_warnings = nullptr;

void collectQmlWarnings(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (s_warnings && type >= QtWarningMsg && message.contains(QLatin1String("qrc:/orcinus/"))) {
        s_warnings->append(message);
    }
    s_previousHandler(type, context, message);
}

int typeOf(const QJsonObject &result)
{
    return result.value(QLatin1String("shown")).toInt();
}
}

QString WhaleAiCanvasPanelTest::writeImage(const QString &name, const QColor &color)
{
    const QString path = m_dir.filePath(name);
    QImage image(64, 32, QImage::Format_RGB32);
    image.fill(color);
    image.save(path);
    return path;
}

QString WhaleAiCanvasPanelTest::writeWav(const QString &name, int seconds)
{
    // 8 kHz, 8 bit, mono silence.
    const int rate = 8000;
    const quint32 dataSize = rate * seconds;
    QByteArray wav;
    auto u32 = [&wav](quint32 v) {
        wav.append(reinterpret_cast<const char *>(&v), 4);
    };
    auto u16 = [&wav](quint16 v) {
        wav.append(reinterpret_cast<const char *>(&v), 2);
    };
    wav.append("RIFF");
    u32(36 + dataSize);
    wav.append("WAVEfmt ");
    u32(16);
    u16(1);
    u16(1);
    u32(rate);
    u32(rate);
    u16(1);
    u16(8);
    wav.append("data");
    u32(dataSize);
    wav.append(QByteArray(dataSize, char(128)));
    const QString path = m_dir.filePath(name);
    QFile file(path);
    file.open(QIODevice::WriteOnly);
    file.write(wav);
    return path;
}

QQuickItem *WhaleAiCanvasPanelTest::findItem(const QString &objectName) const
{
    // The delegates of a ListView have no QObject parent: through the item tree.
    std::function<QQuickItem *(QQuickItem *)> find = [&find, &objectName](QQuickItem *item) -> QQuickItem * {
        if (!item) {
            return nullptr;
        }
        if (item->objectName() == objectName) {
            return item;
        }
        const auto children = item->childItems();
        for (QQuickItem *child : children) {
            if (QQuickItem *found = find(child)) {
                return found;
            }
        }
        return nullptr;
    };
    return find(m_panel->canvasWidget()->rootObject());
}

// A widget style Qt Quick Controls does not have (as in the CI and outside Plasma): the canvas
// must load all the same. Before main(): Qt reads it when the application starts.
static void overrideWidgetStyle()
{
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        qputenv("QT_STYLE_OVERRIDE", "breeze");
    }
}
Q_CONSTRUCTOR_FUNCTION(overrideWidgetStyle)

void WhaleAiCanvasPanelTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QVERIFY(m_dir.isValid());
    s_warnings = &m_qmlWarnings;
    s_previousHandler = qInstallMessageHandler(collectQmlWarnings);
}

void WhaleAiCanvasPanelTest::init()
{
    m_qmlWarnings.clear();
    m_window = new QMainWindow;
    m_panel = new WhaleAiCanvasPanel;
    m_window->setCentralWidget(m_panel);
    m_window->resize(600, 700);
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window));
    // The window's shortcuts work only in the active window.
    m_window->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(m_window));
}

void WhaleAiCanvasPanelTest::cleanup()
{
    delete m_window;
    m_window = nullptr;
    m_panel = nullptr;
    QVERIFY2(m_qmlWarnings.isEmpty(), qPrintable(m_qmlWarnings.join(QLatin1Char('\n'))));
}

void WhaleAiCanvasPanelTest::testLoads()
{
    QVERIFY2(m_panel->qmlErrors().isEmpty(), qPrintable(m_panel->qmlErrors().join(QLatin1Char('\n'))));
    QQuickItem *placeholder = findItem(QStringLiteral("placeholder"));
    QVERIFY(placeholder);
    QVERIFY(placeholder->isVisible());
}

void WhaleAiCanvasPanelTest::testAllBlocks()
{
    // Every kind of block is shown without a QML warning.
    const QString a = writeImage(QStringLiteral("a.png"), Qt::red);
    const QString b = writeImage(QStringLiteral("b.png"), Qt::blue);
    const QString wav = writeWav(QStringLiteral("tone.wav"), 1);
    QDir().mkpath(m_dir.filePath(QStringLiteral("renders")));
    writeImage(QStringLiteral("renders/frame.png"), Qt::green);
    const QList<QJsonObject> given{
        {{QStringLiteral("type"), QStringLiteral("image")}, {QStringLiteral("path"), a}, {QStringLiteral("title"), QStringLiteral("Red")}},
        {{QStringLiteral("type"), QStringLiteral("compare")}, {QStringLiteral("paths"), QJsonArray{a, b}}},
        {{QStringLiteral("type"), QStringLiteral("files")}, {QStringLiteral("paths"), QJsonArray{a, b, m_dir.path()}}},
        {{QStringLiteral("type"), QStringLiteral("text")},
         {QStringLiteral("text"), QStringLiteral("# Result\n\nSee [the file](%1).").arg(QUrl::fromLocalFile(a).toString())}},
        {{QStringLiteral("type"), QStringLiteral("audio")}, {QStringLiteral("path"), wav}, {QStringLiteral("text"), QStringLiteral("A tone")}},
        {{QStringLiteral("type"), QStringLiteral("video")}, {QStringLiteral("path"), wav}},
        {{QStringLiteral("type"), QStringLiteral("watch")}, {QStringLiteral("path"), m_dir.filePath(QStringLiteral("renders"))}},
        {{QStringLiteral("type"), QStringLiteral("stream")}, {QStringLiteral("url"), QStringLiteral("rtsp://camera.invalid/live")}},
    };
    QList<WhaleAiCanvasBlock> blocks;
    for (const QJsonObject &json : given) {
        QString error;
        const auto block = WhaleAiCanvasModel::parse(json, &error);
        QVERIFY2(block, qPrintable(error));
        blocks.append(*block);
    }
    m_panel->model()->show(blocks, false);
    auto list = findItem(QStringLiteral("blocks"));
    QVERIFY(list);
    QTRY_COMPARE(list->property("count").toInt(), int(given.size()));
    QVERIFY(!findItem(QStringLiteral("placeholder"))->isVisible());
    // Scroll through all of them, so every delegate is created at least once.
    for (int i = 0; i < given.size(); ++i) {
        QMetaObject::invokeMethod(list, "positionViewAtIndex", Q_ARG(int, i), Q_ARG(int, 0));
        QTest::qWait(30);
    }
    m_panel->clear();
    QTRY_VERIFY(findItem(QStringLiteral("placeholder"))->isVisible());
}

void WhaleAiCanvasPanelTest::testClickShowsInView()
{
    const QString a = writeImage(QStringLiteral("click.png"), Qt::red);
    WhaleAiCanvasBlock block;
    block.type = WhaleAiCanvasBlock::Type::Image;
    block.paths = {a};
    m_panel->model()->show({block}, false);
    QSignalSpy activated(m_panel, &WhaleAiCanvasPanel::localUrlActivated);
    QQuickItem *picture = nullptr;
    QTRY_VERIFY((picture = findItem(QStringLiteral("picture"))) && picture->height() > 0 && picture->width() > 0);
    const QPointF centre = picture->mapToScene(QPointF(10, picture->height() / 2));
    QTest::mouseClick(m_panel->canvasWidget(), Qt::LeftButton, {}, centre.toPoint());
    QTRY_COMPARE(activated.count(), 1);
    QCOMPARE(activated.first().first().toUrl(), QUrl::fromLocalFile(a));

    // Links in a text: local ones too; other schemes than http(s) do nothing.
    m_panel->openLink(QUrl::fromLocalFile(a).toString());
    QCOMPARE(activated.count(), 2);
    m_panel->openLink(QStringLiteral("javascript:alert(1)"));
    QCOMPARE(activated.count(), 2);
}

void WhaleAiCanvasPanelTest::testKeysStayInCanvas()
{
    // Plain keys never trigger the window's actions while the canvas has the focus (CV0).
    QVERIFY(WhaleAiQuickWidget::keepsKey(Qt::Key_Delete, Qt::NoModifier));
    QVERIFY(WhaleAiQuickWidget::keepsKey(Qt::Key_K, Qt::ShiftModifier));
    QVERIFY(!WhaleAiQuickWidget::keepsKey(Qt::Key_F5, Qt::NoModifier));
    QVERIFY(!WhaleAiQuickWidget::keepsKey(Qt::Key_C, Qt::ControlModifier));
    QVERIFY(!WhaleAiQuickWidget::keepsKey(Qt::Key_Delete, Qt::AltModifier));

    int deleted = 0;
    int reloaded = 0;
    auto deleteAction = new QAction(m_window);
    deleteAction->setShortcut(Qt::Key_Delete);
    connect(deleteAction, &QAction::triggered, [&deleted] {
        ++deleted;
    });
    auto reloadAction = new QAction(m_window);
    reloadAction->setShortcut(Qt::Key_F5);
    connect(reloadAction, &QAction::triggered, [&reloaded] {
        ++reloaded;
    });
    m_window->addAction(deleteAction);
    m_window->addAction(reloadAction);
    m_panel->canvasWidget()->setFocus();
    QTRY_VERIFY(m_panel->canvasWidget()->hasFocus());
    QTest::keyClick(m_panel->canvasWidget(), Qt::Key_Delete);
    QTest::keyClick(m_panel->canvasWidget(), Qt::Key_F5);
    QCOMPARE(deleted, 0);
    QCOMPARE(reloaded, 1);
}

void WhaleAiCanvasPanelTest::testPlaysSound()
{
    WhaleAiCanvasPlayer *player = m_panel->player();
#if HAVE_MPV
    QVERIFY(player->isAvailable());
    const QString wav = writeWav(QStringLiteral("long.wav"), 3);
    const QString url = QUrl::fromLocalFile(wav).toString();
    player->toggle(url);
    QCOMPARE(player->source(), url);
    QTRY_VERIFY_WITH_TIMEOUT(player->isLoaded(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(player->position() > 0.2, 5000);
    QVERIFY(player->isPlaying());
    player->toggle(url);
    QTRY_VERIFY(!player->isPlaying());
    // A hidden canvas plays nothing.
    player->toggle(url);
    QTRY_VERIFY(player->isPlaying());
    m_window->hide();
    QCOMPARE(player->source(), QString());
    QVERIFY(!player->isPlaying());
#else
    QVERIFY(!player->isAvailable());
#endif
}

void WhaleAiCanvasPanelTest::testTools()
{
    int shown = 0;
    QHash<QString, WhaleAiWindowTool> tools;
    for (const WhaleAiWindowTool &tool : WhaleAiCanvasTools::create(m_panel, [&shown] {
             ++shown;
         })) {
        tools.insert(tool.name, tool);
    }
    QCOMPARE(tools.size(), 3);
    QVERIFY(tools.value(QStringLiteral("get_canvas")).readOnly);
    // A block's properties go out in the order a model writes them (local models only accept
    // that order): sorted, "title" would leave only "url" to follow it.
    const QByteArray schema = WhaleAiMcpServer::serialize(tools.value(QStringLiteral("canvas_show")).properties);
    const auto position = [&schema](const QString &name) {
        return schema.indexOf(QByteArray(QByteArray("\"") + name.toLatin1() + QByteArray("\":{")));
    };
    const QStringList order{QStringLiteral("type"),
                            QStringLiteral("title"),
                            QStringLiteral("path"),
                            QStringLiteral("before"),
                            QStringLiteral("after"),
                            QStringLiteral("paths"),
                            QStringLiteral("text"),
                            QStringLiteral("url")};
    QVERIFY(position(order.constFirst()) >= 0);
    for (qsizetype i = 1; i < order.size(); ++i) {
        QVERIFY2(position(order.at(i - 1)) < position(order.at(i)), qPrintable(order.at(i)));
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
    const QString a = writeImage(QStringLiteral("tool.png"), Qt::red);
    bool error = false;
    QString notice;

    // All or nothing: a wrong block shows none and says which.
    QJsonObject result =
        call(QStringLiteral("canvas_show"),
             {{QStringLiteral("blocks"),
               QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("image")}, {QStringLiteral("path"), a}},
                          QJsonObject{{QStringLiteral("type"), QStringLiteral("image")}, {QStringLiteral("path"), QStringLiteral("/nope.png")}}}}},
             &error);
    QVERIFY(error);
    QVERIFY(result.value(QLatin1String("error")).toString().startsWith(QLatin1String("Block 1:")));
    QCOMPARE(m_panel->model()->rowCount(), 0);
    QCOMPARE(shown, 0);
    call(QStringLiteral("canvas_show"), {{QStringLiteral("blocks"), QJsonArray()}}, &error);
    QVERIFY(error);

    result = call(QStringLiteral("canvas_show"),
                  {{QStringLiteral("blocks"),
                    QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("image")}, {QStringLiteral("path"), a}},
                               QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), QStringLiteral("Done.")}}}}},
                  &error,
                  &notice);
    QVERIFY(!error);
    QCOMPARE(typeOf(result), 2);
    QCOMPARE(m_panel->model()->rowCount(), 2);
    QCOMPARE(shown, 1);
    QVERIFY(!notice.isEmpty());

    // replace clears first.
    call(QStringLiteral("canvas_show"),
         {{QStringLiteral("replace"), true},
          {QStringLiteral("blocks"),
           QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), QStringLiteral("Only")}}}}},
         &error);
    QVERIFY(!error);
    QCOMPARE(m_panel->model()->rowCount(), 1);

    result = call(QStringLiteral("get_canvas"), {}, &error, &notice);
    QVERIFY(!error);
    QVERIFY(notice.isEmpty());
    QCOMPARE(result.value(QLatin1String("blocks")).toArray().size(), 1);
    QCOMPARE(result.value(QLatin1String("blocks")).toArray().first().toObject().value(QLatin1String("text")).toString(), QStringLiteral("Only"));
    QCOMPARE(result.value(QLatin1String("visible")).toBool(), true);

    call(QStringLiteral("canvas_clear"), {}, &error, &notice);
    QVERIFY(!error);
    QVERIFY(!notice.isEmpty());
    QCOMPARE(m_panel->model()->rowCount(), 0);
    // Clearing an empty canvas is no news for the transcript.
    call(QStringLiteral("canvas_clear"), {}, &error, &notice);
    QVERIFY(notice.isEmpty());
}

QTEST_MAIN(WhaleAiCanvasPanelTest)

#include "whaleaicanvaspaneltest.moc"
