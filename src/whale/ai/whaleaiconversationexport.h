/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAICONVERSATIONEXPORT_H
#define WHALEAICONVERSATIONEXPORT_H

#include "whaleaiconversationstore.h"

#include <QDialog>

class QCheckBox;
class QWidget;

/**
 * @brief Exports a saved conversation of the AI panel as Markdown or PDF (AI15).
 *
 * Both show the conversation for reading: a head with the title, the agent and the folder, the
 * user's messages and the agent's answers (Markdown as it is). With Options::actions also what
 * the agent did and thought and the panel's notices; with Options::times each entry's time.
 * Permission requests with their answer and errors are always there. The PDF is neutral (black on
 * white, a little blue), A4, with page numbers. Text is plain text, for anywhere: the agent's
 * Markdown as the text it shows (lists keep their marks), each entry under a line of who wrote it.
 */
namespace WhaleAiConversationExport
{
enum class Format {
    Pdf,
    Markdown,
    Text,
};

struct Options {
    bool actions = false;
    bool times = true;
};

QString toMarkdown(const WhaleAiConversation &conversation, const Options &options);
QString toText(const WhaleAiConversation &conversation, const Options &options);
/**
 * @return @p entries (records of WhaleAiTranscript) as plain text, without a head: what toText()
 *         writes below it, also for copying a part of the transcript. @p agentName names the
 *         agent's answers.
 */
QString entriesToText(const QJsonArray &entries, const QString &agentName, const Options &options);
/** @return The text the user reads of the Markdown @p markdown: paragraphs apart, list marks kept. */
QString markdownText(const QString &markdown);
/** @return The HTML the PDF is made of (for QTextDocument). */
QString toHtml(const WhaleAiConversation &conversation, const Options &options);
/** Writes @p conversation to @p path. @return False with @p error on failure. */
bool write(const WhaleAiConversation &conversation, Format format, const Options &options, const QString &path, QString *error = nullptr);
/** @return A file name from the title, e.g. "Sort my pictures.pdf". */
QString fileName(const WhaleAiConversation &conversation, Format format);

/**
 * Asks what goes in (ExportDialog), then where, and writes it. Reports a failure in a message box.
 * @return Whether a file was written.
 */
bool exportInteractively(const WhaleAiConversation &conversation, Format format, QWidget *parent);
}

/** @brief The short dialog before the export: what goes in. Remembers the last choice. */
class WhaleAiExportDialog : public QDialog
{
    Q_OBJECT

public:
    explicit WhaleAiExportDialog(WhaleAiConversationExport::Format format, QWidget *parent = nullptr);

    WhaleAiConversationExport::Options options() const;

    void accept() override;

private:
    QCheckBox *m_actions = nullptr;
    QCheckBox *m_times = nullptr;
};

#endif
