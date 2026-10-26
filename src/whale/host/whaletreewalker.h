/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALETREEWALKER_H
#define WHALETREEWALKER_H

#include "whaletreescanner.h"

#include <QByteArray>
#include <QUrl>

#include <functional>

/**
 * The scan itself: fts(3) over a local tree, run in a worker thread. Used by
 * WhaleTreeScanJob; nothing here touches Qt objects of the GUI thread.
 */
namespace WhaleTreeWalker
{
/// What a scan reports: the tree, or why there is none.
struct Outcome {
    WhaleTreeNode root;
    bool failed = false;
    WhaleTreeScanner::Error error = WhaleTreeScanner::Error::InvalidUrl;
    QString path;
    /// Rough memory of the tree's nodes, for the cache's budget.
    qint64 estimatedBytes = 0;
    int nodeCount = 0;
    /// A folder could not be watched (watch budget used up, inotify failed).
    bool unwatched = false;
};

struct Callbacks {
    /// Checked after every entry; true ends the scan.
    std::function<bool()> canceled;
    /// The number of entries visited so far, from time to time.
    std::function<void(int)> progress;
    /// Called for every folder the scan enters, before its entries are read; false if it
    /// could not be watched. Optional.
    std::function<bool(const QByteArray &path)> watchFolder;
};

/**
 * Scans @p root (any URL; only local folders are scanned, others fail).
 */
Outcome scan(const QUrl &root, const WhaleTreeScanner::Options &options, const Callbacks &callbacks);

/**
 * @return The number of scans running in this process; for tests.
 */
int runningScans();

/**
 * Tests only: @p hook is called in the worker thread with the path of every entry
 * the scan visits, before it is handled.
 */
void setVisitHookForTesting(const std::function<void(const QString &path)> &hook);
}

#endif
