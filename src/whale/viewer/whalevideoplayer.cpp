/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whalevideoplayer.h"

#include <QGuiApplication>
#include <QMetaObject>

#include <mpv/client.h>

#include <clocale>

namespace
{
enum Property : quint64 {
    TimePos = 1,
    Duration,
    Pause,
    Mute,
    EofReached,
    Width,
    Height,
};

void wakeup(void *context)
{
    // From mpv's thread: handled in the GUI thread (dropped if the player is gone by then).
    QMetaObject::invokeMethod(static_cast<QObject *>(context), "processEvents", Qt::QueuedConnection);
}

qint64 entryIdOf(const mpv_node &result)
{
    if (result.format != MPV_FORMAT_NODE_MAP) {
        return -1;
    }
    for (int i = 0; i < result.u.list->num; ++i) {
        if (qstrcmp(result.u.list->keys[i], "playlist_entry_id") == 0 && result.u.list->values[i].format == MPV_FORMAT_INT64) {
            return result.u.list->values[i].u.int64;
        }
    }
    return -1;
}
}

WhaleVideoPlayer::WhaleVideoPlayer(QObject *parent)
    : QObject(parent)
{
    // libmpv refuses to start with another numeric locale.
    std::setlocale(LC_NUMERIC, "C");
    m_mpv = mpv_create();
    if (!m_mpv) {
        return;
    }
    m_rendersToWidget = QGuiApplication::platformName() != QLatin1String("offscreen");
    mpv_set_option_string(m_mpv, "config", "no");
    mpv_set_option_string(m_mpv, "terminal", "no");
    mpv_set_option_string(m_mpv, "input-default-bindings", "no");
    mpv_set_option_string(m_mpv, "input-vo-keyboard", "no");
    mpv_set_option_string(m_mpv, "osc", "no");
    mpv_set_option_string(m_mpv, "idle", "yes");
    // At the end of a file that does not loop, the last frame stays.
    mpv_set_option_string(m_mpv, "keep-open", "yes");
    mpv_set_option_string(m_mpv, "hwdec", "auto-safe");
    mpv_set_option_string(m_mpv, "vo", m_rendersToWidget ? "libmpv" : "null");
    if (!m_rendersToWidget) {
        mpv_set_option_string(m_mpv, "ao", "null");
    }
    if (mpv_initialize(m_mpv) < 0) {
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
        return;
    }
    mpv_observe_property(m_mpv, TimePos, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, Duration, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, Pause, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, Mute, "mute", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, EofReached, "eof-reached", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, Width, "dwidth", MPV_FORMAT_INT64);
    mpv_observe_property(m_mpv, Height, "dheight", MPV_FORMAT_INT64);
    mpv_set_wakeup_callback(m_mpv, wakeup, this);
    setOptions(m_options);
}

WhaleVideoPlayer::~WhaleVideoPlayer()
{
    if (m_mpv) {
        mpv_set_wakeup_callback(m_mpv, nullptr, nullptr);
        mpv_terminate_destroy(m_mpv);
    }
}

bool WhaleVideoPlayer::isValid() const
{
    return m_mpv;
}

mpv_handle *WhaleVideoPlayer::handle() const
{
    return m_mpv;
}

bool WhaleVideoPlayer::rendersToWidget() const
{
    return m_rendersToWidget;
}

void WhaleVideoPlayer::setOptions(const Options &options)
{
    m_options = options;
    if (!m_mpv) {
        return;
    }
    mpv_set_property_string(m_mpv, "loop-file", options.loop ? "inf" : "no");
    setFlag("mute", !options.sound);
}

const WhaleVideoPlayer::Options &WhaleVideoPlayer::options() const
{
    return m_options;
}

void WhaleVideoPlayer::load(const QUrl &url)
{
    m_url = url;
    m_entryId = -1;
    m_loaded = false;
    m_error = false;
    m_position = 0;
    m_duration = 0;
    m_atEnd = false;
    m_videoSize = QSize();
    if (!m_mpv) {
        m_error = true;
        Q_EMIT failed(url);
        return;
    }
    setFlag("pause", !m_options.autoplay);
    const QByteArray path = url.toLocalFile().toUtf8();
    const char *command[] = {"loadfile", path.constData(), "replace", nullptr};
    mpv_node result;
    if (mpv_command_ret(m_mpv, command, &result) < 0) {
        m_error = true;
        Q_EMIT failed(url);
        return;
    }
    m_entryId = entryIdOf(result);
    mpv_free_node_contents(&result);
    Q_EMIT stateChanged();
}

