/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaletoolbarsettings.h"

#include <KConfigGroup>

namespace
{
const char showFileViewToolBarsKey[] = "ShowFileViewToolBars";
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

#include "moc_whaletoolbarsettings.cpp"
