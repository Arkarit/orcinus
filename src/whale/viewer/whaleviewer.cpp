/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleviewer.h"

#include "config-dolphin.h"
#include "views/dolphinview.h"
#include "whaleviewersettings.h"
#if HAVE_MPV
#include "whalevideoplayer.h"
#include "whalevideowidget.h"
#endif

#include <KLocalizedString>

#include <QKeyEvent>
#include <QMovie>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QTimer>
#include <QVariantAnimation>
#include <QtMath>

namespace
{
QPointer<WhaleViewer> s_instance;

// Wheel events closer than this go together: only the file they end at is loaded.
constexpr int LoadDelay = 40;
constexpr int OverlayDuration = 1000;
constexpr int OverlayFadeDuration = 300;
constexpr int CursorHideDelay = 1500;
constexpr qreal ZoomStep = 1.25;
constexpr qreal MaxZoom = 32;
constexpr double SeekStep = 5;
}

/// Over the image or video (a video widget covers what the viewer paints itself).
class WhaleViewerOverlay : public QWidget
{
public:
    explicit WhaleViewerOverlay(WhaleViewer *viewer)
        : QWidget(viewer)
        , m_viewer(viewer)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event)
        QPainter painter(this);
        m_viewer->paintOverlay(painter);
    }

private:
    WhaleViewer *m_viewer;
};

WhaleViewer::WhaleViewer(DolphinView *source, const WhaleViewerList &list, QWidget *window)
    : QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint)
    , m_source(source)
    , m_window(window)
    , m_list(list)
    , m_moveSelection(!list.isSelection() || WhaleViewerSettings::multiSelection() == WhaleViewerSettings::MultiSelection::MoveSelection)
    , m_cache(new WhaleViewerCache(this))
    , m_loadTimer(new QTimer(this))
    , m_overlayTimer(new QTimer(this))
    , m_cursorTimer(new QTimer(this))
    , m_overlayFade(new QVariantAnimation(this))
    , m_overlayWidget(new WhaleViewerOverlay(this))
{
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setWindowTitle(i18nc("@title:window", "Viewer"));

    m_loadTimer->setSingleShot(true);
    m_loadTimer->setInterval(LoadDelay);
    connect(m_loadTimer, &QTimer::timeout, this, &WhaleViewer::load);
    m_upscale = WhaleViewerSettings::upscale();
    m_smoothZoom = WhaleViewerSettings::smoothZoom();
    m_cache->setUpscale(m_upscale);
    connect(m_cache, &WhaleViewerCache::imageReady, this, &WhaleViewer::slotImageReady);

    m_overlayTimer->setSingleShot(true);
    m_overlayTimer->setInterval(OverlayDuration);
    connect(m_overlayTimer, &QTimer::timeout, m_overlayFade, [this]() {
        m_overlayFade->start();
    });
    m_overlayFade->setStartValue(1.0);
    m_overlayFade->setEndValue(0.0);
    m_overlayFade->setDuration(OverlayFadeDuration);
    connect(m_overlayFade, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_overlayOpacity = value.toReal();
        m_overlayWidget->update();
    });
    connect(m_overlayFade, &QVariantAnimation::finished, this, [this]() {
        m_overlayText.clear();
        m_overlayWidget->update();
    });

    m_cursorTimer->setSingleShot(true);
    m_cursorTimer->setInterval(CursorHideDelay);
    connect(m_cursorTimer, &QTimer::timeout, this, [this]() {
        if (m_seeking) {
            return;
        }
        setCursor(Qt::BlankCursor);
        m_pointerShown = false;
        m_overlayWidget->update();
    });
    m_cursorTimer->start();

    if (source) {
        connect(source, &QObject::destroyed, this, &QWidget::close);
    }
}

WhaleViewer::~WhaleViewer()
{
    // The widget's render context goes before the player.
    delete m_videoWidget;
    delete m_videoPlayer;
}

void WhaleViewer::install()
{
    DolphinView::setShownInternally([](const KFileItem &item) {
        return WhaleViewerList::isDisplayable(item, WhaleViewerSettings::mimeTypes());
    });
}

