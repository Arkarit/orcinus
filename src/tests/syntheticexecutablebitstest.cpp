/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "views/syntheticexecutablebits.h"

#include <KIO/CopyJob>

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>
#include <QUrl>

class SyntheticExecutableBitsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    void testLooksExecutable();
    void testRemoveFromCopies();
    void testCopyFromFileSystemWithoutExecutableBit();
    void testCopyFromOtherFileSystemKeepsBits();

private:
    QString createFile(const QString &path, const QByteArray &content);
    bool isExecutable(const QString &path) const;
    void copyAndWait(const QString &sourceFolder, const QString &destination);

    std::unique_ptr<QTemporaryDir> m_dir;
};

namespace
{
constexpr QFile::Permissions executable = QFile::ExeOwner | QFile::ExeUser | QFile::ExeGroup | QFile::ExeOther;
}

void SyntheticExecutableBitsTest::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
}

void SyntheticExecutableBitsTest::cleanup()
{
    SyntheticExecutableBits::setSourceCheckForTesting(nullptr);
    m_dir.reset();
}

QString SyntheticExecutableBitsTest::createFile(const QString &path, const QByteArray &content)
{
    const QString fullPath = m_dir->filePath(path);
    QFile file(fullPath);
    if (!file.open(QIODevice::WriteOnly)) {
        return QString();
    }
    file.write(content);
    file.close();
    file.setPermissions(file.permissions() | executable);
    return fullPath;
}

bool SyntheticExecutableBitsTest::isExecutable(const QString &path) const
{
    return QFile::permissions(path) & executable;
}

void SyntheticExecutableBitsTest::copyAndWait(const QString &sourceFolder, const QString &destination)
{
    KIO::CopyJob *job = KIO::copy(QUrl::fromLocalFile(sourceFolder), QUrl::fromLocalFile(destination), KIO::HideProgressInfo);
    SyntheticExecutableBits::cleanUpAfter(job);
    QSignalSpy finished(job, &KJob::finished);
    QVERIFY(finished.wait());
    QThreadPool::globalInstance()->waitForDone();
}

void SyntheticExecutableBitsTest::testLooksExecutable()
{
    QVERIFY(SyntheticExecutableBits::looksExecutable(createFile(QStringLiteral("script"), "#!/bin/sh\necho hi\n")));
    QVERIFY(SyntheticExecutableBits::looksExecutable(createFile(QStringLiteral("binary"), QByteArray("\177ELF\2\1\1"))));
    QVERIFY(!SyntheticExecutableBits::looksExecutable(createFile(QStringLiteral("text.txt"), "hello")));
    QVERIFY(!SyntheticExecutableBits::looksExecutable(createFile(QStringLiteral("empty"), QByteArray())));
}

void SyntheticExecutableBitsTest::testRemoveFromCopies()
{
    const QString script = createFile(QStringLiteral("script"), "#!/bin/sh\n");
    const QString text = createFile(QStringLiteral("text.txt"), "hello");
    const QString link = m_dir->filePath(QStringLiteral("link"));
    QVERIFY(QFile::link(text, link));

    // Only the link is passed: the file it points to must keep its bits.
    SyntheticExecutableBits::removeFromCopies({link});
    QVERIFY(isExecutable(text));

    SyntheticExecutableBits::removeFromCopies({script, text, m_dir->path()});
    QVERIFY(isExecutable(script));
    QVERIFY(!isExecutable(text));
    QVERIFY(QFile::permissions(text) & QFile::ReadOwner);
    QVERIFY(isExecutable(m_dir->path()));
}

void SyntheticExecutableBitsTest::testCopyFromFileSystemWithoutExecutableBit()
{
    QVERIFY(QDir(m_dir->path()).mkpath(QStringLiteral("source/sub")));
    createFile(QStringLiteral("source/script"), "#!/bin/sh\n");
    createFile(QStringLiteral("source/photo.jpg"), "\xff\xd8\xff\xe0");
    createFile(QStringLiteral("source/sub/notes.txt"), "hello");
    const QString sourceFolder = m_dir->filePath(QStringLiteral("source"));
    SyntheticExecutableBits::setSourceCheckForTesting([sourceFolder](const QString &folder) {
        return folder.startsWith(sourceFolder);
    });

    const QString destination = m_dir->filePath(QStringLiteral("destination"));
    QVERIFY(QDir().mkpath(destination));
    copyAndWait(sourceFolder, destination);

    QVERIFY(isExecutable(destination + QStringLiteral("/source/script")));
    QVERIFY(!isExecutable(destination + QStringLiteral("/source/photo.jpg")));
    QVERIFY(!isExecutable(destination + QStringLiteral("/source/sub/notes.txt")));
    QVERIFY(isExecutable(destination + QStringLiteral("/source/sub")));
}

void SyntheticExecutableBitsTest::testCopyFromOtherFileSystemKeepsBits()
{
    QVERIFY(QDir(m_dir->path()).mkpath(QStringLiteral("source")));
    createFile(QStringLiteral("source/tool"), "no shebang, but meant to be executable");
    SyntheticExecutableBits::setSourceCheckForTesting([](const QString &) {
        return false;
    });

    const QString destination = m_dir->filePath(QStringLiteral("destination"));
    QVERIFY(QDir().mkpath(destination));
    copyAndWait(m_dir->filePath(QStringLiteral("source")), destination);

    QVERIFY(isExecutable(destination + QStringLiteral("/source/tool")));
}

QTEST_GUILESS_MAIN(SyntheticExecutableBitsTest)

#include "syntheticexecutablebitstest.moc"
