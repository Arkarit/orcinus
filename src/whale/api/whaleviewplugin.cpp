/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "whaleviewplugin.h"

#include <QUrl>

class WhaleViewPluginPrivate
{
};

WhaleViewPlugin::WhaleViewPlugin(QObject *parent)
    : QObject(parent)
{
}

WhaleViewPlugin::~WhaleViewPlugin() = default;

bool WhaleViewPlugin::supportsUrl(const QUrl &url) const
{
    Q_UNUSED(url)
    return true;
}

#include "moc_whaleviewplugin.cpp"
