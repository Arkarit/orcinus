/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef FILENAMEHIGHLIGHTINGSETTINGS_H
#define FILENAMEHIGHLIGHTINGSETTINGS_H

#include "dolphin_export.h"
#include "views/filenamehighlighter.h"

#include <KSharedConfig>

#include <QList>

/**
 * @brief Global file name highlighting settings: an on/off switch and an ordered rule list.
 *
 * The settings are stored in the group [FileNameHighlighting] of the application
 * configuration. Each rule is a sub-group [FileNameHighlighting][RuleN]; the order
 * of the rules is their priority.
 */
struct DOLPHIN_EXPORT FileNameHighlightingSettings {
    bool enabled = false;
    QList<FileNameHighlightRule> rules;

    /**
     * @return The settings used if no configuration exists: disabled, with the default rules.
     */
    static FileNameHighlightingSettings defaults();

    /**
     * @return The rules offered initially and restored by "Defaults".
     */
    static QList<FileNameHighlightRule> defaultRules();

    static FileNameHighlightingSettings load(const KSharedConfigPtr &config = KSharedConfig::openConfig());
    void save(const KSharedConfigPtr &config = KSharedConfig::openConfig()) const;

    static QString conditionName(FileNameHighlightRule::Condition condition);
    static std::optional<FileNameHighlightRule::Condition> conditionFromName(const QString &name);
    static QString itemKindName(FileNameHighlightRule::ItemKind kind);
    static std::optional<FileNameHighlightRule::ItemKind> itemKindFromName(const QString &name);
    static QString colorRoleName(KColorScheme::ForegroundRole role);
    static std::optional<KColorScheme::ForegroundRole> colorRoleFromName(const QString &name);

    bool operator==(const FileNameHighlightingSettings &other) const;
};

#endif
