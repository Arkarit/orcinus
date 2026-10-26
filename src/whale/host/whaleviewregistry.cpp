/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleviewregistry.h"

#include "whaleviewapiversion.h"
#include "whaleviewdebug.h"
#include "whaleviewplugin.h"

#include <KPluginFactory>

#include <QJsonValue>

WhaleViewRegistry::WhaleViewRegistry(QObject *parent)
    : QObject(parent)
{
}

WhaleViewRegistry::~WhaleViewRegistry() = default;

QString WhaleViewRegistry::pluginNamespace()
{
    return QStringLiteral("orcinus/views");
}

bool WhaleViewRegistry::isCompatible(const KPluginMetaData &metaData, QString *reason)
{
    auto reject = [reason](const QString &text) {
        if (reason) {
            *reason = text;
        }
        return false;
    };

    if (metaData.pluginId().isEmpty()) {
        return reject(QStringLiteral("the metadata has no plugin ID"));
    }

    const QJsonValue version = metaData.rawData().value(QLatin1String(WHALE_VIEW_API_VERSION_KEY));
    if (version.isUndefined()) {
        return reject(QStringLiteral("the metadata has no \"%1\" entry").arg(QLatin1String(WHALE_VIEW_API_VERSION_KEY)));
    }
    if (!version.isDouble() || version.toDouble() != version.toInt()) {
        return reject(QStringLiteral("\"%1\" is not an integer").arg(QLatin1String(WHALE_VIEW_API_VERSION_KEY)));
    }
    if (version.toInt() != WHALE_VIEW_API_VERSION) {
        return reject(QStringLiteral("it was built for view API version %1, but this Orcinus provides version %2; rebuild the plugin against this Orcinus")
                          .arg(version.toInt())
                          .arg(WHALE_VIEW_API_VERSION));
    }

    if (reason) {
        reason->clear();
    }
    return true;
}

QList<KPluginMetaData> WhaleViewRegistry::plugins()
{
    scan();
    if (m_unavailablePluginIds.isEmpty()) {
        return m_plugins;
    }
    QList<KPluginMetaData> available;
    for (const KPluginMetaData &metaData : std::as_const(m_plugins)) {
        if (!m_unavailablePluginIds.contains(metaData.pluginId())) {
            available.append(metaData);
        }
    }
    return available;
}

KPluginMetaData WhaleViewRegistry::pluginMetaData(const QString &pluginId)
{
    scan();
    if (m_unavailablePluginIds.contains(pluginId)) {
        return {};
    }
    for (const KPluginMetaData &metaData : std::as_const(m_plugins)) {
        if (metaData.pluginId() == pluginId) {
            return metaData;
        }
    }
    return {};
}

WhaleViewPlugin *WhaleViewRegistry::plugin(const QString &pluginId)
{
    if (m_unavailablePluginIds.contains(pluginId)) {
        qCDebug(WhaleViewDebug) << "The view plugin" << pluginId << "is made unavailable";
        return nullptr;
    }
    if (WhaleViewPlugin *instance = m_instances.value(pluginId)) {
        return instance;
    }
    if (m_failedPluginIds.contains(pluginId)) {
        return nullptr;
    }

    const KPluginMetaData metaData = pluginMetaData(pluginId);
    if (!metaData.isValid()) {
        qCWarning(WhaleViewDebug) << "No compatible view plugin with the ID" << pluginId;
        return nullptr;
    }

    const auto result = KPluginFactory::instantiatePlugin<WhaleViewPlugin>(metaData, this);
    if (!result) {
        qCWarning(WhaleViewDebug).noquote() << "Could not load view plugin" << pluginId << "from" << metaData.fileName() << ":" << result.errorText;
        m_failedPluginIds.insert(pluginId);
        return nullptr;
    }

    m_instances.insert(pluginId, result.plugin);
    return result.plugin;
}

void WhaleViewRegistry::setPluginAvailable(const QString &pluginId, bool available)
{
    if (available) {
        m_unavailablePluginIds.remove(pluginId);
    } else {
        m_unavailablePluginIds.insert(pluginId);
    }
}

void WhaleViewRegistry::scan()
{
    if (m_scanned) {
        return;
    }
    m_scanned = true;

    const QList<KPluginMetaData> candidates = KPluginMetaData::findPlugins(pluginNamespace());
    for (const KPluginMetaData &metaData : candidates) {
        QString reason;
        if (isCompatible(metaData, &reason)) {
            m_plugins.append(metaData);
        } else {
            qCWarning(WhaleViewDebug).noquote() << "Ignoring view plugin" << metaData.fileName() << ":" << reason;
        }
    }
    // A stable order for the menus, whatever order the plugin directories are read in.
    std::stable_sort(m_plugins.begin(), m_plugins.end(), [](const KPluginMetaData &a, const KPluginMetaData &b) {
        return a.pluginId() < b.pluginId();
    });
}

#include "moc_whaleviewregistry.cpp"
