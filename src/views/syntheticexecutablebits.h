/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SYNTHETICEXECUTABLEBITS_H
#define SYNTHETICEXECUTABLEBITS_H

#include "dolphin_export.h"

#include <QStringList>

#include <functional>

namespace KIO
{
class CopyJob;
}

/**
 * FAT, NTFS and exFAT have no executable bit; their mount options often mark
 * every file as executable. KIO copies that synthetic bit to the destination.
 *
 * After a copy or move from such a file system to a POSIX one, this removes the
 * executable bits again from every copied file that is not really executable
 * (an ELF binary or a script starting with "#!"). It runs in a worker thread
 * once the job has finished, so the copy itself stays unchanged.
 */
namespace SyntheticExecutableBits
{
/** Watches @p job and cleans up the files it copied once it has finished. */
DOLPHIN_EXPORT void cleanUpAfter(const KIO::CopyJob *job);

/** True if @p localPath is on a file system without a real executable bit. */
DOLPHIN_EXPORT bool isOnFileSystemWithoutExecutableBit(const QString &localPath);

/** True if the file starts like an ELF binary or a "#!" script. */
DOLPHIN_EXPORT bool looksExecutable(const QString &localPath);

/**
 * Removes the executable bits of the regular files in @p localPaths that do not
 * look executable. Symbolic links and folders are left alone.
 */
DOLPHIN_EXPORT void removeFromCopies(const QStringList &localPaths);

/** For tests: replaces the file system check for copy sources. */
DOLPHIN_EXPORT void setSourceCheckForTesting(std::function<bool(const QString &)> check);
}

#endif
