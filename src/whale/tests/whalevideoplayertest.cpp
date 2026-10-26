/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * The player of videos of the built-in viewer (V3b, .privatedoc/design-notes.md), on the
 * offscreen platform (mpv without video and audio output): loading, autoplay, pause, seeking,
 * loop, sound, errors, replacing a file.
 */

#include "whalevideoplayer.h"
#include "testdir.h"
#include "whaletestvideo.h"

#include <QSignalSpy>
#include <QTest>

class WhaleVideoPlayerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    void testPlays();
    void testPausedWithoutAutoplay();
    void testSeek();
    void testLoop();
    void testOnce();
    void testSound();
    void testError();
    void testReplace();

private:
    QUrl createVideo(const QString &name);
    bool loadAndWait(WhaleVideoPlayer &player, const QUrl &url);

    TestDir *m_testDir = nullptr;
};

void WhaleVideoPlayerTest::init()
{
    m_testDir = new TestDir();
}

void WhaleVideoPlayerTest::cleanup()
{
    delete m_testDir;
    m_testDir = nullptr;
}

QUrl WhaleVideoPlayerTest::createVideo(const QString &name)
{
    m_testDir->createFile(name, whaleTestVideo());
    return QUrl::fromLocalFile(m_testDir->path() + QLatin1Char('/') + name);
}

bool WhaleVideoPlayerTest::loadAndWait(WhaleVideoPlayer &player, const QUrl &url)
{
    player.load(url);
    return QTest::qWaitFor(
        [&player]() {
            return player.isLoaded() && player.duration() > 0;
        },
        5000);
}

void WhaleVideoPlayerTest::testPlays()
{
    WhaleVideoPlayer player;
    QVERIFY(player.isValid());
    // No screen to draw on.
    QVERIFY(!player.rendersToWidget());
    const QUrl url = createVideo(QStringLiteral("clip.webm"));
    QSignalSpy loaded(&player, &WhaleVideoPlayer::loaded);
    QVERIFY(loadAndWait(player, url));
    QCOMPARE(loaded.count(), 1);
    QCOMPARE(loaded.first().first().toUrl(), url);
    QCOMPARE(player.url(), url);
    QVERIFY(qAbs(player.duration() - 2.0) < 0.1);
    QTRY_COMPARE(player.videoSize(), QSize(32, 24));
    QVERIFY(!player.isPaused());
    QVERIFY(!player.isMuted());
    QTRY_VERIFY(player.position() > 0.3);

    player.stop();
    QVERIFY(player.url().isEmpty());
    QVERIFY(!player.isLoaded());
}

void WhaleVideoPlayerTest::testPausedWithoutAutoplay()
{
    WhaleVideoPlayer player;
    player.setOptions({false, true, true});
    QVERIFY(loadAndWait(player, createVideo(QStringLiteral("clip.webm"))));
    QTRY_VERIFY(player.isPaused());
    QTest::qWait(300);
    QVERIFY(player.position() < 0.05);
    player.togglePause();
    QTRY_VERIFY(!player.isPaused());
    QTRY_VERIFY(player.position() > 0.2);
}

void WhaleVideoPlayerTest::testSeek()
{
    WhaleVideoPlayer player;
    player.setOptions({false, true, true});
    QVERIFY(loadAndWait(player, createVideo(QStringLiteral("clip.webm"))));
    player.seekTo(1.0);
    QTRY_VERIFY(qAbs(player.position() - 1.0) < 0.15);
    player.seekBy(-5);
    QTRY_VERIFY(player.position() < 0.15);
}

void WhaleVideoPlayerTest::testLoop()
{
    WhaleVideoPlayer player;
    QVERIFY(loadAndWait(player, createVideo(QStringLiteral("clip.webm"))));
    QTRY_VERIFY_WITH_TIMEOUT(player.position() > 1.5, 5000);
    // Round again, not stopped at the end.
    QTRY_VERIFY_WITH_TIMEOUT(player.position() < 1.0, 5000);
    QVERIFY(!player.isAtEnd());
    QVERIFY(!player.isPaused());
}

void WhaleVideoPlayerTest::testOnce()
{
    WhaleVideoPlayer player;
    player.setOptions({true, true, false});
    QVERIFY(loadAndWait(player, createVideo(QStringLiteral("clip.webm"))));
    // The last frame stays.
    QTRY_VERIFY_WITH_TIMEOUT(player.isAtEnd(), 5000);
    QTRY_VERIFY(player.isPaused());
    QVERIFY(player.isLoaded());
    // Playing again starts at the beginning.
    player.togglePause();
    QTRY_VERIFY(!player.isAtEnd());
    QTRY_VERIFY(!player.isPaused());
    QTRY_VERIFY(player.position() < 1.0);
}

void WhaleVideoPlayerTest::testSound()
{
    WhaleVideoPlayer player;
    player.setOptions({true, false, true});
    QVERIFY(loadAndWait(player, createVideo(QStringLiteral("clip.webm"))));
    QTRY_VERIFY(player.isMuted());
    player.toggleMute();
    QTRY_VERIFY(!player.isMuted());
}

void WhaleVideoPlayerTest::testError()
{
    m_testDir->createFile(QStringLiteral("broken.webm"), "not a video");
    const QUrl url = QUrl::fromLocalFile(m_testDir->path() + QStringLiteral("/broken.webm"));
    WhaleVideoPlayer player;
    QSignalSpy failed(&player, &WhaleVideoPlayer::failed);
    player.load(url);
    QTRY_COMPARE(failed.count(), 1);
    QCOMPARE(failed.first().first().toUrl(), url);
    QVERIFY(player.hasError());
    QVERIFY(!player.isLoaded());

    // A good file afterwards plays.
    QVERIFY(loadAndWait(player, createVideo(QStringLiteral("clip.webm"))));
    QVERIFY(!player.hasError());
}

void WhaleVideoPlayerTest::testReplace()
{
    const QUrl first = createVideo(QStringLiteral("first.webm"));
    const QUrl second = createVideo(QStringLiteral("second.webm"));
    WhaleVideoPlayer player;
    QSignalSpy loaded(&player, &WhaleVideoPlayer::loaded);
    QSignalSpy failed(&player, &WhaleVideoPlayer::failed);
    // Replaced right away: only the second one counts.
    player.load(first);
    player.load(second);
    QTRY_VERIFY(player.isLoaded());
    QTest::qWait(200);
    QCOMPARE(loaded.count(), 1);
    QCOMPARE(loaded.first().first().toUrl(), second);
    QCOMPARE(failed.count(), 0);
}

QTEST_MAIN(WhaleVideoPlayerTest)

#include "whalevideoplayertest.moc"
