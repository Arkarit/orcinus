/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whalerecursivecontent.h"

#include "dolphinviewbridge.h"
#include "views/dolphinview.h"
#include "whalerecursiveprojection.h"
#include "whaletreecache.h"
#include "whaletreescannerimpl.h"
#include "whaleviewdebug.h"

#include <KLocalizedString>

#include <QDir>
#include <QDropEvent>
#include <QFutureWatcher>
#include <QMimeData>
#include <QTimer>
#include <QtConcurrentRun>

namespace
{
// A change below the location is shown after this delay, so a burst of changes (e.g. a
// file operation on many files) costs one scan.
constexpr int RescanDelayMs = 300;
}

WhaleRecursiveContent::WhaleRecursiveContent(DolphinView *dolphinView, DolphinViewBridge *bridge, QObject *parent)
    : QObject(parent)
    , m_dolphinView(dolphinView)
    , m_bridge(bridge)
    , m_rescanTimer(new QTimer(this))
{
    m_rescanTimer->setSingleShot(true);
    m_rescanTimer->setInterval(RescanDelayMs);
    connect(m_rescanTimer, &QTimer::timeout, this, &WhaleRecursiveContent::startScan);

    setTreeCache(nullptr);

    // Loading the location (a new one while enabled, or F5) scans it again.
    connect(dolphinView, &DolphinView::directoryLoadingStarted, this, &WhaleRecursiveContent::slotLoadingStarted);
    connect(dolphinView, &DolphinView::hiddenFilesShownChanged, this, [this](bool shown) {
        if (m_enabled && shown != m_includeHidden) {
            startScan();
        }
    });

    installDropGuard();
}

WhaleRecursiveContent::~WhaleRecursiveContent()
{
    cancel();
}

void WhaleRecursiveContent::setTreeCache(WhaleTreeCache *cache)
{
    if (m_scanner && cache == m_cache) {
        return;
    }
    if (m_cache) {
        disconnect(m_cache, nullptr, this, nullptr);
    }
    const bool scanning = m_scanner && m_scanner->isRunning();
    cancel();
    m_cache = cache;
    m_scanner = std::make_unique<WhaleTreeScannerImpl>(cache);
    connect(m_scanner.get(), &WhaleTreeScanner::finished, this, &WhaleRecursiveContent::slotScanFinished);
    connect(m_scanner.get(), &WhaleTreeScanner::failed, this, &WhaleRecursiveContent::slotScanFailed);
    if (cache) {
        connect(cache, &WhaleTreeCache::treeChanged, this, &WhaleRecursiveContent::slotTreeChanged);
    }
    if (scanning) {
        startScan();
    }
}

void WhaleRecursiveContent::setEnabled(bool enabled)
{
    if (enabled == m_enabled) {
        return;
    }

    const WhaleSelection selection = m_bridge->selection();
    if (enabled) {
        if (!m_bridge->isLoading()) {
            // The location is shown already: load it again, so the file list goes through
            // its loading ("Loading…", status bar) and restores its current item afterwards.
            m_dolphinView->reload();
        }
        m_enabled = true;
        m_bridge->setExternalItemsEnabled(true);
        // The selected items of the location are among the files below it.
        m_dolphinView->markUrlsAsSelected(selection.selected());
        startScan();
    } else {
        m_enabled = false;
        cancel();
        m_fileCount = 0;
        m_timing = Timing();
        m_bridge->setExternalItemsEnabled(false);
        // The selected items that are the location's own stay selected.
        m_dolphinView->markUrlsAsSelected(selection.selected());
        if (!selection.current().isEmpty()) {
            m_dolphinView->markUrlAsCurrent(selection.current());
        }
        if (!m_bridge->isLoading()) {
            m_dolphinView->updateViewState();
        }
    }
    Q_EMIT stateChanged();
}

bool WhaleRecursiveContent::isEnabled() const
{
    return m_enabled;
}

bool WhaleRecursiveContent::isBusy() const
{
    return (m_scanner && m_scanner->isRunning()) || m_projection;
}

int WhaleRecursiveContent::fileCount() const
{
    return m_fileCount;
}

WhaleRecursiveContent::Timing WhaleRecursiveContent::timing() const
{
    return m_timing;
}

QString WhaleRecursiveContent::pasteRefusal() const
{
    if (!m_enabled) {
        return {};
    }
    return i18nc("@info",
                 "Cannot paste into a recursive view because no single destination folder is selected. "
                 "Turn off “Show flat (no folders)” first.");
}

QString WhaleRecursiveContent::dropRefusal()
{
    return i18nc("@info",
                 "Cannot drop files into a recursive view because it contains items from multiple folders. "
                 "Turn off “Show flat (no folders)” to choose a destination.");
}

QString WhaleRecursiveContent::lastRefusal() const
{
    return m_lastRefusal;
}

int WhaleRecursiveContent::refusalCount() const
{
    return m_refusalCount;
}

void WhaleRecursiveContent::explain(const QString &message)
{
    m_lastRefusal = message;
    ++m_refusalCount;
    Q_EMIT requestMessage(message);
    Q_EMIT stateChanged();
}

void WhaleRecursiveContent::startScan()
{
    if (!m_enabled) {
        return;
    }
    cancel();
    m_root = m_bridge->url();
    m_includeHidden = m_dolphinView->hiddenFilesShown();
    if (!m_root.isLocalFile()) {
        // Only local folders are scanned; the host enables this for those only.
        apply({});
        return;
    }

    WhaleTreeScanner::Options options;
    options.includeHidden = m_includeHidden;
    m_clock.start();
    m_scanner->scan(m_root, options);
    Q_EMIT stateChanged();
}

