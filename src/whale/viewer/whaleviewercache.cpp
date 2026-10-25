/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleviewercache.h"

#include <QColorSpace>
#include <QFutureWatcher>
#include <QImageReader>
#include <QThreadPool>
#include <QtConcurrentTask>

namespace
{
// Qt refuses images over 256 MB (about 64 megapixels) by default: panoramas are larger.
constexpr int AllocationLimitMB = 2048;
constexpr qint64 DefaultBudget = 512 * 1024 * 1024;
// Two threads: the shown file and one neighbour at a time, without taking every core.
constexpr int ThreadCount = 2;
constexpr int CurrentPriority = 1;

// Lives as long as the process: closing the viewer does not wait for a large image to finish
// decoding; the tasks only touch copies.
QThreadPool *pool()
{
    static QThreadPool *const instance = []() {
        auto threadPool = new QThreadPool;
        threadPool->setMaxThreadCount(ThreadCount);
        return threadPool;
    }();
    return instance;
}

struct Decoded {
    QImage full;
    QImage scaled;
    bool animated = false;
};

// The window is sRGB to the compositor: other profiles (Adobe RGB, Display P3, CMYK …) are
// converted, or their colours come out wrong.
void convertToSRgb(QImage &image)
{
    const QColorSpace colorSpace = image.colorSpace();
    if (!colorSpace.isValid() || colorSpace == QColorSpace::SRgb) {
        return;
    }
    if (colorSpace.colorModel() == QColorSpace::ColorModel::Rgb) {
        image.convertToColorSpace(QColorSpace::SRgb);
    } else {
        image.convertToColorSpace(QColorSpace::SRgb, image.hasAlphaChannel() ? QImage::Format_ARGB32 : QImage::Format_RGB32);
    }
}

Decoded decode(const QString &path, QImage full, QSize available, qreal dpr, bool upscale, std::shared_ptr<std::atomic_bool> cancelled)
{
    if (*cancelled) {
        return {};
    }
    bool animated = false;
    if (full.isNull()) {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        reader.setAllocationLimit(AllocationLimitMB);
        full = reader.read();
        convertToSRgb(full);
        animated = reader.supportsAnimation() && reader.imageCount() > 1;
    }
    if (full.isNull() || *cancelled) {
        return {full, QImage(), false};
    }
    const QSize target = WhaleViewerCache::fittedSize(full.size(), available, upscale);
    QImage scaled = target == full.size() ? full : full.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    scaled.setDevicePixelRatio(dpr);
    return {full, scaled, animated};
}
}

WhaleViewerCache::WhaleViewerCache(QObject *parent)
    : QObject(parent)
    , m_budget(DefaultBudget)
{
}

WhaleViewerCache::~WhaleViewerCache()
{
    for (const Pending &pending : std::as_const(m_pending)) {
        *pending.cancelled = true;
    }
}

QSize WhaleViewerCache::fittedSize(const QSize &imageSize, const QSize &available, bool upscale)
{
    QSize target = imageSize;
    if (upscale || target.width() > available.width() || target.height() > available.height()) {
        target.scale(available, Qt::KeepAspectRatio);
    }
    return target.expandedTo(QSize(1, 1));
}

void WhaleViewerCache::setUpscale(bool upscale)
{
    m_upscale = upscale;
}

void WhaleViewerCache::request(const QUrl &url, const QSize &available, qreal dpr)
{
    const auto entryIt = m_entries.constFind(url);
    if (entryIt != m_entries.constEnd() && (entryIt->error || entryIt->available == available)) {
        return;
    }
    const auto pendingIt = m_pending.constFind(url);
    if (pendingIt != m_pending.constEnd() && pendingIt->available == available) {
        return;
    }
    // An image already decoded is only scaled again.
    start(url, entryIt != m_entries.constEnd() ? entryIt->full : QImage(), available, dpr);
}

void WhaleViewerCache::requestFull(const QUrl &url, qreal dpr)
{
    const auto entryIt = m_entries.constFind(url);
    if (entryIt == m_entries.constEnd() || entryIt->error || !entryIt->full.isNull() || m_pending.contains(url)) {
        return;
    }
    start(url, QImage(), entryIt->available, dpr);
}

void WhaleViewerCache::start(const QUrl &url, const QImage &full, const QSize &available, qreal dpr)
{
    const auto pendingIt = m_pending.constFind(url);
    if (pendingIt != m_pending.constEnd()) {
        *pendingIt->cancelled = true;
    }
    auto cancelled = std::make_shared<std::atomic_bool>(false);
    m_pending.insert(url, {cancelled, available});

    auto watcher = new QFutureWatcher<Decoded>(this);
    const bool rescale = !full.isNull();
    connect(watcher, &QFutureWatcher<Decoded>::finished, this, [this, watcher, url, cancelled, available, rescale]() {
        watcher->deleteLater();
        const auto it = m_pending.constFind(url);
        if (it == m_pending.constEnd() || it->cancelled != cancelled) {
            return; // forgotten or asked for again meanwhile
        }
        m_pending.erase(it);
        const Decoded decoded = watcher->result();
        Entry entry;
        entry.full = decoded.full;
        entry.fullSize = decoded.full.size();
        entry.scaled = decoded.scaled;
        entry.available = available;
        entry.error = decoded.scaled.isNull();
        // Only scaled again: the file was not read.
        const Entry *before = rescale ? this->entry(url) : nullptr;
        entry.animated = before ? before->animated : decoded.animated;
        m_entries.insert(url, entry);
        enforceBudget();
        Q_EMIT imageReady(url);
    });
    const int priority = url == m_current ? CurrentPriority : 0;
    watcher->setFuture(QtConcurrent::task(&decode)
                           .withArguments(url.toLocalFile(), full, available, dpr, m_upscale, cancelled)
                           .onThreadPool(*pool())
                           .withPriority(priority)
                           .spawn());
}

void WhaleViewerCache::setCurrent(const QUrl &url)
{
    m_current = url;
    enforceBudget();
}

void WhaleViewerCache::retainOnly(const QList<QUrl> &urls)
{
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        it = urls.contains(it.key()) ? std::next(it) : m_entries.erase(it);
    }
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (urls.contains(it.key())) {
            ++it;
        } else {
            *it->cancelled = true;
            it = m_pending.erase(it);
        }
    }
}

const WhaleViewerCache::Entry *WhaleViewerCache::entry(const QUrl &url) const
{
    const auto it = m_entries.constFind(url);
    return it == m_entries.constEnd() ? nullptr : &*it;
}

QList<QUrl> WhaleViewerCache::urls() const
{
    return m_entries.keys();
}

bool WhaleViewerCache::isPending(const QUrl &url) const
{
    return m_pending.contains(url);
}

void WhaleViewerCache::setFullImageBudget(qint64 bytes)
{
    m_budget = bytes;
    enforceBudget();
}

void WhaleViewerCache::enforceBudget()
{
    qint64 used = 0;
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
        if (it.key() != m_current) {
            used += it->full.sizeInBytes();
        }
    }
    for (auto it = m_entries.begin(); it != m_entries.end() && used > m_budget; ++it) {
        if (it.key() != m_current && !it->full.isNull()) {
            used -= it->full.sizeInBytes();
            it->full = QImage();
        }
    }
}

#include "moc_whaleviewercache.cpp"
