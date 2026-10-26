/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "treemapview.h"
#include "whaleviewplugin.h"

#include <KPluginFactory>

#include <QUrl>

/**
 * The treemap view (TreemapView). Only local folders can be shown, because the tree
 * scanner only scans those so far.
 */
class WhaleTreemapPlugin : public WhaleViewPlugin
{
    Q_OBJECT

public:
    explicit WhaleTreemapPlugin(QObject *parent)
        : WhaleViewPlugin(parent)
    {
    }

    WhaleView *createView(WhaleViewContext *context, QObject *parent) override
    {
        return new TreemapView(context, parent);
    }

    bool supportsUrl(const QUrl &url) const override
    {
        return url.isLocalFile();
    }
};

K_PLUGIN_CLASS_WITH_JSON(WhaleTreemapPlugin, "orcinus_treemap.json")

#include "whaletreemapplugin.moc"
