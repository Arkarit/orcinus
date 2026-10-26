/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef WHALEVIEWPLUGIN_H
#define WHALEVIEWPLUGIN_H

#include "whaleviewapi_export.h"

#include <QObject>

#include <memory>

class QUrl;
class WhaleView;
class WhaleViewContext;
class WhaleViewPluginPrivate;

/**
 * @brief Entry point of a native Whale view plugin.
 *
 * A plugin is a KCoreAddons plugin in the namespace "orcinus/views", declared with
 * K_PLUGIN_CLASS_WITH_JSON. Its JSON metadata must contain the integer key
 * "X-Orcinus-ViewApiVersion" (WHALE_VIEW_API_VERSION_KEY) with the value of
 * WHALE_VIEW_API_VERSION the plugin was built against.
 *
 * Whale creates at most one instance of each plugin and never unloads it.
 * The instance creates one WhaleView per view area.
 */
class WHALEVIEWAPI_EXPORT WhaleViewPlugin : public QObject
{
    Q_OBJECT

public:
    explicit WhaleViewPlugin(QObject *parent = nullptr);
    ~WhaleViewPlugin() override;

    /**
     * Creates a new view for one view area.
     * @param context The host interface for the new view. It outlives the view.
     * @param parent  The owner of the new view.
     */
    virtual WhaleView *createView(WhaleViewContext *context, QObject *parent) = 0;

    /**
     * @return True if the plugin can show the location @p url.
     *         The default implementation returns true.
     */
    virtual bool supportsUrl(const QUrl &url) const;

private:
    const std::unique_ptr<WhaleViewPluginPrivate> d;
};

#endif
