/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef WHALEPREVIEWPROVIDER_H
#define WHALEPREVIEWPROVIDER_H

#include "whaleviewapi_export.h"

#include <KFileItem>

#include <QList>
#include <QObject>
#include <QPixmap>
#include <QSize>
#include <QUrl>

#include <memory>

class WhalePreviewProviderPrivate;

/**
 * @brief Thumbnails of files for a view, made by KDE's preview system.
 *
 * The previews come from the same thumbnail plugins as in the file list (the user's
 * choice in Whale's preview settings), and KDE's thumbnail cache is shared, so a
 * thumbnail made once is not made again.
 *
 * A view requests previews only for the items it shows or will show soon, and cancels
 * the requests of items that went out of sight. Every request is answered once per
 * item, asynchronously, by previewAvailable() or previewFailed(), unless it is
 * canceled first. Several requests may run at the same time; nothing blocks the GUI.
 *
 * Previews are only made while isEnabled(): the user wants previews at the location
 * ("Show Previews") and the view declares WhaleViewCapability::Previews. When that
 * ends, all requests are canceled; when it changes, enabledChanged() is emitted.
 * Changing the location cancels all requests, too.
 *
 * The provider belongs to the view's context and lives as long as it; requests still
 * running when it is destroyed are canceled.
 */
class WHALEVIEWAPI_EXPORT WhalePreviewProvider : public QObject
{
    Q_OBJECT

public:
    ~WhalePreviewProvider() override;

    /**
     * @return True if the view should show previews; requests are only served then.
     *         Otherwise it shows its usual icons.
     */
    virtual bool isEnabled() const = 0;

    /**
     * Requests previews of @p items, each fitting into @p size (device-independent
     * pixels, the aspect ratio is kept) for a screen with @p devicePixelRatio.
     * Items whose preview is already requested with the same size are not requested
     * again; a request with another size replaces the earlier one. Null items are
     * ignored. Does nothing unless isEnabled().
     */
    virtual void requestPreviews(const KFileItemList &items, const QSize &size, qreal devicePixelRatio = 1.0) = 0;

    /**
     * Cancels the requests for @p urls: no signal follows for them any more. Work for
     * them that has not started is dropped.
     */
    virtual void cancel(const QList<QUrl> &urls) = 0;

    /**
     * Cancels all requests.
     */
    virtual void cancelAll() = 0;

    /**
     * @return True if a preview of @p url is requested and not answered yet.
     */
    virtual bool isPending(const QUrl &url) const = 0;

Q_SIGNALS:
    /**
     * The preview of @p item, as requested.
     */
    void previewAvailable(const KFileItem &item, const QPixmap &preview);

    /**
     * There is no preview of @p item: no thumbnail plugin handles its type, it is too
     * large, or making it failed. The view keeps showing the item's icon.
     */
    void previewFailed(const KFileItem &item);

    /**
     * isEnabled() has changed.
     */
    void enabledChanged(bool enabled);

protected:
    explicit WhalePreviewProvider(QObject *parent = nullptr);

private:
    const std::unique_ptr<WhalePreviewProviderPrivate> d;
};

#endif
