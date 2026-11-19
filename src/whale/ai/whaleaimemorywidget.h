/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAIMEMORYWIDGET_H
#define WHALEAIMEMORYWIDGET_H

#include "whaleaimemory.h"

#include <QDialog>
#include <QGroupBox>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QTreeWidget;

/**
 * @brief The "Memory" group of the AI settings (AI16e): the mode and the notes.
 *
 * The mode is stored with apply(), like the rest of the page; notes are added, edited and deleted
 * at once (they are not settings, and the panel changes them too).
 */
class WhaleAiMemoryWidget : public QGroupBox
{
    Q_OBJECT

public:
    explicit WhaleAiMemoryWidget(const WhaleAiMemory &memory = WhaleAiMemory(), QWidget *parent = nullptr);

    void apply();
    void restoreDefaults();
    /** Reads the notes again. */
    void fill();

Q_SIGNALS:
    /** The mode was changed (not stored yet). */
    void changed();

private:
    void updateButtons();
    void addNote();
    void editCurrent();
    void removeCurrent();

    WhaleAiMemory m_memory;
    QComboBox *m_mode = nullptr;
    QTreeWidget *m_list = nullptr;
    QPushButton *m_add = nullptr;
    QPushButton *m_edit = nullptr;
    QPushButton *m_remove = nullptr;
};

/** @brief Writes or changes a note of the memory: where it applies and its text. */
class WhaleAiNoteDialog : public QDialog
{
    Q_OBJECT

public:
    /** @p folder empty: global. */
    WhaleAiNoteDialog(const QString &text, const QString &folder, QWidget *parent = nullptr);

    QString text() const;
    /** @return The folder, empty for a global note. */
    QString folder() const;

private:
    void updateState();

    QRadioButton *m_global = nullptr;
    QRadioButton *m_inFolder = nullptr;
    QLineEdit *m_folder = nullptr;
    QPushButton *m_browse = nullptr;
    QPlainTextEdit *m_text = nullptr;
    QLabel *m_count = nullptr;
    QPushButton *m_ok = nullptr;
};

#endif
