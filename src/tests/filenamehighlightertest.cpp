/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "views/filenamehighlighter.h"

#include <KColorUtils>
#include <KConfigGroup>
#include <KFileItem>

#include <QDir>
#include <QFile>
#include <QPalette>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using Condition = FileNameHighlightRule::Condition;

namespace
{
FileNameHighlightRule rule(Condition condition, const FileNameTextStyle &style, const QStringList &patterns = {})
{
    FileNameHighlightRule result;
    result.condition = condition;
    result.patterns = patterns;
    result.style = style;
    return result;
}

FileNameTextStyle colorStyle(const FileNameColor &color)
{
    FileNameTextStyle style;
    style.color = color;
    return style;
}

FileNameTextStyle boldStyle(bool bold = true)
{
    FileNameTextStyle style;
    style.bold = bold;
    return style;
}

FileNameTextStyle italicStyle(bool italic = true)
{
    FileNameTextStyle style;
    style.italic = italic;
    return style;
}

QPalette paletteWithBase(const QColor &base)
{
    QPalette palette;
    palette.setColor(QPalette::Base, base);
    return palette;
}
}

class FileNameHighlighterTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testDisabled();
    void testEnabledWithoutRules();
    void testExecutable_data();
    void testExecutable();
    void testWindowsExecutableDoesNotQueryFileSystem();
    void testFileSystemTypeIsCachedPerDirectory();
    void testSymlinks();
    void testMimeType_data();
    void testMimeType();
    void testCascade();
    void testOffBlocksLowerRules();
    void testDisabledRulesAreSkipped();
    void testFontOnlyEvaluationSkipsMimeTypeRules();
    void testAffectsFont();
    void testFontFor();
    void testSchemeRoleFollowsColorScheme();
    void testCustomColor();
    void testContrastCorrection();

private:
    KFileItem createFile(const QString &name, QFile::Permissions permissions);
    KFileItem itemFor(const QString &name) const;
    KSharedConfigPtr createColorScheme(const QString &name, const QColor &positiveText);

    QTemporaryDir *m_dir = nullptr;
};

void FileNameHighlighterTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void FileNameHighlighterTest::init()
{
    m_dir = new QTemporaryDir();
    QVERIFY(m_dir->isValid());
}

void FileNameHighlighterTest::cleanup()
{
    delete m_dir;
    m_dir = nullptr;
}

KFileItem FileNameHighlighterTest::createFile(const QString &name, QFile::Permissions permissions)
{
    QFile file(m_dir->filePath(name));
    if (!file.open(QIODevice::WriteOnly)) {
        return KFileItem();
    }
    file.write("test");
    file.close();
    file.setPermissions(permissions);
    return itemFor(name);
}

KFileItem FileNameHighlighterTest::itemFor(const QString &name) const
{
    return KFileItem(QUrl::fromLocalFile(m_dir->filePath(name)));
}

KSharedConfigPtr FileNameHighlighterTest::createColorScheme(const QString &name, const QColor &positiveText)
{
    KSharedConfigPtr config = KSharedConfig::openConfig(m_dir->filePath(name), KConfig::SimpleConfig);
    KConfigGroup group(config, QStringLiteral("Colors:View"));
    group.writeEntry("ForegroundPositive", positiveText);
    group.sync();
    return config;
}

