/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef FILENAMEHIGHLIGHTINGRULEDIALOG_H
#define FILENAMEHIGHLIGHTINGRULEDIALOG_H

#include "views/filenamehighlighter.h"

#include <QDialog>

class KColorButton;
class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QWidget;

/**
 * @brief Dialog to add or edit a file name highlighting rule.
 */
class FileNameHighlightingRuleDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FileNameHighlightingRuleDialog(const FileNameHighlightRule &rule, QWidget *parent = nullptr);

    FileNameHighlightRule rule() const;

    /**
     * @return The name of @p rule, or a description of its condition if it has no name.
     */
    static QString displayName(const FileNameHighlightRule &rule);

    /**
     * @return A file name that matches the condition of @p rule, used for previews.
     */
    static QString exampleFileName(const FileNameHighlightRule &rule);

    /**
     * @return @p baseFont with the bold and italic attributes of @p style applied.
     */
    static QFont previewFont(const FileNameTextStyle &style, const QFont &baseFont);

    /**
     * @return The name color of @p style on a view using @p palette, or the text color
     *         of @p palette if @p style has no color.
     */
    static QColor previewColor(const FileNameTextStyle &style, const QPalette &palette);

    /**
     * @return The patterns of @p text, separated by semicolons or commas.
     */
    static QStringList parsePatterns(const QString &text);

    /**
     * @return The patterns of @p patterns that are neither a known MIME type nor
     *         a group pattern of a known MIME type group.
     */
    static QStringList unknownMimeTypePatterns(const QStringList &patterns);

private:
    void updateWidgets();
    bool hasPatterns() const;
    void chooseMimeTypes();
    FileNameHighlightRule::Condition condition() const;
    FileNameTextStyle style() const;

    FileNameHighlightRule m_rule;

    QLineEdit *m_name;
    QComboBox *m_condition;
    QComboBox *m_appliesTo;
    int m_appliesToChoice = 0; // index chosen before the Executable condition fixed it
    QLabel *m_patternsLabel;
    QWidget *m_patternsWidget;
    QLineEdit *m_patterns;
    QPushButton *m_chooseMimeTypes;
    QLabel *m_patternsWarning;
    QCheckBox *m_caseSensitive;
    QRadioButton *m_colorUnchanged;
    QRadioButton *m_colorFromScheme;
    QRadioButton *m_colorCustom;
    QComboBox *m_colorRole;
    KColorButton *m_customColor;
    QComboBox *m_bold;
    QComboBox *m_italic;
    QLabel *m_preview;
    QDialogButtonBox *m_buttonBox;

    friend class FileNameHighlightingTabTest; // For unit testing
};

#endif
