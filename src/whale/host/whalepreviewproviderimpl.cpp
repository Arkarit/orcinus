/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whalepreviewproviderimpl.h"

#include "dolphinviewbridge.h"

#include <KConfigGroup>
#include <KIO/PreviewJob>
#include <KJobWidgets>
#include <KSharedConfig>

#include <QApplication>
#include <QPointer>
#include <QTimer>

WhalePreviewProviderImpl::WhalePreviewProviderImpl(DolphinViewBridge *bridge, QObject *parent)
    : WhalePreviewProvider(parent)
    , m_bridge(bridge)
{
    // The thumbnail plugins the user chose for the file list (KFileItemModelRolesUpdater).
    const KConfigGroup globalConfig(KSharedConfig::openConfig(), QStringLiteral("PreviewSettings"));
    m_enabledPlugins = globalConfig.readEntry("Plugins", KIO::PreviewJob::defaultPlugins());

    connect(bridge, &DolphinViewBridge::previewsShownChanged, this, &WhalePreviewProviderImpl::updateEnabled);
    connect(bridge, &DolphinViewBridge::urlChanged, this, &WhalePreviewProviderImpl::cancelAll);
}

WhalePreviewProviderImpl::~WhalePreviewProviderImpl()
{
    const auto jobs = m_pendingPerJob.keys();
    for (KIO::PreviewJob *job : jobs) {
        killJob(job);
    }
    killUnusedJobs();
}

bool WhalePreviewProviderImpl::isEnabled() const
{
    return m_enabled;
}

void WhalePreviewProviderImpl::setViewSupportsPreviews(bool supports)
{
    m_viewSupportsPreviews = supports;
    updateEnabled();
}

void WhalePreviewProviderImpl::updateEnabled()
{
    const bool enabled = m_viewSupportsPreviews && m_bridge->previewsShown();
    if (enabled == m_enabled) {
        return;
    }
    m_enabled = enabled;
    if (!enabled) {
        cancelAll();
    }
    Q_EMIT enabledChanged(enabled);
}

void WhalePreviewProviderImpl::requestPreviews(const KFileItemList &items, const QSize &size, qreal devicePixelRatio)
{
    if (!m_enabled || size.isEmpty()) {
        return;
    }

    KFileItemList newItems;
    for (const KFileItem &item : items) {
        if (item.isNull()) {
            continue;
        }
        const auto pending = m_pending.constFind(item.url());
        if (pending != m_pending.constEnd()) {
            if (pending->size == size && pending->devicePixelRatio == devicePixelRatio) {
                continue;
            }
            release(item.url()); // requested again with another size
        }
        newItems.append(item);
    }
    if (newItems.isEmpty()) {
        return;
    }

    auto job = new KIO::PreviewJob(newItems, size, &m_enabledPlugins);
    job->setDevicePixelRatio(devicePixelRatio);
    if (job->uiDelegate()) {
        KJobWidgets::setWindow(job, qApp->activeWindow());
    }
    connect(job, &KIO::PreviewJob::gotPreview, this, [this, job](const KFileItem &item, const QPixmap &preview) {
        slotGotPreview(job, item, preview);
    });
    connect(job, &KIO::PreviewJob::failed, this, [this, job](const KFileItem &item) {
        slotFailed(job, item);
    });
    connect(job, &KJob::finished, this, &WhalePreviewProviderImpl::slotJobFinished);

    for (const KFileItem &item : std::as_const(newItems)) {
        m_pending.insert(item.url(), Pending{job, item, size, devicePixelRatio});
    }
    m_pendingPerJob.insert(job, newItems.count());
    m_statistics.requested += newItems.count();
    ++m_statistics.jobsStarted;
    m_statistics.maxPending = std::max(m_statistics.maxPending, int(m_pending.count()));
}

void WhalePreviewProviderImpl::cancel(const QList<QUrl> &urls)
{
    for (const QUrl &url : urls) {
        if (m_pending.contains(url)) {
            release(url);
            ++m_statistics.canceled;
        }
    }
}

void WhalePreviewProviderImpl::cancelAll()
{
    cancel(m_pending.keys());
}

bool WhalePreviewProviderImpl::isPending(const QUrl &url) const
{
    return m_pending.contains(url);
}

void WhalePreviewProviderImpl::slotGotPreview(KIO::PreviewJob *job, const KFileItem &item, const QPixmap &preview)
{
    const auto pending = m_pending.constFind(item.url());
    if (pending == m_pending.constEnd() || pending->job != job) {
        return; // canceled, or requested again in the meantime
    }
    release(item.url());
    ++m_statistics.generated;
    Q_EMIT previewAvailable(item, preview);
}

void WhalePreviewProviderImpl::slotFailed(KIO::PreviewJob *job, const KFileItem &item)
{
    const auto pending = m_pending.constFind(item.url());
    if (pending == m_pending.constEnd() || pending->job != job) {
        return;
    }
    release(item.url());
    ++m_statistics.failed;
    Q_EMIT previewFailed(item);
}

void WhalePreviewProviderImpl::slotJobFinished(KJob *finishedJob)
{
    auto job = static_cast<KIO::PreviewJob *>(finishedJob);
    m_unusedJobs.removeAll(job);
    if (!m_pendingPerJob.contains(job)) {
        return;
    }
    // Items the job did not answer (e.g. after an error) get no preview.
    KFileItemList unanswered;
    for (auto it = m_pending.cbegin(); it != m_pending.cend(); ++it) {
        if (it->job == job) {
            unanswered.append(it->item);
        }
    }
    m_pendingPerJob.remove(job);
    for (const KFileItem &item : std::as_const(unanswered)) {
        m_pending.remove(item.url());
        ++m_statistics.failed;
        Q_EMIT previewFailed(item);
    }
}

void WhalePreviewProviderImpl::release(const QUrl &url)
{
    const Pending pending = m_pending.take(url);
    auto count = m_pendingPerJob.find(pending.job);
    if (count == m_pendingPerJob.end() || --*count > 0) {
        return;
    }
    // Nobody waits for this job any more. It may still have canceled items to work on;
    // it is stopped once the event loop runs (not inside its own signal), unless it
    // has finished by itself until then.
    m_pendingPerJob.erase(count);
    disconnect(pending.job, &KIO::PreviewJob::gotPreview, this, nullptr);
    disconnect(pending.job, &KIO::PreviewJob::failed, this, nullptr);
    m_unusedJobs.append(pending.job);
    QTimer::singleShot(0, this, &WhalePreviewProviderImpl::killUnusedJobs);
}

void WhalePreviewProviderImpl::killUnusedJobs()
{
    const QList<QPointer<KIO::PreviewJob>> jobs = std::exchange(m_unusedJobs, {});
    for (KIO::PreviewJob *job : jobs) {
        if (job) { // not finished by itself in the meantime
            killJob(job);
        }
    }
}

void WhalePreviewProviderImpl::killJob(KIO::PreviewJob *job)
{
    m_pendingPerJob.remove(job);
    disconnect(job, nullptr, this, nullptr);
    job->kill(KJob::Quietly);
    ++m_statistics.jobsKilled;
}

WhalePreviewProviderImpl::Statistics WhalePreviewProviderImpl::statistics() const
{
    return m_statistics;
}

QList<QUrl> WhalePreviewProviderImpl::pendingUrls() const
{
    return m_pending.keys();
}

int WhalePreviewProviderImpl::runningJobs() const
{
    return m_pendingPerJob.count();
}

#include "moc_whalepreviewproviderimpl.cpp"
