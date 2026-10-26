/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whalestyles.h"

WhaleStyles *WhaleStyles::self()
{
    static WhaleStyles styles(WhaleStyleStore::defaultConfig());
    return &styles;
}

WhaleStyles::WhaleStyles(KSharedConfigPtr config, QObject *parent)
    : QObject(parent)
    , m_store(std::move(config))
{
}

WhaleStyles::~WhaleStyles() = default;

const WhaleStyleStore &WhaleStyles::store() const
{
    return m_store;
}

WhaleStyleStore &WhaleStyles::editableStore()
{
    return m_store;
}

void WhaleStyles::save()
{
    m_store.save();
    Q_EMIT changed();
}

void WhaleStyles::reload()
{
    const QList<WhaleStyle> before = m_store.styles();
    m_store.load();
    if (m_store.styles() != before) {
        Q_EMIT changed();
    }
}

#include "moc_whalestyles.cpp"