void WhaleRecursiveContent::cancel()
{
    m_rescanTimer->stop();
    m_rescanPending = false;
    if (m_scanner && m_scanner->isRunning()) {
        m_scanner->cancel();
    }
    if (m_projection) {
        // The worker ends at its next check; its result is dropped with the watcher.
        m_projectionCanceled->store(true);
        m_projection->disconnect(this);
        m_projection->deleteLater();
        m_projection = nullptr;
    }
}

void WhaleRecursiveContent::slotScanFinished(const WhaleTreeNode &root)
{
    m_timing.scanMs = m_clock.restart();
    auto canceled = std::make_shared<std::atomic_bool>(false);
    m_projectionCanceled = canceled;
    m_projection = new QFutureWatcher<WhaleRecursiveProjection::Result>(this);
    connect(m_projection, &QFutureWatcherBase::finished, this, &WhaleRecursiveContent::slotProjected);
    m_projection->setFuture(QtConcurrent::run([root, canceled]() {
        return WhaleRecursiveProjection::project(root, [canceled]() {
            return canceled->load();
        });
    }));
}

void WhaleRecursiveContent::slotScanFailed()
{
    // E.g. the location vanished: an empty list, which also completes its loading. The
    // listing of the location reports the error itself.
    m_timing.scanMs = m_clock.restart();
    apply({});
}

void WhaleRecursiveContent::slotProjected()
{
    QFutureWatcher<WhaleRecursiveProjection::Result> *watcher = m_projection;
    m_projection = nullptr;
    watcher->deleteLater();
    const WhaleRecursiveProjection::Result result = watcher->result();
    if (result.canceled) {
        return;
    }
    m_timing.projectionMs = m_clock.restart();
    m_timing.folderLinks = result.folderLinks;
    apply(result.items);
}

void WhaleRecursiveContent::apply(const KFileItemList &items)
{
    // While the location loads, its loading completes with the items, and the file list
    // restores its view state then; otherwise items may have appeared that the file list
    // waits for to select them (e.g. after Duplicate or the rename dialog).
    const bool loading = m_bridge->isLoading();
    QElapsedTimer clock;
    clock.start();
    m_bridge->setExternalItems(items);
    m_timing.modelMs = clock.elapsed();
    m_fileCount = items.count();
    ++m_timing.updates;
    if (!loading) {
        m_dolphinView->updateViewState();
    }
    qCDebug(WhaleViewDebug) << "Recursive files of" << m_root << ":" << m_fileCount << "files, scan" << m_timing.scanMs << "ms, projection"
                            << m_timing.projectionMs << "ms, model" << m_timing.modelMs << "ms";
    if (m_rescanPending) {
        m_rescanPending = false;
        m_rescanTimer->start();
    }
    Q_EMIT stateChanged();
}

void WhaleRecursiveContent::slotTreeChanged(const QString &rootPath)
{
    if (!m_enabled || !m_root.isLocalFile() || rootPath != QDir::cleanPath(m_root.toLocalFile())) {
        return;
    }
    if (isBusy()) {
        // The running scan may have missed it: scan again once it is shown.
        m_rescanPending = true;
    } else {
        m_rescanTimer->start();
    }
}

void WhaleRecursiveContent::slotLoadingStarted()
{
    if (m_enabled) {
        startScan();
    }
}

void WhaleRecursiveContent::installDropGuard()
{
    m_dragWidget = m_bridge->fileListDragWidget();
    if (m_dragWidget) {
        m_dragWidget->installEventFilter(this);
    } else {
        qCWarning(WhaleViewDebug) << "No drag widget in the file list: drops into recursive views cannot be refused";
    }
}

bool WhaleRecursiveContent::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_dragWidget) {
        return false;
    }
    switch (event->type()) {
    case QEvent::MouseButtonPress:
        m_pressedInView = true;
        break;
    case QEvent::MouseButtonRelease:
        m_pressedInView = false;
        break;
    case QEvent::DragEnter:
    case QEvent::DragMove:
    case QEvent::Drop:
        if (m_enabled) {
            return handleDragEvent(event);
        }
        break;
    default:
        break;
    }
    return false;
}

bool WhaleRecursiveContent::handleDragEvent(QEvent *event)
{
    // No destination: the items come from many folders. Nothing reaches the file list's
    // controller, so no item is highlighted as a target, no folder opens on hover, and no
    // drop job starts.
    auto dropEvent = static_cast<QDropEvent *>(event);
    const QMimeData *data = dropEvent->mimeData();
    if (event->type() == QEvent::DragEnter && m_pressedInView) {
        // A drag the user has just started in this view; it leaves to other places.
        m_ownDrag = data;
        m_pressedInView = false;
    }
    dropEvent->setDropAction(Qt::IgnoreAction);
    dropEvent->ignore();

    // A drag from elsewhere is explained once, when it enters (some platforms never deliver
    // a refused drop) or is dropped anyway; moving over the view says nothing.
    const bool own = data && data == m_ownDrag.data();
    if (!own && event->type() != QEvent::DragMove && (!data || data != m_explainedDrag.data())) {
        m_explainedDrag = data;
        explain(dropRefusal());
    }
    return true;
}

#include "moc_whalerecursivecontent.cpp"
