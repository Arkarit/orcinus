/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "previewjobkill.h"

#include <KIO/PreviewJob>
#include <kio_version.h>

#include <QCoreApplication>
#include <QPointer>
#include <QTimer>

void PreviewJobKill::kill(KIO::PreviewJob *job)
{
#if KIO_VERSION < QT_VERSION_CHECK(6, 25, 0)
    // While the job is running, its sub-jobs are its direct children.
    const QList<KJob *> children = job->findChildren<KJob *>(Qt::FindDirectChildrenOnly);
    job->kill();

    // Killing detaches the sub-jobs (KCompositeJob::clearSubjobs() resets their parent).
    QList<QPointer<KJob>> detachedSubJobs;
    for (KJob *child : children) {
        if (child->parent() != job) {
            detachedSubJobs.append(child);
        }
    }
    if (!detachedSubJobs.isEmpty()) {
        QTimer::singleShot(0, qApp, [detachedSubJobs]() {
            for (const QPointer<KJob> &subJob : detachedSubJobs) {
                delete subJob.data();
            }
        });
    }
#else
    job->kill();
#endif
}
