/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DOLPHINVIEWBRIDGE_H
#define DOLPHINVIEWBRIDGE_H

#include <QObject>
#include <QUrl>

class DolphinView;
class WhaleItemList;

/**
 * @brief Whale's only access to the internals of a DolphinView.
 *
 * Plugin views show the content of the hidden DolphinView of their view area: it
 * keeps loading the location, and its KFileItemModel stays the only source of the
 * items. The bridge translates that state into the types of the plugin API, so no
 * other Whale class needs to know DolphinView internals or kitemviews.
 *
 * There is one bridge per view area (WhaleViewHost). It must outlive the item
 * lists it creates.
 */
class DolphinViewBridge : public QObject
{
    Q_OBJECT

public:
    explicit DolphinViewBridge(DolphinView *dolphinView, QObject *parent = nullptr);
    ~DolphinViewBridge() override;

    QUrl url() const;

    /**
     * @return A new list that mirrors the items of the DolphinView in their order
     *         until the list is destroyed. The caller owns it through @p parent.
     */
    WhaleItemList *createItemList(QObject *parent) const;

Q_SIGNALS:
    void urlChanged(const QUrl &url);
    void loadingStarted();
    void loadingCompleted();
    void loadingCanceled();

private:
    DolphinView *const m_dolphinView;
};

#endif
