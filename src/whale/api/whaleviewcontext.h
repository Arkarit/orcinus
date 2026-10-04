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

Q_SIGNALS:
    /**
     * Is emitted when the location of the view area has changed.
     */
    void urlChanged(const QUrl &url);

protected:
    explicit WhaleViewContext(QObject *parent = nullptr);

private:
    const std::unique_ptr<WhaleViewContextPrivate> d;
};

#endif
