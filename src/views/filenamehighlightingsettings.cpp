/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "filenamehighlightingsettings.h"

#include <KConfigGroup>
#include <KLocalizedString>

namespace
{
const QString groupName = QStringLiteral("FileNameHighlighting");
const QString ruleGroupPrefix = QStringLiteral("Rule");

// Provisional color roles of the default rules, to be decided after a visual
// review with real light and dark color schemes.
constexpr KColorScheme::ForegroundRole provisionalExecutableColorRole = KColorScheme::PositiveText;
constexpr KColorScheme::ForegroundRole provisionalMediaColorRole = KColorScheme::NeutralText;

struct ColorRoleName {
    KColorScheme::ForegroundRole role;
    const char *name;
};

// Stable names used in the configuration file.
constexpr ColorRoleName colorRoleNames[] = {
    {KColorScheme::NormalText, "NormalText"},
    {KColorScheme::InactiveText, "InactiveText"},
    {KColorScheme::ActiveText, "ActiveText"},
    {KColorScheme::LinkText, "LinkText"},
    {KColorScheme::VisitedText, "VisitedText"},
    {KColorScheme::NegativeText, "NegativeText"},
    {KColorScheme::NeutralText, "NeutralText"},
    {KColorScheme::PositiveText, "PositiveText"},
};

void writeOptionalBool(KConfigGroup &group, const char *key, const std::optional<bool> &value)
{
    if (value.has_value()) {
        group.writeEntry(key, *value);
    }
}

std::optional<bool> readOptionalBool(const KConfigGroup &group, const char *key)
{
    if (!group.hasKey(key)) {
        return std::nullopt;
    }
    return group.readEntry(key, false);
}
}

FileNameHighlightingSettings FileNameHighlightingSettings::defaults()
{
    FileNameHighlightingSettings settings;
    settings.enabled = false;
    settings.rules = defaultRules();
    return settings;
}

QList<FileNameHighlightRule> FileNameHighlightingSettings::defaultRules()
{
    FileNameHighlightRule executable;
    executable.name = i18nc("@item:inlistbox file name highlighting rule", "Executable files");
    executable.condition = FileNameHighlightRule::Condition::Executable;
    executable.style.bold = true;
    executable.style.color = FileNameColor::schemeRole(provisionalExecutableColorRole);

    FileNameHighlightRule symlinks;
    symlinks.name = i18nc("@item:inlistbox file name highlighting rule", "Symbolic links");
    symlinks.condition = FileNameHighlightRule::Condition::Symlink;
    symlinks.style.italic = true;

    FileNameHighlightRule media;
    media.name = i18nc("@item:inlistbox file name highlighting rule", "Media files");
    media.condition = FileNameHighlightRule::Condition::MimeType;
    media.patterns = {QStringLiteral("image/*"), QStringLiteral("audio/*"), QStringLiteral("video/*")};
    media.style.color = FileNameColor::schemeRole(provisionalMediaColorRole);

    return {executable, symlinks, media};
}

FileNameHighlightingSettings FileNameHighlightingSettings::load(const KSharedConfigPtr &config)
{
    if (!config->hasGroup(groupName)) {
        return defaults();
    }

    const KConfigGroup group(config, groupName);
    FileNameHighlightingSettings settings;
    settings.enabled = group.readEntry("Enabled", false);

    const int ruleCount = group.readEntry("RuleCount", 0);
    for (int i = 0; i < ruleCount; ++i) {
        const KConfigGroup ruleGroup = group.group(ruleGroupPrefix + QString::number(i));
        if (!ruleGroup.exists()) {
            continue;
        }

        // Rules with unknown conditions or item kinds, e.g. written by a newer version, are skipped.
        const std::optional<FileNameHighlightRule::Condition> condition = conditionFromName(ruleGroup.readEntry("Condition", QString()));
        if (!condition.has_value()) {
            continue;
        }
        const std::optional<FileNameHighlightRule::ItemKind> appliesTo =
            itemKindFromName(ruleGroup.readEntry("AppliesTo", itemKindName(FileNameHighlightRule::ItemKind::FilesAndFolders)));
        if (!appliesTo.has_value()) {
            continue;
        }

        FileNameHighlightRule rule;
        rule.name = ruleGroup.readEntry("Name", QString());
        rule.enabled = ruleGroup.readEntry("Enabled", true);
        rule.condition = *condition;
        rule.appliesTo = *appliesTo;
        rule.patterns = ruleGroup.readEntry("Patterns", QStringList());
        rule.caseSensitive = ruleGroup.readEntry("CaseSensitive", false);

        if (ruleGroup.hasKey("ColorRole")) {
            const std::optional<KColorScheme::ForegroundRole> role = colorRoleFromName(ruleGroup.readEntry("ColorRole", QString()));
            if (role.has_value()) {
                rule.style.color = FileNameColor::schemeRole(*role);
            }
        } else if (ruleGroup.hasKey("Color")) {
            rule.style.color = FileNameColor::custom(ruleGroup.readEntry("Color", QColor()));
        }
        rule.style.bold = readOptionalBool(ruleGroup, "Bold");
        rule.style.italic = readOptionalBool(ruleGroup, "Italic");

        settings.rules.append(rule);
    }

    return settings;
}

