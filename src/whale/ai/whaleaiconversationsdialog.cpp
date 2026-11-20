/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaiconversationsdialog.h"
#include "whaleaiconversationexport.h"

#include <KLocalizedString>
#include <KMessageBox>

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonObject>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <tuple>

namespace
{
const int IdRole = Qt::UserRole;
}

WhaleAiConversationsDialog::WhaleAiConversationsDialog(const WhaleAiConversationStore &store, const QString &currentId, QWidget *parent)
    : QDialog(parent)
    , m_store(store)
    , m_currentId(currentId)
{
    setWindowTitle(i18nc("@title:window", "Saved Conversations"));

    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("search"));
    m_search->setPlaceholderText(i18nc("@info:placeholder", "Search titles, agents and messages…"));
    m_search->setClearButtonEnabled(true);

    m_list = new QTreeWidget(this);
    m_list->setObjectName(QStringLiteral("conversations"));
    m_list->setHeaderLabels({i18nc("@title:column", "Conversation"), i18nc("@title:column", "Agent"), i18nc("@title:column", "Last Message")});
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setAllColumnsShowFocus(true);
    m_list->header()->setStretchLastSection(false);
    m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_list->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_list->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    m_open = new QPushButton(QIcon::fromTheme(QStringLiteral("document-open")), i18nc("@action:button", "Open"), this);
    m_open->setObjectName(QStringLiteral("open"));
    m_remove = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-delete")), i18nc("@action:button", "Delete"), this);
    m_remove->setObjectName(QStringLiteral("remove"));
    m_removeAll = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-clear-history")), i18nc("@action:button", "Delete All…"), this);
    m_removeAll->setObjectName(QStringLiteral("removeAll"));
    // One button for the formats: the row stays short.
    m_export = new QPushButton(QIcon::fromTheme(QStringLiteral("document-export")), i18nc("@action:button", "Export"), this);
    m_export->setObjectName(QStringLiteral("export"));
    auto *exportMenu = new QMenu(m_export);
    using Format = WhaleAiConversationExport::Format;
    const QList<std::tuple<Format, QString, QString, QString>> formats{
        {Format::Pdf, QStringLiteral("application-pdf"), i18nc("@action:inmenu", "As PDF…"), QStringLiteral("exportPdf")},
        {Format::Markdown, QStringLiteral("text-markdown"), i18nc("@action:inmenu", "As Markdown…"), QStringLiteral("exportMarkdown")},
        {Format::Text, QStringLiteral("text-plain"), i18nc("@action:inmenu", "As Text…"), QStringLiteral("exportText")},
    };
    for (const auto &[format, icon, text, name] : formats) {
        QAction *action = exportMenu->addAction(QIcon::fromTheme(icon), text);
        action->setObjectName(name);
        connect(action, &QAction::triggered, this, [this, format] {
            exportCurrent(format);
        });
    }
    m_export->setMenu(exportMenu);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *row = new QHBoxLayout;
    row->addWidget(m_open);
    row->addWidget(m_remove);
    row->addWidget(m_export);
    row->addStretch();
    row->addWidget(m_removeAll);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_search);
    layout->addWidget(m_list, 1);
    layout->addLayout(row);
    layout->addWidget(buttons);

    connect(m_search, &QLineEdit::textChanged, this, &WhaleAiConversationsDialog::filter);
    connect(m_list, &QTreeWidget::currentItemChanged, this, &WhaleAiConversationsDialog::updateButtons);
    connect(m_list, &QTreeWidget::itemActivated, this, &WhaleAiConversationsDialog::openCurrent);
    connect(m_open, &QPushButton::clicked, this, &WhaleAiConversationsDialog::openCurrent);
    connect(m_remove, &QPushButton::clicked, this, &WhaleAiConversationsDialog::removeCurrent);
    connect(m_removeAll, &QPushButton::clicked, this, &WhaleAiConversationsDialog::removeAll);

    fill();
    resize(640, 420);
}

QString WhaleAiConversationsDialog::chosenId() const
{
    return m_chosenId;
}

