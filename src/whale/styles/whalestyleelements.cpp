/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whalestyleelements.h"

#include "whalestyleelement.h"
#include "whalestyleelementtypes.h"

WhaleStyleElement::~WhaleStyleElement() = default;

bool WhaleStyleElement::isAvailable(const DolphinMainWindow *window) const
{
    Q_UNUSED(window)
    return true;
}

QString WhaleStyleElement::valueIfMissing() const
{
    return QString();
}

const WhaleStyleElements &WhaleStyleElements::self()
{
    static WhaleStyleElements elements;
    static const bool filled = [] {
        WhaleStyleElementTypes::addTo(elements);
        return true;
    }();
    Q_UNUSED(filled)
    return elements;
}

WhaleStyleElements::WhaleStyleElements() = default;

WhaleStyleElements::~WhaleStyleElements() = default;

void WhaleStyleElements::add(std::unique_ptr<WhaleStyleElement> element)
{
    if (!element || this->element(element->key())) {
        return;
    }
    m_elements.push_back(std::move(element));
}

QList<const WhaleStyleElement *> WhaleStyleElements::elements() const
{
    QList<const WhaleStyleElement *> result;
    result.reserve(m_elements.size());
    for (const auto &element : m_elements) {
        result.append(element.get());
    }
    return result;
}

const WhaleStyleElement *WhaleStyleElements::element(const QString &key) const
{
    for (const auto &element : m_elements) {
        if (element->key() == key) {
            return element.get();
        }
    }
    return nullptr;
}
