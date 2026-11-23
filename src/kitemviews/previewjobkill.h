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
}

#endif