bool WhaleViewer::openItem(const KFileItem &item, DolphinView *view, QWidget *window)
{
    const QStringList mimeTypes = WhaleViewerSettings::mimeTypes();
    if (!view || !WhaleViewerList::isDisplayable(item, mimeTypes)) {
        return false;
    }
    show(view, WhaleViewerList::fromFolder(view->items(), item, mimeTypes), window);
    return true;
}

KFileItemList WhaleViewer::openItems(const KFileItemList &items, DolphinView *view, QWidget *window)
{
    if (!view) {
        return items;
    }
    const QStringList mimeTypes = WhaleViewerSettings::mimeTypes();
    KFileItemList shown;
    KFileItemList others;
    for (const KFileItem &item : items) {
        (WhaleViewerList::isDisplayable(item, mimeTypes) ? shown : others).append(item);
    }
    if (shown.count() == 1) {
        openItem(shown.first(), view, window);
    } else if (shown.count() > 1) {
        show(view, WhaleViewerList::fromSelection(shown, view->currentItemUrl(), mimeTypes), window);
    }
    return others;
}

void WhaleViewer::show(DolphinView *source, const WhaleViewerList &list, QWidget *window)
{
    if (s_instance) {
        s_instance->close();
    }
    auto viewer = new WhaleViewer(source, list, window);
    s_instance = viewer;
    QScreen *screen = window ? window->screen() : nullptr;
    if (screen) {
        viewer->setScreen(screen);
        viewer->setGeometry(screen->geometry());
    }
    viewer->showFullScreen();
    viewer->activateWindow();
    viewer->setFocus();
    viewer->showOverlay(list.currentItem().name());
    viewer->load();
}

WhaleViewer *WhaleViewer::instance()
{
    return s_instance;
}

const WhaleViewerList &WhaleViewer::list() const
{
    return m_list;
}

QUrl WhaleViewer::currentUrl() const
{
    return m_list.currentItem().url();
}

bool WhaleViewer::isLoaded() const
{
    if (isVideo()) {
        return m_videoPlayer && m_videoPlayer->url() == currentUrl() && m_videoPlayer->isLoaded();
    }
    return !m_shownError && !m_shown.isNull() && m_shownUrl == currentUrl();
}

bool WhaleViewer::hasLoadError() const
{
    if (isVideo()) {
        return m_videoPlayer && m_videoPlayer->url() == currentUrl() && m_videoPlayer->hasError();
    }
    return m_shownError && m_shownUrl == currentUrl();
}

bool WhaleViewer::movesSelection() const
{
    return m_moveSelection;
}

QString WhaleViewer::overlayText() const
{
    return m_overlayText;
}

const WhaleViewerCache &WhaleViewer::cache() const
{
    return *m_cache;
}

void WhaleViewer::step(int steps)
{
    const int before = m_list.currentIndex();
    const bool stopped = m_list.move(steps, WhaleViewerSettings::wrapAround());
    if (m_list.currentIndex() != before) {
        pagedTo(steps < 0 ? -1 : 1);
    } else if (stopped) {
        showOverlay(steps < 0 ? i18nc("@info:status in the viewer", "First file") : i18nc("@info:status in the viewer", "Last file"));
    }
}

void WhaleViewer::showIndex(int index)
{
    const int before = m_list.currentIndex();
    m_list.setCurrentIndex(index);
    if (m_list.currentIndex() != before) {
        pagedTo(m_list.currentIndex() < before ? -1 : 1);
    }
}

void WhaleViewer::pagedTo(int direction)
{
    m_direction = direction;
    m_zoom = 0;
    stopAnimation();
    stopVideo();
    showOverlay(m_list.currentItem().name());
    if (isVideo()) {
        // Black until it plays, not the image before.
        m_shown = QImage();
        m_shownUrl = currentUrl();
        m_shownError = false;
        update();
    } else {
        // A preloaded file shows at once; the view follows (and the rest loads) once paging rests.
        showFromCache();
    }
    scheduleLoad();
    Q_EMIT currentChanged(currentUrl());
}

void WhaleViewer::scheduleLoad()
{
    m_loadTimer->start();
}

