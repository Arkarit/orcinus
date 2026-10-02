/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "filenamehighlighter.h"

#include <KColorUtils>
#include <KFileItem>

#include <QFileInfo>
#include <QMimeType>
#include <QPalette>

#include <sys/stat.h>

namespace
{
// Windows executable types are recognized by suffix rather than by MIME type:
// the MIME types of *.exe also cover libraries and drivers such as *.dll or *.sys.
const QStringList windowsExecutableSuffixes = {
    QStringLiteral("exe"),
};

constexpr int maximumContrastSteps = 20;
constexpr qreal contrastStep = 0.05;

QString mimeTypeGroupPattern(const QString &pattern)
{
    // "image/*" -> "image"
    if (pattern.endsWith(QLatin1String("/*")) && pattern.count(QLatin1Char('/')) == 1) {
        return pattern.chopped(2);
    }
    return QString();
}
}

const qreal FileNameHighlighter::MinimumContrastRatio = 4.5;

FileNameColor FileNameColor::schemeRole(KColorScheme::ForegroundRole role)
{
    FileNameColor color;
    color.m_source = Source::SchemeRole;
    color.m_role = role;
    return color;
}

FileNameColor FileNameColor::custom(const QColor &customColor)
{
    FileNameColor color;
    if (customColor.isValid()) {
        color.m_source = Source::Custom;
        color.m_customColor = customColor;
    }
    return color;
}

FileNameColor::Source FileNameColor::source() const
{
    return m_source;
}

bool FileNameColor::isSet() const
{
    return m_source != Source::Unset;
}

KColorScheme::ForegroundRole FileNameColor::role() const
{
    return m_role;
}

QColor FileNameColor::customColor() const
{
    return m_customColor;
}

bool FileNameColor::operator==(const FileNameColor &other) const
{
    if (m_source != other.m_source) {
        return false;
    }
    switch (m_source) {
    case Source::SchemeRole:
        return m_role == other.m_role;
    case Source::Custom:
        return m_customColor == other.m_customColor;
    case Source::Unset:
        break;
    }
    return true;
}

bool FileNameTextStyle::isEmpty() const
{
    return !color.isSet() && !bold.has_value() && !italic.has_value();
}

bool FileNameTextStyle::operator==(const FileNameTextStyle &other) const
{
    return color == other.color && bold == other.bold && italic == other.italic;
}

bool FileNameHighlightRule::operator==(const FileNameHighlightRule &other) const
{
    return name == other.name && enabled == other.enabled && condition == other.condition && patterns == other.patterns && style == other.style;
}

FileNameHighlighter::FileNameHighlighter(bool enabled,
                                         const QList<FileNameHighlightRule> &rules,
                                         FileSystemTypeFunction fileSystemType,
                                         KSharedConfigPtr colorSchemeConfig)
    : m_enabled(enabled)
    , m_rules(rules)
    , m_fileSystemType(fileSystemType ? std::move(fileSystemType) : FileSystemTypeFunction(&KFileSystemType::fileSystemType))
    , m_colorSchemeConfig(std::move(colorSchemeConfig))
{
    for (const FileNameHighlightRule &rule : std::as_const(m_rules)) {
        if (!rule.enabled || (!rule.style.bold.has_value() && !rule.style.italic.has_value())) {
            continue;
        }
        m_affectsFont = true;
        if (rule.condition == FileNameHighlightRule::Condition::MimeType) {
            m_fontDependsOnMimeType = true;
        }
    }
}

bool FileNameHighlighter::isEnabled() const
{
    return m_enabled;
}

QList<FileNameHighlightRule> FileNameHighlighter::rules() const
{
    return m_rules;
}

bool FileNameHighlighter::affectsFont() const
{
    return m_enabled && m_affectsFont;
}

bool FileNameHighlighter::fontDependsOnMimeType() const
{
    return m_enabled && m_fontDependsOnMimeType;
}

