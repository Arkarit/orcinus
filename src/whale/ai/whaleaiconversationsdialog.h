/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAICONVERSATIONSDIALOG_H
#define WHALEAICONVERSATIONSDIALOG_H

#include "whaleaiconversationstore.h"

#include <QDialog>

class QLineEdit;
class QPushButton;
class QTreeWidget;

/**
 * @brief All saved conversations of the AI panel (AI14b): search, open, delete.
 *
 * The search looks at the titles, the agents and what was written. Accepted with the conversation
 * to open in chosenId().
 */
class WhaleAiConversationsDialog : public QDialog
{
    Q_OBJECT

public:
    /** @p currentId: the conversation the panel shows; it cannot be opened again or deleted. */
    explicit WhaleAiConversationsDialog(const WhaleAiConversationStore &store, const QString &currentId, QWidget *parent = nullptr);

    QString chosenId() const;

private:
    void fill();
    void filter();
    void updateButtons();
    void openCurrent();
    void removeCurrent();
    void removeAll();
    void exportCurrent(bool pdf);

    WhaleAiConversationStore m_store;
    QString m_currentId;
    QString m_chosenId;
    QList<WhaleAiConversation> m_conversations;
    QStringList m_searchTexts; ///< per conversation, lower case
    QLineEdit *m_search = nullptr;
    QTreeWidget *m_list = nullptr;
    QPushButton *m_open = nullptr;
    QPushButton *m_remove = nullptr;
    QPushButton *m_removeAll = nullptr;
    QPushButton *m_exportPdf = nullptr;
    QPushButton *m_exportMarkdown = nullptr;
};

#endif
