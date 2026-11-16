/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAICANVASMEDIA_H
#define WHALEAICANVASMEDIA_H

#include <QPointer>
#include <QQuickFramebufferObject>

class WhaleVideoPlayer;

/**
 * @brief Plays the sounds, videos and streams of the canvas (CV1), one at a time.
 *
 * Wraps the viewer's WhaleVideoPlayer (libmpv) for QML. Without libmpv (HAVE_MPV off) it is not
 * available and the canvas offers to open the file in another program instead.
 */
class WhaleAiCanvasPlayer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ isAvailable CONSTANT)
    Q_PROPERTY(QString source READ source NOTIFY changed)
    Q_PROPERTY(bool playing READ isPlaying NOTIFY changed)
    Q_PROPERTY(bool loaded READ isLoaded NOTIFY changed)
    Q_PROPERTY(bool failed READ hasFailed NOTIFY changed)
    Q_PROPERTY(double position READ position NOTIFY changed)
    Q_PROPERTY(double duration READ duration NOTIFY changed)

public:
    explicit WhaleAiCanvasPlayer(QObject *parent = nullptr);
    ~WhaleAiCanvasPlayer() override;

    bool isAvailable() const;
    QString source() const;
    bool isPlaying() const;
    bool isLoaded() const;
    bool hasFailed() const;
    double position() const;
    double duration() const;
    WhaleVideoPlayer *player() const;

    /**
     * Plays @p url (file or stream) from the start; the one playing so far stops. With @p video,
     * mpv starts once a WhaleAiCanvasVideo for it can draw (its video output needs the render
     * context first), or after a moment without one.
     */
    Q_INVOKABLE void play(const QString &url, bool video = false);
    /** Pauses or plays @p url, starting it if another one plays. */
    Q_INVOKABLE void toggle(const QString &url, bool video = false);
    /** A WhaleAiCanvasVideo for this player can draw now. */
    void videoReady();
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE void stop();

Q_SIGNALS:
    void changed();

private:
    void load(const QString &url);

    WhaleVideoPlayer *m_player = nullptr;
    QString m_source;
    /** A video waiting for its WhaleAiCanvasVideo. */
    QString m_pendingVideo;
};

/**
 * @brief Shows the video of a WhaleAiCanvasPlayer in QML (mpv's render API, OpenGL).
 *
 * Only one item of a player may draw (mpv has one render context per player): the canvas shows
 * it only for the block that plays. Needs Qt Quick on OpenGL; "supported" tells otherwise.
 */
class WhaleAiCanvasVideo : public QQuickFramebufferObject
{
    Q_OBJECT
    Q_PROPERTY(WhaleAiCanvasPlayer *player READ player WRITE setPlayer NOTIFY playerChanged)
    Q_PROPERTY(bool supported READ isSupported NOTIFY supportedChanged)

public:
    explicit WhaleAiCanvasVideo(QQuickItem *parent = nullptr);

    WhaleAiCanvasPlayer *player() const;
    void setPlayer(WhaleAiCanvasPlayer *player);
    bool isSupported() const;
    Renderer *createRenderer() const override;

Q_SIGNALS:
    void playerChanged();
    void supportedChanged();

private Q_SLOTS:
    /** The renderer made mpv's render context. */
    void onRenderReady();

protected:
    void itemChange(ItemChange change, const ItemChangeData &value) override;

private:
    QPointer<WhaleAiCanvasPlayer> m_player;
    bool m_supported = false;
};

#endif
