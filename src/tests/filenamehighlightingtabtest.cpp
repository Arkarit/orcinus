/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "settings/viewmodes/filenamehighlightingtab.h"
#include "settings/viewmodes/expansiontintwidget.h"
#include "settings/viewmodes/filenamehighlightingruledialog.h"
#include "views/filenamehighlightingsettings.h"

#include <KColorButton>
#include <KConfigGroup>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QToolButton>

using Condition = FileNameHighlightRule::Condition;
using Preset = ExpansionTintSettings::Preset;

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
    void testDialogNamePatterns();
    void testDialogAppliesToExecutable();
    void testParsePatterns();
    void testUnknownMimeTypePatterns();
    void testDisplayName();

    void testExpansionTintLoadsSettings();
    void testExpansionTintPresets();
    void testExpansionTintEditSwitchesToCustom();
    void testExpansionTintAddAndRemove();
    void testExpansionTintApplyAndDefaults();
    void testScreenshot();
};

void FileNameHighlightingTabTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void FileNameHighlightingTabTest::init()
{
    KConfigGroup(KSharedConfig::openConfig(), QStringLiteral("FileNameHighlighting")).deleteGroup();
    KConfigGroup(KSharedConfig::openConfig(), QStringLiteral("ExpansionTint")).deleteGroup();
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

    FileNameHighlightRule namePattern = rule(QStringLiteral("Sources"), Condition::NamePattern);
    namePattern.patterns = {QStringLiteral("*.cpp"), QStringLiteral("*.h")};
    namePattern.caseSensitive = true;
    namePattern.style.italic = true;
    QTest::newRow("name pattern") << namePattern;

    FileNameHighlightRule folders = rule(QStringLiteral("Build folders"), Condition::NamePattern);
    folders.patterns = {QStringLiteral("build*")};
    folders.appliesTo = FileNameHighlightRule::ItemKind::Folders;
    folders.style.bold = true;
    QTest::newRow("folders only") << folders;

    FileNameHighlightRule fileLinks = rule(QStringLiteral("File links"), Condition::Symlink);
    fileLinks.appliesTo = FileNameHighlightRule::ItemKind::Files;
    fileLinks.style.italic = true;
    QTest::newRow("files only") << fileLinks;
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
    QVERIFY(!dialog.m_patternsWarning->isVisibleTo(&dialog));

    // Without patterns, OK is disabled and the warning line says why.
    dialog.m_patterns->setText(QStringLiteral(" ; "));
    QVERIFY(!ok->isEnabled());
    QVERIFY(dialog.m_patternsWarning->isVisibleTo(&dialog));
    QVERIFY(dialog.m_patternsWarning->text().contains(QStringLiteral("MIME type")));

    // For name patterns, the warning also tells how to match every name.
    dialog.m_condition->setCurrentIndex(dialog.m_condition->findData(static_cast<int>(Condition::NamePattern)));
    QVERIFY(!ok->isEnabled());
    QVERIFY(dialog.m_patternsWarning->text().contains(QLatin1Char('*')));
    dialog.m_patterns->setText(QStringLiteral("*"));
    QVERIFY(ok->isEnabled());
    QVERIFY(!dialog.m_patternsWarning->isVisibleTo(&dialog));

    // Other conditions have no patterns.
    dialog.m_patterns->clear();
    dialog.m_condition->setCurrentIndex(dialog.m_condition->findData(static_cast<int>(Condition::Symlink)));
    QVERIFY(ok->isEnabled());
    QVERIFY(dialog.rule().patterns.isEmpty());
}

void FileNameHighlightingTabTest::testDialogNamePatterns()
{
    FileNameHighlightRule namePattern = rule(QStringLiteral("Sources"), Condition::NamePattern);
    namePattern.patterns = {QStringLiteral("*.cpp")};
    namePattern.caseSensitive = true;

    FileNameHighlightingRuleDialog dialog(namePattern);
    QVERIFY(!dialog.m_patternsWarning->isVisibleTo(&dialog));
    QVERIFY(dialog.m_caseSensitive->isVisibleTo(&dialog));
    QVERIFY(!dialog.m_chooseMimeTypes->isVisibleTo(&dialog));

    // Patterns are matched against names, which never contain a slash.
    dialog.m_patterns->setText(QStringLiteral("src/*.cpp"));
    QVERIFY(dialog.m_patternsWarning->isVisibleTo(&dialog));

    // Case sensitivity only belongs to name patterns.
    dialog.m_condition->setCurrentIndex(dialog.m_condition->findData(static_cast<int>(Condition::MimeType)));
    QVERIFY(!dialog.m_caseSensitive->isVisibleTo(&dialog));
    QVERIFY(dialog.m_chooseMimeTypes->isVisibleTo(&dialog));
    QVERIFY(!dialog.rule().caseSensitive);
}

