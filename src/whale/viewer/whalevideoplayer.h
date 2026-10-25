/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEVIDEOPLAYER_H
#define WHALEVIDEOPLAYER_H

#include <QObject>
#include <QSize>
#include <QUrl>

struct mpv_handle;

/**
 * @brief Plays the videos of the built-in viewer with libmpv (Whale, V3).
 *
 * One player for all videos of a viewer: load() replaces the file before. The player keeps the
 * state mpv reports (position, duration, pause, sound) and tells about changes; events come
 * from mpv's thread and are handled in the GUI thread.
 *
 * It draws into a WhaleVideoWidget (mpv's render API, OpenGL). On the "offscreen" platform there
 * is no screen to draw on: mpv plays without video and audio output there, which is what the
 * tests use.
 *
 * See .privatedoc/whale-viewer-plan.md.
 */
class WhaleVideoPlayer : public QObject
{
    Q_OBJECT

public:
    struct Options {
        bool autoplay = true;
        bool sound = true;
        bool loop = true;
    };

    explicit WhaleVideoPlayer(QObject *parent = nullptr);
    ~WhaleVideoPlayer() override;

    /** @return Whether mpv started; without it, every file fails. */
    bool isValid() const;
    mpv_handle *handle() const;
    /** @return Whether the video goes to a WhaleVideoWidget (not on the "offscreen" platform). */
    bool rendersToWidget() const;

    /** For the files loaded from now on; sound and loop also for the one playing. */
    void setOptions(const Options &options);
    const Options &options() const;

    /** Plays @p url (a local file) instead of the file before; paused unless autoplay. */
    void load(const QUrl &url);
    /** Stops and forgets the file. */
    void stop();

    void setPaused(bool paused);
    /** Pauses or plays; at the end of a file that does not loop, plays it from the start. */
    void togglePause();
    void seekBy(double seconds);
    void seekTo(double seconds);
    void toggleMute();

    QUrl url() const;
    /** @return Whether url() is loaded and can play. */
    bool isLoaded() const;
    /** @return Whether url() failed to load or play. */
    bool hasError() const;
    double position() const;
    double duration() const;
    bool isPaused() const;
    bool isMuted() const;
    /** @return Whether a file that does not loop stopped at its end. */
    bool isAtEnd() const;
    QSize videoSize() const;

Q_SIGNALS:
    /** @p url is loaded and plays (or waits paused). */
    void loaded(const QUrl &url);
    /** @p url could not be loaded or played. */
    void failed(const QUrl &url);
    /** Position, duration, pause, sound or the end changed. */
    void stateChanged();

private:
    Q_INVOKABLE void processEvents();
    void setFlag(const char *name, bool value);

    mpv_handle *m_mpv = nullptr;
    bool m_rendersToWidget = false;
    Options m_options;

    QUrl m_url;
    qint64 m_entryId = -1;
    bool m_loaded = false;
    bool m_error = false;
    double m_position = 0;
    double m_duration = 0;
    bool m_paused = false;
    bool m_muted = false;
    bool m_atEnd = false;
    QSize m_videoSize;
};

#endif
