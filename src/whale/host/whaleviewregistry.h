/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEVIEWREGISTRY_H
#define WHALEVIEWREGISTRY_H

#include <KPluginMetaData>

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>

class WhaleViewPlugin;

/**
 * @brief Finds and loads the native view plugins of Whale.
 *
 * Plugins are searched in the KCoreAddons namespace "whale/views" of the Qt
 * library paths. Their metadata is checked for the exact view API version
 * without loading the library; incompatible plugins are rejected with a
 * warning. Compatible plugins are loaded lazily on first use, instantiated
 * once and never unloaded.
 */
class WhaleViewRegistry : public QObject
{
    Q_OBJECT

public:
    explicit WhaleViewRegistry(QObject *parent = nullptr);
    ~WhaleViewRegistry() override;

    /**
     * @return The plugin namespace that is searched ("whale/views").
     */
    static QString pluginNamespace();

    /**
     * @return True if the plugin described by @p metaData can be used by this Whale.
     *         The library is not loaded for this check.
     * @param reason Receives a human-readable explanation if the plugin is rejected.
     */
    static bool isCompatible(const KPluginMetaData &metaData, QString *reason = nullptr);

    /**
     * @return The metadata of all compatible plugins. The plugin namespace is
     *         searched on the first call; no plugin library is loaded.
     */
    QList<KPluginMetaData> plugins();

    /**
     * @return The metadata of the compatible plugin with the ID @p pluginId,
     *         or invalid metadata if there is none.
     */
    KPluginMetaData pluginMetaData(const QString &pluginId);

    /**
     * @return The instance of the compatible plugin with the ID @p pluginId. The
     *         library is loaded on the first call. Returns nullptr with a warning
     *         if the plugin is unknown, incompatible or cannot be loaded.
     *         The registry owns the instance.
     */
    WhaleViewPlugin *plugin(const QString &pluginId);

private:
    void scan();

    bool m_scanned = false;
    QList<KPluginMetaData> m_plugins;
    QHash<QString, WhaleViewPlugin *> m_instances;
    QSet<QString> m_failedPluginIds;
};

#endif
