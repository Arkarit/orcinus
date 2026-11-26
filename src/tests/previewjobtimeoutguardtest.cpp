/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kitemviews/previewjobkill.h"

#include <KJob>
#include <kio_version.h>

#include <QTest>

namespace KIO
{
// Stand-in with the class name of KIO's private per-file preview job.
class FilePreviewJob : public KJob
{
    Q_OBJECT

public:
    void start() override
    {
        m_timer = startTimer(10);
    }
    void finish()
    {
        emitResult();
    }

    int timeouts = 0;

protected:
    void timerEvent(QTimerEvent *event) override
    {
        if (event->timerId() == m_timer) {
            ++timeouts;
        }
    }

private:
    int m_timer = -1;
};
}

class PreviewJobTimeoutGuardTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void testRunningJobGetsTimeouts();
    void testFinishedJobGetsNoTimeouts();
};

void PreviewJobTimeoutGuardTest::initTestCase()
{
    PreviewJobKill::installFinishedJobTimeoutGuard();
}

void PreviewJobTimeoutGuardTest::testRunningJobGetsTimeouts()
{
    KIO::FilePreviewJob job;
    job.setAutoDelete(false);
    job.start();
    QTRY_VERIFY(job.timeouts > 0);
}

void PreviewJobTimeoutGuardTest::testFinishedJobGetsNoTimeouts()
{
#if KIO_VERSION >= QT_VERSION_CHECK(6, 25, 0)
    QSKIP("The guard is only installed for KIO < 6.25");
#endif
    KIO::FilePreviewJob job;
    job.setAutoDelete(false);
    job.start();
    job.finish();

    // A running job with the same timer interval shows that the timeouts were due.
    KIO::FilePreviewJob reference;
    reference.setAutoDelete(false);
    reference.start();
    QTRY_VERIFY(reference.timeouts >= 3);
    QCOMPARE(job.timeouts, 0);
}

QTEST_MAIN(PreviewJobTimeoutGuardTest)

#include "previewjobtimeoutguardtest.moc"
