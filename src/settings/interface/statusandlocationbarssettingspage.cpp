/*
 * SPDX-FileCopyrightText: 2023 Dimosthenis Krallis <dimosthenis.krallis@outlook.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "statusandlocationbarssettingspage.h"
#include "dolphinmainwindow.h"
#include "dolphinviewcontainer.h"
#include "settings/interface/folderstabssettingspage.h"
#ifndef IS_KCM
#include "whaletoolbarsettings.h" // Whale: TD4 (not in the KCM, which has no toolbars of Orcinus)
#endif

#include <KLocalizedString>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>

#include <QButtonGroup>
#include <QRadioButton>
#include <QSpacerItem>
#include <QStyle>
#include <QStyleOption>

StatusAndLocationBarsSettingsPage::StatusAndLocationBarsSettingsPage(QWidget *parent, FoldersTabsSettingsPage *foldersPage)
    : SettingsPageBase(parent)
    , m_editableUrl(nullptr)
    , m_showFullPath(nullptr)
    , m_statusBarButtonGroup(nullptr)
    , m_showStatusBarSmall(nullptr)
    , m_showStatusBarFullWidth(nullptr)
    , m_showZoomSlider(nullptr)
    , m_disableStatusBar(nullptr)
{
    // We need to update some urls at the Folders & Tabs tab. We get that from foldersPage and set it on a private attribute
    // foldersTabsPage. That way, we can modify the necessary stuff from here. Specifically, any changes on locationUpdateInitialViewOptions()
    // which is a copy of updateInitialViewOptions() on Folders & Tabs.
    foldersTabsPage = foldersPage;

    QFormLayout *topLayout = new QFormLayout(this);
    topLayout->setFormAlignment(Qt::AlignHCenter);

#ifndef IS_KCM
    // Whale: the toolbar density (TD4). The order of the entries follows
    // WhaleToolbarSettings::Density.
    m_barDensity = new QComboBox(this);
    m_barDensity->setObjectName(QStringLiteral("barDensity"));
    m_barDensity->addItem(i18nc("@item:inlistbox Density of the bars", "Normal"));
    m_barDensity->addItem(i18nc("@item:inlistbox Density of the bars", "Compact"));
    m_barDensity->addItem(i18nc("@item:inlistbox Density of the bars", "Very compact"));
    m_barDensity->setToolTip(i18nc("@info:tooltip",
                                   "How much room the toolbars, the location bars, the filter bar and the status bar take. "
                                   "Very compact draws text fields without a frame."));
    topLayout->addRow(i18nc("@label:listbox", "Bar density:"), m_barDensity);
    topLayout->addItem(new QSpacerItem(0, Dolphin::VERTICAL_SPACER_HEIGHT, QSizePolicy::Fixed, QSizePolicy::Fixed));
#endif

    // Status bar
    m_statusBarButtonGroup = new QButtonGroup(this);
    m_showStatusBarSmall = new QRadioButton(i18nc("@option:radio", "Small"), this);
    m_showStatusBarFullWidth = new QRadioButton(i18nc("@option:radio", "Full width"), this);
    m_showZoomSlider = new QCheckBox(i18nc("@option:check", "Show zoom slider"), this);
    m_disableStatusBar = new QRadioButton(i18nc("@option:check", "Disabled"), this);

    m_statusBarButtonGroup->addButton(m_showStatusBarSmall, GeneralSettings::EnumShowStatusBar::Small);
    m_statusBarButtonGroup->addButton(m_showStatusBarFullWidth, GeneralSettings::EnumShowStatusBar::FullWidth);
    m_statusBarButtonGroup->addButton(m_disableStatusBar, GeneralSettings::EnumShowStatusBar::Disabled);

    topLayout->addRow(i18nc("@title:group", "Status Bar:"), m_showStatusBarSmall);
    topLayout->addRow(QString(), m_showStatusBarFullWidth);

    // Indent the m_showZoomSlider checkbox under m_showStatusBarFullWidth.
    QHBoxLayout *zoomSliderLayout = new QHBoxLayout;
    QStyleOption opt;
    opt.initFrom(this);
    zoomSliderLayout->addItem(
        new QSpacerItem(style()->pixelMetric(QStyle::PM_IndicatorWidth, &opt, this), Dolphin::VERTICAL_SPACER_HEIGHT, QSizePolicy::Fixed, QSizePolicy::Fixed));
    zoomSliderLayout->addWidget(m_showZoomSlider);

    topLayout->addRow(QString(), zoomSliderLayout);

    topLayout->addRow(QString(), m_disableStatusBar);
    topLayout->addItem(new QSpacerItem(0, Dolphin::VERTICAL_SPACER_HEIGHT, QSizePolicy::Fixed, QSizePolicy::Fixed));

    // Location bar
    m_editableUrl = new QCheckBox(i18nc("@option:check Startup Settings", "Make location bar editable"));
    topLayout->addRow(i18n("Location bar:"), m_editableUrl);

    m_showFullPath = new QCheckBox(i18nc("@option:check Startup Settings", "Show full path inside location bar"));
    topLayout->addRow(QString(), m_showFullPath);

    loadSettings();

    locationUpdateInitialViewOptions();

    connect(m_editableUrl, &QCheckBox::toggled, this, &StatusAndLocationBarsSettingsPage::locationSlotSettingsChanged);
    connect(m_showFullPath, &QCheckBox::toggled, this, &StatusAndLocationBarsSettingsPage::locationSlotSettingsChanged);

    connect(m_statusBarButtonGroup, &QButtonGroup::idClicked, this, &StatusAndLocationBarsSettingsPage::changed);
    connect(m_statusBarButtonGroup, &QButtonGroup::idClicked, this, &StatusAndLocationBarsSettingsPage::onShowStatusBarToggled);
    connect(m_showZoomSlider, &QCheckBox::toggled, this, &StatusAndLocationBarsSettingsPage::changed);
#ifndef IS_KCM
    connect(m_barDensity, &QComboBox::currentIndexChanged, this, &StatusAndLocationBarsSettingsPage::changed);
    // Whale: a profile applied while the dialog is open sets the density; OK must keep it (TD5).
    connect(WhaleToolbarSettings::self(), &WhaleToolbarSettings::toolbarDensityChanged, this, [this](WhaleToolbarSettings::Density density) {
        m_barDensity->setCurrentIndex(static_cast<int>(density));
    });
#endif
    // Whale: ST4, a style applied while the dialog is open sets the status bar; OK must keep it.
    connect(GeneralSettings::self(), &GeneralSettings::showStatusBarChanged, this, [this]() {
        m_statusBarButtonGroup->button(GeneralSettings::showStatusBar())->setChecked(true);
        onShowStatusBarToggled();
    });
}

StatusAndLocationBarsSettingsPage::~StatusAndLocationBarsSettingsPage() = default;

void StatusAndLocationBarsSettingsPage::applySettings()
{
    GeneralSettings *settings = GeneralSettings::self();

    settings->setEditableUrl(m_editableUrl->isChecked());
    settings->setShowFullPath(m_showFullPath->isChecked());

    settings->setShowStatusBar(m_statusBarButtonGroup->checkedId());
    settings->setShowZoomSlider(m_showZoomSlider->isChecked());

    settings->save();

#ifndef IS_KCM
    // Whale: applied at once by WhaleDensityStyle (TD4)
    WhaleToolbarSettings::self()->setToolbarDensity(static_cast<WhaleToolbarSettings::Density>(m_barDensity->currentIndex()));
#endif
}

void StatusAndLocationBarsSettingsPage::onShowStatusBarToggled()
{
    const bool checked = (m_statusBarButtonGroup->checkedId() == GeneralSettings::EnumShowStatusBar::FullWidth);
    m_showZoomSlider->setEnabled(checked);
}

void StatusAndLocationBarsSettingsPage::restoreDefaults()
{
    GeneralSettings *settings = GeneralSettings::self();
    settings->useDefaults(true);
    loadSettings();
    settings->useDefaults(false);
#ifndef IS_KCM
    // Whale: not a kcfg setting; Normal is Dolphin's look (TD4).
    m_barDensity->setCurrentIndex(static_cast<int>(WhaleToolbarSettings::Density::Normal));
#endif
}

void StatusAndLocationBarsSettingsPage::locationSlotSettingsChanged()
{
    // Provide a hint that the startup settings have been changed. This allows the views
    // to apply the startup settings only if they have been explicitly changed by the user
    // (see bug #254947).
    GeneralSettings::setModifiedStartupSettings(true);

    // Enable and disable home URL controls appropriately
    locationUpdateInitialViewOptions();
    Q_EMIT changed();
}

void StatusAndLocationBarsSettingsPage::locationUpdateInitialViewOptions()
{
    foldersTabsPage->m_homeUrlBoxLayoutContainer->setEnabled(foldersTabsPage->m_homeUrlRadioButton->isChecked());
    foldersTabsPage->m_buttonBoxLayoutContainer->setEnabled(foldersTabsPage->m_homeUrlRadioButton->isChecked());
}

void StatusAndLocationBarsSettingsPage::loadSettings()
{
    m_editableUrl->setChecked(GeneralSettings::editableUrl());
    m_showFullPath->setChecked(GeneralSettings::showFullPath());
    m_statusBarButtonGroup->button(GeneralSettings::showStatusBar())->setChecked(true);
    m_showZoomSlider->setChecked(GeneralSettings::showZoomSlider());
#ifndef IS_KCM
    m_barDensity->setCurrentIndex(static_cast<int>(WhaleToolbarSettings::self()->toolbarDensity())); // Whale: TD4
#endif

    onShowStatusBarToggled();
}

#include "moc_statusandlocationbarssettingspage.cpp"
