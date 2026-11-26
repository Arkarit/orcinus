/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "previewjobkill.h"

#include <KIO/PreviewJob>
#include <kio_version.h>

#include <QCoreApplication>
#include <QEvent>
#include <QPointer>
#include <QTimer>

#include <cstring>

#if KIO_VERSION < QT_VERSION_CHECK(6, 25, 0)
namespace
{
// KJob::isFinished() is protected; a pointer to it can be formed through a derived class.
struct KJobFinishedAccess : KJob {
    using KJob::isFinished;
};
constexpr bool (KJob::*kjobIsFinished)() const = &KJobFinishedAccess::isFinished;

class FinishedJobTimeoutGuard : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() != QEvent::Timer) {
            return false;
        }
        const QMetaObject *metaObject = watched->metaObject();
        if (metaObject != m_filePreviewJobMetaObject) {
            // KIO::FilePreviewJob is private API, so it is recognized by its class name.
            if (m_filePreviewJobMetaObject || std::strcmp(metaObject->className(), "KIO::FilePreviewJob") != 0) {
                return false;
            }
            m_filePreviewJobMetaObject = metaObject;
        }
        // The only timer of a FilePreviewJob is its timeout; after the job finished it is stale.
        return (static_cast<KJob *>(watched)->*kjobIsFinished)();
    }

private:
    const QMetaObject *m_filePreviewJobMetaObject = nullptr;
};
}
#endif

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

void PreviewJobKill::installFinishedJobTimeoutGuard()
{
#if KIO_VERSION < QT_VERSION_CHECK(6, 25, 0)
    static bool installed = false;
    if (installed || !qApp) {
        return;
    }
    installed = true;
    qApp->installEventFilter(new FinishedJobTimeoutGuard(qApp));
#endif
}
