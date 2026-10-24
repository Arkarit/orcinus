/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleviewersettings.h"

#include <KConfigGroup>
#include <KSharedConfig>

namespace
{
const QString GroupName = QStringLiteral("Viewer");
const char MimeTypesKey[] = "MimeTypes";
const char WrapAroundKey[] = "WrapAround";
const char MultiSelectionKey[] = "MultiSelection";

KConfigGroup group()
{
    return KConfigGroup(KSharedConfig::openConfig(), GroupName);
}
}

namespace WhaleViewerSettings
{
QStringList mimeTypes()
{
    return group().readEntry(MimeTypesKey, QStringList{QStringLiteral("image/*")});
}

void setMimeTypes(const QStringList &mimeTypes)
{
    KConfigGroup viewerGroup = group();
    viewerGroup.writeEntry(MimeTypesKey, mimeTypes);
    viewerGroup.sync();
}

bool wrapAround()
{
    return group().readEntry(WrapAroundKey, false);
}

void setWrapAround(bool wrapAround)
{
    KConfigGroup viewerGroup = group();
    viewerGroup.writeEntry(WrapAroundKey, wrapAround);
    viewerGroup.sync();
}

MultiSelection multiSelection()
{
    const QString value = group().readEntry(MultiSelectionKey, QString());
    return value == QLatin1String("MoveSelection") ? MultiSelection::MoveSelection : MultiSelection::KeepSelection;
}

void setMultiSelection(MultiSelection multiSelection)
{
    KConfigGroup viewerGroup = group();
    if (multiSelection == MultiSelection::KeepSelection) {
        viewerGroup.deleteEntry(MultiSelectionKey);
    } else {
        viewerGroup.writeEntry(MultiSelectionKey, QStringLiteral("MoveSelection"));
    }
    viewerGroup.sync();
}
}
