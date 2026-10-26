/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEVIEWSTATE_H
#define WHALEVIEWSTATE_H

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>

/**
 * @brief Which view a view area shows at a location, and the states of its views.
 *
 * The states are the opaque WhaleView::saveState() blobs, kept per view ID, so a view
 * never gets the state of another one. A state is kept even while its view is not
 * shown or its plugin is missing, so it survives until the plugin comes back.
 */
struct WhaleViewsState {
    /// The view ID that was shown (WhaleViewHost::dolphinViewId() for the file list).
    QString activeViewId;
    QHash<QString, QByteArray> states;

    bool operator==(const WhaleViewsState &other) const = default;
};

/**
 * @brief Whale's own persistence formats around Dolphin's.
 *
 * Dolphin's formats stay untouched:
 * - History: the location state of a KUrlNavigator history entry (DolphinView::saveState())
 *   is wrapped into an envelope: magic, format version, Dolphin's state, WhaleViewsState.
 *   Data without the magic is Dolphin's own state of an older entry and read as such.
 * - Session: a key of its own next to Dolphin's "State" in each tab group holds the
 *   WhaleViewsState of the tab's view areas.
 *
 * The format has a version of its own, independent of the plugin API version.
 * Nothing read here can make Whale fail: damaged or newer data is ignored with a warning.
 */
namespace WhalePersistence
{
/// Version of the formats written here. Increase it for any change of them.
constexpr quint32 FormatVersion = 1;

/// The key of the session data in a tab's group, next to Dolphin's "State".
inline constexpr char SessionKey[] = "WhaleViews";

/**
 * @return @p dolphinState of a history entry, wrapped together with @p views.
 */
QByteArray wrapLocationState(const QByteArray &dolphinState, const WhaleViewsState &views);

/**
 * Splits a history entry's state.
 * @param dolphinState Receives Dolphin's state: all of @p data if it has no envelope,
 *        nothing if the envelope is damaged.
 * @param views Receives Whale's part if there is a usable envelope.
 * @return True if @p data had a usable envelope.
 */
bool unwrapLocationState(const QByteArray &data, QByteArray *dolphinState, WhaleViewsState *views);

/**
 * @return The session data of the view areas of a tab (one or two).
 */
QByteArray writeSessionState(const QList<WhaleViewsState> &panes);

/**
 * @return The view areas of a tab's session data; empty for missing, damaged or newer data.
 */
QList<WhaleViewsState> readSessionState(const QByteArray &data);
}

#endif
