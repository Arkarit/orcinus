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
 * below it each image type Qt can decode; built with libmpv, "Videos" likewise with the video
 * types (V3). With all of a group on, the setting is the group (WhaleViewerSettings::mimeTypes()
 * "image/" and asterisk), so types that become known later are included; otherwise the names of
 * the types that are on. Other patterns in the setting are kept as they are.
 *
 * Also whether paging goes round (wrap-around) whether small images are enlarged to the
 * screen (V2b), whether zoomed in pixels are smoothed (V2c) and, with videos, autoplay, sound
 * and loop (V3). The A/B switch for multiple selections stays
 * hidden in orcinusrc.
 */
class WhaleViewerSettingsPage : public SettingsPageBase
{
    Q_OBJECT

public:
    explicit WhaleViewerSettingsPage(QWidget *parent = nullptr);
    ~WhaleViewerSettingsPage() override;

    void applySettings() override;
    void restoreDefaults() override;

    /** @return The icon of the page in the dialog (a monitor in the style of Breeze). */
    static QIcon icon();

    /** @return The image types Qt can decode, by their canonical names, sorted. */
    static QStringList imageMimeTypes();
    /** @return The video types, by their canonical names, sorted (without the image ones). */
    static QStringList videoMimeTypes();

private:
    void loadSettings();
    void setPatterns(const QStringList &patterns);
    QStringList patterns() const;

    QTreeWidgetItem *addGroup(const QString &title, const QString &pattern, const QStringList &names);
    QCheckBox *addCheckBox(const QString &text, const QString &objectName);

    QTreeWidget *m_types = nullptr;
    /// "Images" and, with libmpv, "Videos": their pattern in column 1.
    QList<QTreeWidgetItem *> m_groups;
    QCheckBox *m_wrapAround = nullptr;
    QCheckBox *m_upscale = nullptr;
    QCheckBox *m_smoothZoom = nullptr;
    QCheckBox *m_videoAutoplay = nullptr;
    QCheckBox *m_videoSound = nullptr;
    QCheckBox *m_videoLoop = nullptr;
    QStringList m_otherPatterns;
    bool m_loading = false;
};

#endif
