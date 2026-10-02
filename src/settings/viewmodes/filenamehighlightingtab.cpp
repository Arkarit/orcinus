/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "filenamehighlightingtab.h"

#include "filenamehighlightingruledialog.h"
#include "views/filenamehighlightingsettings.h"

#include <KLocalizedString>

#include <QCheckBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QTreeView>
#include <QVBoxLayout>

FileNameHighlightingRuleModel::FileNameHighlightingRuleModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

QList<FileNameHighlightRule> FileNameHighlightingRuleModel::rules() const
{
    return m_rules;
}

void FileNameHighlightingRuleModel::setRules(const QList<FileNameHighlightRule> &rules)
{
    beginResetModel();
    m_rules = rules;
    endResetModel();
}

FileNameHighlightRule FileNameHighlightingRuleModel::rule(int row) const
{
    return m_rules.value(row);
}

void FileNameHighlightingRuleModel::setRule(int row, const FileNameHighlightRule &rule)
{
    if (row < 0 || row >= m_rules.count()) {
        return;
    }
    m_rules[row] = rule;
    Q_EMIT dataChanged(index(row, 0), index(row, ColumnCount - 1));
}

void FileNameHighlightingRuleModel::addRule(const FileNameHighlightRule &rule)
{
    beginInsertRows(QModelIndex(), m_rules.count(), m_rules.count());
    m_rules.append(rule);
    endInsertRows();
}

void FileNameHighlightingRuleModel::removeRule(int row)
{
    if (row < 0 || row >= m_rules.count()) {
        return;
    }
    beginRemoveRows(QModelIndex(), row, row);
    m_rules.removeAt(row);
    endRemoveRows();
}

void FileNameHighlightingRuleModel::moveRule(int from, int to)
{
    if (from < 0 || from >= m_rules.count() || to < 0 || to >= m_rules.count() || from == to) {
        return;
    }
    // QAbstractItemModel expects the destination before which the row is inserted.
    beginMoveRows(QModelIndex(), from, from, QModelIndex(), to > from ? to + 1 : to);
    m_rules.move(from, to);
    endMoveRows();
}

void FileNameHighlightingRuleModel::setPreviewAppearance(const QPalette &palette, const QFont &font)
{
    m_palette = palette;
    m_font = font;
    if (!m_rules.isEmpty()) {
        Q_EMIT dataChanged(index(0, PreviewColumn), index(m_rules.count() - 1, PreviewColumn));
    }
}

int FileNameHighlightingRuleModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rules.count();
}

int FileNameHighlightingRuleModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant FileNameHighlightingRuleModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rules.count()) {
        return QVariant();
    }

    const FileNameHighlightRule &rule = m_rules.at(index.row());
    switch (index.column()) {
    case RuleColumn:
        if (role == Qt::DisplayRole) {
            return FileNameHighlightingRuleDialog::displayName(rule);
        } else if (role == Qt::CheckStateRole) {
            return rule.enabled ? Qt::Checked : Qt::Unchecked;
        }
        break;
    case PreviewColumn:
        if (role == Qt::DisplayRole) {
            return FileNameHighlightingRuleDialog::exampleFileName(rule);
        } else if (role == Qt::FontRole) {
            return FileNameHighlightingRuleDialog::previewFont(rule.style, m_font);
        } else if (role == Qt::ForegroundRole) {
            return FileNameHighlightingRuleDialog::previewColor(rule.style, m_palette);
        }
        break;
    }
    return QVariant();
}

bool FileNameHighlightingRuleModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.column() != RuleColumn || role != Qt::CheckStateRole || index.row() >= m_rules.count()) {
        return false;
    }
    m_rules[index.row()].enabled = value.toInt() == Qt::Checked;
    Q_EMIT dataChanged(index, index.siblingAtColumn(PreviewColumn));
    return true;
}

Qt::ItemFlags FileNameHighlightingRuleModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags flags = QAbstractTableModel::flags(index);
    if (index.isValid() && index.column() == RuleColumn) {
        flags |= Qt::ItemIsUserCheckable;
    }
    return flags;
}

QVariant FileNameHighlightingRuleModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QVariant();
    }
    switch (section) {
    case RuleColumn:
        return i18nc("@title:column file name highlighting rule", "Rule");
    case PreviewColumn:
        return i18nc("@title:column", "Preview");
    }
    return QVariant();
}

FileNameHighlightingTab::FileNameHighlightingTab(QWidget *parent)
    : SettingsPageBase(parent)
    , m_enabled(nullptr)
    , m_ruleView(nullptr)
    , m_ruleModel(new FileNameHighlightingRuleModel(this))
    , m_addButton(nullptr)
    , m_editButton(nullptr)
    , m_removeButton(nullptr)
    , m_moveUpButton(nullptr)
    , m_moveDownButton(nullptr)
{
    auto topLayout = new QVBoxLayout(this);

    m_enabled = new QCheckBox(i18nc("@option:check", "Highlight file names"), this);
    topLayout->addWidget(m_enabled);

    auto rulesLayout = new QHBoxLayout();
    topLayout->addLayout(rulesLayout, 1);

    m_ruleView = new QTreeView(this);
    m_ruleView->setModel(m_ruleModel);
    m_ruleView->setRootIsDecorated(false);
    m_ruleView->setUniformRowHeights(true);
    m_ruleView->setAllColumnsShowFocus(true);
    m_ruleView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_ruleView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_ruleView->header()->setStretchLastSection(false);
    m_ruleView->header()->setSectionResizeMode(FileNameHighlightingRuleModel::RuleColumn, QHeaderView::Stretch);
    m_ruleView->header()->setSectionResizeMode(FileNameHighlightingRuleModel::PreviewColumn, QHeaderView::ResizeToContents);
    rulesLayout->addWidget(m_ruleView, 1);

    auto buttonsLayout = new QVBoxLayout();
    rulesLayout->addLayout(buttonsLayout);
    m_addButton = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), i18nc("@action:button", "Add…"), this);
    m_editButton = new QPushButton(QIcon::fromTheme(QStringLiteral("document-edit")), i18nc("@action:button", "Edit…"), this);
    m_removeButton = new QPushButton(QIcon::fromTheme(QStringLiteral("list-remove")), i18nc("@action:button", "Remove"), this);
    m_moveUpButton = new QPushButton(QIcon::fromTheme(QStringLiteral("go-up")), i18nc("@action:button", "Move Up"), this);
    m_moveDownButton = new QPushButton(QIcon::fromTheme(QStringLiteral("go-down")), i18nc("@action:button", "Move Down"), this);
    buttonsLayout->addWidget(m_addButton);
    buttonsLayout->addWidget(m_editButton);
    buttonsLayout->addWidget(m_removeButton);
    buttonsLayout->addWidget(m_moveUpButton);
    buttonsLayout->addWidget(m_moveDownButton);
    buttonsLayout->addStretch();

    auto hint = new QLabel(i18nc("@info", "Rules higher in the list take precedence."), this);
    hint->setWordWrap(true);
    topLayout->addWidget(hint);

    m_ruleModel->setPreviewAppearance(palette(), font());

    const FileNameHighlightingSettings settings = FileNameHighlightingSettings::load();
    loadSettings(settings.enabled, settings.rules);

    connect(m_enabled, &QCheckBox::toggled, this, [this]() {
        updateButtons();
        Q_EMIT changed();
    });
    connect(m_ruleModel, &QAbstractItemModel::dataChanged, this, [this](const QModelIndex &topLeft) {
        // Changes of the preview appearance alone are no changes of the settings.
        if (topLeft.column() == FileNameHighlightingRuleModel::RuleColumn) {
            Q_EMIT changed();
        }
    });
    connect(m_ruleView->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &FileNameHighlightingTab::updateButtons);
    connect(m_ruleView, &QTreeView::doubleClicked, this, &FileNameHighlightingTab::editRule);
    connect(m_addButton, &QPushButton::clicked, this, &FileNameHighlightingTab::addRule);
    connect(m_editButton, &QPushButton::clicked, this, &FileNameHighlightingTab::editRule);
    connect(m_removeButton, &QPushButton::clicked, this, &FileNameHighlightingTab::removeRule);
    connect(m_moveUpButton, &QPushButton::clicked, this, [this]() {
        moveRule(-1);
    });
    connect(m_moveDownButton, &QPushButton::clicked, this, [this]() {
        moveRule(1);
    });
}