void FileNameHighlightingSettings::save(const KSharedConfigPtr &config) const
{
    KConfigGroup group(config, groupName);

    // Rewrite the whole list so that no stale rules remain.
    const QStringList subGroups = group.groupList();
    for (const QString &subGroup : subGroups) {
        if (subGroup.startsWith(ruleGroupPrefix)) {
            group.deleteGroup(subGroup);
        }
    }

    group.writeEntry("Enabled", enabled);
    group.writeEntry("RuleCount", rules.count());

    for (int i = 0; i < rules.count(); ++i) {
        const FileNameHighlightRule &rule = rules.at(i);
        KConfigGroup ruleGroup = group.group(ruleGroupPrefix + QString::number(i));
        ruleGroup.writeEntry("Name", rule.name);
        ruleGroup.writeEntry("Enabled", rule.enabled);
        ruleGroup.writeEntry("Condition", conditionName(rule.condition));
        if (rule.appliesTo != FileNameHighlightRule::ItemKind::FilesAndFolders) {
            ruleGroup.writeEntry("AppliesTo", itemKindName(rule.appliesTo));
        }
        if (!rule.patterns.isEmpty()) {
            ruleGroup.writeEntry("Patterns", rule.patterns);
        }
        if (rule.caseSensitive) {
            ruleGroup.writeEntry("CaseSensitive", true);
        }

        switch (rule.style.color.source()) {
        case FileNameColor::Source::SchemeRole:
            ruleGroup.writeEntry("ColorRole", colorRoleName(rule.style.color.role()));
            break;
        case FileNameColor::Source::Custom:
            ruleGroup.writeEntry("Color", rule.style.color.customColor());
            break;
        case FileNameColor::Source::Unset:
            break;
        }
        writeOptionalBool(ruleGroup, "Bold", rule.style.bold);
        writeOptionalBool(ruleGroup, "Italic", rule.style.italic);
    }

    config->sync();
}

QString FileNameHighlightingSettings::conditionName(FileNameHighlightRule::Condition condition)
{
    switch (condition) {
    case FileNameHighlightRule::Condition::Executable:
        return QStringLiteral("Executable");
    case FileNameHighlightRule::Condition::Symlink:
        return QStringLiteral("Symlink");
    case FileNameHighlightRule::Condition::MimeType:
        return QStringLiteral("MimeType");
    case FileNameHighlightRule::Condition::NamePattern:
        return QStringLiteral("NamePattern");
    }
    return QString();
}

std::optional<FileNameHighlightRule::Condition> FileNameHighlightingSettings::conditionFromName(const QString &name)
{
    for (const auto condition : {FileNameHighlightRule::Condition::Executable,
                                 FileNameHighlightRule::Condition::Symlink,
                                 FileNameHighlightRule::Condition::MimeType,
                                 FileNameHighlightRule::Condition::NamePattern}) {
        if (conditionName(condition) == name) {
            return condition;
        }
    }
    return std::nullopt;
}

QString FileNameHighlightingSettings::itemKindName(FileNameHighlightRule::ItemKind kind)
{
    switch (kind) {
    case FileNameHighlightRule::ItemKind::FilesAndFolders:
        return QStringLiteral("FilesAndFolders");
    case FileNameHighlightRule::ItemKind::Folders:
        return QStringLiteral("Folders");
    case FileNameHighlightRule::ItemKind::Files:
        return QStringLiteral("Files");
    }
    return QString();
}

std::optional<FileNameHighlightRule::ItemKind> FileNameHighlightingSettings::itemKindFromName(const QString &name)
{
    for (const auto kind :
         {FileNameHighlightRule::ItemKind::FilesAndFolders, FileNameHighlightRule::ItemKind::Folders, FileNameHighlightRule::ItemKind::Files}) {
        if (itemKindName(kind) == name) {
            return kind;
        }
    }
    return std::nullopt;
}

QString FileNameHighlightingSettings::colorRoleName(KColorScheme::ForegroundRole role)
{
    for (const ColorRoleName &entry : colorRoleNames) {
        if (entry.role == role) {
            return QString::fromLatin1(entry.name);
        }
    }
    return QString();
}

std::optional<KColorScheme::ForegroundRole> FileNameHighlightingSettings::colorRoleFromName(const QString &name)
{
    for (const ColorRoleName &entry : colorRoleNames) {
        if (name == QLatin1String(entry.name)) {
            return entry.role;
        }
    }
    return std::nullopt;
}

bool FileNameHighlightingSettings::operator==(const FileNameHighlightingSettings &other) const
{
    return enabled == other.enabled && rules == other.rules;
}
