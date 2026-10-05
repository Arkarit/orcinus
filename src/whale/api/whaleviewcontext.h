/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef WHALEVIEWCONTEXT_H
#define WHALEVIEWCONTEXT_H

#include "whaleselection.h"
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

    /**
     * @return The selection of the view area. The host keeps it, so it is the same
     *         in every view of the area and survives switching views.
     */
    virtual WhaleSelection selection() const = 0;

    /**
     * Replaces the selection of the view area, e.g. after the user changed it in the
     * view. URLs that are not in items() are ignored. An empty current() keeps the
     * current item. selectionChanged() follows only if the resulting selection
     * differs from @p selection, e.g. because URLs were ignored.
     */
    virtual void setSelection(const WhaleSelection &selection) = 0;

    /**
     * Opens items as the user does with Enter or a double click: folders are entered,
     * files are opened with their application, as in the built-in file list. URLs
     * that are not in items() are ignored.
     */
    virtual void activateItems(const QList<QUrl> &urls) = 0;

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

    /**
     * Is emitted when the selection of the view area has changed, by any view or
     * action (e.g. "Select All"), or because selected items disappeared. Changes are
     * collected and reported once the event loop is idle, so items() is up to date
     * when the signal arrives.
     */
    void selectionChanged(const WhaleSelection &selection);

protected:
    explicit WhaleViewContext(QObject *parent = nullptr);

private:
    const std::unique_ptr<WhaleViewContextPrivate> d;
};

#endif
