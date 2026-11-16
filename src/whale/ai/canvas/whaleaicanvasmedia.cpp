/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaicanvasmedia.h"

#include "config-dolphin.h"

#include <QGuiApplication>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>

#if HAVE_MPV
#include "whalevideoplayer.h"

#include <mpv/render_gl.h>
#endif

WhaleAiCanvasPlayer::WhaleAiCanvasPlayer(QObject *parent)
    : QObject(parent)
{
#if HAVE_MPV
    m_player = new WhaleVideoPlayer(this);
    WhaleVideoPlayer::Options options;
    options.autoplay = true;
    options.sound = true;
    options.loop = false;
    m_player->setOptions(options);
    connect(m_player, &WhaleVideoPlayer::stateChanged, this, &WhaleAiCanvasPlayer::changed);
    connect(m_player, &WhaleVideoPlayer::loaded, this, &WhaleAiCanvasPlayer::changed);
    connect(m_player, &WhaleVideoPlayer::failed, this, &WhaleAiCanvasPlayer::changed);
#endif
}

WhaleAiCanvasPlayer::~WhaleAiCanvasPlayer() = default;

bool WhaleAiCanvasPlayer::isAvailable() const
{
#if HAVE_MPV
    return m_player->isValid();
#else
    return false;
#endif
}

QString WhaleAiCanvasPlayer::source() const
{
    return m_source;
}

bool WhaleAiCanvasPlayer::isPlaying() const
{
#if HAVE_MPV
    return !m_source.isEmpty() && !m_player->isPaused() && !m_player->isAtEnd() && !m_player->hasError();
#else
    return false;
#endif
}

bool WhaleAiCanvasPlayer::isLoaded() const
{
#if HAVE_MPV
    return !m_source.isEmpty() && m_player->isLoaded();
#else
    return false;
#endif
}

bool WhaleAiCanvasPlayer::hasFailed() const
{
#if HAVE_MPV
    return !m_source.isEmpty() && m_player->hasError();
#else
    return !m_source.isEmpty();
#endif
}

double WhaleAiCanvasPlayer::position() const
{
#if HAVE_MPV
    return m_source.isEmpty() ? 0 : m_player->position();
#else
    return 0;
#endif
}

double WhaleAiCanvasPlayer::duration() const
{
#if HAVE_MPV
    return m_source.isEmpty() ? 0 : m_player->duration();
#else
    return 0;
#endif
}

WhaleVideoPlayer *WhaleAiCanvasPlayer::player() const
{
    return m_player;
}

void WhaleAiCanvasPlayer::play(const QString &url, bool video)
{
    m_source = url;
    m_pendingVideo.clear();
#if HAVE_MPV
    // Only where a video item can draw at all (not on "offscreen", the tests): there mpv would
    // fail to open its video output without the render context.
    if (video && m_player->rendersToWidget() && QQuickWindow::graphicsApi() == QSGRendererInterface::OpenGL) {
        m_pendingVideo = url;
        // The video item comes with the next frames; without one, the sound plays anyway.
        QTimer::singleShot(1500, this, [this, url] {
            if (m_pendingVideo == url) {
                videoReady();
            }
        });
        Q_EMIT changed();
        return;
    }
#else
    Q_UNUSED(video)
#endif
    load(url);
    Q_EMIT changed();
}

void WhaleAiCanvasPlayer::videoReady()
{
    if (m_pendingVideo.isEmpty() || m_pendingVideo != m_source) {
        return;
    }
    m_pendingVideo.clear();
    load(m_source);
    Q_EMIT changed();
}

void WhaleAiCanvasPlayer::load(const QString &url)
{
#if HAVE_MPV
    m_player->load(QUrl(url));
#else
    Q_UNUSED(url)
#endif
}

void WhaleAiCanvasPlayer::toggle(const QString &url, bool video)
{
    if (url != m_source) {
        play(url, video);
        return;
    }
    if (!m_pendingVideo.isEmpty()) {
        return;
    }
#if HAVE_MPV
    m_player->togglePause();
#endif
    Q_EMIT changed();
}

void WhaleAiCanvasPlayer::seek(double seconds)
{
#if HAVE_MPV
    if (!m_source.isEmpty()) {
        m_player->seekTo(seconds);
    }
#else
    Q_UNUSED(seconds)
#endif
}

void WhaleAiCanvasPlayer::stop()
{
    if (m_source.isEmpty()) {
        return;
    }
    m_source.clear();
    m_pendingVideo.clear();
#if HAVE_MPV
    m_player->stop();
#endif
    Q_EMIT changed();
}

// ---------------------------------------------------------------------------------------------

namespace
{
#if HAVE_MPV
class VideoRenderer : public QQuickFramebufferObject::Renderer
{
public:
    explicit VideoRenderer(WhaleAiCanvasVideo *item, WhaleVideoPlayer *player)
        : m_item(item)
        , m_player(player)
    {
    }

