/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALETOOLBARSETTINGS_H
#define WHALETOOLBARSETTINGS_H

#include <KSharedConfig>

#include <QObject>

/**
 * @brief The settings of the toolbars that are not part of their layouts.
 *
 * One for the application (self()): whether the File View Toolbars are shown. It is one
 * setting for all of them, in every view, tab and window of the process. Stored in the group
 * "WhaleToolbars" of the application's configuration (orcinusrc), written at once, so a
 * process started later reads the current value. Running processes do not see each other's
 * changes.
 *
 * The layouts of the toolbars are XML GUI files (WhaleToolbarManager), not settings; Configure
 * Toolbars works the same whether the File View Toolbars are shown or not.
 */
class WhaleToolbarSettings : public QObject
{
    Q_OBJECT

public:
    static WhaleToolbarSettings *self();

    explicit WhaleToolbarSettings(KSharedConfigPtr config, QObject *parent = nullptr);
    ~WhaleToolbarSettings() override;

    static QString groupName();

    /**
     * @return The configuration the settings are stored in (also the markers of
     *         WhaleToolbarMigration).
     */
    KSharedConfigPtr config() const;

    /**
     * @return Whether the File View Toolbars are shown; true without a setting.
     */
    bool fileViewToolBarsVisible() const;
    void setFileViewToolBarsVisible(bool visible);

Q_SIGNALS:
    void fileViewToolBarsVisibleChanged(bool visible);

private:
    KSharedConfigPtr m_config;
};

#endif
