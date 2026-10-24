/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEVIEWERSETTINGS_H
#define WHALEVIEWERSETTINGS_H

#include <QStringList>

/**
 * @brief The settings of the built-in viewer, in whalerc [Viewer] (Whale).
 *
 * See .privatedoc/whale-viewer-plan.md.
 */
namespace WhaleViewerSettings
{
/**
 * How the selection of the view follows the viewer while it pages through a multiple selection
 * (V-E3). Without a multiple selection, the selection always moves to the shown file.
 * Experiment of V1, hidden setting: the one the user does not choose is removed again.
 */
enum class MultiSelection {
    KeepSelection, ///< A: the selection stays, only the current item moves
    MoveSelection, ///< B: the shown file becomes the selection
};

/**
 * @return The MIME types the viewer opens: names such as image/png, or groups given as the
 *         first part with an asterisk (image/ and *). Default: all images.
 */
QStringList mimeTypes();
void setMimeTypes(const QStringList &mimeTypes);

/** @return Whether paging goes from the last file to the first and back. Default off. */
bool wrapAround();
void setWrapAround(bool wrapAround);

MultiSelection multiSelection();
void setMultiSelection(MultiSelection multiSelection);
}

#endif
