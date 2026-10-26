/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef WHALETREESCANNER_H
#define WHALETREESCANNER_H

#include "whaletreenode.h"
#include "whaleviewapi_export.h"

#include <QObject>
#include <QString>
#include <QUrl>

#include <memory>

class WhaleTreeScannerPrivate;

/**
 * Options of a WhaleTreeScanner scan.
 */
struct WhaleTreeScanOptions {
    /// The deepest WhaleTreeNode::depth() in the tree; -1 for no limit. Deeper entries
    /// are still scanned and counted in the sizes and counts, but get no node.
    int maxDepth = -1;
    /// Do not enter folders on another file system (mount points) than the root's.
    bool stayOnFilesystem = true;
    /// Include entries whose name starts with a dot. (This is the scanner's own
    /// option, independent of what a view shows.)
    bool includeHidden = true;
};

/**
 * @brief Scans a local folder tree with the sizes of all entries.
 *
 * The scan runs in a worker thread; the scanner itself is used from the GUI thread,
 * and all its signals arrive there. A scan delivers either finished() with the whole
 * tree, or canceled(), or failed(); never a partial tree.
 *
 * One scan runs at a time: scan() refuses to start while one is running. cancel()
 * ends a scan at once for the caller; a new scan may follow right away.
 *
 * Rules of the tree:
 * - Symbolic links are entries of their own and are never followed, so there are no
 *   loops and nothing is counted twice. Only a link given as the root is followed.
 * - Entries that vanish while the scan runs are left out; a folder that cannot be read
 *   (no permission, vanished) is an entry without children, and WhaleTreeNode::isComplete()
 *   is false for it and the folders above it.
 * - Sizes are taken when the scan visits an entry; a file that changes later keeps the
 *   size it had then. Hard links are counted once per link.
 *
 * Only local folders (file: URLs) can be scanned so far.
 */
class WHALEVIEWAPI_EXPORT WhaleTreeScanner : public QObject
{
    Q_OBJECT

public:
    using Options = WhaleTreeScanOptions;

    enum class Error {
        /// The URL is empty or not valid.
        InvalidUrl,
        /// The URL is not a local file: URL (remote and virtual locations are not supported yet).
        NotLocal,
        /// The root does not exist.
        NotFound,
        /// The root is not a folder.
        NotAFolder,
        /// The root folder cannot be read.
        AccessDenied,
    };
    Q_ENUM(Error)

    ~WhaleTreeScanner() override;

    /**
     * Starts scanning the folder @p root and emits started().
     * @return False, and nothing happens, if a scan is running already.
     */
    virtual bool scan(const QUrl &root, const Options &options = Options()) = 0;

    /**
     * Cancels the running scan: it emits canceled() before returning, nothing else
     * follows for that scan, and isRunning() is false. Does nothing if no scan runs.
     */
    virtual void cancel() = 0;

    virtual bool isRunning() const = 0;

Q_SIGNALS:
    void started(const QUrl &root);

    /**
     * From time to time while scanning: the number of entries visited so far.
     */
    void progress(int visitedEntries);

    /**
     * The scan is complete. @p root is the root folder with the tree below it.
     */
    void finished(const WhaleTreeNode &root);

    void canceled();

    /**
     * The scan could not be done. @p message describes it for the user.
     */
    void failed(WhaleTreeScanner::Error error, const QString &message);

protected:
    explicit WhaleTreeScanner(QObject *parent = nullptr);

private:
    const std::unique_ptr<WhaleTreeScannerPrivate> d;
};

#endif
