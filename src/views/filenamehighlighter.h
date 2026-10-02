/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef FILENAMEHIGHLIGHTER_H
#define FILENAMEHIGHLIGHTER_H

#include "dolphin_export.h"

#include <KColorScheme>
#include <KFileSystemType>
#include <KSharedConfig>

#include <QBitArray>
#include <QColor>
#include <QHash>
#include <QList>
#include <QStringList>

#include <functional>
#include <optional>

class KFileItem;
class QPalette;

/**
 * @brief Color source of a file name highlighting rule.
 *
 * A color either references a role of the KDE color scheme, which follows
 * the active scheme automatically, or is an explicit custom color.
 */
class DOLPHIN_EXPORT FileNameColor
{
public:
    enum class Source {
        Unset,
        SchemeRole,
        Custom,
    };

    FileNameColor() = default;
    static FileNameColor schemeRole(KColorScheme::ForegroundRole role);
    static FileNameColor custom(const QColor &color);

    Source source() const;
    bool isSet() const;
    KColorScheme::ForegroundRole role() const;
    QColor customColor() const;

    bool operator==(const FileNameColor &other) const;

private:
    Source m_source = Source::Unset;
    KColorScheme::ForegroundRole m_role = KColorScheme::NormalText;
    QColor m_customColor;
};

/**
 * @brief Text style of a file name. Unset attributes leave the name unchanged.
 */
struct DOLPHIN_EXPORT FileNameTextStyle {
    FileNameColor color;
    std::optional<bool> bold;
    std::optional<bool> italic;

    bool isEmpty() const;
    bool operator==(const FileNameTextStyle &other) const;
};

/**
 * @brief A single file name highlighting rule.
 *
 * A rule has exactly one kind of condition. Pattern based conditions may have
 * several patterns of that kind, which match if any of them matches.
 */
struct DOLPHIN_EXPORT FileNameHighlightRule {
    enum class Condition {
        Executable,
        Symlink,
        MimeType,
    };

    QString name;
    bool enabled = true;
    Condition condition = Condition::Executable;
    QStringList patterns;
    FileNameTextStyle style;

    bool operator==(const FileNameHighlightRule &other) const;
};

/**
 * @brief Resolves the text style of file names from an ordered list of rules.
 *
 * Rules are evaluated from top to bottom. For each style attribute, the first
 * matching enabled rule that sets the attribute wins.
 *
 * Instances are immutable apart from internal caches and are meant to be used
 * from the GUI thread.
 */
class DOLPHIN_EXPORT FileNameHighlighter
{
public:
    enum Attribute {
        Color = 0x1,
        Bold = 0x2,
        Italic = 0x4,
        Font = Bold | Italic,
        AllAttributes = Color | Font,
    };
    Q_DECLARE_FLAGS(Attributes, Attribute)

    using FileSystemTypeFunction = std::function<KFileSystemType::Type(const QString &path)>;

    /**
     * @param fileSystemType Returns the file system type of a local directory.
     *                       Defaults to KFileSystemType::fileSystemType().
     * @param colorSchemeConfig Color scheme used to resolve scheme roles.
     *                          Defaults to the color scheme of the application.
     */
    explicit FileNameHighlighter(bool enabled,
                                 const QList<FileNameHighlightRule> &rules,
                                 FileSystemTypeFunction fileSystemType = {},
                                 KSharedConfigPtr colorSchemeConfig = {});

    bool isEnabled() const;
    QList<FileNameHighlightRule> rules() const;

    /**
     * @return True if an enabled rule sets bold or italic.
     */
    bool affectsFont() const;

    /**
     * @return True if an enabled MIME type rule sets bold or italic.
     */
    bool fontDependsOnMimeType() const;

    /**
     * @return The resolved style of @p item, limited to the @p wanted attributes.
     *         Rules that set none of the wanted attributes are not evaluated.
     */
    FileNameTextStyle styleFor(const KFileItem &item, Attributes wanted = AllAttributes) const;

    /**
     * @return The color for @p color on a view using @p palette, or an invalid color
     *         if @p color is unset. Results are cached until clearColorCache().
     */
    QColor nameColor(const FileNameColor &color, const QPalette &palette) const;

    void clearColorCache() const;
    void clearFileSystemCache() const;

    /**
     * @return True if @p item is an executable file: a known Windows executable type
     *         on any file system, or a regular file with an execute bit on a file
     *         system with meaningful POSIX execute bits.
     */
    bool isExecutableFile(const KFileItem &item) const;

    /**
     * @return True if the file name of @p item has a known Windows executable suffix.
     */
    static bool isWindowsExecutable(const KFileItem &item);

    /**
     * @return The color for @p color, adjusted to contrast with the base color of
     *         @p palette, or an invalid color if @p color is unset.
     */
    static QColor resolveNameColor(const FileNameColor &color, const QPalette &palette, const KSharedConfigPtr &colorSchemeConfig = {});

    /**
     * @return @p color, lightened or darkened until it has at least the minimum
     *         contrast ratio against @p background.
     */
    static QColor ensureContrast(const QColor &color, const QColor &background);

    /**
     * Minimum contrast ratio between a highlighted file name and the view background.
     * Provisional value; to be decided after a visual review with real color schemes.
     */
    static const qreal MinimumContrastRatio;

private:
    bool ruleMatches(int ruleIndex, const KFileItem &item, std::optional<QString> &mimeTypeName) const;
    bool mimeTypeRuleMatches(int ruleIndex, const KFileItem &item, std::optional<QString> &mimeTypeName) const;
    bool posixExecuteBitsAreMeaningful(const KFileItem &item) const;

    bool m_enabled;
    QList<FileNameHighlightRule> m_rules;
    FileSystemTypeFunction m_fileSystemType;
    KSharedConfigPtr m_colorSchemeConfig;
    bool m_affectsFont = false;
    bool m_fontDependsOnMimeType = false;

    mutable QHash<QString, QBitArray> m_mimeTypeMatches;
    mutable QHash<QString, bool> m_posixExecuteBitsMeaningful;
    mutable QHash<QString, QColor> m_nameColors;

    friend class FileNameHighlighterTest; // For unit testing
};

Q_DECLARE_OPERATORS_FOR_FLAGS(FileNameHighlighter::Attributes)

#endif