FileNameTextStyle FileNameHighlighter::styleFor(const KFileItem &item, Attributes wanted) const
{
    FileNameTextStyle style;
    if (!m_enabled || item.isNull()) {
        return style;
    }

    // The MIME type is only determined if a rule that can still contribute needs it.
    std::optional<QString> mimeTypeName;

    for (int i = 0; i < m_rules.count() && wanted; ++i) {
        const FileNameHighlightRule &rule = m_rules.at(i);
        if (!rule.enabled) {
            continue;
        }

        const bool contributesColor = (wanted & Color) && rule.style.color.isSet();
        const bool contributesBold = (wanted & Bold) && rule.style.bold.has_value();
        const bool contributesItalic = (wanted & Italic) && rule.style.italic.has_value();
        if (!contributesColor && !contributesBold && !contributesItalic) {
            continue;
        }

        if (!ruleMatches(i, item, mimeTypeName)) {
            continue;
        }

        if (contributesColor) {
            style.color = rule.style.color;
            wanted &= ~Attributes(Color);
        }
        if (contributesBold) {
            style.bold = rule.style.bold;
            wanted &= ~Attributes(Bold);
        }
        if (contributesItalic) {
            style.italic = rule.style.italic;
            wanted &= ~Attributes(Italic);
        }
    }

    return style;
}

QFont FileNameHighlighter::fontFor(const KFileItem &item, const QFont &baseFont) const
{
    QFont font(baseFont);
    if (!affectsFont()) {
        return font;
    }

    const FileNameTextStyle style = styleFor(item, Font);
    if (style.bold.has_value()) {
        font.setBold(*style.bold);
    }
    if (style.italic.has_value()) {
        font.setItalic(*style.italic);
    }
    return font;
}

QColor FileNameHighlighter::nameColor(const FileNameColor &color, const QPalette &palette) const
{
    if (!color.isSet()) {
        return QColor();
    }

    const QString key = QStringLiteral("%1|%2|%3|%4")
                            .arg(static_cast<int>(color.source()))
                            .arg(static_cast<int>(color.role()))
                            .arg(color.customColor().rgba())
                            .arg(palette.base().color().rgba());
    const auto it = m_nameColors.constFind(key);
    if (it != m_nameColors.constEnd()) {
        return it.value();
    }

    const QColor resolved = resolveNameColor(color, palette, m_colorSchemeConfig);
    m_nameColors.insert(key, resolved);
    return resolved;
}

void FileNameHighlighter::clearColorCache() const
{
    m_nameColors.clear();
}

void FileNameHighlighter::clearFileSystemCache() const
{
    m_posixExecuteBitsMeaningful.clear();
}

bool FileNameHighlighter::isExecutableFile(const KFileItem &item) const
{
    // Excludes directories, broken symbolic links (reported with a sentinel file
    // type and 0777 permissions by KIO), FIFOs, sockets and devices.
    if (!item.isRegularFile()) {
        return false;
    }

    if (isWindowsExecutable(item)) {
        return true;
    }

    const mode_t permissions = item.permissions();
    if (permissions == static_cast<mode_t>(KFileItem::Unknown)) {
        return false;
    }

    // Any execute bit counts, like in GNU ls. KFileItem::isExecutable() is not used
    // because it depends on the file owner and needs expensive user database lookups.
    if (!(permissions & (S_IXUSR | S_IXGRP | S_IXOTH))) {
        return false;
    }

    return posixExecuteBitsAreMeaningful(item);
}

bool FileNameHighlighter::isWindowsExecutable(const KFileItem &item)
{
    const QString suffix = item.suffix();
    if (suffix.isEmpty()) {
        return false;
    }
    return windowsExecutableSuffixes.contains(suffix, Qt::CaseInsensitive);
}