void WhaleViewer::load()
{
    m_loadTimer->stop();
    followInView();

    if (m_list.isEmpty()) {
        return;
    }
    const QUrl url = currentUrl();
    m_cache->setCurrent(url);
    m_cache->retainOnly(wantedUrls());
    if (isVideo()) {
        startVideo();
        preload();
        return;
    }
    const WhaleViewerCache::Entry *entry = m_cache->entry(url);
    if (entry && (entry->error || entry->available == availableSize())) {
        showFromCache();
        startAnimation();
        Q_EMIT loadFinished(url);
        preload();
    } else {
        m_cache->request(url, availableSize(), devicePixelRatioF());
    }
}

void WhaleViewer::preload()
{
    const QList<QUrl> urls = wantedUrls();
    for (const QUrl &url : urls) {
        m_cache->request(url, availableSize(), devicePixelRatioF());
    }
}

void WhaleViewer::slotImageReady(const QUrl &url)
{
    if (url != currentUrl()) {
        return; // a neighbour
    }
    // Also for another window size, or the full image again for zoom: then it is no news.
    const bool wasLoaded = isLoaded();
    showFromCache();
    if (!m_loadTimer->isActive()) {
        if (!wasLoaded) {
            startAnimation();
            Q_EMIT loadFinished(url);
        }
        preload();
    }
}

bool WhaleViewer::showFromCache()
{
    const WhaleViewerCache::Entry *entry = m_cache->entry(currentUrl());
    if (!entry) {
        return false;
    }
    m_shown = entry->scaled;
    m_shownUrl = currentUrl();
    m_shownError = entry->error;
    update();
    return true;
}

QList<QUrl> WhaleViewer::wantedUrls() const
{
    QList<QUrl> urls;
    const KFileItemList items = m_list.items();
    const int count = items.count();
    const int current = m_list.currentIndex();
    const bool wrapAround = WhaleViewerSettings::wrapAround();
    for (const int offset : {0, m_direction, -m_direction, 2 * m_direction}) {
        int index = current + offset;
        if (wrapAround) {
            index = ((index % count) + count) % count;
        }
        // Videos are not decoded ahead.
        if (index >= 0 && index < count && !urls.contains(items.at(index).url()) && !WhaleViewerList::isVideo(items.at(index))) {
            urls.append(items.at(index).url());
        }
    }
    return urls;
}

QSize WhaleViewer::availableSize() const
{
    return size() * devicePixelRatioF();
}

const WhaleViewerCache::Entry *WhaleViewer::shownEntry() const
{
    return isLoaded() ? m_cache->entry(currentUrl()) : nullptr;
}

qreal WhaleViewer::fittedZoom() const
{
    const WhaleViewerCache::Entry *entry = shownEntry();
    if (!entry || entry->fullSize.isEmpty()) {
        return 0;
    }
    return qreal(WhaleViewerCache::fittedSize(entry->fullSize, availableSize(), m_upscale).width()) / entry->fullSize.width();
}

QRectF WhaleViewer::imageRect() const
{
    const WhaleViewerCache::Entry *entry = shownEntry();
    if (!entry) {
        return QRectF();
    }
    const QSizeF size = QSizeF(entry->fullSize) * zoom() / devicePixelRatioF();
    if (m_zoom > 0) {
        return QRectF(m_origin, size);
    }
    return QRectF(QPointF((width() - size.width()) / 2, (height() - size.height()) / 2), size);
}

bool WhaleViewer::isZoomed() const
{
    return m_zoom > 0;
}

qreal WhaleViewer::zoom() const
{
    return m_zoom > 0 ? m_zoom : fittedZoom();
}

QRectF WhaleViewer::visibleImageRect() const
{
    const QRectF image = imageRect();
    if (image.isEmpty()) {
        return QRectF();
    }
    const QRectF visible = image.intersected(QRectF(rect()));
    return QRectF(imagePosition(visible.topLeft()), imagePosition(visible.bottomRight()));
}

QPointF WhaleViewer::imagePosition(const QPointF &position) const
{
    const QRectF image = imageRect();
    if (image.isEmpty()) {
        return QPointF();
    }
    return (position - image.topLeft()) * devicePixelRatioF() / zoom();
}

