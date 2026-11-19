/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaimemorywidget.h"
#include "whaleaiagents.h"

#include <KLocalizedString>
#include <KMessageBox>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace
{
const int IdRole = Qt::UserRole;
}

WhaleAiMemoryWidget::WhaleAiMemoryWidget(const WhaleAiMemory &memory, QWidget *parent)
    : QGroupBox(i18nc("@title:group", "Memory"), parent)
    , m_memory(memory)
{
    setObjectName(QStringLiteral("memoryGroup"));
    m_mode = new QComboBox(this);
    m_mode->setObjectName(QStringLiteral("memoryMode"));
    m_mode->addItem(i18nc("@item:inlistbox memory of the AI agents", "Off"), int(WhaleAiAgents::MemoryMode::Off));
    m_mode->addItem(i18nc("@item:inlistbox memory of the AI agents", "Manual: the agent proposes, you keep"), int(WhaleAiAgents::MemoryMode::Manual));
    m_mode->addItem(i18nc("@item:inlistbox memory of the AI agents", "Automatic: the agent keeps notes itself"), int(WhaleAiAgents::MemoryMode::Automatic));
    m_mode->setCurrentIndex(m_mode->findData(int(WhaleAiAgents::memoryMode())));
    connect(m_mode, &QComboBox::currentIndexChanged, this, &WhaleAiMemoryWidget::changed);

    auto *hint = new QLabel(i18nc("@info",
                                  "Notes the agents get in later conversations: global ones everywhere, folder notes in that folder and "
                                  "below. Agents can also look through your saved conversations. You see every note an agent keeps, and "
                                  "notes never permit anything. In the AI panel, “/remember …” keeps a note for the folder you are in, "
                                  "“/remember global …” one for everywhere."),
                            this);
    hint->setWordWrap(true);

    m_list = new QTreeWidget(this);
    m_list->setObjectName(QStringLiteral("notes"));
    m_list->setHeaderLabels({i18nc("@title:column where a note applies", "Where"), i18nc("@title:column", "Note"), i18nc("@title:column", "Changed")});
    m_list->setRootIsDecorated(false);
    m_list->setAllColumnsShowFocus(true);
    m_list->setWordWrap(true);
    m_list->header()->setStretchLastSection(false);
    m_list->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_list->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_list->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_list->setColumnWidth(0, 200);
    m_list->setMinimumHeight(120);

    m_add = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), i18nc("@action:button", "Add…"), this);
    m_add->setObjectName(QStringLiteral("addNote"));
    m_edit = new QPushButton(QIcon::fromTheme(QStringLiteral("document-edit")), i18nc("@action:button", "Edit…"), this);
    m_edit->setObjectName(QStringLiteral("editNote"));
    m_remove = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-delete")), i18nc("@action:button", "Delete"), this);
    m_remove->setObjectName(QStringLiteral("removeNote"));
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_add);
    buttons->addWidget(m_edit);
    buttons->addWidget(m_remove);
    buttons->addStretch();

    auto *form = new QFormLayout;
    form->addRow(i18nc("@label:listbox", "Memory:"), m_mode);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(hint);
    layout->addWidget(m_list, 1);
    layout->addLayout(buttons);

    connect(m_list, &QTreeWidget::currentItemChanged, this, &WhaleAiMemoryWidget::updateButtons);
    connect(m_list, &QTreeWidget::itemActivated, this, &WhaleAiMemoryWidget::editCurrent);
    connect(m_add, &QPushButton::clicked, this, &WhaleAiMemoryWidget::addNote);
    connect(m_edit, &QPushButton::clicked, this, &WhaleAiMemoryWidget::editCurrent);
    connect(m_remove, &QPushButton::clicked, this, &WhaleAiMemoryWidget::removeCurrent);
    fill();
}

void WhaleAiMemoryWidget::apply()
{
    WhaleAiAgents::setMemoryMode(WhaleAiAgents::MemoryMode(m_mode->currentData().toInt()));
}

void WhaleAiMemoryWidget::restoreDefaults()
{
    // Orcinus' default for new users; the notes are the user's and stay.
    m_mode->setCurrentIndex(m_mode->findData(int(WhaleAiAgents::MemoryMode::Manual)));
}

void WhaleAiMemoryWidget::fill()
{
    const QString current = m_list->currentItem() ? m_list->currentItem()->data(0, IdRole).toString() : QString();
    m_list->clear();
    for (const WhaleAiNote &note : m_memory.notes()) {
        auto *item = new QTreeWidgetItem(m_list);
        if (note.isGlobal()) {
            item->setText(0, i18nc("@item a note of the memory that applies everywhere", "Global"));
        } else {
            item->setText(0, note.folder);
            item->setToolTip(0, note.folder);
            if (!QFileInfo(note.folder).isDir()) {
                // Moved or deleted: the note waits for nothing.
                item->setText(0, i18nc("@item %1 is a folder that is gone", "%1 (gone)", note.folder));
                item->setForeground(0, palette().brush(QPalette::Disabled, QPalette::Text));
            }
        }
        item->setText(1, note.text);
        item->setToolTip(1, note.byAgent ? i18nc("@info:tooltip", "Kept by an agent") : i18nc("@info:tooltip", "Written by you"));
        item->setText(2, QLocale().toString(note.updated, QLocale::ShortFormat));
        item->setData(0, IdRole, note.id);
        if (note.id == current) {
            m_list->setCurrentItem(item);
        }
    }
    updateButtons();
}

