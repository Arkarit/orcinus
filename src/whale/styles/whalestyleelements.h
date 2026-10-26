/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALESTYLEELEMENTS_H
#define WHALESTYLEELEMENTS_H

#include <QList>
#include <QString>

#include <memory>
#include <vector>

class WhaleStyleElement;

/**
 * @brief The registry of the element types of styles (ST1).
 *
 * The order of the registry is the order in which a style's elements are applied: first the
 * layout (split view), then the views, then the bars and panels. Storing, applying and the user
 * interface go through the registry only.
 */
class WhaleStyleElements
{
public:
    /** @return The registry with all element types of Whale. */
    static const WhaleStyleElements &self();

    WhaleStyleElements();
    ~WhaleStyleElements();

    WhaleStyleElements(const WhaleStyleElements &) = delete;
    WhaleStyleElements &operator=(const WhaleStyleElements &) = delete;

    /** Appends @p element. Its key must not be registered yet; a second one is ignored. */
    void add(std::unique_ptr<WhaleStyleElement> element);

    /** @return All element types in the order of applying. */
    QList<const WhaleStyleElement *> elements() const;

    /** @return The element type with @p key, or nullptr for an unknown key. */
    const WhaleStyleElement *element(const QString &key) const;

private:
    std::vector<std::unique_ptr<WhaleStyleElement>> m_elements;
};

#endif
