/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALERECURSIVECONTENT_H
#define WHALERECURSIVECONTENT_H

#include "whaletreescanner.h"

#include <KFileItem>

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QUrl>

#include <atomic>
#include <memory>

class DolphinView;
class DolphinViewBridge;
class QMimeData;
class QTimer;
class WhaleTreeCache;
class WhaleTreeNode;
class WhaleTreeScannerImpl;
template<typename T>
class QFutureWatcher;
namespace WhaleRecursiveProjection
{
struct Result;
}

/**
 * @brief The file list of a view area showing the files below its location (F1,
 * "Show flat (no folders)", WhaleContentScope::RecursiveFiles).
 *
 * The file list stays Dolphin's own (Icons, Compact, Details, with its selection,
 * actions, previews, sorting, filter and drags); only its items change. While enabled,
 * the DolphinView's model takes its items from here (KFileItemModel external items)
 * instead of the location's listing:
 *
 *   WhaleTreeScanner (shared cache, M8b) -> WhaleRecursiveProjection (worker) -> model
 *
 * - Files only, no folder rows; local folders only (the host enables it for those).
 * - Hidden files follow "Show Hidden Files": without them the scan leaves out hidden
 *   entries and does not enter hidden folders (the scanner's includeHidden).
 * - Loading the location (navigation, F5) scans again; so does a change below it that the
 *   cache reports (any file operation, also by other programs), after a short delay. The
 *   model keeps unchanged items, so selection and position survive.
 * - Drops into the view are refused: its items come from many folders, so there is no
 *   destination. A drag from elsewhere is explained once (requestMessage()).
 */
class WhaleRecursiveContent : public QObject
{
    Q_OBJECT

public:
    WhaleRecursiveContent(DolphinView *dolphinView, DolphinViewBridge *bridge, QObject *parent = nullptr);
    ~WhaleRecursiveContent() override;

    /**
     * The tree scans of the window; without them every scan runs on its own.
     */
    void setTreeCache(WhaleTreeCache *cache);

    /**
     * Shows the files below the location (true) or the location's own items (false). The
     * selection is kept as far as its items are still shown; no navigation happens.
     */
    void setEnabled(bool enabled);
    bool isEnabled() const;

    /**
     * @return True while the files of the location are scanned or projected.
     */
    bool isBusy() const;

    /**
     * @return The number of files of the last projection (before the filter bar).
     */
    int fileCount() const;

    /**
     * Timing of the last update, in milliseconds (-1: not measured); for tests and
     * development tools.
     */
    struct Timing {
        qint64 scanMs = -1; ///< scan, or the cache hit
        qint64 projectionMs = -1; ///< KFileItems in the worker
        qint64 modelMs = -1; ///< KFileItemModel::setExternalItems() in the GUI thread
        int folderLinks = 0;
        int updates = 0; ///< projections applied to the model since enabled
    };
    Timing timing() const;

    /**
     * What would create new items in the view, which has no destination folder.
     */
    enum class Refusal {
        Paste,
        Drop,
        Create, ///< Create New, New Folder, Duplicate
        Transfer, ///< copying or moving items here from the other half of a split view
    };

    /**
     * @return The explanation for refusing @p kind in the view.
     */
    static QString refusal(Refusal kind);

    /**
     * @return The explanation for refusing to paste into the view, or an empty string
     *         while it shows the location's own items.
     */
    QString pasteRefusal() const;

    /**
     * @return The explanation for refusing drops into the view.
     */
    static QString dropRefusal();

    /**
     * @return The last explanation shown for a refused drop or paste; for tests and
     *         development tools.
     */
    QString lastRefusal() const;
    int refusalCount() const;

    /**
     * Shows @p message as the explanation of a refused drop, paste or the like
     * (requestMessage()); also for the plugin views of the host without a destination.
     */
    void explain(const QString &message);

Q_SIGNALS:
    /**
     * Enabled, busy or the file count changed.
     */
    void stateChanged();

    /**
     * @p message should be shown to the user in the view area's message area.
     */
    void requestMessage(const QString &message);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void startScan();
    void cancel();
    void slotScanFinished(const WhaleTreeNode &root);
    void slotScanFailed();
    void slotProjected();
    void apply(const KFileItemList &items);
    void slotTreeChanged(const QString &rootPath);
    void slotLoadingStarted();
    void installDropGuard();
    bool handleDragEvent(QEvent *event);

    DolphinView *const m_dolphinView;
    DolphinViewBridge *const m_bridge;
    QPointer<WhaleTreeCache> m_cache;
    std::unique_ptr<WhaleTreeScannerImpl> m_scanner;
    QFutureWatcher<WhaleRecursiveProjection::Result> *m_projection = nullptr;
    std::shared_ptr<std::atomic_bool> m_projectionCanceled;
    QTimer *m_rescanTimer;

    bool m_enabled = false;
    QUrl m_root; // the location being scanned or shown
    bool m_includeHidden = false;
    bool m_rescanPending = false; // a change was reported while busy
    int m_fileCount = 0;
    Timing m_timing;
    QElapsedTimer m_clock;

    QPointer<QWidget> m_dragWidget;
    bool m_pressedInView = false; // a drag starting now is the view's own
    QPointer<const QMimeData> m_ownDrag;
    QPointer<const QMimeData> m_explainedDrag;
    QString m_lastRefusal;
    int m_refusalCount = 0;
};

#endif