void WhaleViewer::setZoom(qreal zoom, const QPointF &anchor)
{
    const WhaleViewerCache::Entry *entry = shownEntry();
    const qreal fitted = fittedZoom();
    if (!entry || fitted <= 0) {
        return;
    }
    zoom = qBound(qMin(fitted, 1.0), zoom, MaxZoom);
    if (qFuzzyCompare(zoom, fitted) || (fitted <= 1 && zoom < fitted)) {
        fitToWindow();
        return;
    }
    const QPointF point = imagePosition(anchor);
    m_zoom = zoom;
    m_origin = anchor - point * zoom / devicePixelRatioF();
    clampOrigin();
    // The full image of a neighbour may have been dropped for the memory budget.
    m_cache->requestFull(currentUrl(), devicePixelRatioF());
    update();
}

void WhaleViewer::zoomBy(qreal factor, const QPointF &anchor)
{
    setZoom(zoom() * factor, anchor);
}

void WhaleViewer::fitToWindow()
{
    m_zoom = 0;
    update();
}

void WhaleViewer::pan(const QPointF &delta)
{
    if (m_zoom > 0) {
        m_origin += delta;
        clampOrigin();
        update();
    }
}

void WhaleViewer::clampOrigin()
{
    const QSizeF size = imageRect().size();
    // Smaller than the window: centred; larger: no black edge where there is more image.
    m_origin.setX(size.width() <= width() ? (width() - size.width()) / 2 : qBound(width() - size.width(), m_origin.x(), 0.0));
    m_origin.setY(size.height() <= height() ? (height() - size.height()) / 2 : qBound(height() - size.height(), m_origin.y(), 0.0));
}

bool WhaleViewer::isVideo() const
{
    return WhaleViewerList::isVideo(m_list.currentItem());
}

WhaleVideoPlayer *WhaleViewer::videoPlayer() const
{
    return m_videoPlayer;
}

void WhaleViewer::startVideo()
{
#if HAVE_MPV
    if (!m_videoPlayer) {
        m_videoPlayer = new WhaleVideoPlayer(this);
        m_videoPlayer->setOptions({WhaleViewerSettings::videoAutoplay(), WhaleViewerSettings::videoSound(), WhaleViewerSettings::videoLoop()});
        connect(m_videoPlayer, &WhaleVideoPlayer::loaded, this, [this](const QUrl &url) {
            if (url == currentUrl()) {
                Q_EMIT loadFinished(url);
            }
        });
        connect(m_videoPlayer, &WhaleVideoPlayer::failed, this, [this](const QUrl &url) {
            if (url == currentUrl()) {
                if (m_videoWidget) {
                    m_videoWidget->hide();
                }
                update();
                Q_EMIT loadFinished(url);
            }
        });
        connect(m_videoPlayer, &WhaleVideoPlayer::stateChanged, m_overlayWidget, qOverload<>(&QWidget::update));
        if (m_videoPlayer->rendersToWidget()) {
            m_videoWidget = new WhaleVideoWidget(m_videoPlayer, this);
            m_videoWidget->setGeometry(rect());
            m_overlayWidget->raise();
        }
    }
    if (m_videoWidget) {
        m_videoWidget->show();
    }
    m_videoPlayer->load(currentUrl());
#endif
}

void WhaleViewer::stopVideo()
{
    if (m_videoPlayer && !m_videoPlayer->url().isEmpty()) {
        m_videoPlayer->stop();
    }
    if (m_videoWidget) {
        m_videoWidget->hide();
    }
}

bool WhaleViewer::isAnimating() const
{
    return m_movie && m_movie->state() == QMovie::Running;
}

int WhaleViewer::animationFrame() const
{
    return m_movie ? m_movie->currentFrameNumber() : -1;
}

void WhaleViewer::startAnimation()
{
    const WhaleViewerCache::Entry *entry = shownEntry();
    if (m_movie || !entry || !entry->animated) {
        return;
    }
    m_movie = new QMovie(currentUrl().toLocalFile(), QByteArray(), this);
    connect(m_movie, &QMovie::frameChanged, this, [this]() {
        m_frame = m_movie->currentImage();
        update();
    });
    // Over and over, whatever the file says.
    connect(m_movie, &QMovie::finished, m_movie, &QMovie::start);
    m_movie->start();
}

void WhaleViewer::stopAnimation()
{
    delete m_movie;
    m_movie = nullptr;
    m_frame = QImage();
}

