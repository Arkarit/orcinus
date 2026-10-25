/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whalevideowidget.h"

#include "whalevideoplayer.h"

#include <QGuiApplication>
#include <QOpenGLContext>
#include <QPointer>
#include <qguiapplication_platform.h>

#include <mpv/render_gl.h>

namespace
{
void *procAddress(void *, const char *name)
{
    QOpenGLContext *context = QOpenGLContext::currentContext();
    return context ? reinterpret_cast<void *>(context->getProcAddress(name)) : nullptr;
}

void onUpdate(void *context)
{
    // From mpv's render thread: a new frame is due.
    QPointer<QWidget> widget(static_cast<QWidget *>(context));
    QMetaObject::invokeMethod(
        static_cast<QWidget *>(context),
        [widget]() {
            if (widget) {
                widget->update();
            }
        },
        Qt::QueuedConnection);
}
}

WhaleVideoWidget::WhaleVideoWidget(WhaleVideoPlayer *player, QWidget *parent)
    : QOpenGLWidget(parent)
    , m_player(player)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);
}

WhaleVideoWidget::~WhaleVideoWidget()
{
    if (m_render) {
        makeCurrent();
        mpv_render_context_set_update_callback(m_render, nullptr, nullptr);
        mpv_render_context_free(m_render);
        doneCurrent();
    }
}

void WhaleVideoWidget::initializeGL()
{
    if (!m_player->isValid() || m_render) {
        return;
    }
    mpv_opengl_init_params glParams{procAddress, nullptr};
    const char *apiType = MPV_RENDER_API_TYPE_OPENGL;
    QList<mpv_render_param> params{
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(apiType)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glParams},
    };
    // The display lets mpv decode in hardware and hand the frames over without copies.
#if QT_CONFIG(wayland)
    if (auto wayland = qApp->nativeInterface<QNativeInterface::QWaylandApplication>()) {
        params.append({MPV_RENDER_PARAM_WL_DISPLAY, wayland->display()});
    }
#endif
#if QT_CONFIG(xcb)
    if (auto x11 = qApp->nativeInterface<QNativeInterface::QX11Application>()) {
        params.append({MPV_RENDER_PARAM_X11_DISPLAY, x11->display()});
    }
#endif
    params.append({MPV_RENDER_PARAM_INVALID, nullptr});
    if (mpv_render_context_create(&m_render, m_player->handle(), params.data()) < 0) {
        m_render = nullptr;
        return;
    }
    mpv_render_context_set_update_callback(m_render, onUpdate, this);
}

void WhaleVideoWidget::paintGL()
{
    if (!m_render) {
        return;
    }
    const qreal dpr = devicePixelRatioF();
    mpv_opengl_fbo fbo{static_cast<int>(defaultFramebufferObject()), qRound(width() * dpr), qRound(height() * dpr), 0};
    int flipY = 1;
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flipY},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    mpv_render_context_render(m_render, params);
}

#include "moc_whalevideowidget.cpp"