void FileNameHighlightingTabTest::testDialogAppliesToExecutable()
{
    FileNameHighlightRule folders = rule(QStringLiteral("Build folders"), Condition::NamePattern);
    folders.patterns = {QStringLiteral("build*")};
    folders.appliesTo = FileNameHighlightRule::ItemKind::Folders;

    FileNameHighlightingRuleDialog dialog(folders);
    QVERIFY(dialog.m_appliesTo->isEnabled());

    // Executable files are always files: the choice is fixed and not stored.
    dialog.m_condition->setCurrentIndex(dialog.m_condition->findData(static_cast<int>(Condition::Executable)));
    QVERIFY(!dialog.m_appliesTo->isEnabled());
    QCOMPARE(dialog.m_appliesTo->currentData().toInt(), static_cast<int>(FileNameHighlightRule::ItemKind::Files));
    QCOMPARE(dialog.rule().appliesTo, FileNameHighlightRule::ItemKind::FilesAndFolders);

    // Another condition brings the user's choice back.
    dialog.m_condition->setCurrentIndex(dialog.m_condition->findData(static_cast<int>(Condition::Symlink)));
    QVERIFY(dialog.m_appliesTo->isEnabled());
    QCOMPARE(dialog.rule().appliesTo, FileNameHighlightRule::ItemKind::Folders);

    // An executable rule opens with the fixed choice.
    const FileNameHighlightingRuleDialog executableDialog(rule(QStringLiteral("Programs"), Condition::Executable));
    QVERIFY(!executableDialog.m_appliesTo->isEnabled());
    QCOMPARE(executableDialog.m_appliesTo->currentData().toInt(), static_cast<int>(FileNameHighlightRule::ItemKind::Files));
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

    FileNameHighlightRule namePattern = rule(QString(), Condition::NamePattern);
    namePattern.patterns = {QStringLiteral("*.cpp"), QStringLiteral("Makefile")};
    QVERIFY(FileNameHighlightingRuleDialog::displayName(namePattern).contains(QLatin1String("*.cpp, Makefile")));
    QCOMPARE(FileNameHighlightingRuleDialog::exampleFileName(namePattern), QStringLiteral("example.cpp"));
    namePattern.patterns = {QStringLiteral("file?.txt")};
    QCOMPARE(FileNameHighlightingRuleDialog::exampleFileName(namePattern), QStringLiteral("filex.txt"));
    namePattern.patterns = {QStringLiteral("[ab].txt")};
    QCOMPARE(FileNameHighlightingRuleDialog::exampleFileName(namePattern), QStringLiteral("example"));
}

namespace
{
void selectPreset(ExpansionTintWidget *widget, Preset preset)
{
    QComboBox *comboBox = widget->presetComboBox();
    comboBox->setCurrentIndex(comboBox->findData(int(preset)));
}

QList<QColor> buttonColors(const ExpansionTintWidget *widget)
{
    QList<QColor> colors;
    const QList<KColorButton *> buttons = widget->colorButtons();
    for (const KColorButton *button : buttons) {
        colors.append(button->color());
    }
    return colors;
}
}

void FileNameHighlightingTabTest::testExpansionTintLoadsSettings()
{
    {
        FileNameHighlightingTab tab;
        ExpansionTintWidget *widget = tab.expansionTintWidget();
        QCOMPARE(widget->settings(), ExpansionTintSettings::defaults());
        QCOMPARE(widget->presetComboBox()->currentData().toInt(), int(Preset::PastelVivid));
        QCOMPARE(buttonColors(widget), ExpansionTintSettings::presetColors(Preset::PastelVivid, widget->palette()));
    }

    ExpansionTintSettings settings;
    settings.preset = Preset::Custom;
    settings.customColors = {QColor(Qt::red), QColor(Qt::green), QColor(Qt::blue)};
    settings.save();

    FileNameHighlightingTab tab;
    ExpansionTintWidget *widget = tab.expansionTintWidget();
    QCOMPARE(widget->settings(), settings);
    QCOMPARE(widget->presetComboBox()->currentData().toInt(), int(Preset::Custom));
    QCOMPARE(buttonColors(widget), settings.customColors);
}

