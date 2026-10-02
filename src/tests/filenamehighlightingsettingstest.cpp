/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "views/filenamehighlightingsettings.h"

#include <KConfigGroup>

#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using Condition = FileNameHighlightRule::Condition;

class FileNameHighlightingSettingsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testMissingGroupGivesDefaults();
    void testDefaultRules();
    void testRoundTrip();
    void testEmptyRuleList();
    void testUnsetAttributesStayUnset();
    void testColorKeys();
    void testUnknownEntriesAreSkipped();
    void testSaveRemovesStaleRules();

private:
    KSharedConfigPtr freshConfig() const;
    KSharedConfigPtr reopenedConfig() const;

    QTemporaryDir *m_dir = nullptr;
};

void FileNameHighlightingSettingsTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void FileNameHighlightingSettingsTest::init()
{
    m_dir = new QTemporaryDir();
    QVERIFY(m_dir->isValid());
}

void FileNameHighlightingSettingsTest::cleanup()
{
    delete m_dir;
    m_dir = nullptr;
}

KSharedConfigPtr FileNameHighlightingSettingsTest::freshConfig() const
{
    return KSharedConfig::openConfig(m_dir->filePath(QStringLiteral("whalerc")), KConfig::SimpleConfig);
}

KSharedConfigPtr FileNameHighlightingSettingsTest::reopenedConfig() const
{
    // A separate KConfig instance reads what has been written to disk.
    KSharedConfigPtr config = freshConfig();
    config->reparseConfiguration();
    return config;
}

void FileNameHighlightingSettingsTest::testMissingGroupGivesDefaults()
{
    const FileNameHighlightingSettings settings = FileNameHighlightingSettings::load(freshConfig());
    QVERIFY(!settings.enabled);
    QCOMPARE(settings.rules, FileNameHighlightingSettings::defaultRules());
    QCOMPARE(settings, FileNameHighlightingSettings::defaults());
}

void FileNameHighlightingSettingsTest::testDefaultRules()
{
    const QList<FileNameHighlightRule> rules = FileNameHighlightingSettings::defaultRules();
    QCOMPARE(rules.count(), 3);

    QCOMPARE(rules.at(0).condition, Condition::Executable);
    QCOMPARE(rules.at(0).style.bold, std::optional<bool>(true));

    QCOMPARE(rules.at(1).condition, Condition::Symlink);
    QCOMPARE(rules.at(1).style.italic, std::optional<bool>(true));

    QCOMPARE(rules.at(2).condition, Condition::MimeType);
    QCOMPARE(rules.at(2).patterns, (QStringList{QStringLiteral("image/*"), QStringLiteral("audio/*"), QStringLiteral("video/*")}));

    for (const FileNameHighlightRule &rule : rules) {
        QVERIFY(rule.enabled);
        QVERIFY(!rule.name.isEmpty());
        // Default rules follow the color scheme instead of using fixed colors.
        QVERIFY(rule.style.color.source() != FileNameColor::Source::Custom);
    }
    QCOMPARE(rules.at(2).style.color.source(), FileNameColor::Source::SchemeRole);
}

void FileNameHighlightingSettingsTest::testRoundTrip()
{
    FileNameHighlightRule custom;
    custom.name = QStringLiteral("Sources");
    custom.enabled = false;
    custom.condition = Condition::MimeType;
    custom.patterns = {QStringLiteral("text/x-c++src"), QStringLiteral("text/x-chdr")};
    custom.style.color = FileNameColor::custom(QColor(10, 20, 30));
    custom.style.bold = false;
    custom.style.italic = true;

    FileNameHighlightingSettings settings;
    settings.enabled = true;
    settings.rules = FileNameHighlightingSettings::defaultRules();
    settings.rules.prepend(custom);
    settings.save(freshConfig());

    const FileNameHighlightingSettings loaded = FileNameHighlightingSettings::load(reopenedConfig());
    QCOMPARE(loaded.enabled, true);
    QCOMPARE(loaded.rules.count(), 4);
    QCOMPARE(loaded.rules.first(), custom);
    QCOMPARE(loaded, settings);
}

void FileNameHighlightingSettingsTest::testEmptyRuleList()
{
    FileNameHighlightingSettings settings;
    settings.enabled = true;
    settings.save(freshConfig());

    const FileNameHighlightingSettings loaded = FileNameHighlightingSettings::load(reopenedConfig());
    QVERIFY(loaded.enabled);
    QVERIFY(loaded.rules.isEmpty());
}

