/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "settings/viewmodes/filenamehighlightingtab.h"
#include "settings/viewmodes/filenamehighlightingruledialog.h"
#include "views/filenamehighlightingsettings.h"

#include <KColorButton>
#include <KConfigGroup>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

using Condition = FileNameHighlightRule::Condition;

namespace
{
FileNameHighlightRule rule(const QString &name, Condition condition)
{
    FileNameHighlightRule result;
    result.name = name;
    result.condition = condition;
    return result;
}

QStringList ruleNames(const QList<FileNameHighlightRule> &rules)
{
    QStringList names;
    for (const FileNameHighlightRule &rule : rules) {
        names.append(rule.name);
    }
    return names;
}
}

class FileNameHighlightingTabTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();

    void testLoadsDefaults();
    void testModelEditing();
    void testApplyKeepsOrder();
    void testRestoreDefaults();
    void testChangedSignal();
    void testDialogRoundTrip_data();
    void testDialogRoundTrip();
    void testDialogPrefillsCustomColor();
    void testDialogRequiresMimeTypes();
    void testParsePatterns();
    void testUnknownMimeTypePatterns();
    void testDisplayName();
};

void FileNameHighlightingTabTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void FileNameHighlightingTabTest::init()
{
    KConfigGroup(KSharedConfig::openConfig(), QStringLiteral("FileNameHighlighting")).deleteGroup();
    KSharedConfig::openConfig()->sync();
}

void FileNameHighlightingTabTest::testLoadsDefaults()
{
    const FileNameHighlightingTab tab;
    QCOMPARE(tab.ruleModel()->rules(), FileNameHighlightingSettings::defaultRules());
}

void FileNameHighlightingTabTest::testModelEditing()
{
    FileNameHighlightingRuleModel model;
    model.setRules({rule(QStringLiteral("a"), Condition::Executable), rule(QStringLiteral("b"), Condition::Symlink)});
    model.addRule(rule(QStringLiteral("c"), Condition::MimeType));
    QCOMPARE(ruleNames(model.rules()), (QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}));

    model.moveRule(2, 0);
    QCOMPARE(ruleNames(model.rules()), (QStringList{QStringLiteral("c"), QStringLiteral("a"), QStringLiteral("b")}));
    model.moveRule(0, 1);
    QCOMPARE(ruleNames(model.rules()), (QStringList{QStringLiteral("a"), QStringLiteral("c"), QStringLiteral("b")}));

    model.removeRule(1);
    QCOMPARE(ruleNames(model.rules()), (QStringList{QStringLiteral("a"), QStringLiteral("b")}));

    // The check box of the first column enables and disables a rule.
    const QModelIndex first = model.index(0, FileNameHighlightingRuleModel::RuleColumn);
    QVERIFY(model.flags(first) & Qt::ItemIsUserCheckable);
    QVERIFY(model.setData(first, Qt::Unchecked, Qt::CheckStateRole));
    QVERIFY(!model.rule(0).enabled);
    QCOMPARE(model.data(first, Qt::CheckStateRole).toInt(), int(Qt::Unchecked));
}

void FileNameHighlightingTabTest::testApplyKeepsOrder()
{
    FileNameHighlightingTab tab;
    FileNameHighlightingRuleModel *model = tab.ruleModel();
    model->moveRule(2, 0);
    model->addRule(rule(QStringLiteral("Custom"), Condition::Symlink));
    tab.applySettings();

    const FileNameHighlightingSettings loaded = FileNameHighlightingSettings::load();
    QCOMPARE(loaded.rules, model->rules());
    QCOMPARE(loaded.rules.last().name, QStringLiteral("Custom"));
    QCOMPARE(loaded.rules.first().condition, Condition::MimeType);
}

void FileNameHighlightingTabTest::testRestoreDefaults()
{
    FileNameHighlightingTab tab;
    tab.ruleModel()->setRules({rule(QStringLiteral("Only"), Condition::Symlink)});

    QSignalSpy changed(&tab, &SettingsPageBase::changed);
    tab.restoreDefaults();
    QCOMPARE(changed.count(), 1);
    QCOMPARE(tab.ruleModel()->rules(), FileNameHighlightingSettings::defaultRules());

    tab.applySettings();
    QCOMPARE(FileNameHighlightingSettings::load(), FileNameHighlightingSettings::defaults());
}

void FileNameHighlightingTabTest::testChangedSignal()
{
    FileNameHighlightingTab tab;
    QSignalSpy changed(&tab, &SettingsPageBase::changed);

    // A new palette only changes the previews, not the settings.
    tab.ruleModel()->setPreviewAppearance(QPalette(Qt::darkBlue), QFont());
    QCOMPARE(changed.count(), 0);

    tab.ruleModel()->setData(tab.ruleModel()->index(0, FileNameHighlightingRuleModel::RuleColumn), Qt::Unchecked, Qt::CheckStateRole);
    QCOMPARE(changed.count(), 1);

    tab.ruleModel()->setRule(0, rule(QStringLiteral("Edited"), Condition::Executable));
    QCOMPARE(changed.count(), 2);
}

