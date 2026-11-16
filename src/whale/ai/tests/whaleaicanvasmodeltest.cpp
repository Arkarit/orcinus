/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaicanvasmodel.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

class WhaleAiCanvasModelTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void testParse_data();
    void testParse();
    void testShowAndLimit();
    void testJsonRoundTrip();
    void testWatch();

private:
    QString write(const QString &name, const QByteArray &data = QByteArray("x"));
    QString writeImage(const QString &name, const QColor &color);
    QTemporaryDir m_dir;
};

QString WhaleAiCanvasModelTest::write(const QString &name, const QByteArray &data)
{
    const QString path = m_dir.filePath(name);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(data);
    }
    return path;
}

QString WhaleAiCanvasModelTest::writeImage(const QString &name, const QColor &color)
{
    const QString path = m_dir.filePath(name);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QImage image(8, 8, QImage::Format_RGB32);
    image.fill(color);
    image.save(path);
    return path;
}

void WhaleAiCanvasModelTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    writeImage(QStringLiteral("a.png"), Qt::red);
    writeImage(QStringLiteral("b.png"), Qt::blue);
    write(QStringLiteral("song.wav"));
    write(QStringLiteral("clip.mp4"));
    QDir().mkpath(m_dir.filePath(QStringLiteral("folder")));
}

void WhaleAiCanvasModelTest::testParse_data()
{
    QTest::addColumn<QJsonObject>("json");
    QTest::addColumn<bool>("valid");
    const QString a = m_dir.filePath(QStringLiteral("a.png"));
    const QString b = m_dir.filePath(QStringLiteral("b.png"));
    const QString folder = m_dir.filePath(QStringLiteral("folder"));
    auto o = [](std::initializer_list<std::pair<QString, QJsonValue>> list) {
        QJsonObject json;
        for (const auto &[key, value] : list) {
            json.insert(key, value);
        }
        return json;
    };
    // Valid ones; file:// URLs count as paths.
    QTest::newRow("image") << o({{"type", "image"}, {"path", a}, {"title", "Red"}, {"text", "caption"}}) << true;
    QTest::newRow("image url") << o({{"type", "image"}, {"path", QUrl::fromLocalFile(a).toString()}}) << true;
    QTest::newRow("audio") << o({{"type", "audio"}, {"path", m_dir.filePath(QStringLiteral("song.wav"))}}) << true;
    QTest::newRow("video") << o({{"type", "video"}, {"path", m_dir.filePath(QStringLiteral("clip.mp4"))}}) << true;
    QTest::newRow("text") << o({{"type", "text"}, {"text", "# Hello"}}) << true;
    QTest::newRow("files") << o({{"type", "files"}, {"paths", QJsonArray{a, b, folder}}}) << true;
    QTest::newRow("compare") << o({{"type", "compare"}, {"paths", QJsonArray{a, b}}}) << true;
    QTest::newRow("watch") << o({{"type", "watch"}, {"path", folder}}) << true;
    QTest::newRow("stream") << o({{"type", "stream"}, {"url", "rtsp://camera.local/live"}}) << true;
    // Invalid ones.
    QTest::newRow("no type") << o({{"path", a}}) << false;
    QTest::newRow("unknown type") << o({{"type", "html"}, {"text", "<b>x</b>"}}) << false;
    QTest::newRow("relative path") << o({{"type", "image"}, {"path", "a.png"}}) << false;
    QTest::newRow("missing file") << o({{"type", "image"}, {"path", m_dir.filePath(QStringLiteral("nope.png"))}}) << false;
    QTest::newRow("image is folder") << o({{"type", "image"}, {"path", folder}}) << false;
    QTest::newRow("image two paths") << o({{"type", "image"}, {"paths", QJsonArray{a, b}}}) << false;
    QTest::newRow("watch a file") << o({{"type", "watch"}, {"path", a}}) << false;
    QTest::newRow("compare one") << o({{"type", "compare"}, {"paths", QJsonArray{a}}}) << false;
    QTest::newRow("files empty") << o({{"type", "files"}, {"paths", QJsonArray()}}) << false;
    QTest::newRow("text empty") << o({{"type", "text"}, {"text", "  "}}) << false;
    QTest::newRow("text too long") << o({{"type", "text"}, {"text", QString(WhaleAiCanvasModel::MaxText + 1, QLatin1Char('x'))}}) << false;
    QTest::newRow("stream local") << o({{"type", "stream"}, {"url", QUrl::fromLocalFile(a).toString()}}) << false;
    QTest::newRow("stream javascript") << o({{"type", "stream"}, {"url", "javascript:alert(1)"}}) << false;
    QTest::newRow("title too long") << o({{"type", "text"}, {"text", "x"}, {"title", QString(300, QLatin1Char('t'))}}) << false;
}

void WhaleAiCanvasModelTest::testParse()
{
    QFETCH(QJsonObject, json);
    QFETCH(bool, valid);
    QString error;
    const auto block = WhaleAiCanvasModel::parse(json, &error);
    QCOMPARE(block.has_value(), valid);
    QCOMPARE(error.isEmpty(), valid);
}

