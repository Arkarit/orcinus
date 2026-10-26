/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALESTYLE_H
#define WHALESTYLE_H

#include <QKeySequence>
#include <QMap>
#include <QString>

/**
 * @brief What a style says about one element of the window (ST1).
 *
 * The value is text, written and read by the element type (WhaleStyleElement). A style leaves an
 * element that is not enabled as it is, whatever its value says.
 */
struct WhaleStyleElementState {
    bool enabled = false;
    QString value;

    bool operator==(const WhaleStyleElementState &other) const = default;
};

/**
 * @brief A style: a name, an optional shortcut and the states of elements (ST1).
 *
 * The ID is made from the name when the style is added (WhaleStyleStore) and stays when it is
 * renamed, because the name of its action and so its shortcut depend on it.
 *
 * Elements are keyed by WhaleStyleElement::key(). They may include keys no element type knows
 * (written by a newer Whale); they are kept and written back unchanged. The order in which the
 * elements are applied is the order of the registry (WhaleStyleElements), not of the style.
 */
struct WhaleStyle {
    QString id;
    QString name;
    QKeySequence shortcut;
    QMap<QString, WhaleStyleElementState> elements;

    /** Entries of the style's group that this version does not know, kept for a newer Whale. */
    QMap<QString, QString> unknownEntries;

    bool operator==(const WhaleStyle &other) const = default;
};

#endif
