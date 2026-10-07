/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALERECURSIVEPROJECTION_H
#define WHALERECURSIVEPROJECTION_H

#include "whaletreenode.h"

#include <KFileItem>

#include <functional>

/**
 * @brief The files of a scanned tree as a flat list of KFileItems (F1, "Show flat (no folders)").
 *
 * The tree comes from WhaleTreeScanner, so recursion, hidden entries, file system borders
 * and symbolic links follow the scanner's rules; nothing walks the file system again. Only
 * the details of each file that the tree does not hold (type, permissions, owner, times,
 * link target) are read with one stat per file, the way KIO's file worker lists a folder,
 * so the items equal those of Dolphin's directory lister (delayed MIME types included).
 *
 * Meant to run in a worker thread; nothing here touches the GUI.
 */
namespace WhaleRecursiveProjection
{
struct Result {
    /// Every non-folder entry below the root, in any depth (not the root itself).
    KFileItemList items;
    /// Symbolic links to folders: left out, because they are folders for the user and the
    /// scan does not follow them.
    int folderLinks = 0;
    /// Entries that vanished between the scan and the projection.
    int vanished = 0;
    bool canceled = false;
};

/**
 * @return The files below @p root. @p canceled is asked from time to time; true ends the
 *         projection early (Result::canceled).
 */
Result project(const WhaleTreeNode &root, const std::function<bool()> &canceled = {});

/**
 * @return The item of the local file @p path as KIO's file worker would list it, or a null
 *         item if it does not exist. @p isFolder is set if it is a folder or a link to one.
 */
KFileItem fileItem(const QString &path, bool *isFolder = nullptr);
}

#endif
