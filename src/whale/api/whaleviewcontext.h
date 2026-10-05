/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef WHALEVIEWCONTEXT_H
#define WHALEVIEWCONTEXT_H

#include "whaleviewapi_export.h"

#include <QObject>
#include <QUrl>

#include <memory>

class WhaleItemList;
class WhaleViewContextPrivate;

/**
 * @brief The host as seen by a view plugin.
 *
 * Whale creates one context per view instance and keeps it alive for the
 * lifetime of that view. A view plugin only uses this interface to talk to
 * Whale; it never sees Whale's internal classes.
 *
 * The interface is intentionally small and grows with the API version.
 */
class WHALEVIEWAPI_EXPORT WhaleViewContext : public QObject
{
    Q_OBJECT

public:
    ~WhaleViewContext() override;

    /**
     * @return The location shown in the view area.
     */
    virtual QUrl url() const = 0;

    /**
     * @return The items of the location, in the host's order. The list belongs to
     *         the context and lives as long as it. It is created on the first call,
     *         so a view that does not need the items does not cost their upkeep.
     */
    virtual WhaleItemList *items() const = 0;

Q_SIGNALS:
    /**
     * Is emitted when the location of the view area has changed. The items of the
     * new location follow while it is loaded.
     */
    void urlChanged(const QUrl &url);

    /**
     * Is emitted when the host starts loading the location. Until loadingCompleted()
     * or loadingCanceled() the items may still be incomplete.
     */
    void loadingStarted();

    /**
     * Is emitted when the location has been loaded completely.
     */
    void loadingCompleted();

    /**
     * Is emitted when loading the location has been canceled.
     */
    void loadingCanceled();

protected:
    explicit WhaleViewContext(QObject *parent = nullptr);

private:
    const std::unique_ptr<WhaleViewContextPrivate> d;
};

#endif