QColor FileNameHighlighter::resolveNameColor(const FileNameColor &color, const QPalette &palette, const KSharedConfigPtr &colorSchemeConfig)
{
    QColor resolved;
    switch (color.source()) {
    case FileNameColor::Source::Unset:
        return QColor();
    case FileNameColor::Source::SchemeRole: {
        const KColorScheme scheme(QPalette::Active, KColorScheme::View, colorSchemeConfig);
        resolved = scheme.foreground(color.role()).color();
        break;
    }
    case FileNameColor::Source::Custom:
        resolved = color.customColor();
        break;
    }

    return ensureContrast(resolved, palette.base().color());
}

QColor FileNameHighlighter::ensureContrast(const QColor &color, const QColor &background)
{
    if (!color.isValid() || !background.isValid()) {
        return color;
    }

    // Move away from the background: darken on light backgrounds, lighten on dark ones.
    const qreal direction = KColorUtils::luma(background) > 0.5 ? -contrastStep : contrastStep;

    QColor adjusted = color;
    for (int step = 0; step < maximumContrastSteps && KColorUtils::contrastRatio(adjusted, background) < MinimumContrastRatio; ++step) {
        adjusted = KColorUtils::shade(adjusted, direction);
    }
    return adjusted;
}

bool FileNameHighlighter::ruleMatches(int ruleIndex, const KFileItem &item, std::optional<QString> &mimeTypeName) const
{
    switch (m_rules.at(ruleIndex).condition) {
    case FileNameHighlightRule::Condition::Executable:
        return isExecutableFile(item);
    case FileNameHighlightRule::Condition::Symlink:
        return item.isLink();
    case FileNameHighlightRule::Condition::MimeType:
        return mimeTypeRuleMatches(ruleIndex, item, mimeTypeName);
    }
    return false;
}

bool FileNameHighlighter::mimeTypeRuleMatches(int ruleIndex, const KFileItem &item, std::optional<QString> &mimeTypeName) const
{
    if (!mimeTypeName.has_value()) {
        mimeTypeName = item.currentMimeType().name();
    }
    if (mimeTypeName->isEmpty()) {
        return false;
    }

    auto it = m_mimeTypeMatches.find(*mimeTypeName);
    if (it == m_mimeTypeMatches.end()) {
        // Evaluate all MIME type rules once per MIME type.
        const QMimeType mimeType = item.currentMimeType();
        QBitArray matches(m_rules.count());
        const QString group = mimeTypeName->section(QLatin1Char('/'), 0, 0);
        for (int i = 0; i < m_rules.count(); ++i) {
            const FileNameHighlightRule &rule = m_rules.at(i);
            if (rule.condition != FileNameHighlightRule::Condition::MimeType) {
                continue;
            }
            for (const QString &pattern : rule.patterns) {
                const QString patternGroup = mimeTypeGroupPattern(pattern);
                // A group pattern only matches the major type of the canonical name,
                // exact types also match their aliases and sub-classes.
                const bool patternMatches = patternGroup.isEmpty() ? mimeType.inherits(pattern) : patternGroup == group;
                if (patternMatches) {
                    matches.setBit(i);
                    break;
                }
            }
        }
        it = m_mimeTypeMatches.insert(*mimeTypeName, matches);
    }

    return it.value().testBit(ruleIndex);
}

bool FileNameHighlighter::posixExecuteBitsAreMeaningful(const KFileItem &item) const
{
    bool isLocal = false;
    const QUrl url = item.mostLocalUrl(&isLocal);
    if (!isLocal) {
        // Trust the permissions reported by the KIO worker.
        return true;
    }

    const QString directory = QFileInfo(url.toLocalFile()).absolutePath();
    const auto it = m_posixExecuteBitsMeaningful.constFind(directory);
    if (it != m_posixExecuteBitsMeaningful.constEnd()) {
        return it.value();
    }

    // Mount options make all files on these file systems appear executable.
    const KFileSystemType::Type type = m_fileSystemType(directory);
    const bool meaningful = type != KFileSystemType::Fat && type != KFileSystemType::Ntfs && type != KFileSystemType::Exfat;
    m_posixExecuteBitsMeaningful.insert(directory, meaningful);
    return meaningful;
}