    ~VideoRenderer() override
    {
        // The GL context is current while Qt deletes the renderer.
        if (m_render) {
            mpv_render_context_set_update_callback(m_render, nullptr, nullptr);
            mpv_render_context_free(m_render);
        }
        delete m_guard;
    }

    QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override
    {
        return new QOpenGLFramebufferObject(size);
    }

    void render() override
    {
        if (!m_render && !create()) {
            return;
        }
        QOpenGLFramebufferObject *target = framebufferObject();
        mpv_opengl_fbo fbo{static_cast<int>(target->handle()), target->width(), target->height(), 0};
        int flipY = 0;
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
            {MPV_RENDER_PARAM_FLIP_Y, &flipY},
            {MPV_RENDER_PARAM_INVALID, nullptr},
        };
        mpv_render_context_render(m_render, params);
    }

private:
    static void *procAddress(void *, const char *name)
    {
        QOpenGLContext *context = QOpenGLContext::currentContext();
        return context ? reinterpret_cast<void *>(context->getProcAddress(name)) : nullptr;
    }

    static void onUpdate(void *data)
    {
        // From mpv's thread; the item may be gone by the time the GUI thread looks.
        auto guard = static_cast<QPointer<QQuickItem> *>(data);
        QQuickItem *item = guard->data();
        if (item) {
            QMetaObject::invokeMethod(item, &QQuickItem::update, Qt::QueuedConnection);
        }
    }

    bool create()
    {
        if (!m_player || !m_player->isValid()) {
            return false;
        }
        mpv_opengl_init_params glParams{procAddress, nullptr};
        const char *apiType = MPV_RENDER_API_TYPE_OPENGL;
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(apiType)},
            {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glParams},
            {MPV_RENDER_PARAM_INVALID, nullptr},
        };
        if (mpv_render_context_create(&m_render, m_player->handle(), params) < 0) {
            // The item of the block played before may still hold the player's only render
            // context until the scene graph drops it: try again with the next frame.
            m_render = nullptr;
            if (++m_attempts < 20) {
                QTimer::singleShot(50, m_item, &QQuickItem::update);
            }
            return false;
        }
        m_guard = new QPointer<QQuickItem>(m_item);
        mpv_render_context_set_update_callback(m_render, onUpdate, m_guard);
        QMetaObject::invokeMethod(m_item, "onRenderReady", Qt::QueuedConnection);
        return true;
    }

    WhaleAiCanvasVideo *m_item;
    QPointer<WhaleVideoPlayer> m_player;
    mpv_render_context *m_render = nullptr;
    QPointer<QQuickItem> *m_guard = nullptr;
    int m_attempts = 0;
};
#endif

class NoRenderer : public QQuickFramebufferObject::Renderer
{
public:
    QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override
    {
        return new QOpenGLFramebufferObject(size);
    }
    void render() override
    {
    }
};
}

WhaleAiCanvasVideo::WhaleAiCanvasVideo(QQuickItem *parent)
    : QQuickFramebufferObject(parent)
{
}

WhaleAiCanvasPlayer *WhaleAiCanvasVideo::player() const
{
    return m_player;
}

void WhaleAiCanvasVideo::setPlayer(WhaleAiCanvasPlayer *player)
{
    if (m_player == player) {
        return;
    }
    m_player = player;
    Q_EMIT playerChanged();
}

void WhaleAiCanvasVideo::onRenderReady()
{
    if (m_player) {
        m_player->videoReady();
    }
}

bool WhaleAiCanvasVideo::isSupported() const
{
    return m_supported;
}

QQuickFramebufferObject::Renderer *WhaleAiCanvasVideo::createRenderer() const
{
#if HAVE_MPV
    if (m_supported && m_player) {
        return new VideoRenderer(const_cast<WhaleAiCanvasVideo *>(this), m_player->player());
    }
#endif
    return new NoRenderer;
}

void WhaleAiCanvasVideo::itemChange(ItemChange change, const ItemChangeData &value)
{
    QQuickFramebufferObject::itemChange(change, value);
    if (change != ItemSceneChange) {
        return;
    }
    // mpv draws with OpenGL; Qt Quick on Vulkan or software has no frame buffer for it.
    bool supported = false;
#if HAVE_MPV
    // On "offscreen" (the tests) the player has no video output either.
    if (value.window && QGuiApplication::platformName() != QLatin1String("offscreen")) {
        supported = QQuickWindow::graphicsApi() == QSGRendererInterface::OpenGL;
    }
#endif
    if (supported != m_supported) {
        m_supported = supported;
        Q_EMIT supportedChanged();
    }
}

#include "moc_whaleaicanvasmedia.cpp"
