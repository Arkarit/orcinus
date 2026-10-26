/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleviewersettings.h"

#include "config-dolphin.h"

#include <KConfigGroup>
#include <KSharedConfig>

namespace
{
const QString GroupName = QStringLiteral("Viewer");
const char MimeTypesKey[] = "MimeTypes";
const char WrapAroundKey[] = "WrapAround";
const char MultiSelectionKey[] = "MultiSelection";
const char UpscaleKey[] = "Upscale";
const char SmoothZoomKey[] = "SmoothZoom";
const char VideoAutoplayKey[] = "VideoAutoplay";
const char VideoSoundKey[] = "VideoSound";
const char VideoLoopKey[] = "VideoLoop";

KConfigGroup group()
{
    return KConfigGroup(KSharedConfig::openConfig(), GroupName);
}

void writeBool(const char *key, bool value)
{
    KConfigGroup viewerGroup = group();
    viewerGroup.writeEntry(key, value);
    viewerGroup.sync();
}
}

namespace WhaleViewerSettings
{
QStringList mimeTypes()
{
    QStringList defaults{QStringLiteral("image/*")};
    if (videoSupported()) {
        defaults.append(QStringLiteral("video/*"));
    }
    return group().readEntry(MimeTypesKey, defaults);
}

void setMimeTypes(const QStringList &mimeTypes)
{
    KConfigGroup viewerGroup = group();
    viewerGroup.writeEntry(MimeTypesKey, mimeTypes);
    viewerGroup.sync();
}

bool videoSupported()
{
    return HAVE_MPV;
}

bool videoAutoplay()
{
    return group().readEntry(VideoAutoplayKey, true);
}

void setVideoAutoplay(bool autoplay)
{
    writeBool(VideoAutoplayKey, autoplay);
}

bool videoSound()
{
    return group().readEntry(VideoSoundKey, true);
}

void setVideoSound(bool sound)
{
    writeBool(VideoSoundKey, sound);
}

bool videoLoop()
{
    return group().readEntry(VideoLoopKey, true);
}

void setVideoLoop(bool loop)
{
    writeBool(VideoLoopKey, loop);
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

bool upscale()
{
    return group().readEntry(UpscaleKey, false);
}

void setUpscale(bool upscale)
{
    KConfigGroup viewerGroup = group();
    viewerGroup.writeEntry(UpscaleKey, upscale);
    viewerGroup.sync();
}

bool smoothZoom()
{
    return group().readEntry(SmoothZoomKey, false);
}

void setSmoothZoom(bool smoothZoom)
{
    KConfigGroup viewerGroup = group();
    viewerGroup.writeEntry(SmoothZoomKey, smoothZoom);
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