void WhaleViewer::followInView()
{
    if (m_source && !m_list.isEmpty()) {
        m_source->showViewerItem(currentUrl(), m_moveSelection);
    }
}

void WhaleViewer::showOverlay(const QString &text)
{
    m_overlayFade->stop();
    m_overlayText = text;
    m_overlayOpacity = 1.0;
    m_overlayTimer->start();
    m_overlayWidget->update();
}

void WhaleViewer::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);

    const WhaleViewerCache::Entry *entry = shownEntry();
    if (m_zoom > 0 && entry) {
        const QRectF image = imageRect();
        const QRectF visible = image.intersected(QRectF(rect()));
        const QImage &full = m_frame.isNull() ? entry->full : m_frame;
        if (!full.isNull()) {
            // Only the visible part, from the full image.
            const qreal scale = image.width() / entry->fullSize.width();
            const QRectF source((visible.topLeft() - image.topLeft()) / scale, visible.size() / scale);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, m_smoothZoom || m_zoom < 1);
            painter.drawImage(visible, full, source);
        } else {
            // Until the full image is there again.
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.drawImage(image, m_shown);
        }
    } else if (!m_frame.isNull() && entry) {
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(imageRect(), m_frame);
    } else if (!m_shown.isNull() && !m_shownError) {
        // Fitted in another window size (just resized): drawn scaled until the new one is there.
        QSizeF size = m_shown.deviceIndependentSize();
        if (size.width() > width() || size.height() > height()) {
            size.scale(this->size(), Qt::KeepAspectRatio);
        }
        const QRectF target(QPointF((width() - size.width()) / 2, (height() - size.height()) / 2), size);
        painter.drawImage(target, m_shown);
    }

    if (hasLoadError()) {
        painter.setPen(Qt::white);
        const QString name = currentUrl().fileName();
        painter.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, i18nc("@info in the viewer, %1 is a file name", "%1 cannot be displayed.", name));
    }
}

void WhaleViewer::paintOverlay(QPainter &painter)
{
    painter.setPen(Qt::white);
    const bool controls = controlsVisible();
    if (controls) {
        paintControls(painter);
    }
    if (!m_overlayText.isEmpty() && m_overlayOpacity > 0) {
        QFont overlayFont = font();
        overlayFont.setPointSizeF(overlayFont.pointSizeF() * 1.25);
        painter.setFont(overlayFont);
        const QFontMetrics metrics(overlayFont);
        const int margin = metrics.height() / 2;
        const QString text = metrics.elidedText(m_overlayText, Qt::ElideMiddle, width() * 4 / 5);
        QRect box(0, 0, metrics.horizontalAdvance(text) + 2 * margin, metrics.height() + margin);
        // Above the controls while they show.
        const int bottom = controls ? height() - controlBarRect().top() + metrics.height() : 2 * metrics.height();
        box.moveCenter(QPoint(width() / 2, height() - bottom - metrics.height()));

        painter.setOpacity(m_overlayOpacity);
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(box, margin, margin);
        painter.fillPath(path, QColor(0, 0, 0, 170));
        painter.drawText(box, Qt::AlignCenter, text);
    }
}

bool WhaleViewer::controlsVisible() const
{
#if HAVE_MPV
    return isVideo() && isLoaded() && (m_pointerShown || m_seeking || m_videoPlayer->isPaused());
#else
    return false;
#endif
}

QRect WhaleViewer::controlBarRect() const
{
    const int line = fontMetrics().height();
    return QRect(line, height() - 3 * line, width() - 2 * line, 2 * line);
}

QRect WhaleViewer::playButtonRect() const
{
    const QRect bar = controlBarRect();
    return QRect(bar.topLeft(), QSize(bar.height(), bar.height()));
}

QRect WhaleViewer::seekAreaRect() const
{
    // Between the button and the time ("0:00:00 / 0:00:00" and "Muted" at most).
    const QRect bar = controlBarRect();
    const QFontMetrics metrics = fontMetrics();
    const int text = metrics.horizontalAdvance(QStringLiteral("0:00:00 / 0:00:00"))
        + metrics.horizontalAdvance(i18nc("@info in the controls of a video in the viewer", "Muted")) + 3 * metrics.height();
    return QRect(QPoint(playButtonRect().right() + 1, bar.top()), QPoint(qMax(playButtonRect().right() + 1, bar.right() - text), bar.bottom()));
}

