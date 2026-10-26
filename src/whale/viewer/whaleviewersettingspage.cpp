/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleviewersettingspage.h"

#include "whaleviewersettings.h"

#include <KLocalizedString>

#include <QCheckBox>
#include <QHeaderView>
#include <QIcon>
#include <QImageReader>
#include <QLabel>
#include <QMimeDatabase>
#include <QTreeWidget>
#include <QVBoxLayout>

static void initResources()
{
    // The resources of a static library are only linked in when something refers to them.
    Q_INIT_RESOURCE(whaleviewer);
}

namespace
{
const QString ImagesPattern = QStringLiteral("image/*");
const QString VideosPattern = QStringLiteral("video/*");

bool isOrInherits(const QMimeType &type, QLatin1String prefix)
{
    if (type.name().startsWith(prefix)) {
        return true;
    }
    const QStringList ancestors = type.allAncestors();
    return std::any_of(ancestors.cbegin(), ancestors.cend(), [prefix](const QString &name) {
        return name.startsWith(prefix);
    });
}

bool isGroupPattern(const QString &pattern)
{
    return pattern.startsWith(QLatin1String("image/")) || (WhaleViewerSettings::videoSupported() && pattern.startsWith(QLatin1String("video/")));
}
}

WhaleViewerSettingsPage::WhaleViewerSettingsPage(QWidget *parent)
    : SettingsPageBase(parent)
{
    auto layout = new QVBoxLayout(this);

    auto label =
        new QLabel(i18nc("@info", "Orcinus shows these file types in its own viewer when they are opened, instead of opening their application:"), this);
    label->setWordWrap(true);
    layout->addWidget(label);

    m_types = new QTreeWidget(this);
    m_types->setColumnCount(2);
    m_types->setHeaderLabels({i18nc("@title:column", "File Type"), i18nc("@title:column", "MIME Type")});
    m_types->setRootIsDecorated(true);
    m_types->setUniformRowHeights(true);
    m_types->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_types->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    layout->addWidget(m_types, 1);

    m_groups.append(addGroup(i18nc("@item:inlistbox file types", "Images"), ImagesPattern, imageMimeTypes()));
    if (WhaleViewerSettings::videoSupported()) {
        m_groups.append(addGroup(i18nc("@item:inlistbox file types", "Videos"), VideosPattern, videoMimeTypes()));
    }

    m_wrapAround = addCheckBox(i18nc("@option:check", "Continue with the first file after the last one"), QStringLiteral("wrapAround"));
    m_upscale = addCheckBox(i18nc("@option:check", "Enlarge small images to fill the screen"), QStringLiteral("upscale"));
    m_smoothZoom = addCheckBox(i18nc("@option:check", "Smooth the pixels when zoomed in beyond 100%"), QStringLiteral("smoothZoom"));
    if (WhaleViewerSettings::videoSupported()) {
        m_videoAutoplay = addCheckBox(i18nc("@option:check", "Play videos as soon as they are shown"), QStringLiteral("videoAutoplay"));
        m_videoSound = addCheckBox(i18nc("@option:check", "Play the sound of videos"), QStringLiteral("videoSound"));
        m_videoLoop = addCheckBox(i18nc("@option:check", "Play videos over and over"), QStringLiteral("videoLoop"));
    }

    loadSettings();

    connect(m_types, &QTreeWidget::itemChanged, this, [this]() {
        if (!m_loading) {
            Q_EMIT changed();
        }
    });
}

WhaleViewerSettingsPage::~WhaleViewerSettingsPage() = default;

