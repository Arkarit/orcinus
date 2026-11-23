/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "syntheticexecutablebits.h"

#include <KFileSystemType>
#include <KIO/CopyJob>

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QThreadPool>
#include <QUrl>

#include <memory>

namespace
{
std::function<bool(const QString &)> &sourceCheckForTesting()
{
    static std::function<bool(const QString &)> check;
    return check;
}

struct CopiedFile {
    QString source;
    QString destination;
};

bool sourceHasNoExecutableBit(const QString &sourcePath, QHash<QString, bool> &cache)
{
    const QString folder = QFileInfo(sourcePath).absolutePath();
    const auto it = cache.constFind(folder);
    if (it != cache.constEnd()) {
        return *it;
    }
    const auto &check = sourceCheckForTesting();
    const bool result = check ? check(folder) : SyntheticExecutableBits::isOnFileSystemWithoutExecutableBit(folder);
    cache.insert(folder, result);
    return result;
}

void cleanUp(const QList<CopiedFile> &copies)
{
    QHash<QString, bool> cache;
    QStringList destinations;
    for (const CopiedFile &copy : copies) {
        if (sourceHasNoExecutableBit(copy.source, cache)) {
            destinations.append(copy.destination);
        }
    }
    SyntheticExecutableBits::removeFromCopies(destinations);
}
}

void SyntheticExecutableBits::cleanUpAfter(const KIO::CopyJob *job)
{
    auto copies = std::make_shared<QList<CopiedFile>>();

    QObject::connect(job,
                     &KIO::CopyJob::copyingDone,
                     job,
                     [copies](KIO::Job *, const QUrl &from, const QUrl &to, const QDateTime &, bool directory, bool renamed) {
                         // A rename stays on the same file system, so the bits are the real ones.
                         if (directory || renamed || !from.isLocalFile() || !to.isLocalFile()) {
                             return;
                         }
                         copies->append({from.toLocalFile(), to.toLocalFile()});
                     });

    QObject::connect(job, &KJob::finished, job, [copies]() {
        if (copies->isEmpty()) {
            return;
        }
        const QList<CopiedFile> list = *copies;
        QThreadPool::globalInstance()->start([list]() {
            cleanUp(list);
        });
    });
}

bool SyntheticExecutableBits::isOnFileSystemWithoutExecutableBit(const QString &localPath)
{
    switch (KFileSystemType::fileSystemType(localPath)) {
    case KFileSystemType::Fat:
    case KFileSystemType::Ntfs:
    case KFileSystemType::Exfat:
        return true;
    default:
        return false;
    }
}

bool SyntheticExecutableBits::looksExecutable(const QString &localPath)
{
    QFile file(localPath);
    if (!file.open(QIODevice::ReadOnly)) {
        // When in doubt, keep the bit.
        return true;
    }
    const QByteArray start = file.read(4);
    // "\177ELF" is the ELF magic number.
    return start.startsWith("#!") || start == QByteArrayView("\177ELF");
}

void SyntheticExecutableBits::removeFromCopies(const QStringList &localPaths)
{
    constexpr QFile::Permissions executable = QFile::ExeOwner | QFile::ExeUser | QFile::ExeGroup | QFile::ExeOther;

    QHash<QString, bool> destinationCache;
    for (const QString &path : localPaths) {
        const QFileInfo info(path);
        if (info.isSymLink() || !info.isFile() || !(info.permissions() & executable)) {
            continue;
        }
        // Changing the bits is pointless when the copy went to such a file system, too.
        const QString folder = info.absolutePath();
        auto it = destinationCache.find(folder);
        if (it == destinationCache.end()) {
            it = destinationCache.insert(folder, isOnFileSystemWithoutExecutableBit(folder));
        }
        if (*it || looksExecutable(path)) {
            continue;
        }
        QFile::setPermissions(path, info.permissions() & ~executable);
    }
}

void SyntheticExecutableBits::setSourceCheckForTesting(std::function<bool(const QString &)> check)
{
    sourceCheckForTesting() = std::move(check);
}