void FileNameHighlightingSettingsTest::testUnsetAttributesStayUnset()
{
    FileNameHighlightRule rule;
    rule.condition = Condition::Symlink;
    rule.style.bold = false;

    FileNameHighlightingSettings settings;
    settings.rules = {rule};
    settings.save(freshConfig());

    const KConfigGroup ruleGroup = KConfigGroup(reopenedConfig(), QStringLiteral("FileNameHighlighting")).group(QStringLiteral("Rule0"));
    QVERIFY(ruleGroup.hasKey("Bold"));
    QVERIFY(!ruleGroup.hasKey("Italic"));
    QVERIFY(!ruleGroup.hasKey("Color"));
    QVERIFY(!ruleGroup.hasKey("ColorRole"));
    QVERIFY(!ruleGroup.hasKey("Patterns"));

    const FileNameHighlightRule loaded = FileNameHighlightingSettings::load(reopenedConfig()).rules.first();
    QCOMPARE(loaded.style.bold, std::optional<bool>(false));
    QVERIFY(!loaded.style.italic.has_value());
    QVERIFY(!loaded.style.color.isSet());
}

void FileNameHighlightingSettingsTest::testColorKeys()
{
    FileNameHighlightRule role;
    role.condition = Condition::Executable;
    role.style.color = FileNameColor::schemeRole(KColorScheme::PositiveText);

    FileNameHighlightRule custom;
    custom.condition = Condition::Executable;
    custom.style.color = FileNameColor::custom(QColor(1, 2, 3));

    FileNameHighlightingSettings settings;
    settings.rules = {role, custom};
    settings.save(freshConfig());

    const KConfigGroup group(reopenedConfig(), QStringLiteral("FileNameHighlighting"));
    QCOMPARE(group.group(QStringLiteral("Rule0")).readEntry("ColorRole", QString()), QStringLiteral("PositiveText"));
    QVERIFY(!group.group(QStringLiteral("Rule0")).hasKey("Color"));
    QVERIFY(group.group(QStringLiteral("Rule1")).hasKey("Color"));
    QVERIFY(!group.group(QStringLiteral("Rule1")).hasKey("ColorRole"));

    // ColorRole wins if both keys are present; an unknown role means no color.
    KSharedConfigPtr config = reopenedConfig();
    KConfigGroup editable(config, QStringLiteral("FileNameHighlighting"));
    editable.group(QStringLiteral("Rule0")).writeEntry("Color", QColor(4, 5, 6));
    editable.group(QStringLiteral("Rule1")).writeEntry("ColorRole", QStringLiteral("NoSuchRole"));
    config->sync();

    const FileNameHighlightingSettings loaded = FileNameHighlightingSettings::load(reopenedConfig());
    QCOMPARE(loaded.rules.at(0).style.color, FileNameColor::schemeRole(KColorScheme::PositiveText));
    QVERIFY(!loaded.rules.at(1).style.color.isSet());
}

void FileNameHighlightingSettingsTest::testUnknownEntriesAreSkipped()
{
    KSharedConfigPtr config = freshConfig();
    KConfigGroup group(config, QStringLiteral("FileNameHighlighting"));
    group.writeEntry("Enabled", true);
    group.writeEntry("RuleCount", 4);
    group.group(QStringLiteral("Rule0")).writeEntry("Condition", QStringLiteral("Symlink"));
    group.group(QStringLiteral("Rule1")).writeEntry("Condition", QStringLiteral("FromTheFuture"));
    // Rule2 is missing.
    group.group(QStringLiteral("Rule3")).writeEntry("Condition", QStringLiteral("Executable"));
    config->sync();

    const FileNameHighlightingSettings loaded = FileNameHighlightingSettings::load(reopenedConfig());
    QCOMPARE(loaded.rules.count(), 2);
    QCOMPARE(loaded.rules.at(0).condition, Condition::Symlink);
    QCOMPARE(loaded.rules.at(1).condition, Condition::Executable);
}

void FileNameHighlightingSettingsTest::testSaveRemovesStaleRules()
{
    FileNameHighlightingSettings settings = FileNameHighlightingSettings::defaults();
    settings.save(freshConfig());

    settings.rules.removeLast();
    settings.save(reopenedConfig());

    const KConfigGroup group(reopenedConfig(), QStringLiteral("FileNameHighlighting"));
    QVERIFY(group.group(QStringLiteral("Rule1")).exists());
    QVERIFY(!group.group(QStringLiteral("Rule2")).exists());
    QCOMPARE(FileNameHighlightingSettings::load(reopenedConfig()).rules.count(), 2);
}

QTEST_GUILESS_MAIN(FileNameHighlightingSettingsTest)

#include "filenamehighlightingsettingstest.moc"
