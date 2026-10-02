/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef FILENAMEHIGHLIGHTINGTAB_H
#define FILENAMEHIGHLIGHTINGTAB_H

#include "settings/settingspagebase.h"
#include "views/filenamehighlighter.h"

#include <QAbstractTableModel>
#include <QFont>
#include <QPalette>

class QCheckBox;
class QPushButton;
class QTreeView;

/**
 * @brief Table model of the ordered file name highlighting rules.
 *
 * The first column shows whether a rule is enabled and its name,
 * the second column a preview of its style.
 */
class FileNameHighlightingRuleModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column {
        RuleColumn,
        PreviewColumn,
        ColumnCount,
    };

    explicit FileNameHighlightingRuleModel(QObject *parent = nullptr);

    QList<FileNameHighlightRule> rules() const;
    void setRules(const QList<FileNameHighlightRule> &rules);

    FileNameHighlightRule rule(int row) const;
    void setRule(int row, const FileNameHighlightRule &rule);
    void addRule(const FileNameHighlightRule &rule);
    void removeRule(int row);
    void moveRule(int from, int to);

    /**
     * Sets the palette and font used to preview the rules.
     */
    void setPreviewAppearance(const QPalette &palette, const QFont &font);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private:
    QList<FileNameHighlightRule> m_rules;
    QPalette m_palette;
    QFont m_font;
};

/**
 * @brief Tab of the view settings to configure file name highlighting.
 */
class FileNameHighlightingTab : public SettingsPageBase
{
    Q_OBJECT

public:
    explicit FileNameHighlightingTab(QWidget *parent = nullptr);

    void applySettings() override;
    void restoreDefaults() override;

    FileNameHighlightingRuleModel *ruleModel() const;

protected:
    void changeEvent(QEvent *event) override;

private:
    void loadSettings(bool enabled, const QList<FileNameHighlightRule> &rules);
    void updateButtons();
    int currentRow() const;
    void selectRow(int row);

    void addRule();
    void editRule();
    void removeRule();
    void moveRule(int offset);

    QCheckBox *m_enabled;
    QTreeView *m_ruleView;
    FileNameHighlightingRuleModel *m_ruleModel;
    QPushButton *m_addButton;
    QPushButton *m_editButton;
    QPushButton *m_removeButton;
    QPushButton *m_moveUpButton;
    QPushButton *m_moveDownButton;
};

#endif