static QString timeText(double seconds)
{
    const int total = qMax(0, qRound(seconds));
    const int hours = total / 3600;
    const int minutes = total / 60 % 60;
    const QString rest = QStringLiteral("%1:%2").arg(minutes, hours > 0 ? 2 : 1, 10, QLatin1Char('0')).arg(total % 60, 2, 10, QLatin1Char('0'));
    return hours > 0 ? QStringLiteral("%1:%2").arg(hours).arg(rest) : rest;
}

void WhaleViewer::paintControls(QPainter &painter)
{
#if HAVE_MPV
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    const QFontMetrics metrics = fontMetrics();
    const int line = metrics.height();
    const QRect bar = controlBarRect();
    QPainterPath background;
    background.addRoundedRect(bar, line / 2, line / 2);
    painter.fillPath(background, QColor(0, 0, 0, 170));

    // Play or pause, as a click on it does.
    const QRectF button = QRectF(playButtonRect()).adjusted(line * 0.6, line * 0.55, -line * 0.6, -line * 0.55);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    if (m_videoPlayer->isPaused()) {
        painter.drawPolygon(QPolygonF{button.topLeft(), QPointF(button.right(), button.center().y()), button.bottomLeft()});
    } else {
        const qreal barWidth = button.width() / 3;
        painter.drawRect(QRectF(button.topLeft(), QSizeF(barWidth, button.height())));
        painter.drawRect(QRectF(QPointF(button.right() - barWidth, button.top()), QSizeF(barWidth, button.height())));
    }

    // The progress.
    const QRect area = seekAreaRect();
    const qreal thickness = line / 4.0;
    const QRectF track(area.left() + line / 2.0, area.center().y() - thickness / 2, area.width() - line, thickness);
    const double duration = m_videoPlayer->duration();
    const qreal fraction = duration > 0 ? qBound(0.0, m_videoPlayer->position() / duration, 1.0) : 0;
    painter.setBrush(QColor(255, 255, 255, 90));
    painter.drawRoundedRect(track, thickness / 2, thickness / 2);
    painter.setBrush(Qt::white);
    painter.drawRoundedRect(QRectF(track.topLeft(), QSizeF(track.width() * fraction, thickness)), thickness / 2, thickness / 2);
    painter.drawEllipse(QPointF(track.left() + track.width() * fraction, track.center().y()), line / 3.0, line / 3.0);

    // The time, and whether the sound is off.
    painter.setPen(Qt::white);
    QString text = QStringLiteral("%1 / %2").arg(timeText(m_videoPlayer->position()), timeText(duration));
    if (m_videoPlayer->isMuted()) {
        text = i18nc("@info in the controls of a video in the viewer", "Muted") + QStringLiteral("   ") + text;
    }
    painter.drawText(QRect(QPoint(area.right() + 1, bar.top()), QPoint(bar.right() - line, bar.bottom())), Qt::AlignRight | Qt::AlignVCenter, text);
    painter.restore();
#else
    Q_UNUSED(painter)
#endif
}

void WhaleViewer::seekToPosition(qreal x)
{
#if HAVE_MPV
    const QRect area = seekAreaRect();
    const qreal line = fontMetrics().height();
    const qreal fraction = qBound(0.0, (x - area.left() - line / 2) / (area.width() - line), 1.0);
    m_videoPlayer->seekTo(fraction * m_videoPlayer->duration());
#else
    Q_UNUSED(x)
#endif
}

void WhaleViewer::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    m_overlayWidget->setGeometry(rect());
    if (m_videoWidget) {
        m_videoWidget->setGeometry(rect());
    }
    if (!m_list.isEmpty()) {
        m_cache->request(currentUrl(), availableSize(), devicePixelRatioF());
    }
    if (m_zoom > 0) {
        clampOrigin();
    }
}