void FileNameHighlighterTest::testDisabled()
{
    const KFileItem item = createFile(QStringLiteral("script.sh"), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    const FileNameHighlighter highlighter(false, {rule(Condition::Executable, boldStyle())});

    QVERIFY(highlighter.styleFor(item).isEmpty());
    QVERIFY(!highlighter.affectsFont());
}

void FileNameHighlighterTest::testEnabledWithoutRules()
{
    const KFileItem item = createFile(QStringLiteral("script.sh"), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    const FileNameHighlighter highlighter(true, {});

    QVERIFY(highlighter.isEnabled());
    QVERIFY(highlighter.styleFor(item).isEmpty());
    QVERIFY(!highlighter.affectsFont());
}

void FileNameHighlighterTest::testExecutable_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<bool>("executeBit");
    QTest::addColumn<int>("fileSystemType");
    QTest::addColumn<bool>("expected");

    const int posix = KFileSystemType::Other;

    QTest::newRow("script with x on posix") << "script.sh" << true << posix << true;
    QTest::newRow("script without x on posix") << "script.sh" << false << posix << false;
    QTest::newRow("exe without x on posix") << "foo.exe" << false << posix << true;
    QTest::newRow("upper case exe on posix") << "FOO.EXE" << false << posix << true;
    QTest::newRow("dll without x on posix") << "foo.dll" << false << posix << false;
    QTest::newRow("dll with x on posix") << "foo.dll" << true << posix << true;
    QTest::newRow("multiple suffixes ending in exe") << "foo.tar.exe" << false << posix << true;

    const std::pair<const char *, KFileSystemType::Type> nonPosixFileSystems[] = {
        {"ntfs", KFileSystemType::Ntfs},
        {"fat", KFileSystemType::Fat},
        {"exfat", KFileSystemType::Exfat},
    };
    for (const auto &[label, type] : nonPosixFileSystems) {
        QTest::addRow("exe on %s", label) << "foo.exe" << true << int(type) << true;
        QTest::addRow("script with synthetic x on %s", label) << "script.sh" << true << int(type) << false;
        QTest::addRow("dll with synthetic x on %s", label) << "foo.dll" << true << int(type) << false;
        QTest::addRow("bat with synthetic x on %s", label) << "run.bat" << true << int(type) << false;
        QTest::addRow("msi with synthetic x on %s", label) << "setup.msi" << true << int(type) << false;
    }
}

void FileNameHighlighterTest::testExecutable()
{
    QFETCH(QString, name);
    QFETCH(bool, executeBit);
    QFETCH(int, fileSystemType);
    QFETCH(bool, expected);

    QFile::Permissions permissions = QFile::ReadOwner | QFile::WriteOwner;
    if (executeBit) {
        permissions |= QFile::ExeOwner;
    }
    const KFileItem item = createFile(name, permissions);
    QVERIFY(!item.isNull());

    const FileNameHighlighter highlighter(true, {rule(Condition::Executable, boldStyle())}, [fileSystemType](const QString &) {
        return static_cast<KFileSystemType::Type>(fileSystemType);
    });

    QCOMPARE(highlighter.isExecutableFile(item), expected);
    QCOMPARE(highlighter.styleFor(item).bold.has_value(), expected);
}

void FileNameHighlighterTest::testWindowsExecutableDoesNotQueryFileSystem()
{
    const KFileItem exe = createFile(QStringLiteral("foo.exe"), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QVERIFY(QDir(m_dir->path()).mkdir(QStringLiteral("bar.exe")));
    const KFileItem directory = itemFor(QStringLiteral("bar.exe"));

    int queries = 0;
    const FileNameHighlighter highlighter(true, {rule(Condition::Executable, boldStyle())}, [&queries](const QString &) {
        ++queries;
        return KFileSystemType::Other;
    });

    QVERIFY(highlighter.isExecutableFile(exe));
    QVERIFY(!highlighter.isExecutableFile(directory));
    QCOMPARE(queries, 0);
}

void FileNameHighlighterTest::testFileSystemTypeIsCachedPerDirectory()
{
    const KFileItem first = createFile(QStringLiteral("first.sh"), QFile::ReadOwner | QFile::ExeOwner);
    const KFileItem second = createFile(QStringLiteral("second.sh"), QFile::ReadOwner | QFile::ExeOwner);

    int queries = 0;
    const FileNameHighlighter highlighter(true, {rule(Condition::Executable, boldStyle())}, [&queries](const QString &) {
        ++queries;
        return KFileSystemType::Other;
    });

    QVERIFY(highlighter.isExecutableFile(first));
    QVERIFY(highlighter.isExecutableFile(second));
    QCOMPARE(queries, 1);

    highlighter.clearFileSystemCache();
    QVERIFY(highlighter.isExecutableFile(first));
    QCOMPARE(queries, 2);
}

void FileNameHighlighterTest::testSymlinks()
{
    createFile(QStringLiteral("target.sh"), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QVERIFY(QFile::link(m_dir->filePath(QStringLiteral("target.sh")), m_dir->filePath(QStringLiteral("link.sh"))));
    QVERIFY(QFile::link(m_dir->filePath(QStringLiteral("missing")), m_dir->filePath(QStringLiteral("broken.exe"))));

    const FileNameHighlighter highlighter(true, {rule(Condition::Executable, boldStyle()), rule(Condition::Symlink, italicStyle())}, [](const QString &) {
        return KFileSystemType::Other;
    });

    const FileNameTextStyle linkStyle = highlighter.styleFor(itemFor(QStringLiteral("link.sh")));
    QCOMPARE(linkStyle.bold, std::optional<bool>(true));
    QCOMPARE(linkStyle.italic, std::optional<bool>(true));

    // KIO reports broken links with 0777 permissions; they must not count as executable.
    const KFileItem broken = itemFor(QStringLiteral("broken.exe"));
    QVERIFY(broken.isLink());
    QVERIFY(!highlighter.isExecutableFile(broken));
    const FileNameTextStyle brokenStyle = highlighter.styleFor(broken);
    QVERIFY(!brokenStyle.bold.has_value());
    QCOMPARE(brokenStyle.italic, std::optional<bool>(true));

    const FileNameTextStyle targetStyle = highlighter.styleFor(itemFor(QStringLiteral("target.sh")));
    QCOMPARE(targetStyle.bold, std::optional<bool>(true));
    QVERIFY(!targetStyle.italic.has_value());
}

void FileNameHighlighterTest::testMimeType_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<QStringList>("patterns");
    QTest::addColumn<bool>("expected");

    const QStringList media = {QStringLiteral("image/*"), QStringLiteral("audio/*"), QStringLiteral("video/*")};

    QTest::newRow("image") << "photo.png" << media << true;
    QTest::newRow("audio") << "sound.mp3" << media << true;
    QTest::newRow("video") << "movie.mp4" << media << true;
    QTest::newRow("text is no media") << "notes.txt" << media << false;
    // Inherits from video/quicktime, but its canonical major type is "application".
    QTest::newRow("group does not follow inheritance") << "media.qtl" << media << false;
    QTest::newRow("exact type follows inheritance") << "main.cpp" << QStringList{QStringLiteral("text/plain")} << true;
    QTest::newRow("exact type") << "photo.png" << QStringList{QStringLiteral("image/png")} << true;
    QTest::newRow("other exact type") << "photo.png" << QStringList{QStringLiteral("image/jpeg")} << false;
    QTest::newRow("unknown type") << "photo.png" << QStringList{QStringLiteral("image/does-not-exist")} << false;
}

void FileNameHighlighterTest::testMimeType()
{
    QFETCH(QString, name);
    QFETCH(QStringList, patterns);
    QFETCH(bool, expected);

    const KFileItem item = createFile(name, QFile::ReadOwner | QFile::WriteOwner);
    const FileNameHighlighter highlighter(true, {rule(Condition::MimeType, boldStyle(), patterns)});

    QCOMPARE(highlighter.styleFor(item).bold.has_value(), expected);
}

void FileNameHighlighterTest::testCascade()
{
    createFile(QStringLiteral("target.mp4"), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QVERIFY(QFile::link(m_dir->filePath(QStringLiteral("target.mp4")), m_dir->filePath(QStringLiteral("link.mp4"))));

    const FileNameColor positive = FileNameColor::schemeRole(KColorScheme::PositiveText);
    const FileNameColor custom = FileNameColor::custom(Qt::magenta);
    const FileNameHighlighter highlighter(true,
                                          {
                                              rule(Condition::Executable, boldStyle()),
                                              rule(Condition::Symlink, italicStyle()),
                                              rule(Condition::MimeType, colorStyle(positive), {QStringLiteral("video/*")}),
                                              rule(Condition::Executable, colorStyle(custom)),
                                          },
                                          [](const QString &) {
                                              return KFileSystemType::Other;
                                          });

    const FileNameTextStyle style = highlighter.styleFor(itemFor(QStringLiteral("link.mp4")));
    QCOMPARE(style.bold, std::optional<bool>(true));
    QCOMPARE(style.italic, std::optional<bool>(true));
    // The scheme role of the higher rule blocks the custom color of the lower one.
    QCOMPARE(style.color, positive);
}

void FileNameHighlighterTest::testOffBlocksLowerRules()
{
    createFile(QStringLiteral("target.png"), QFile::ReadOwner | QFile::WriteOwner);
    QVERIFY(QFile::link(m_dir->filePath(QStringLiteral("target.png")), m_dir->filePath(QStringLiteral("link.png"))));

    const FileNameColor normal = FileNameColor::schemeRole(KColorScheme::NormalText);
    const FileNameHighlighter highlighter(true,
                                          {
                                              rule(Condition::MimeType, italicStyle(false), {QStringLiteral("image/png")}),
                                              rule(Condition::MimeType, colorStyle(normal), {QStringLiteral("image/png")}),
                                              rule(Condition::Symlink, italicStyle()),
                                              rule(Condition::MimeType, colorStyle(FileNameColor::custom(Qt::red)), {QStringLiteral("image/*")}),
                                          });

    const FileNameTextStyle style = highlighter.styleFor(itemFor(QStringLiteral("link.png")));
    QCOMPARE(style.italic, std::optional<bool>(false));
    QCOMPARE(style.color, normal);
}

void FileNameHighlighterTest::testDisabledRulesAreSkipped()
{
    const KFileItem item = createFile(QStringLiteral("photo.png"), QFile::ReadOwner | QFile::WriteOwner);

    FileNameHighlightRule disabled = rule(Condition::MimeType, boldStyle(), {QStringLiteral("image/*")});
    disabled.enabled = false;
    const FileNameHighlighter highlighter(true, {disabled, rule(Condition::MimeType, boldStyle(false), {QStringLiteral("image/*")})});

    QCOMPARE(highlighter.styleFor(item).bold, std::optional<bool>(false));
    QVERIFY(highlighter.affectsFont());
}

void FileNameHighlighterTest::testFontOnlyEvaluationSkipsMimeTypeRules()
{
    const KFileItem item = createFile(QStringLiteral("photo.png"), QFile::ReadOwner | QFile::WriteOwner);

    const FileNameHighlighter highlighter(true,
                                          {
                                              rule(Condition::Symlink, italicStyle()),
                                              rule(Condition::MimeType, colorStyle(FileNameColor::custom(Qt::red)), {QStringLiteral("image/*")}),
                                          });

    QVERIFY(highlighter.styleFor(item, FileNameHighlighter::Font).isEmpty());
    QVERIFY(highlighter.m_mimeTypeMatches.isEmpty());

    QVERIFY(highlighter.styleFor(item).color.isSet());
    QCOMPARE(highlighter.m_mimeTypeMatches.count(), 1);
}

void FileNameHighlighterTest::testAffectsFont()
{
    const FileNameTextStyle color = colorStyle(FileNameColor::custom(Qt::red));

    const FileNameHighlighter colorOnly(true, {rule(Condition::MimeType, color, {QStringLiteral("image/*")})});
    QVERIFY(!colorOnly.affectsFont());
    QVERIFY(!colorOnly.fontDependsOnMimeType());

    const FileNameHighlighter flagFont(true, {rule(Condition::Symlink, italicStyle()), rule(Condition::MimeType, color, {QStringLiteral("image/*")})});
    QVERIFY(flagFont.affectsFont());
    QVERIFY(!flagFont.fontDependsOnMimeType());

    const FileNameHighlighter mimeFont(true, {rule(Condition::MimeType, boldStyle(), {QStringLiteral("image/*")})});
    QVERIFY(mimeFont.affectsFont());
    QVERIFY(mimeFont.fontDependsOnMimeType());
}

void FileNameHighlighterTest::testFontFor()
{
    createFile(QStringLiteral("target.sh"), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QVERIFY(QFile::link(m_dir->filePath(QStringLiteral("target.sh")), m_dir->filePath(QStringLiteral("link.sh"))));
    const KFileItem link = itemFor(QStringLiteral("link.sh"));
    const QFont baseFont;

    // Without font rules, links are not italic: the rules replace Dolphin's built-in italic font.
    const FileNameHighlighter colorOnly(true, {rule(Condition::Symlink, colorStyle(FileNameColor::custom(Qt::red)))});
    QCOMPARE(colorOnly.fontFor(link, baseFont), baseFont);

    const FileNameHighlighter fonts(true, {rule(Condition::Executable, boldStyle()), rule(Condition::Symlink, italicStyle())}, [](const QString &) {
        return KFileSystemType::Other;
    });
    const QFont font = fonts.fontFor(link, baseFont);
    QVERIFY(font.bold());
    QVERIFY(font.italic());

    // "Off" removes an attribute of the base font.
    QFont italicBase;
    italicBase.setItalic(true);
    const FileNameHighlighter off(true, {rule(Condition::Symlink, italicStyle(false))});
    QVERIFY(!off.fontFor(link, italicBase).italic());

    const FileNameHighlighter disabled(false, {rule(Condition::Symlink, boldStyle())});
    QCOMPARE(disabled.fontFor(link, baseFont), baseFont);
}

void FileNameHighlighterTest::testSchemeRoleFollowsColorScheme()
{
    const QColor lightPositive(0, 110, 40);
    const QColor darkPositive(120, 230, 150);
    const KSharedConfigPtr light = createColorScheme(QStringLiteral("light.colors"), lightPositive);
    const KSharedConfigPtr dark = createColorScheme(QStringLiteral("dark.colors"), darkPositive);

    const FileNameColor positive = FileNameColor::schemeRole(KColorScheme::PositiveText);
    QCOMPARE(FileNameHighlighter::resolveNameColor(positive, paletteWithBase(Qt::white), light), lightPositive);
    QCOMPARE(FileNameHighlighter::resolveNameColor(positive, paletteWithBase(Qt::black), dark), darkPositive);

    const FileNameHighlighter highlighter(true, {}, {}, dark);
    QCOMPARE(highlighter.nameColor(positive, paletteWithBase(Qt::black)), darkPositive);
    QVERIFY(!highlighter.nameColor(FileNameColor(), paletteWithBase(Qt::black)).isValid());
}

void FileNameHighlighterTest::testCustomColor()
{
    const QColor color(200, 0, 0);
    QCOMPARE(FileNameHighlighter::resolveNameColor(FileNameColor::custom(color), paletteWithBase(Qt::white)), color);
    QVERIFY(!FileNameColor::custom(QColor()).isSet());
}

void FileNameHighlighterTest::testContrastCorrection()
{
    const QColor darkBlue(20, 20, 90);
    const QColor nearlyBlack(30, 30, 30);

    // Readable colors stay unchanged.
    QCOMPARE(FileNameHighlighter::ensureContrast(darkBlue, Qt::white), darkBlue);

    // A dark color on a dark background is lightened until it is readable.
    const QColor corrected = FileNameHighlighter::ensureContrast(darkBlue, nearlyBlack);
    QVERIFY(KColorUtils::contrastRatio(corrected, nearlyBlack) >= FileNameHighlighter::MinimumContrastRatio);
    QVERIFY(KColorUtils::luma(corrected) > KColorUtils::luma(darkBlue));

    // A light color on a light background is darkened.
    const QColor lightYellow(250, 250, 200);
    const QColor darkened = FileNameHighlighter::ensureContrast(lightYellow, Qt::white);
    QVERIFY(KColorUtils::contrastRatio(darkened, Qt::white) >= FileNameHighlighter::MinimumContrastRatio);
}

QTEST_MAIN(FileNameHighlighterTest)

#include "filenamehighlightertest.moc"