void WhaleAiCanvasModelTest::testShowAndLimit()
{
    WhaleAiCanvasModel model;
    QSignalSpy count(&model, &WhaleAiCanvasModel::countChanged);
    WhaleAiCanvasBlock text;
    text.type = WhaleAiCanvasBlock::Type::Text;
    text.text = QStringLiteral("one");
    model.show({text, text}, false);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(count.count(), 1);
    QCOMPARE(model.data(model.index(0), WhaleAiCanvasModel::TypeRole).toString(), QStringLiteral("text"));
    QCOMPARE(model.data(model.index(0), WhaleAiCanvasModel::TextRole).toString(), QStringLiteral("one"));

    text.text = QStringLiteral("two");
    model.show({text}, true);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0), WhaleAiCanvasModel::TextRole).toString(), QStringLiteral("two"));

    // Beyond the limit, the oldest go.
    QList<WhaleAiCanvasBlock> many;
    for (int i = 0; i < WhaleAiCanvasModel::MaxBlocks; ++i) {
        text.text = QString::number(i);
        many.append(text);
    }
    model.show(many, false);
    QCOMPARE(model.rowCount(), WhaleAiCanvasModel::MaxBlocks);
    QCOMPARE(model.data(model.index(0), WhaleAiCanvasModel::TextRole).toString(), QStringLiteral("0"));

    model.clear();
    QCOMPARE(model.rowCount(), 0);
}

void WhaleAiCanvasModelTest::testJsonRoundTrip()
{
    const QString a = m_dir.filePath(QStringLiteral("a.png"));
    const QString b = m_dir.filePath(QStringLiteral("b.png"));
    const QJsonArray given{
        QJsonObject{{QStringLiteral("type"), QStringLiteral("image")}, {QStringLiteral("path"), a}, {QStringLiteral("title"), QStringLiteral("Red")}},
        QJsonObject{{QStringLiteral("type"), QStringLiteral("compare")}, {QStringLiteral("paths"), QJsonArray{a, b}}},
        QJsonObject{{QStringLiteral("type"), QStringLiteral("files")}, {QStringLiteral("paths"), QJsonArray{a}}},
        QJsonObject{{QStringLiteral("type"), QStringLiteral("stream")}, {QStringLiteral("url"), QStringLiteral("https://example.org/live.m3u8")}},
    };
    WhaleAiCanvasModel model;
    QList<WhaleAiCanvasBlock> blocks;
    for (const QJsonValue &value : given) {
        QString error;
        const auto block = WhaleAiCanvasModel::parse(value.toObject(), &error);
        QVERIFY2(block, qPrintable(error));
        blocks.append(*block);
    }
    model.show(blocks, false);
    QCOMPARE(model.toJson(), given);
    QCOMPARE(model.data(model.index(1), WhaleAiCanvasModel::NamesRole).toStringList(), QStringList({QStringLiteral("a.png"), QStringLiteral("b.png")}));
    QCOMPARE(model.data(model.index(0), WhaleAiCanvasModel::UrlsRole).toStringList(), QStringList{QUrl::fromLocalFile(a).toString()});
    QCOMPARE(model.data(model.index(0), WhaleAiCanvasModel::IconsRole).toStringList(), QStringList{QStringLiteral("image-png")});
}

void WhaleAiCanvasModelTest::testWatch()
{
    // A watched folder shows its newest image or video and follows it.
    const QString folder = m_dir.filePath(QStringLiteral("renders"));
    QDir().mkpath(folder);
    WhaleAiCanvasBlock watch;
    watch.type = WhaleAiCanvasBlock::Type::Watch;
    watch.paths = {folder};
    WhaleAiCanvasModel model;
    model.show({watch}, false);
    const QModelIndex index = model.index(0);
    QCOMPARE(model.data(index, WhaleAiCanvasModel::CurrentRole).toString(), QString());

    write(QStringLiteral("renders/notes.txt"));
    const QString first = writeImage(QStringLiteral("renders/frame1.png"), Qt::green);
    QTRY_COMPARE(model.data(index, WhaleAiCanvasModel::CurrentRole).toString(), QUrl::fromLocalFile(first).toString());

    // Time stamps of the file system may be coarse: the next one is clearly newer.
    QThread::msleep(1100);
    const QString second = writeImage(QStringLiteral("renders/frame2.png"), Qt::yellow);
    QTRY_COMPARE(model.data(index, WhaleAiCanvasModel::CurrentRole).toString(), QUrl::fromLocalFile(second).toString());

    // Overwritten in place: the same file, a new revision.
    const int revision = model.data(index, WhaleAiCanvasModel::RevisionRole).toInt();
    QThread::msleep(1100);
    writeImage(QStringLiteral("renders/frame2.png"), Qt::black);
    QTRY_VERIFY(model.data(index, WhaleAiCanvasModel::RevisionRole).toInt() > revision);
    QCOMPARE(WhaleAiCanvasModel::newestMedia(folder), second);
}

QTEST_GUILESS_MAIN(WhaleAiCanvasModelTest)

#include "whaleaicanvasmodeltest.moc"