void WhaleViewer::keyPressEvent(QKeyEvent *event)
{
    if (isVideo() && m_videoPlayer) {
        // V-E17; Up, Down and the rest page as with images.
        switch (event->key()) {
        case Qt::Key_Left:
            m_videoPlayer->seekBy(-SeekStep);
            event->accept();
            return;
        case Qt::Key_Right:
            m_videoPlayer->seekBy(SeekStep);
            event->accept();
            return;
        case Qt::Key_Space:
            m_videoPlayer->togglePause();
            event->accept();
            return;
        case Qt::Key_M:
            m_videoPlayer->toggleMute();
            event->accept();
            return;
        default:
            break;
        }
    }
    switch (event->key()) {
    case Qt::Key_Escape:
        close();
        break;
    case Qt::Key_Left:
    case Qt::Key_Up:
    case Qt::Key_PageUp:
    case Qt::Key_Backspace:
        step(-1);
        break;
    case Qt::Key_Right:
    case Qt::Key_Down:
    case Qt::Key_PageDown:
        step(1);
        break;
    case Qt::Key_Home:
        showIndex(0);
        break;
    case Qt::Key_End:
        showIndex(m_list.count() - 1);
        break;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        zoomBy(ZoomStep, QRectF(rect()).center());
        break;
    case Qt::Key_Minus:
        zoomBy(1 / ZoomStep, QRectF(rect()).center());
        break;
    case Qt::Key_1:
        setZoom(1, QRectF(rect()).center());
        break;
    case Qt::Key_0:
        fitToWindow();
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
    event->accept();
}

void WhaleViewer::wheelEvent(QWheelEvent *event)
{
    event->accept();
    if (event->modifiers() & Qt::ControlModifier) {
        zoomBy(qPow(ZoomStep, event->angleDelta().y() / 120.0), event->position());
        return;
    }
    // Accumulate: one notch is 120; touchpads send less at a time.
    m_wheelDelta += event->angleDelta().y();
    const int notches = m_wheelDelta / 120;
    m_wheelDelta -= notches * 120;
    if (notches != 0) {
        step(-notches);
    }
}

void WhaleViewer::mousePressEvent(QMouseEvent *event)
{
    event->accept();
    if (event->button() == Qt::MiddleButton && m_zoom > 0) {
        m_panning = true;
        m_panPosition = event->position();
        m_cursorTimer->stop();
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (event->button() != Qt::LeftButton) {
        return;
    }
    // The controls of a video (V-E18) before the edges.
    if (controlsVisible() && controlBarRect().contains(event->position().toPoint())) {
        if (playButtonRect().contains(event->position().toPoint())) {
            m_videoPlayer->togglePause();
        } else if (seekAreaRect().contains(event->position().toPoint())) {
            m_seeking = true;
            seekToPosition(event->position().x());
        }
        return;
    }
    // Only the left and right fifth page (V-E10); in between, a click pauses a video (V-E19).
    const qreal x = event->position().x();
    if (x < width() / 5.0) {
        step(-1);
    } else if (x > width() * 4 / 5.0) {
        step(1);
    } else if (isVideo() && isLoaded()) {
        m_videoPlayer->togglePause();
    }
}

void WhaleViewer::mouseMoveEvent(QMouseEvent *event)
{
    QWidget::mouseMoveEvent(event);
    if (m_panning) {
        pan(event->position() - m_panPosition);
        m_panPosition = event->position();
        return;
    }
    if (m_seeking) {
        seekToPosition(event->position().x());
    }
    unsetCursor();
    m_cursorTimer->start();
    if (!m_pointerShown) {
        m_pointerShown = true;
        m_overlayWidget->update();
    }
}

void WhaleViewer::mouseReleaseEvent(QMouseEvent *event)
{
    event->accept();
    if (event->button() == Qt::LeftButton && m_seeking) {
        m_seeking = false;
        m_overlayWidget->update();
    }
    if (event->button() == Qt::MiddleButton && m_panning) {
        m_panning = false;
        unsetCursor();
        m_cursorTimer->start();
    }
}

void WhaleViewer::closeEvent(QCloseEvent *event)
{
    stopVideo();
    // A file paged to but not loaded yet: the view still shows where the viewer stopped.
    if (m_loadTimer->isActive()) {
        m_loadTimer->stop();
        followInView();
    }
    QWidget::closeEvent(event);
    if (m_window) {
        m_window->activateWindow();
    }
    if (m_source) {
        m_source->setFocus();
    }
}

#include "moc_whaleviewer.cpp"