void WhaleAiConversationsDialog::fill()
{
    m_list->clear();
    // With the entries: the search goes through what was written, too.
    m_conversations = m_store.list(true);
    m_searchTexts.clear();
    for (const WhaleAiConversation &conversation : std::as_const(m_conversations)) {
        QStringList texts{conversation.title, conversation.agentName};
        for (const QJsonValue &entry : conversation.entries) {
            texts.append(entry.toObject().value(QLatin1String("text")).toString());
        }
        m_searchTexts.append(texts.join(QLatin1Char('\n')).toLower());

        auto *item = new QTreeWidgetItem(m_list);
        item->setText(0, conversation.title);
        item->setToolTip(0, conversation.folder);
        item->setText(1, conversation.agentName);
        item->setText(2, QLocale().toString(conversation.updated, QLocale::ShortFormat));
        item->setData(0, IdRole, conversation.id);
        if (conversation.id == m_currentId) {
            item->setText(0, i18nc("@item %1 is the title of the conversation the AI panel shows", "%1 (open)", conversation.title));
        }
    }
    m_list->setCurrentItem(m_list->topLevelItem(0));
    filter();
}

void WhaleAiConversationsDialog::filter()
{
    const QStringList words = m_search->text().toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        const QString &text = m_searchTexts.at(i);
        const bool match = std::all_of(words.cbegin(), words.cend(), [&text](const QString &word) {
            return text.contains(word);
        });
        m_list->topLevelItem(i)->setHidden(!match);
    }
    QTreeWidgetItem *current = m_list->currentItem();
    if (!current || current->isHidden()) {
        m_list->setCurrentItem(nullptr);
        for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
            if (!m_list->topLevelItem(i)->isHidden()) {
                m_list->setCurrentItem(m_list->topLevelItem(i));
                break;
            }
        }
    }
    updateButtons();
}

void WhaleAiConversationsDialog::updateButtons()
{
    const QTreeWidgetItem *current = m_list->currentItem();
    const bool chosen = current && !current->isHidden() && current->data(0, IdRole).toString() != m_currentId;
    m_open->setEnabled(chosen);
    m_remove->setEnabled(chosen);
    // The open one, too: it is saved as it is.
    const bool selected = current && !current->isHidden();
    m_export->setEnabled(selected);
    m_removeAll->setEnabled(m_list->topLevelItemCount() > (m_currentId.isEmpty() ? 0 : 1));
}

void WhaleAiConversationsDialog::openCurrent()
{
    if (!m_open->isEnabled()) {
        return;
    }
    m_chosenId = m_list->currentItem()->data(0, IdRole).toString();
    accept();
}

void WhaleAiConversationsDialog::removeCurrent()
{
    if (!m_remove->isEnabled()) {
        return;
    }
    const int row = m_list->indexOfTopLevelItem(m_list->currentItem());
    m_store.remove(m_list->currentItem()->data(0, IdRole).toString());
    fill();
    // The next one, where the deleted one was.
    if (QTreeWidgetItem *next = m_list->topLevelItem(std::min(row, m_list->topLevelItemCount() - 1)); next && !next->isHidden()) {
        m_list->setCurrentItem(next);
    }
}

void WhaleAiConversationsDialog::exportCurrent(WhaleAiConversationExport::Format format)
{
    const QTreeWidgetItem *current = m_list->currentItem();
    if (!current || current->isHidden()) {
        return;
    }
    if (const std::optional<WhaleAiConversation> conversation = m_store.load(current->data(0, IdRole).toString())) {
        WhaleAiConversationExport::exportInteractively(*conversation, format, this);
    }
}

void WhaleAiConversationsDialog::removeAll()
{
    const int answer =
        KMessageBox::warningContinueCancel(this,
                                           m_currentId.isEmpty() ? i18nc("@info", "Delete all saved conversations? This cannot be undone.")
                                                                 : i18nc("@info", "Delete all saved conversations but the open one? This cannot be undone."),
                                           i18nc("@title:window", "Delete All Conversations"),
                                           KStandardGuiItem::del());
    if (answer != KMessageBox::Continue) {
        return;
    }
    for (const WhaleAiConversation &conversation : std::as_const(m_conversations)) {
        if (conversation.id != m_currentId) {
            m_store.remove(conversation.id);
        }
    }
    fill();
}
