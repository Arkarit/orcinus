/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEPREVIEWPROVIDERIMPL_H
#define WHALEPREVIEWPROVIDERIMPL_H

#include "whalepreviewprovider.h"

#include <QHash>
#include <QPointer>
#include <QStringList>

class DolphinViewBridge;
class KJob;

namespace KIO
{
class PreviewJob;
}

/**
 * @brief The WhalePreviewProvider of one view: KIO::PreviewJob, one job per request.
 *
 * Each requestPreviews() starts one KIO::PreviewJob for its new items, with the
 * thumbnail plugins of Whale's preview settings (as KFileItemModelRolesUpdater uses
 * them) and KIO's thumbnail cache. A job whose items are all canceled is killed, so
 * a view that requests what it shows and cancels what went out of sight never has
 * work running for items nobody looks at.
 */
class WhalePreviewProviderImpl : public WhalePreviewProvider
{
    Q_OBJECT

public:
    explicit WhalePreviewProviderImpl(DolphinViewBridge *bridge, QObject *parent = nullptr);
    ~WhalePreviewProviderImpl() override;

    bool isEnabled() const override;
    void requestPreviews(const KFileItemList &items, const QSize &size, qreal devicePixelRatio = 1.0) override;
    void cancel(const QList<QUrl> &urls) override;
    void cancelAll() override;
    bool isPending(const QUrl &url) const override;

    /**
     * Whether the view declares WhaleViewCapability::Previews; set by the host.
     */
    void setViewSupportsPreviews(bool supports);

    /**
     * What the provider did so far; for tests and development tools.
     */
    struct Statistics {
        int requested = 0; ///< items requested (without repeated requests)
        int generated = 0; ///< previews delivered
        int failed = 0; ///< items without preview
        int canceled = 0; ///< requests canceled before an answer
        int jobsStarted = 0;
        int jobsKilled = 0; ///< jobs stopped because nobody waited for them any more
        int maxPending = 0; ///< most items pending at the same time
    };
    Statistics statistics() const;
    QList<QUrl> pendingUrls() const;
    int runningJobs() const;

private:
    struct Pending {
        KIO::PreviewJob *job;
        KFileItem item;
        QSize size;
        qreal devicePixelRatio;
    };

    void updateEnabled();
    void slotGotPreview(KIO::PreviewJob *job, const KFileItem &item, const QPixmap &preview);
    void slotFailed(KIO::PreviewJob *job, const KFileItem &item);
    void slotJobFinished(KJob *job);
    /// Forgets the request for @p url; kills its job if nobody waits for it any more.
    void release(const QUrl &url);
    void killJob(KIO::PreviewJob *job);
    void killUnusedJobs();

    DolphinViewBridge *const m_bridge;
    QStringList m_enabledPlugins;
    bool m_viewSupportsPreviews = false;
    bool m_enabled = false;
    QHash<QUrl, Pending> m_pending;
    QHash<KIO::PreviewJob *, int> m_pendingPerJob;
    // Jobs nobody waits for any more, stopped once the event loop runs.
    QList<QPointer<KIO::PreviewJob>> m_unusedJobs;
    Statistics m_statistics;
};

#endif
