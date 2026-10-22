/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALESTYLEELEMENT_H
#define WHALESTYLEELEMENT_H

#include <QString>

class DolphinMainWindow;

/**
 * @brief One type of element a style can set, e.g. the split view or the status bar (ST1).
 *
 * An element type turns a part of the window into text (capture()) and back (apply()). Its key
 * names the element in the styles file, so it must never change. Types are registered in
 * WhaleStyleElements; a new element is a new subclass and one entry there.
 */
class WhaleStyleElement
{
public:
    virtual ~WhaleStyleElement();

    /** @return The stable key of the element, e.g. "split" or "viewStyle.left". */
    virtual QString key() const = 0;

    /** @return The translated name shown in the user interface. */
    virtual QString title() const = 0;

    /**
     * @return Whether the element can be captured and applied in @p window as it is now,
     *         e.g. the style of the right view only with a split view.
     */
    virtual bool isAvailable(const DolphinMainWindow *window) const;

    /** @return The current state of the element in @p window as text. */
    virtual QString capture(const DolphinMainWindow *window) const = 0;

    /** Sets the element in @p window to @p value, as captured before. Ignores a value it cannot read. */
    virtual void apply(DolphinMainWindow *window, const QString &value) const = 0;

    /** @return A short translated description of @p value, e.g. "Compact, hidden files". */
    virtual QString describe(const QString &value) const = 0;
};

#endif
