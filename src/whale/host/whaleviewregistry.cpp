/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
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
    return QStringLiteral("whale/views");
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
        return reject(QStringLiteral("it was built for view API version %1, but this Whale provides version %2; rebuild the plugin against this Whale")
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
    return m_plugins;
}

KPluginMetaData WhaleViewRegistry::pluginMetaData(const QString &pluginId)
{
    scan();
    for (const KPluginMetaData &metaData : std::as_const(m_plugins)) {
        if (metaData.pluginId() == pluginId) {
            return metaData;
        }
    }
    return {};
}

WhaleViewPlugin *WhaleViewRegistry::plugin(const QString &pluginId)
{
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
}

#include "moc_whaleviewregistry.cpp"
