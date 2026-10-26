/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whalesplitviewstartup.h"

#include "dolphin_generalsettings.h"

#include <KConfigGroup>
#include <KSharedConfig>

namespace
{
const QString FormerKey = QStringLiteral("SplitView");
const QString StateGroup = QStringLiteral("SplitView");
const QString StateKey = QStringLiteral("Enabled");
}

void WhaleSplitViewStartup::migrate()
{
    KConfigGroup general(GeneralSettings::self()->config(), QStringLiteral("General"));
    if (!general.hasKey(FormerKey)) {
        return;
    }
    // Off was the default rather than a choice, so it becomes the last state.
    if (general.readEntry(FormerKey, false)) {
        GeneralSettings::setSplitViewAtStartup(GeneralSettings::EnumSplitViewAtStartup::AlwaysSplit);
    }
    general.deleteEntry(FormerKey);
    GeneralSettings::self()->save();
}

bool WhaleSplitViewStartup::openInSplitView()
{
    switch (GeneralSettings::splitViewAtStartup()) {
    case GeneralSettings::EnumSplitViewAtStartup::AlwaysSplit:
        return true;
    case GeneralSettings::EnumSplitViewAtStartup::AlwaysSingle:
        return false;
    default:
        return KSharedConfig::openStateConfig()->group(StateGroup).readEntry(StateKey, false);
    }
}

void WhaleSplitViewStartup::rememberSplitView(bool enabled)
{
    KConfigGroup state = KSharedConfig::openStateConfig()->group(StateGroup);
    if (state.readEntry(StateKey, false) != enabled || !state.hasKey(StateKey)) {
        state.writeEntry(StateKey, enabled);
        state.sync();
    }
}
