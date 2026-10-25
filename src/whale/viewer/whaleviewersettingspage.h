/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEVIEWERSETTINGSPAGE_H
#define WHALEVIEWERSETTINGSPAGE_H

#include "settings/settingspagebase.h"

class QCheckBox;
class QIcon;
class QTreeWidget;
class QTreeWidgetItem;

/**
 * @brief The "Viewer" page of the settings dialog (Whale, V1c).
 *
 * Which file types the built-in viewer opens, as a tree: "Images" turns all of them on or off,
 * below it each image type Qt can decode. With all of them on, the setting is the group
 * (WhaleViewerSettings::mimeTypes() "image/" and asterisk), so types that become decodable
 * later are included; otherwise the names of the types that are on. Patterns of other groups
 * in the setting (video, V3) are kept as they are.
 *
 * Also whether paging goes round (wrap-around). The A/B switch for multiple selections stays
 * hidden in whalerc.
 */
class WhaleViewerSettingsPage : public SettingsPageBase
{
    Q_OBJECT

public:
    explicit WhaleViewerSettingsPage(QWidget *parent = nullptr);
    ~WhaleViewerSettingsPage() override;

    void applySettings() override;
    void restoreDefaults() override;

    /** @return The icon of the page in the dialog (a TV in the style of Breeze). */
    static QIcon icon();

    /** @return The image types Qt can decode, by their canonical names, sorted. */
    static QStringList imageMimeTypes();

private:
    void loadSettings();
    void setPatterns(const QStringList &patterns);
    QStringList patterns() const;

    QTreeWidget *m_types = nullptr;
    QTreeWidgetItem *m_images = nullptr;
    QCheckBox *m_wrapAround = nullptr;
    QStringList m_otherPatterns;
    bool m_loading = false;
};

#endif
