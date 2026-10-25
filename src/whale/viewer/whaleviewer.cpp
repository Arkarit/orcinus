/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleviewer.h"

#include "views/dolphinview.h"
#include "whaleviewersettings.h"

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
}

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
        update();
    });
    connect(m_overlayFade, &QVariantAnimation::finished, this, [this]() {
        m_overlayText.clear();
        update();
    });

    m_cursorTimer->setSingleShot(true);
    m_cursorTimer->setInterval(CursorHideDelay);
    connect(m_cursorTimer, &QTimer::timeout, this, [this]() {
        setCursor(Qt::BlankCursor);
    });
    m_cursorTimer->start();

    if (source) {
        connect(source, &QObject::destroyed, this, &QWidget::close);
    }
}

WhaleViewer::~WhaleViewer() = default;

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
    return !m_shownError && !m_shown.isNull() && m_shownUrl == currentUrl();
}

bool WhaleViewer::hasLoadError() const
{
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
    showOverlay(m_list.currentItem().name());
    // A preloaded file shows at once; the view follows (and the rest loads) once paging rests.
    showFromCache();
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
        if (index >= 0 && index < count && !urls.contains(items.at(index).url())) {
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
    update();
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

    painter.setPen(Qt::white);
    if (m_shownError) {
        const QString name = m_shownUrl.fileName();
        painter.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, i18nc("@info in the viewer, %1 is a file name", "%1 cannot be displayed.", name));
    }

    if (!m_overlayText.isEmpty() && m_overlayOpacity > 0) {
        QFont overlayFont = font();
        overlayFont.setPointSizeF(overlayFont.pointSizeF() * 1.25);
        painter.setFont(overlayFont);
        const QFontMetrics metrics(overlayFont);
        const int margin = metrics.height() / 2;
        const QString text = metrics.elidedText(m_overlayText, Qt::ElideMiddle, width() * 4 / 5);
        QRect box(0, 0, metrics.horizontalAdvance(text) + 2 * margin, metrics.height() + margin);
        box.moveCenter(QPoint(width() / 2, height() - 3 * metrics.height()));

        painter.setOpacity(m_overlayOpacity);
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(box, margin, margin);
        painter.fillPath(path, QColor(0, 0, 0, 170));
        painter.drawText(box, Qt::AlignCenter, text);
    }
}

void WhaleViewer::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (!m_list.isEmpty()) {
        m_cache->request(currentUrl(), availableSize(), devicePixelRatioF());
    }
    if (m_zoom > 0) {
        clampOrigin();
    }
}

void WhaleViewer::keyPressEvent(QKeyEvent *event)
{
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
    // Only the left and right fifth page (V-E10).
    const qreal x = event->position().x();
    if (x < width() / 5.0) {
        step(-1);
    } else if (x > width() * 4 / 5.0) {
        step(1);
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
    unsetCursor();
    m_cursorTimer->start();
}

void WhaleViewer::mouseReleaseEvent(QMouseEvent *event)
{
    event->accept();
    if (event->button() == Qt::MiddleButton && m_panning) {
        m_panning = false;
        unsetCursor();
        m_cursorTimer->start();
    }
}

void WhaleViewer::closeEvent(QCloseEvent *event)
{
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
