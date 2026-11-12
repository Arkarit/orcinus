/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaletoolbarsettings.h"

#include <KConfigGroup>

namespace
{
const char showFileViewToolBarsKey[] = "ShowFileViewToolBars";
const char toolbarDensityKey[] = "ToolbarDensity";
}

WhaleToolbarSettings *WhaleToolbarSettings::self()
{
    static WhaleToolbarSettings settings(KSharedConfig::openConfig());
    return &settings;
}

WhaleToolbarSettings::WhaleToolbarSettings(KSharedConfigPtr config, QObject *parent)
    : QObject(parent)
    , m_config(std::move(config))
{
}

WhaleToolbarSettings::~WhaleToolbarSettings() = default;

QString WhaleToolbarSettings::groupName()
{
    return QStringLiteral("WhaleToolbars");
}

KSharedConfigPtr WhaleToolbarSettings::config() const
{
    return m_config;
}

bool WhaleToolbarSettings::fileViewToolBarsVisible() const
{
    return m_config->group(groupName()).readEntry(showFileViewToolBarsKey, true);
}

void WhaleToolbarSettings::setFileViewToolBarsVisible(bool visible)
{
    if (visible == fileViewToolBarsVisible()) {
        return;
    }
    KConfigGroup group = m_config->group(groupName());
    group.writeEntry(showFileViewToolBarsKey, visible);
    // At once: a window opened later in another process reads it.
    m_config->sync();
    Q_EMIT fileViewToolBarsVisibleChanged(visible);
}

WhaleToolbarSettings::Density WhaleToolbarSettings::toolbarDensity() const
{
    const QString value = m_config->group(groupName()).readEntry(toolbarDensityKey, QString());
    if (value == QLatin1String("Compact")) {
        return Density::Compact;
    }
    if (value == QLatin1String("VeryCompact")) {
        return Density::VeryCompact;
    }
    return Density::Normal;
}

void WhaleToolbarSettings::setToolbarDensity(Density density)
{
    if (density == toolbarDensity() && hasToolbarDensity()) {
        return;
    }
    const bool changed = density != toolbarDensity();
    QString value;
    switch (density) {
    case Density::Normal:
        value = QStringLiteral("Normal");
        break;
    case Density::Compact:
        value = QStringLiteral("Compact");
        break;
    case Density::VeryCompact:
        value = QStringLiteral("VeryCompact");
        break;
    }
    KConfigGroup group = m_config->group(groupName());
    group.writeEntry(toolbarDensityKey, value);
    m_config->sync();
    if (changed) {
        Q_EMIT toolbarDensityChanged(density);
    }
}

bool WhaleToolbarSettings::hasToolbarDensity() const
{
    return m_config->group(groupName()).hasKey(toolbarDensityKey);
}

#include "moc_whaletoolbarsettings.cpp"
