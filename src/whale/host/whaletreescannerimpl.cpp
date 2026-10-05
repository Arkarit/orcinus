/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaletreescannerimpl.h"

#include "whaletreecache.h"
#include "whaletreewalker.h"

#include <KLocalizedString>

WhaleTreeScannerImpl::WhaleTreeScannerImpl(WhaleTreeCache *cache, QObject *parent)
    : WhaleTreeScanner(parent)
    , m_cache(cache)
{
}

WhaleTreeScannerImpl::~WhaleTreeScannerImpl()
{
    // Quietly: the scan goes on for other consumers, or stops if there are none.
    leaveJob();
}

bool WhaleTreeScannerImpl::scan(const QUrl &root, const Options &options)
{
    if (m_running) {
        return false;
    }
    m_running = true;
    const quint64 scan = ++m_scan;
    Q_EMIT started(root);

    if (m_cache) {
        if (const std::optional<WhaleTreeWalker::Outcome> kept = m_cache->lookup(root, options)) {
            // A kept tree: delivered as a scan would, not within scan().
            QMetaObject::invokeMethod(
                this,
                [this, scan, outcome = *kept]() {
                    if (m_running && scan == m_scan) {
                        m_running = false;
                        deliver(outcome);
                    }
                },
                Qt::QueuedConnection);
            return true;
        }
        m_job = m_cache->job(root, options);
    } else {
        m_job = new WhaleTreeScanJob(root, options, 0, {}, this);
    }
    m_job->addConsumer();
    connect(m_job, &WhaleTreeScanJob::progress, this, &WhaleTreeScanner::progress);
    connect(m_job, &WhaleTreeScanJob::done, this, [this]() {
        const WhaleTreeWalker::Outcome outcome = m_job->outcome();
        m_job->disconnect(this);
        m_job = nullptr; // done: nothing to leave
        m_running = false;
        deliver(outcome);
    });
    return true;
}

void WhaleTreeScannerImpl::cancel()
{
    if (!m_running) {
        return;
    }
    m_running = false;
    ++m_scan;
    leaveJob();
    Q_EMIT canceled();
}

bool WhaleTreeScannerImpl::isRunning() const
{
    return m_running;
}

void WhaleTreeScannerImpl::leaveJob()
{
    if (m_job) {
        m_job->disconnect(this);
        m_job->removeConsumer();
        m_job = nullptr;
    }
}

int WhaleTreeScannerImpl::runningWorkers()
{
    return WhaleTreeWalker::runningScans();
}

void WhaleTreeScannerImpl::setVisitHookForTesting(const std::function<void(const QString &)> &hook)
{
    WhaleTreeWalker::setVisitHookForTesting(hook);
}

void WhaleTreeScannerImpl::deliver(const WhaleTreeWalker::Outcome &outcome)
{
    if (!outcome.failed) {
        Q_EMIT finished(outcome.root);
        return;
    }

    QString message;
    switch (outcome.error) {
    case Error::InvalidUrl:
        message = i18nc("@info", "The location is not valid.");
        break;
    case Error::NotLocal:
        message = i18nc("@info %1 is a location", "Only local folders can be scanned, not %1.", outcome.path);
        break;
    case Error::NotFound:
        message = i18nc("@info %1 is a path", "The folder %1 does not exist.", outcome.path);
        break;
    case Error::NotAFolder:
        message = i18nc("@info %1 is a path", "%1 is not a folder.", outcome.path);
        break;
    case Error::AccessDenied:
        message = i18nc("@info %1 is a path", "The folder %1 cannot be read.", outcome.path);
        break;
    }
    Q_EMIT failed(outcome.error, message);
}

#include "moc_whaletreescannerimpl.cpp"
