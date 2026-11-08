/*
 * SPDX-FileCopyrightText: 2011 Janardhan Reddy <annapareddyjanardhanreddy@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KFILEITEMMODELFILTER_H
#define KFILEITEMMODELFILTER_H

#include "dolphin_export.h"

#include <QStringList>

class KFileItem;
class QRegularExpression;

/**
 * @brief Allows to check whether an item of the KFileItemModel
 *        matches with a set filter-string.
 *
 * Currently the filter is only checked for the KFileItem::text()
 * property of the KFileItem, but this might get extended in
 * future.
 */
class DOLPHIN_EXPORT KFileItemModelFilter
{
public:
    KFileItemModelFilter();
    virtual ~KFileItemModelFilter();

    KFileItemModelFilter(const KFileItemModelFilter &) = delete;
    KFileItemModelFilter &operator=(const KFileItemModelFilter &) = delete;

    /** Filtering modes of KFileItemModelFilter */
    enum FilterMode {
        /** Substring matching. */
        PlainText = 0,
        /** Matching with glob, default. */
        Glob,
        /** Matching with regex. */
        Regex
    };

    /**
     * Sets the pattern that is used for a comparison with the item
     * in KFileItemModelFilter::matches().
     */
    void setPattern(const QString &pattern);
    QString pattern() const;

    /**
     * Sets the filtering mode used in KFileItemModelFilter::matches().
     */
    void setFilterMode(FilterMode mode);
    FilterMode filterMode() const;

    /**
     * Whale: the regular expression of the glob pattern @p pattern. Spaces, commas and semicolons
     * separate several patterns, of which a name has to match one ("*.jpg *.png").
     */
    static QString globToRegularExpression(const QString &pattern);

    /**
     * Enable or disable the case sensitive filtering.
     */
    void setCaseSensitive(bool caseSensitive);
    bool isCaseSensitive() const;

    /** Whale: FB2, the items the pattern applies to; the others always match it. */
    enum class Scope {
        Files,
        Folders,
        FilesAndFolders, // Dolphin's behavior, the default
    };

    /**
     * Whale: FB2, sets the items the pattern applies to, e.g. files only, so that all folders stay
     * shown. The mimetype filters are not affected.
     */
    void setScope(Scope scope);
    Scope scope() const;

    /**
     * Set the list of mimetypes that are used for comparison with the
     * item in KFileItemModelFilter::matchesMimeType.
     */
    void setMimeTypes(const QStringList &types);
    QStringList mimeTypes() const;

    /**
     * Set the list of mimetypes that are used for comparison and excluded with the
     * item in KFileItemModelFilter::matchesMimeType.
     */
    void setExcludeMimeTypes(const QStringList &types);
    QStringList excludeMimeTypes() const;

    /**
     * @return True if either the pattern or mimetype filters has been set.
     */
    bool hasSetFilters() const;

    /**
     * @return True if the item matches with the pattern defined by
     *         @ref setPattern() or @ref setMimeTypes
     */
    bool matches(const KFileItem &item) const;

private:
    /**
     * @return True if item matches pattern set by @ref setPattern.
     */
    bool matchesPattern(const KFileItem &item) const;

    /**
     * @return True if item matches mimetypes set by @ref setMimeTypes.
     */
    bool matchesType(const KFileItem &item) const;

    /**
     * Instantiate and configure m_regExp according to m_filterMode and m_caseSensitive.
     */
    void updateFilter();

    FilterMode m_filterMode; // The current filtering mode.
    bool m_caseSensitive; // If true the matching will be case sensitive.
    Scope m_scope = Scope::FilesAndFolders; // Whale: FB2

    QRegularExpression *m_regExp;
    QString m_lowerCasePattern; // Lowercase version of m_filter for
                                // faster comparison in matches().
    QString m_pattern; // Property set by setPattern().
    QStringList m_mimeTypes; // Property set by setMimeTypes()
    QStringList m_excludeMimeTypes; // Property set by setExcludeMimeTypes()
};
#endif
