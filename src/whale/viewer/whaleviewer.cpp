/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleviewer.h"

#include "views/dolphinview.h"
#include "whaleviewersettings.h"

#include <KLocalizedString>

#include <QFutureWatcher>
#include <QImageReader>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QTimer>
#include <QVariantAnimation>
#include <QtConcurrentRun>

namespace
{
QPointer<WhaleViewer> s_instance;

// Wheel events closer than this go together: only the file they end at is loaded.
constexpr int LoadDelay = 40;
constexpr int OverlayDuration = 1000;
constexpr int OverlayFadeDuration = 300;
constexpr int CursorHideDelay = 1500;

QImage readImage(const QString &path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    return reader.read();
}
}

WhaleViewer::WhaleViewer(DolphinView *source, const WhaleViewerList &list, QWidget *window)
    : QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint)
    , m_source(source)
    , m_window(window)
    , m_list(list)
    , m_moveSelection(!list.isSelection() || WhaleViewerSettings::multiSelection() == WhaleViewerSettings::MultiSelection::MoveSelection)
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
    return !m_loadError && !m_image.isNull() && m_loadedUrl == currentUrl();
}

bool WhaleViewer::hasLoadError() const
{
    return m_loadError && m_loadedUrl == currentUrl();
}

QString WhaleViewer::overlayText() const
{
    return m_overlayText;
}

void WhaleViewer::step(int steps)
{
    const int before = m_list.currentIndex();
    const bool stopped = m_list.move(steps, WhaleViewerSettings::wrapAround());
    if (m_list.currentIndex() != before) {
        showOverlay(m_list.currentItem().name());
        scheduleLoad();
        Q_EMIT currentChanged(currentUrl());
    } else if (stopped) {
        showOverlay(steps < 0 ? i18nc("@info:status in the viewer", "First file") : i18nc("@info:status in the viewer", "Last file"));
    }
}

void WhaleViewer::showIndex(int index)
{
    const int before = m_list.currentIndex();
    m_list.setCurrentIndex(index);
    if (m_list.currentIndex() != before) {
        showOverlay(m_list.currentItem().name());
        scheduleLoad();
        Q_EMIT currentChanged(currentUrl());
    }
}

void WhaleViewer::scheduleLoad()
{
    m_loadTimer->start();
}

void WhaleViewer::load()
{
    m_loadTimer->stop();
    followInView();

    const KFileItem item = m_list.currentItem();
    if (item.isNull()) {
        return;
    }
    const int generation = ++m_generation;
    const QUrl url = item.url();
    auto watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, generation, url]() {
        watcher->deleteLater();
        if (generation != m_generation) {
            return; // paged on meanwhile
        }
        m_image = watcher->result();
        m_loadError = m_image.isNull();
        m_loadedUrl = url;
        updateScaled();
        update();
        Q_EMIT loadFinished(url);
    });
    watcher->setFuture(QtConcurrent::run(readImage, item.localPath()));
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

void WhaleViewer::updateScaled()
{
    if (m_image.isNull()) {
        m_scaled = QPixmap();
        return;
    }
    // Fit into the window in device pixels, never larger than the image (V2 adds zoom).
    const qreal dpr = devicePixelRatioF();
    const QSize available = size() * dpr;
    QSize target = m_image.size();
    if (target.width() > available.width() || target.height() > available.height()) {
        target.scale(available, Qt::KeepAspectRatio);
    }
    m_scaled = QPixmap::fromImage(target == m_image.size() ? m_image : m_image.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    m_scaled.setDevicePixelRatio(dpr);
}

void WhaleViewer::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);

    if (!m_scaled.isNull() && !m_loadError) {
        const QSizeF size = m_scaled.deviceIndependentSize();
        const QPointF topLeft((width() - size.width()) / 2, (height() - size.height()) / 2);
        painter.drawPixmap(topLeft, m_scaled);
    }

    painter.setPen(Qt::white);
    if (m_loadError) {
        const QString name = m_list.currentItem().name();
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
    updateScaled();
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
        return; // zoom, V2
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
    unsetCursor();
    m_cursorTimer->start();
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
