/*
 * SPDX-FileCopyrightText: 2012 Peter Penz <peter.penz19@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "confirmationssettingspage.h"

#include "admin/workerintegration.h"
#include "dolphin_generalsettings.h"
#include "global.h"
#include "whalescriptlauncher.h"

#include <KLocalizedString>
#include <KMessageBox>
#include <KProtocolInfo>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>

namespace
{
enum ScriptExecution {
    AlwaysAsk = 0,
    Open = 1,
    Execute = 2,
    // Whale: in orcinusrc, see WhaleScriptLauncher
    ExecuteInTerminal = 3,
    ExecuteInTerminalAndClose = 4,
    ExecuteInTerminalMinimized = 5,
    ExecuteInTerminalMinimizedAndClose = 6,
};

const bool ConfirmEmptyTrash = true;
const bool ConfirmTrash = false;
const bool ConfirmDelete = true;
const int ConfirmScriptExecution = ScriptExecution::AlwaysAsk;
const bool ConfirmRenameFileType = true;
const bool ConfirmHide = true;
}

ConfirmationsSettingsPage::ConfirmationsSettingsPage(QWidget *parent)
    : SettingsPageBase(parent)
    , m_confirmMoveToTrash(nullptr)
    , m_confirmEmptyTrash(nullptr)
    , m_confirmDelete(nullptr)
    ,

#if HAVE_TERMINAL
    m_confirmClosingTerminalRunningProgram(nullptr)
    ,
#endif

    m_confirmClosingMultipleTabs(nullptr)
{
    QFormLayout *topLayout = new QFormLayout(this);
    topLayout->setFormAlignment(Qt::AlignHCenter);

    QLabel *confirmLabelKde = new QLabel(i18nc("@title:group", "Ask for confirmation in KDE applications when:"), this);
    confirmLabelKde->setWordWrap(true);

    m_confirmMoveToTrash = new QCheckBox(i18nc("@option:check Ask for confirmation when", "Moving files or folders to trash"), this);
    m_confirmEmptyTrash = new QCheckBox(i18nc("@option:check Ask for confirmation when", "Emptying trash"), this);
    m_confirmDelete = new QCheckBox(i18nc("@option:check Ask for confirmation when", "Deleting files or folders"), this);
    m_confirmRenameFileType = new QCheckBox(i18nc("@option:check Ask for confirmation when", "Renaming a file will change its type"), this);
    m_confirmHide = new QCheckBox(i18nc("@option:check Ask for confirmation when", "Renaming a file or folder will hide it"), this);

    QLabel *confirmLabelDolphin = new QLabel(i18nc("@title:group", "Ask for confirmation in Whale when:"), this);
    confirmLabelDolphin->setWordWrap(true);

    m_confirmClosingMultipleTabs = new QCheckBox(i18nc("@option:check Ask for confirmation in Dolphin when", "Closing windows with multiple tabs"), this);

#if HAVE_TERMINAL
    m_confirmClosingTerminalRunningProgram =
        new QCheckBox(i18nc("@option:check Ask for confirmation when", "Closing windows with a program running in the Terminal panel"), this);
#endif

    m_confirmOpenManyFolders = new QCheckBox(i18nc("@option:check Ask for confirmation in Dolphin when", "Opening many folders at once"), this);
    m_confirmOpenManyTerminals = new QCheckBox(i18nc("@option:check Ask for confirmation in Dolphin when", "Opening many terminals at once"), this);
    m_confirmRisksOfActingAsAdmin = new QCheckBox(i18nc("@option:check Ask for confirmation in Dolphin when", "Switching to act as an administrator"), this);

    QLabel *executableScriptLabel = new QLabel(i18nc("@title:group", "When opening an executable file:"), this);
    executableScriptLabel->setWordWrap(true);

    m_confirmScriptExecution = new QComboBox(this);
    m_confirmScriptExecution->addItems({i18n("Always ask"),
                                        i18n("Open in application"),
                                        i18n("Run script"),
                                        i18nc("@item:inlistbox When opening an executable file:", "Run script in terminal"),
                                        i18nc("@item:inlistbox When opening an executable file:", "Run script in terminal and close"),
                                        i18nc("@item:inlistbox When opening an executable file:", "Run script in minimized terminal"),
                                        i18nc("@item:inlistbox When opening an executable file:", "Run script in minimized terminal and close")});
    m_confirmScriptExecution->setItemData(ScriptExecution::ExecuteInTerminal,
                                          i18nc("@info:tooltip", "The terminal stays open until you press Enter."),
                                          Qt::ToolTipRole);
    m_confirmScriptExecution->setItemData(ScriptExecution::ExecuteInTerminalAndClose,
                                          i18nc("@info:tooltip", "The terminal closes when the script ends, unless it failed."),
                                          Qt::ToolTipRole);
    m_confirmScriptExecution->setItemData(ScriptExecution::ExecuteInTerminalMinimized,
                                          i18nc("@info:tooltip",
                                                "The terminal starts in the taskbar and stays open until you press Enter. "
                                                "If the script fails, it comes to the front."),
                                          Qt::ToolTipRole);
    m_confirmScriptExecution->setItemData(ScriptExecution::ExecuteInTerminalMinimizedAndClose,
                                          i18nc("@info:tooltip",
                                                "The terminal starts in the taskbar and closes when the script ends. "
                                                "If the script fails, it comes to the front instead."),
                                          Qt::ToolTipRole);

    topLayout->addRow(confirmLabelKde, m_confirmDelete);
    topLayout->addRow(nullptr, m_confirmMoveToTrash);
    topLayout->addRow(nullptr, m_confirmEmptyTrash);
    topLayout->addRow(nullptr, m_confirmRenameFileType);
    topLayout->addRow(nullptr, m_confirmHide);

    topLayout->addItem(new QSpacerItem(0, Dolphin::VERTICAL_SPACER_HEIGHT, QSizePolicy::Fixed, QSizePolicy::Fixed));

    topLayout->addRow(confirmLabelDolphin, m_confirmClosingMultipleTabs);

#if HAVE_TERMINAL
    topLayout->addRow(nullptr, m_confirmClosingTerminalRunningProgram);
#endif

    topLayout->addRow(nullptr, m_confirmOpenManyFolders);
    topLayout->addRow(nullptr, m_confirmOpenManyTerminals);

    if (KProtocolInfo::isKnownProtocol(QStringLiteral("admin"))) {
        topLayout->addRow(nullptr, m_confirmRisksOfActingAsAdmin);
    } else {
        m_confirmRisksOfActingAsAdmin->hide();
    }

    topLayout->addItem(new QSpacerItem(0, Dolphin::VERTICAL_SPACER_HEIGHT, QSizePolicy::Fixed, QSizePolicy::Fixed));
    topLayout->addRow(executableScriptLabel, m_confirmScriptExecution);

    loadSettings();

    int minimumWidth = 0;
    for (QLabel *label : {confirmLabelKde, confirmLabelDolphin, executableScriptLabel}) {
        QFontMetrics fm(label->font());
        minimumWidth = qMax(minimumWidth, fm.horizontalAdvance(label->text()));
    }

    for (QLabel *label : {confirmLabelKde, confirmLabelDolphin, executableScriptLabel}) {
        label->setMinimumWidth(minimumWidth);
        label->setAlignment(topLayout->labelAlignment() | Qt::AlignVCenter);
    }

    connect(m_confirmMoveToTrash, &QCheckBox::toggled, this, &ConfirmationsSettingsPage::changed);
    connect(m_confirmEmptyTrash, &QCheckBox::toggled, this, &ConfirmationsSettingsPage::changed);
    connect(m_confirmDelete, &QCheckBox::toggled, this, &ConfirmationsSettingsPage::changed);
    connect(m_confirmRenameFileType, &QCheckBox::toggled, this, &ConfirmationsSettingsPage::changed);
    connect(m_confirmHide, &QCheckBox::toggled, this, &ConfirmationsSettingsPage::changed);
    connect(m_confirmScriptExecution, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ConfirmationsSettingsPage::changed);
    connect(m_confirmClosingMultipleTabs, &QCheckBox::toggled, this, &ConfirmationsSettingsPage::changed);
    connect(m_confirmOpenManyFolders, &QCheckBox::toggled, this, &ConfirmationsSettingsPage::changed);
    connect(m_confirmOpenManyTerminals, &QCheckBox::toggled, this, &ConfirmationsSettingsPage::changed);
    connect(m_confirmRisksOfActingAsAdmin, &QCheckBox::toggled, this, &ConfirmationsSettingsPage::changed);

#if HAVE_TERMINAL
    connect(m_confirmClosingTerminalRunningProgram, &QCheckBox::toggled, this, &ConfirmationsSettingsPage::changed);
#endif
}

ConfirmationsSettingsPage::~ConfirmationsSettingsPage() = default;

void ConfirmationsSettingsPage::applySettings()
{
    KSharedConfig::Ptr kioConfig = KSharedConfig::openConfig(QStringLiteral("kiorc"), KConfig::NoGlobals);
    KConfigGroup confirmationGroup(kioConfig, QStringLiteral("Confirmations"));
    confirmationGroup.writeEntry("ConfirmTrash", m_confirmMoveToTrash->isChecked());
    confirmationGroup.writeEntry("ConfirmEmptyTrash", m_confirmEmptyTrash->isChecked());
    confirmationGroup.writeEntry("ConfirmDelete", m_confirmDelete->isChecked());
    confirmationGroup.writeEntry("ConfirmRenameFileType", m_confirmRenameFileType->isChecked());
    confirmationGroup.writeEntry("ConfirmHide", m_confirmHide->isChecked());

    KConfigGroup scriptExecutionGroup(kioConfig, QStringLiteral("Executable scripts"));
    const int index = m_confirmScriptExecution->currentIndex();
    switch (index) {
    case ScriptExecution::AlwaysAsk:
        scriptExecutionGroup.writeEntry("behaviourOnLaunch", "alwaysAsk");
        break;
    case ScriptExecution::Open:
        scriptExecutionGroup.writeEntry("behaviourOnLaunch", "open");
        break;
    case ScriptExecution::Execute:
        scriptExecutionGroup.writeEntry("behaviourOnLaunch", "execute");
        break;
    }
    kioConfig->sync();
    // Whale: the terminal is Whale's own; KIO's choice stays for other applications.
    switch (index) {
    case ScriptExecution::ExecuteInTerminal:
        WhaleScriptLauncher::setMode(WhaleScriptLauncher::Mode::Terminal);
        break;
    case ScriptExecution::ExecuteInTerminalAndClose:
        WhaleScriptLauncher::setMode(WhaleScriptLauncher::Mode::TerminalClose);
        break;
    case ScriptExecution::ExecuteInTerminalMinimized:
        WhaleScriptLauncher::setMode(WhaleScriptLauncher::Mode::Minimized);
        break;
    case ScriptExecution::ExecuteInTerminalMinimizedAndClose:
        WhaleScriptLauncher::setMode(WhaleScriptLauncher::Mode::MinimizedClose);
        break;
    default:
        WhaleScriptLauncher::setMode(WhaleScriptLauncher::Mode::Off);
        break;
    }

    GeneralSettings *settings = GeneralSettings::self();
    settings->setConfirmClosingMultipleTabs(m_confirmClosingMultipleTabs->isChecked());
    settings->setConfirmOpenManyFolders(m_confirmOpenManyFolders->isChecked());
    settings->setConfirmOpenManyTerminals(m_confirmOpenManyTerminals->isChecked());
    if (m_confirmRisksOfActingAsAdmin->isChecked()) {
        KMessageBox::enableMessage(Admin::warningDontShowAgainName);
    } else {
        KMessageBox::saveDontShowAgainContinue(Admin::warningDontShowAgainName);
    }

#if HAVE_TERMINAL
    settings->setConfirmClosingTerminalRunningProgram(m_confirmClosingTerminalRunningProgram->isChecked());
#endif

    settings->save();
}

void ConfirmationsSettingsPage::restoreDefaults()
{
    GeneralSettings *settings = GeneralSettings::self();
    settings->useDefaults(true);
    loadSettings();
    settings->useDefaults(false);

    m_confirmMoveToTrash->setChecked(ConfirmTrash);
    m_confirmEmptyTrash->setChecked(ConfirmEmptyTrash);
    m_confirmDelete->setChecked(ConfirmDelete);
    m_confirmRenameFileType->setChecked(ConfirmRenameFileType);
    m_confirmHide->setChecked(ConfirmHide);
    m_confirmScriptExecution->setCurrentIndex(ConfirmScriptExecution);
    KMessageBox::enableMessage(Admin::warningDontShowAgainName);
}

void ConfirmationsSettingsPage::loadSettings()
{
    KSharedConfig::Ptr kioConfig = KSharedConfig::openConfig(QStringLiteral("kiorc"), KConfig::IncludeGlobals);
    KConfigGroup confirmationGroup(kioConfig, QStringLiteral("Confirmations"));
    m_confirmMoveToTrash->setChecked(confirmationGroup.readEntry("ConfirmTrash", ConfirmTrash));
    m_confirmEmptyTrash->setChecked(confirmationGroup.readEntry("ConfirmEmptyTrash", ConfirmEmptyTrash));
    m_confirmDelete->setChecked(confirmationGroup.readEntry("ConfirmDelete", ConfirmDelete));

    // Migration: ConfirmRenameFileType moved from dolphinrc (GeneralSettings) to kiorc.
    // TODO remove long after KF 6.30
    if (!confirmationGroup.hasKey("ConfirmRenameFileType")) {
        confirmationGroup.writeEntry("ConfirmRenameFileType", GeneralSettings::confirmRenameFileType());
        kioConfig->sync();
    }
    m_confirmRenameFileType->setChecked(confirmationGroup.readEntry("ConfirmRenameFileType", ConfirmRenameFileType));
    m_confirmHide->setChecked(confirmationGroup.readEntry("ConfirmHide", ConfirmHide));

    const KConfigGroup scriptExecutionGroup(KSharedConfig::openConfig(QStringLiteral("kiorc")), QStringLiteral("Executable scripts"));
    const QString value = scriptExecutionGroup.readEntry("behaviourOnLaunch", "alwaysAsk");
    if (value == QLatin1String("alwaysAsk")) {
        m_confirmScriptExecution->setCurrentIndex(ScriptExecution::AlwaysAsk);
    } else if (value == QLatin1String("execute")) {
        m_confirmScriptExecution->setCurrentIndex(ScriptExecution::Execute);
    } else /* if (value == QLatin1String("open"))*/ {
        m_confirmScriptExecution->setCurrentIndex(ScriptExecution::Open);
    }
    // Whale: a terminal wins over KIO's choice.
    switch (WhaleScriptLauncher::mode()) {
    case WhaleScriptLauncher::Mode::Terminal:
        m_confirmScriptExecution->setCurrentIndex(ScriptExecution::ExecuteInTerminal);
        break;
    case WhaleScriptLauncher::Mode::TerminalClose:
        m_confirmScriptExecution->setCurrentIndex(ScriptExecution::ExecuteInTerminalAndClose);
        break;
    case WhaleScriptLauncher::Mode::Minimized:
        m_confirmScriptExecution->setCurrentIndex(ScriptExecution::ExecuteInTerminalMinimized);
        break;
    case WhaleScriptLauncher::Mode::MinimizedClose:
        m_confirmScriptExecution->setCurrentIndex(ScriptExecution::ExecuteInTerminalMinimizedAndClose);
        break;
    case WhaleScriptLauncher::Mode::Off:
        break;
    }

    m_confirmClosingMultipleTabs->setChecked(GeneralSettings::confirmClosingMultipleTabs());

    // KMessageBox for its dontshowAgain settings are true => Yes, false => No, No value => ask
    // we use default = false to not write false into the rc file, but have no value
    // the UI has inversed meaning compared to the interpretation
    m_confirmOpenManyFolders->setChecked(GeneralSettings::confirmOpenManyFolders());
    m_confirmOpenManyTerminals->setChecked(GeneralSettings::confirmOpenManyTerminals());
    m_confirmRisksOfActingAsAdmin->setChecked(KMessageBox::shouldBeShownContinue(Admin::warningDontShowAgainName));

#if HAVE_TERMINAL
    m_confirmClosingTerminalRunningProgram->setChecked(GeneralSettings::confirmClosingTerminalRunningProgram());
#endif
}

#include "moc_confirmationssettingspage.cpp"
