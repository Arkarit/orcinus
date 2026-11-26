/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PREVIEWJOBKILL_H
#define PREVIEWJOBKILL_H

#include "dolphin_export.h"

namespace KIO
{
class PreviewJob;
}

namespace PreviewJobKill
{
/**
 * Kills a KIO::PreviewJob (quietly, like KIO::PreviewJob::kill()).
 *
 * Before KIO 6.25, killing a PreviewJob detaches its per-file sub-jobs and only schedules them
 * for deferred deletion, while their 2 s timeout timer keeps running. Their internal transfer
 * job is not killed and is referenced through a raw pointer. If a nested event loop runs in the
 * meantime (e.g. a context menu opened right after start-up), the transfer job can finish and
 * get deleted inside it, the deferred deletion of the sub-job does not happen there, and the
 * sub-job's timeout then calls kill() on the deleted transfer job, which crashes.
 *
 * As a workaround, the detached sub-jobs are deleted on the next event loop iteration. Unlike a
 * deferred deletion this also happens inside nested event loops, so it is long before the
 * timeout. It is not deleted synchronously because the kill may come from one of its signals.
 *
 * Use this for every PreviewJob that is killed.
 */
DOLPHIN_EXPORT void kill(KIO::PreviewJob *job);

/**
 * Installs an application-wide guard against the timeout of finished preview sub-jobs.
 *
 * Before KIO 6.25, a per-file sub-job (KIO::FilePreviewJob) does not stop its 2 s timeout
 * timer when it finishes. A finished sub-job waits for its deferred deletion, which does not
 * happen inside a nested event loop (e.g. a context menu opened right after start-up) that was
 * entered after deleteLater() was called. When the timeout fires there, it calls kill() on its
 * already deleted transfer job and crashes. This also affects sub-jobs that finished normally,
 * so kill() above cannot cover it.
 *
 * The guard is an event filter on the application that drops timer events for finished
 * KIO::FilePreviewJob objects. It does nothing with KIO 6.25 or newer.
 *
 * Call once after the QApplication has been created.
 */
DOLPHIN_EXPORT void installFinishedJobTimeoutGuard();
}

#endif