void FileNameHighlightingTab::applySettings()
{
    FileNameHighlightingSettings settings;
    settings.enabled = m_enabled->isChecked();
    settings.rules = m_ruleModel->rules();
    settings.save();
}

void FileNameHighlightingTab::restoreDefaults()
{
    const FileNameHighlightingSettings defaults = FileNameHighlightingSettings::defaults();
    loadSettings(defaults.enabled, defaults.rules);
    Q_EMIT changed();
}

FileNameHighlightingRuleModel *FileNameHighlightingTab::ruleModel() const
{
    return m_ruleModel;
}

void FileNameHighlightingTab::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::FontChange) {
        m_ruleModel->setPreviewAppearance(palette(), font());
    }
    SettingsPageBase::changeEvent(event);
}

void FileNameHighlightingTab::loadSettings(bool enabled, const QList<FileNameHighlightRule> &rules)
{
    const QSignalBlocker blocker(m_enabled);
    m_enabled->setChecked(enabled);
    m_ruleModel->setRules(rules);
    // No row is selected initially, so that all previews show their colors.
    selectRow(-1);
    updateButtons();
}

void FileNameHighlightingTab::updateButtons()
{
    const bool enabled = m_enabled->isChecked();
    const int row = currentRow();
    const int count = m_ruleModel->rowCount();

    m_ruleView->setEnabled(enabled);
    m_addButton->setEnabled(enabled);
    m_editButton->setEnabled(enabled && row >= 0);
    m_removeButton->setEnabled(enabled && row >= 0);
    m_moveUpButton->setEnabled(enabled && row > 0);
    m_moveDownButton->setEnabled(enabled && row >= 0 && row < count - 1);
}

int FileNameHighlightingTab::currentRow() const
{
    const QModelIndex current = m_ruleView->currentIndex();
    return current.isValid() ? current.row() : -1;
}

void FileNameHighlightingTab::selectRow(int row)
{
    if (row < 0 || row >= m_ruleModel->rowCount()) {
        m_ruleView->setCurrentIndex(QModelIndex());
        return;
    }
    m_ruleView->setCurrentIndex(m_ruleModel->index(row, FileNameHighlightingRuleModel::RuleColumn));
}

void FileNameHighlightingTab::addRule()
{
    FileNameHighlightRule rule;
    rule.condition = FileNameHighlightRule::Condition::MimeType;

    QPointer<FileNameHighlightingRuleDialog> dialog = new FileNameHighlightingRuleDialog(rule, this);
    if (dialog->exec() == QDialog::Accepted && dialog) {
        m_ruleModel->addRule(dialog->rule());
        selectRow(m_ruleModel->rowCount() - 1);
        updateButtons();
        Q_EMIT changed();
    }
    delete dialog;
}

void FileNameHighlightingTab::editRule()
{
    const int row = currentRow();
    if (row < 0 || !m_enabled->isChecked()) {
        return;
    }

    QPointer<FileNameHighlightingRuleDialog> dialog = new FileNameHighlightingRuleDialog(m_ruleModel->rule(row), this);
    if (dialog->exec() == QDialog::Accepted && dialog) {
        m_ruleModel->setRule(row, dialog->rule());
    }
    delete dialog;
}

void FileNameHighlightingTab::removeRule()
{
    const int row = currentRow();
    if (row < 0) {
        return;
    }
    m_ruleModel->removeRule(row);
    selectRow(qMin(row, m_ruleModel->rowCount() - 1));
    updateButtons();
    Q_EMIT changed();
}

void FileNameHighlightingTab::moveRule(int offset)
{
    const int row = currentRow();
    const int target = row + offset;
    if (row < 0 || target < 0 || target >= m_ruleModel->rowCount()) {
        return;
    }
    m_ruleModel->moveRule(row, target);
    selectRow(target);
    updateButtons();
    Q_EMIT changed();
}

#include "moc_filenamehighlightingtab.cpp"