void WhaleAiMemoryWidget::updateButtons()
{
    m_edit->setEnabled(m_list->currentItem());
    m_remove->setEnabled(m_list->currentItem());
}

void WhaleAiMemoryWidget::addNote()
{
    WhaleAiNoteDialog dialog(QString(), QString(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    QString error;
    if (!m_memory.add(dialog.text(), dialog.folder(), QString(), false, &error)) {
        KMessageBox::error(this, i18nc("@info %1 is the reason", "The note could not be kept: %1", error));
    }
    fill();
}

void WhaleAiMemoryWidget::editCurrent()
{
    const QTreeWidgetItem *item = m_list->currentItem();
    const std::optional<WhaleAiNote> note = item ? m_memory.note(item->data(0, IdRole).toString()) : std::nullopt;
    if (!note) {
        fill();
        return;
    }
    WhaleAiNoteDialog dialog(note->text, note->folder, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    QString error;
    if (!m_memory.update(note->id, dialog.text(), dialog.folder(), &error)) {
        KMessageBox::error(this, i18nc("@info %1 is the reason", "The note could not be changed: %1", error));
    }
    fill();
}

void WhaleAiMemoryWidget::removeCurrent()
{
    const QTreeWidgetItem *item = m_list->currentItem();
    if (!item) {
        return;
    }
    const int row = m_list->indexOfTopLevelItem(const_cast<QTreeWidgetItem *>(item));
    m_memory.remove(item->data(0, IdRole).toString());
    fill();
    m_list->setCurrentItem(m_list->topLevelItem(std::min(row, m_list->topLevelItemCount() - 1)));
}

WhaleAiNoteDialog::WhaleAiNoteDialog(const QString &text, const QString &folder, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(text.isEmpty() ? i18nc("@title:window", "Add Note") : i18nc("@title:window", "Edit Note"));
    m_global = new QRadioButton(i18nc("@option:radio a note of the memory", "Everywhere"), this);
    m_global->setObjectName(QStringLiteral("global"));
    m_inFolder = new QRadioButton(i18nc("@option:radio a note of the memory", "In this folder and below:"), this);
    m_inFolder->setObjectName(QStringLiteral("inFolder"));
    m_folder = new QLineEdit(folder, this);
    m_folder->setObjectName(QStringLiteral("folder"));
    m_folder->setPlaceholderText(QDir::homePath());
    m_browse = new QPushButton(QIcon::fromTheme(QStringLiteral("document-open-folder")), QString(), this);
    m_browse->setToolTip(i18nc("@info:tooltip", "Choose the folder"));
    (folder.isEmpty() && !text.isEmpty() ? m_global : m_inFolder)->setChecked(true);
    m_text = new QPlainTextEdit(text, this);
    m_text->setObjectName(QStringLiteral("text"));
    m_text->setTabChangesFocus(true);
    m_text->setPlaceholderText(i18nc("@info:placeholder", "e.g. Invoices go into one folder per year; PDFs keep their names."));
    m_count = new QLabel(this);
    m_count->setForegroundRole(QPalette::PlaceholderText);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_ok = buttons->button(QDialogButtonBox::Ok);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *folderRow = new QHBoxLayout;
    folderRow->addWidget(m_folder, 1);
    folderRow->addWidget(m_browse);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_global);
    layout->addWidget(m_inFolder);
    layout->addLayout(folderRow);
    layout->addWidget(m_text, 1);
    layout->addWidget(m_count, 0, Qt::AlignRight);
    layout->addWidget(buttons);

    connect(m_global, &QRadioButton::toggled, this, &WhaleAiNoteDialog::updateState);
    connect(m_folder, &QLineEdit::textChanged, this, &WhaleAiNoteDialog::updateState);
    connect(m_text, &QPlainTextEdit::textChanged, this, &WhaleAiNoteDialog::updateState);
    connect(m_browse, &QPushButton::clicked, this, [this] {
        const QString chosen = QFileDialog::getExistingDirectory(this, i18nc("@title:window", "Folder of the Note"), m_folder->text());
        if (!chosen.isEmpty()) {
            m_folder->setText(chosen);
        }
    });
    updateState();
    resize(480, 300);
}

QString WhaleAiNoteDialog::text() const
{
    return m_text->toPlainText().trimmed();
}

QString WhaleAiNoteDialog::folder() const
{
    return m_global->isChecked() ? QString() : QDir::cleanPath(m_folder->text().trimmed());
}

void WhaleAiNoteDialog::updateState()
{
    m_folder->setEnabled(m_inFolder->isChecked());
    m_browse->setEnabled(m_inFolder->isChecked());
    const int length = text().size();
    m_count->setText(i18nc("@info %1 of %2 characters", "%1 / %2", length, WhaleAiMemory::MaxNoteLength));
    const bool folderOk = m_global->isChecked() || QDir::isAbsolutePath(m_folder->text().trimmed());
    m_ok->setEnabled(length > 0 && length <= WhaleAiMemory::MaxNoteLength && folderOk);
}
