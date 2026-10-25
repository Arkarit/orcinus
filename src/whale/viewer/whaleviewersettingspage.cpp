/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleviewersettingspage.h"

#include "whaleviewersettings.h"

#include <KLocalizedString>

#include <QCheckBox>
#include <QHeaderView>
#include <QImageReader>
#include <QLabel>
#include <QMimeDatabase>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace
{
const QString ImagesPattern = QStringLiteral("image/*");

bool isImage(const QMimeType &type)
{
    if (type.name().startsWith(QLatin1String("image/"))) {
        return true;
    }
    const QStringList ancestors = type.allAncestors();
    return std::any_of(ancestors.cbegin(), ancestors.cend(), [](const QString &name) {
        return name.startsWith(QLatin1String("image/"));
    });
}
}

WhaleViewerSettingsPage::WhaleViewerSettingsPage(QWidget *parent)
    : SettingsPageBase(parent)
{
    auto layout = new QVBoxLayout(this);

    auto label = new QLabel(i18nc("@info", "Whale shows these file types in its own viewer when they are opened, instead of opening their application:"), this);
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

    m_images = new QTreeWidgetItem(m_types, {i18nc("@item:inlistbox file types", "Images"), ImagesPattern});
    m_images->setFlags(m_images->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
    const QMimeDatabase database;
    QList<QTreeWidgetItem *> children;
    const QStringList names = imageMimeTypes();
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
    m_images->addChildren(children);

    m_wrapAround = new QCheckBox(i18nc("@option:check", "Continue with the first file after the last one"), this);
    layout->addWidget(m_wrapAround);

    loadSettings();

    connect(m_types, &QTreeWidget::itemChanged, this, [this]() {
        if (!m_loading) {
            Q_EMIT changed();
        }
    });
    connect(m_wrapAround, &QCheckBox::toggled, this, &WhaleViewerSettingsPage::changed);
}

WhaleViewerSettingsPage::~WhaleViewerSettingsPage() = default;

QStringList WhaleViewerSettingsPage::imageMimeTypes()
{
    const QMimeDatabase database;
    QStringList result;
    const QList<QByteArray> supported = QImageReader::supportedMimeTypes();
    for (const QByteArray &name : supported) {
        const QMimeType type = database.mimeTypeForName(QString::fromLatin1(name));
        if (type.isValid() && isImage(type) && !result.contains(type.name())) {
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
}

void WhaleViewerSettingsPage::restoreDefaults()
{
    QStringList defaults = m_otherPatterns;
    defaults.append(ImagesPattern);
    setPatterns(defaults);
    m_wrapAround->setChecked(false);
    Q_EMIT changed();
}

void WhaleViewerSettingsPage::loadSettings()
{
    setPatterns(WhaleViewerSettings::mimeTypes());
    m_wrapAround->setChecked(WhaleViewerSettings::wrapAround());
}

void WhaleViewerSettingsPage::setPatterns(const QStringList &patterns)
{
    m_loading = true;
    m_otherPatterns.clear();
    for (const QString &pattern : patterns) {
        if (!pattern.startsWith(QLatin1String("image/"))) {
            m_otherPatterns.append(pattern);
        }
    }
    const bool allImages = patterns.contains(ImagesPattern);
    for (int i = 0; i < m_images->childCount(); ++i) {
        QTreeWidgetItem *child = m_images->child(i);
        child->setCheckState(0, allImages || patterns.contains(child->text(1)) ? Qt::Checked : Qt::Unchecked);
    }
    m_loading = false;
}

QStringList WhaleViewerSettingsPage::patterns() const
{
    QStringList result = m_otherPatterns;
    if (m_images->checkState(0) == Qt::Checked) {
        result.append(ImagesPattern);
        return result;
    }
    for (int i = 0; i < m_images->childCount(); ++i) {
        const QTreeWidgetItem *child = m_images->child(i);
        if (child->checkState(0) == Qt::Checked) {
            result.append(child->text(1));
        }
    }
    return result;
}

#include "moc_whaleviewersettingspage.cpp"