void FileNameHighlightingTabTest::testExpansionTintPresets()
{
    FileNameHighlightingTab tab;
    ExpansionTintWidget *widget = tab.expansionTintWidget();
    const QList<QColor> customColors = widget->settings().customColors;
    QSignalSpy changed(&tab, &SettingsPageBase::changed);

    selectPreset(widget, Preset::Grey);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(widget->settings().preset, Preset::Grey);
    QCOMPARE(buttonColors(widget), ExpansionTintSettings::presetColors(Preset::Grey, widget->palette()));

    selectPreset(widget, Preset::Off);
    QCOMPARE(changed.count(), 2);
    QVERIFY(widget->colorButtons().isEmpty());

    // The custom colors stay while a preset is selected.
    selectPreset(widget, Preset::Custom);
    QCOMPARE(widget->settings().customColors, customColors);
    QCOMPARE(buttonColors(widget), customColors);
}

void FileNameHighlightingTabTest::testExpansionTintEditSwitchesToCustom()
{
    FileNameHighlightingTab tab;
    ExpansionTintWidget *widget = tab.expansionTintWidget();
    selectPreset(widget, Preset::PrinterPaper);
    QList<QColor> expected = ExpansionTintSettings::presetColors(Preset::PrinterPaper, widget->palette());
    QSignalSpy changed(&tab, &SettingsPageBase::changed);

    // As if the user picked a color in the dialog of the button.
    Q_EMIT widget->colorButtons().at(1)->changed(QColor(Qt::magenta));
    expected[1] = QColor(Qt::magenta);

    QCOMPARE(changed.count(), 1);
    QCOMPARE(widget->settings().preset, Preset::Custom);
    QCOMPARE(widget->presetComboBox()->currentData().toInt(), int(Preset::Custom));
    QCOMPARE(widget->settings().customColors, expected);
    QCOMPARE(buttonColors(widget), expected);
}

void FileNameHighlightingTabTest::testExpansionTintAddAndRemove()
{
    FileNameHighlightingTab tab;
    ExpansionTintWidget *widget = tab.expansionTintWidget();
    selectPreset(widget, Preset::Grey);
    QVERIFY(widget->addColorButton()->isEnabled());
    QVERIFY(!widget->removeColorButton()->isEnabled());

    // Adding a color to a preset continues with its colors as custom colors.
    const QList<QColor> grey = ExpansionTintSettings::presetColors(Preset::Grey, widget->palette());
    widget->addColorButton()->click();
    QCOMPARE(widget->settings().preset, Preset::Custom);
    QCOMPARE(widget->settings().customColors.count(), 3);
    QCOMPARE(widget->settings().customColors.mid(0, 2), grey);
    QVERIFY(widget->removeColorButton()->isEnabled());

    while (widget->addColorButton()->isEnabled()) {
        widget->addColorButton()->click();
    }
    QCOMPARE(widget->colorButtons().count(), ExpansionTintSettings::MaximumColorCount);

    while (widget->removeColorButton()->isEnabled()) {
        widget->removeColorButton()->click();
    }
    QCOMPARE(widget->colorButtons().count(), ExpansionTintSettings::MinimumColorCount);
    QCOMPARE(widget->settings().customColors, grey);
}

void FileNameHighlightingTabTest::testExpansionTintApplyAndDefaults()
{
    FileNameHighlightingTab tab;
    ExpansionTintWidget *widget = tab.expansionTintWidget();
    selectPreset(widget, Preset::Grey);
    tab.applySettings();
    QCOMPARE(ExpansionTintSettings::load().preset, Preset::Grey);

    tab.restoreDefaults();
    QCOMPARE(widget->settings(), ExpansionTintSettings::defaults());
    tab.applySettings();
    QCOMPARE(ExpansionTintSettings::load(), ExpansionTintSettings::defaults());
}

void FileNameHighlightingTabTest::testScreenshot()
{
    // WHALE_TEST_SCREENSHOT_DIR=<dir> saves screenshots of the tab there.
    const QString dir = qEnvironmentVariable("WHALE_TEST_SCREENSHOT_DIR");
    if (dir.isEmpty()) {
        QSKIP("WHALE_TEST_SCREENSHOT_DIR is not set");
    }

    FileNameHighlightingTab tab;
    tab.resize(760, 640);
    tab.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tab));
    QVERIFY(tab.grab().save(dir + QStringLiteral("/highlighting-tab-pastel.png")));

    selectPreset(tab.expansionTintWidget(), Preset::Off);
    QVERIFY(tab.grab().save(dir + QStringLiteral("/highlighting-tab-off.png")));
}

QTEST_MAIN(FileNameHighlightingTabTest)

#include "filenamehighlightingtabtest.moc"
