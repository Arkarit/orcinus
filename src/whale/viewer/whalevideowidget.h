/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEVIDEOWIDGET_H
#define WHALEVIDEOWIDGET_H

#include <QOpenGLWidget>

class WhaleVideoPlayer;
struct mpv_render_context;

/**
 * @brief Draws the video of a WhaleVideoPlayer with mpv's render API (Whale, V3).
 *
 * Neither the keyboard nor the mouse: the viewer beneath keeps both. It must go before its
 * player.
 */
class WhaleVideoWidget : public QOpenGLWidget
{
    Q_OBJECT

public:
    WhaleVideoWidget(WhaleVideoPlayer *player, QWidget *parent);
    ~WhaleVideoWidget() override;

protected:
    void initializeGL() override;
    void paintGL() override;

private:
    WhaleVideoPlayer *m_player;
    mpv_render_context *m_render = nullptr;
};

#endif
