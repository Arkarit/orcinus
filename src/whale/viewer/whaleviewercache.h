/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEVIEWERCACHE_H
#define WHALEVIEWERCACHE_H

#include <QHash>
#include <QImage>
#include <QObject>
#include <QUrl>

#include <atomic>
#include <memory>

/**
 * @brief The images of the built-in viewer, decoded and scaled in the background (Whale).
 *
 * The viewer asks for the file it shows and its neighbours; both are decoded and scaled to fit
 * the window off the GUI thread, the shown one first. An image keeps its full resolution as
 * well, to scale it again for another window size (and, later, for zoom). The full images of
 * other files than the current one stay only within a memory budget; beyond it, only their
 * scaled image stays, and the full one is decoded again when it is needed.
 *
 * Images with a colour profile of their own are converted to sRGB, the colour space the
 * compositor assumes for the window (V-E14).
 *
 * See .privatedoc/whale-viewer-plan.md (V2a).
 */
class WhaleViewerCache : public QObject
{
    Q_OBJECT

public:
    struct Entry {
        /// The image at full resolution, null if it was dropped for the budget or failed.
        QImage full;
        /// The size of the image at full resolution, also when the full image was dropped.
        QSize fullSize;
        /// The image fitted into the available size, with its device pixel ratio set.
        QImage scaled;
        /// The size (in device pixels) it was fitted into.
        QSize available;
        bool error = false;
        /// Whether the file has more than one frame (animated GIF, WebP …); the images are its first.
        bool animated = false;
    };

    explicit WhaleViewerCache(QObject *parent = nullptr);
    ~WhaleViewerCache() override;

    /**
     * @return The size an image of @p imageSize is shown at in @p available (device pixels):
     *         fitted into it with its aspect ratio; never larger than the image unless
     *         @p upscale.
     */
    static QSize fittedSize(const QSize &imageSize, const QSize &available, bool upscale = false);

    /** Whether small images are enlarged to fill the available size (V-E15). Default off. */
    void setUpscale(bool upscale);

    /**
     * Asks for the image of @p url (a local file) fitted into @p available with the device
     * pixel ratio @p dpr. Nothing happens if it is there already or on its way. The current
     * file is decoded before the others. imageReady() tells when it is there.
     */
    void request(const QUrl &url, const QSize &available, qreal dpr);

    /**
     * Asks for the full image of @p url again if it was dropped for the budget (to zoom into
     * it). imageReady() tells when it is there.
     */
    void requestFull(const QUrl &url, qreal dpr);

    /** The file shown: it is decoded first and keeps its full image regardless of the budget. */
    void setCurrent(const QUrl &url);

    /** Forgets every file but @p urls, and stops decoding the others where it can. */
    void retainOnly(const QList<QUrl> &urls);

    /** @return The image of @p url, or nullptr if there is none yet. */
    const Entry *entry(const QUrl &url) const;
    /** @return The files there are images of. */
    QList<QUrl> urls() const;
    /** @return Whether @p url is being decoded or scaled. */
    bool isPending(const QUrl &url) const;

    /** The bytes the full images of other files than the current one may take. */
    void setFullImageBudget(qint64 bytes);

Q_SIGNALS:
    /** The image of @p url is there, or failed (Entry::error). */
    void imageReady(const QUrl &url);

private:
    struct Pending {
        std::shared_ptr<std::atomic_bool> cancelled;
        QSize available;
    };

    void start(const QUrl &url, const QImage &full, const QSize &available, qreal dpr);
    void enforceBudget();

    QHash<QUrl, Entry> m_entries;
    QHash<QUrl, Pending> m_pending;
    QUrl m_current;
    qint64 m_budget;
    bool m_upscale = false;
};

#endif