void WhaleVideoPlayer::stop()
{
    m_url = QUrl();
    m_entryId = -1;
    m_loaded = false;
    m_error = false;
    if (m_mpv) {
        const char *command[] = {"stop", nullptr};
        mpv_command(m_mpv, command);
    }
}

void WhaleVideoPlayer::setPaused(bool paused)
{
    setFlag("pause", paused);
}

void WhaleVideoPlayer::togglePause()
{
    if (m_paused && m_atEnd) {
        seekTo(0);
    }
    setPaused(!m_paused);
}

void WhaleVideoPlayer::seekBy(double seconds)
{
    if (m_mpv && m_loaded) {
        const QByteArray amount = QByteArray::number(seconds);
        const char *command[] = {"seek", amount.constData(), "relative", nullptr};
        mpv_command(m_mpv, command);
    }
}

void WhaleVideoPlayer::seekTo(double seconds)
{
    if (m_mpv && m_loaded) {
        const QByteArray target = QByteArray::number(seconds);
        const char *command[] = {"seek", target.constData(), "absolute", nullptr};
        mpv_command(m_mpv, command);
    }
}

void WhaleVideoPlayer::toggleMute()
{
    setFlag("mute", !m_muted);
}

QUrl WhaleVideoPlayer::url() const
{
    return m_url;
}

bool WhaleVideoPlayer::isLoaded() const
{
    return m_loaded && !m_error;
}

bool WhaleVideoPlayer::hasError() const
{
    return m_error;
}

double WhaleVideoPlayer::position() const
{
    return m_position;
}

double WhaleVideoPlayer::duration() const
{
    return m_duration;
}

bool WhaleVideoPlayer::isPaused() const
{
    return m_paused;
}

bool WhaleVideoPlayer::isMuted() const
{
    return m_muted;
}

bool WhaleVideoPlayer::isAtEnd() const
{
    return m_atEnd;
}

QSize WhaleVideoPlayer::videoSize() const
{
    return m_videoSize;
}

void WhaleVideoPlayer::setFlag(const char *name, bool value)
{
    if (m_mpv) {
        int flag = value ? 1 : 0;
        mpv_set_property(m_mpv, name, MPV_FORMAT_FLAG, &flag);
    }
}

void WhaleVideoPlayer::processEvents()
{
    while (m_mpv) {
        const mpv_event *event = mpv_wait_event(m_mpv, 0);
        if (event->event_id == MPV_EVENT_NONE) {
            break;
        }
        switch (event->event_id) {
        case MPV_EVENT_FILE_LOADED: {
            // Only the file asked for last: one replaced meanwhile may still report.
            const QByteArray path = m_url.toLocalFile().toUtf8();
            char *playing = mpv_get_property_string(m_mpv, "path");
            const bool current = playing && path == playing;
            mpv_free(playing);
            if (current && !m_loaded && !m_error) {
                m_loaded = true;
                Q_EMIT loaded(m_url);
            }
            break;
        }
        case MPV_EVENT_END_FILE: {
            const auto endFile = static_cast<const mpv_event_end_file *>(event->data);
            if (endFile->reason == MPV_END_FILE_REASON_ERROR && endFile->playlist_entry_id == m_entryId && !m_url.isEmpty()) {
                m_error = true;
                m_loaded = false;
                Q_EMIT failed(m_url);
            }
            break;
        }
        case MPV_EVENT_PROPERTY_CHANGE: {
            const auto property = static_cast<const mpv_event_property *>(event->data);
            const bool known = property->format != MPV_FORMAT_NONE && property->data;
            switch (event->reply_userdata) {
            case TimePos:
                m_position = known ? *static_cast<double *>(property->data) : 0;
                break;
            case Duration:
                m_duration = known ? *static_cast<double *>(property->data) : 0;
                break;
            case Pause:
                m_paused = known && *static_cast<int *>(property->data);
                break;
            case Mute:
                m_muted = known && *static_cast<int *>(property->data);
                break;
            case EofReached:
                m_atEnd = known && *static_cast<int *>(property->data);
                break;
            case Width:
                m_videoSize.setWidth(known ? int(*static_cast<int64_t *>(property->data)) : 0);
                break;
            case Height:
                m_videoSize.setHeight(known ? int(*static_cast<int64_t *>(property->data)) : 0);
                break;
            }
            Q_EMIT stateChanged();
            break;
        }
        default:
            break;
        }
    }
}

#include "moc_whalevideoplayer.cpp"
