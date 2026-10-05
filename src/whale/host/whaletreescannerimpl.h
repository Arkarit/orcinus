/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALETREESCANNERIMPL_H
#define WHALETREESCANNERIMPL_H

#include "whaletreescanner.h"

#include <QPointer>

#include <functional>

class WhaleTreeCache;
class WhaleTreeScanJob;
namespace WhaleTreeWalker
{
struct Outcome;
}

/**
 * @brief The WhaleTreeScanner of a view: a consumer of the scans of a WhaleTreeCache.
 *
 * With a cache, scan() hands out a kept tree, or joins a running scan for the same root
 * and options, or starts one; cancel() only leaves that scan, which goes on for its
 * other consumers. Without a cache, every scan runs on its own (WhaleTreeScanJob).
 *
 * Signals are the same in all cases: started() within scan(), then exactly one of
 * finished(), failed() or canceled() later; finished() and failed() never come within
 * scan() (a kept tree is delivered once the event loop runs), canceled() comes within
 * cancel(). progress() only comes while a scan really runs.
 */
class WhaleTreeScannerImpl : public WhaleTreeScanner
{
    Q_OBJECT

public:
    /**
     * @param cache The shared scans; null to scan on its own.
     */
    explicit WhaleTreeScannerImpl(WhaleTreeCache *cache = nullptr, QObject *parent = nullptr);
    ~WhaleTreeScannerImpl() override;

    bool scan(const QUrl &root, const Options &options = Options()) override;
    void cancel() override;
    bool isRunning() const override;

    /**
     * @return The number of scan workers still running in this process, also those of
     *         canceled scans; for tests.
     */
    static int runningWorkers();

    /**
     * Tests only: calls @p hook in the worker thread with the path of every entry the
     * worker visits, before it is handled (e.g. to change files during a scan).
     */
    static void setVisitHookForTesting(const std::function<void(const QString &path)> &hook);

private:
    void leaveJob();
    void deliver(const WhaleTreeWalker::Outcome &outcome);

    QPointer<WhaleTreeCache> m_cache;
    QPointer<WhaleTreeScanJob> m_job;
    bool m_running = false;
    quint64 m_scan = 0; // the current scan, so a late delivery of an old one is dropped
};

#endif