void FileNameHighlightingTabTest::testDialogRoundTrip_data()
{
    QTest::addColumn<FileNameHighlightRule>("rule");

    FileNameHighlightRule unset = rule(QStringLiteral("Unset"), Condition::Executable);
    QTest::newRow("unset style") << unset;

    FileNameHighlightRule schemeRole = rule(QStringLiteral("Scheme"), Condition::Symlink);
    schemeRole.style.color = FileNameColor::schemeRole(KColorScheme::NeutralText);
    schemeRole.style.italic = false;
    QTest::newRow("scheme role") << schemeRole;

    FileNameHighlightRule custom = rule(QString(), Condition::MimeType);
    custom.enabled = false;
    custom.patterns = {QStringLiteral("image/*"), QStringLiteral("text/plain")};
    custom.style.color = FileNameColor::custom(QColor(10, 120, 200));
    custom.style.bold = true;
    QTest::newRow("custom color") << custom;
}

void FileNameHighlightingTabTest::testDialogRoundTrip()
{
    QFETCH(FileNameHighlightRule, rule);
    const FileNameHighlightingRuleDialog dialog(rule);
    QCOMPARE(dialog.rule(), rule);
}

void FileNameHighlightingTabTest::testDialogPrefillsCustomColor()
{
    FileNameHighlightRule schemeRule = rule(QStringLiteral("Scheme"), Condition::Executable);
    schemeRule.style.color = FileNameColor::schemeRole(KColorScheme::PositiveText);

    FileNameHighlightingRuleDialog dialog(schemeRule);
    const QColor schemeColor = FileNameHighlighter::resolveNameColor(schemeRule.style.color, dialog.palette());

    dialog.m_colorCustom->setChecked(true);
    QCOMPARE(dialog.m_customColor->color(), schemeColor);
    QCOMPARE(dialog.rule().style.color, FileNameColor::custom(schemeColor));

    dialog.m_colorUnchanged->setChecked(true);
    QVERIFY(!dialog.rule().style.color.isSet());
}

void FileNameHighlightingTabTest::testDialogRequiresMimeTypes()
{
    FileNameHighlightRule mimeRule = rule(QStringLiteral("Media"), Condition::MimeType);
    mimeRule.patterns = {QStringLiteral("image/*")};

    FileNameHighlightingRuleDialog dialog(mimeRule);
    QPushButton *ok = dialog.m_buttonBox->button(QDialogButtonBox::Ok);
    QVERIFY(ok->isEnabled());

    dialog.m_mimeTypes->setText(QStringLiteral(" ; "));
    QVERIFY(!ok->isEnabled());

    // Other conditions have no patterns.
    dialog.m_condition->setCurrentIndex(dialog.m_condition->findData(static_cast<int>(Condition::Symlink)));
    QVERIFY(ok->isEnabled());
    QVERIFY(dialog.rule().patterns.isEmpty());
}

void FileNameHighlightingTabTest::testParsePatterns()
{
    QCOMPARE(FileNameHighlightingRuleDialog::parsePatterns(QStringLiteral(" image/* ; audio/*,video/*;; image/* ")),
             (QStringList{QStringLiteral("image/*"), QStringLiteral("audio/*"), QStringLiteral("video/*")}));
    QVERIFY(FileNameHighlightingRuleDialog::parsePatterns(QString()).isEmpty());
}

void FileNameHighlightingTabTest::testUnknownMimeTypePatterns()
{
    const QStringList patterns = {
        QStringLiteral("image/*"),
        QStringLiteral("text/plain"),
        QStringLiteral("nosuchgroup/*"),
        QStringLiteral("image/no-such-type"),
    };
    QCOMPARE(FileNameHighlightingRuleDialog::unknownMimeTypePatterns(patterns),
             (QStringList{QStringLiteral("nosuchgroup/*"), QStringLiteral("image/no-such-type")}));
}

void FileNameHighlightingTabTest::testDisplayName()
{
    FileNameHighlightRule named = rule(QStringLiteral("Media files"), Condition::MimeType);
    QCOMPARE(FileNameHighlightingRuleDialog::displayName(named), QStringLiteral("Media files"));

    FileNameHighlightRule unnamed = rule(QString(), Condition::MimeType);
    unnamed.patterns = {QStringLiteral("image/*"), QStringLiteral("audio/*")};
    QVERIFY(FileNameHighlightingRuleDialog::displayName(unnamed).contains(QLatin1String("image/*, audio/*")));
    QCOMPARE(FileNameHighlightingRuleDialog::exampleFileName(unnamed), QStringLiteral("photo.jpg"));
}

QTEST_MAIN(FileNameHighlightingTabTest)

#include "filenamehighlightingtabtest.moc"