QTreeWidgetItem *WhaleViewerSettingsPage::addGroup(const QString &title, const QString &pattern, const QStringList &names)
{
    auto group = new QTreeWidgetItem(m_types, {title, pattern});
    group->setFlags(group->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
    const QMimeDatabase database;
    QList<QTreeWidgetItem *> children;
    for (const QString &name : names) {
        const QMimeType type = database.mimeTypeForName(name);
        auto child = new QTreeWidgetItem({type.comment(), name});
        child->setFlags(child->flags() | Qt::ItemIsUserCheckable);
        child->setCheckState(0, Qt::Unchecked);
        children.append(child);
    }
    std::sort(children.begin(), children.end(), [](const QTreeWidgetItem *a, const QTreeWidgetItem *b) {
        return a->text(0).localeAwareCompare(b->text(0)) < 0;
    });
    group->addChildren(children);
    return group;
}

QCheckBox *WhaleViewerSettingsPage::addCheckBox(const QString &text, const QString &objectName)
{
    auto checkBox = new QCheckBox(text, this);
    checkBox->setObjectName(objectName);
    layout()->addWidget(checkBox);
    connect(checkBox, &QCheckBox::toggled, this, &WhaleViewerSettingsPage::changed);
    return checkBox;
}

QIcon WhaleViewerSettingsPage::icon()
{
    initResources();
    return QIcon(QStringLiteral(":/whale/icons/whale-viewer.svg"));
}

QStringList WhaleViewerSettingsPage::imageMimeTypes()
{
    const QMimeDatabase database;
    QStringList result;
    const QList<QByteArray> supported = QImageReader::supportedMimeTypes();
    for (const QByteArray &name : supported) {
        const QMimeType type = database.mimeTypeForName(QString::fromLatin1(name));
        if (type.isValid() && isOrInherits(type, QLatin1String("image/")) && !result.contains(type.name())) {
            result.append(type.name());
        }
    }
    result.sort();
    return result;
}

QStringList WhaleViewerSettingsPage::videoMimeTypes()
{
    const QStringList images = imageMimeTypes();
    QStringList result;
    const QList<QMimeType> types = QMimeDatabase().allMimeTypes();
    for (const QMimeType &type : types) {
        if (isOrInherits(type, QLatin1String("video/")) && !images.contains(type.name()) && !result.contains(type.name())) {
            result.append(type.name());
        }
    }
    result.sort();
    return result;
}

void WhaleViewerSettingsPage::applySettings()
{
    WhaleViewerSettings::setMimeTypes(patterns());
    WhaleViewerSettings::setWrapAround(m_wrapAround->isChecked());
    WhaleViewerSettings::setUpscale(m_upscale->isChecked());
    WhaleViewerSettings::setSmoothZoom(m_smoothZoom->isChecked());
    if (WhaleViewerSettings::videoSupported()) {
        WhaleViewerSettings::setVideoAutoplay(m_videoAutoplay->isChecked());
        WhaleViewerSettings::setVideoSound(m_videoSound->isChecked());
        WhaleViewerSettings::setVideoLoop(m_videoLoop->isChecked());
    }
}

void WhaleViewerSettingsPage::restoreDefaults()
{
    QStringList defaults = m_otherPatterns;
    for (const QTreeWidgetItem *group : std::as_const(m_groups)) {
        defaults.append(group->text(1));
    }
    setPatterns(defaults);
    m_wrapAround->setChecked(false);
    m_upscale->setChecked(false);
    m_smoothZoom->setChecked(false);
    if (WhaleViewerSettings::videoSupported()) {
        m_videoAutoplay->setChecked(true);
        m_videoSound->setChecked(true);
        m_videoLoop->setChecked(true);
    }
    Q_EMIT changed();
}

void WhaleViewerSettingsPage::loadSettings()
{
    setPatterns(WhaleViewerSettings::mimeTypes());
    m_wrapAround->setChecked(WhaleViewerSettings::wrapAround());
    m_upscale->setChecked(WhaleViewerSettings::upscale());
    m_smoothZoom->setChecked(WhaleViewerSettings::smoothZoom());
    if (WhaleViewerSettings::videoSupported()) {
        m_videoAutoplay->setChecked(WhaleViewerSettings::videoAutoplay());
        m_videoSound->setChecked(WhaleViewerSettings::videoSound());
        m_videoLoop->setChecked(WhaleViewerSettings::videoLoop());
    }
}

void WhaleViewerSettingsPage::setPatterns(const QStringList &patterns)
{
    m_loading = true;
    m_otherPatterns.clear();
    for (const QString &pattern : patterns) {
        if (!isGroupPattern(pattern)) {
            m_otherPatterns.append(pattern);
        }
    }
    for (QTreeWidgetItem *group : std::as_const(m_groups)) {
        const bool all = patterns.contains(group->text(1));
        for (int i = 0; i < group->childCount(); ++i) {
            QTreeWidgetItem *child = group->child(i);
            child->setCheckState(0, all || patterns.contains(child->text(1)) ? Qt::Checked : Qt::Unchecked);
        }
    }
    m_loading = false;
}

QStringList WhaleViewerSettingsPage::patterns() const
{
    QStringList result = m_otherPatterns;
    for (const QTreeWidgetItem *group : std::as_const(m_groups)) {
        if (group->checkState(0) == Qt::Checked) {
            result.append(group->text(1));
            continue;
        }
        for (int i = 0; i < group->childCount(); ++i) {
            const QTreeWidgetItem *child = group->child(i);
            if (child->checkState(0) == Qt::Checked) {
                result.append(child->text(1));
            }
        }
    }
    return result;
}

#include "moc_whaleviewersettingspage.cpp"
