/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaiconversationexport.h"
#include "whaleaiagents.h"

#include <KLocalizedString>
#include <KMessageBox>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLocale>
#include <QPageLayout>
#include <QPageSize>
#include <QPdfWriter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextDocument>
#include <QVBoxLayout>

#include <algorithm>

namespace WhaleAiConversationExport
{
namespace
{
// Neutral: black on white, grey for the minor things, one blue.
const char Accent[] = "#2a5d9f";
const char Minor[] = "#6b7280";
const char UserBackground[] = "#f1f4f8";
const char UserBorder[] = "#d3dae4";
const char ErrorColor[] = "#b42318";

QString kindOf(const QJsonObject &entry)
{
    return entry.value(QLatin1String("kind")).toString();
}

bool isAction(const QString &kind)
{
    return kind == QLatin1String("ToolCall") || kind == QLatin1String("Action") || kind == QLatin1String("Thought") || kind == QLatin1String("Notice");
}

/** @return Whether @p entry goes in with @p options. */
bool included(const QJsonObject &entry, const Options &options)
{
    return options.actions || !isAction(kindOf(entry));
}

QDateTime timeOf(const QJsonObject &entry)
{
    return QDateTime::fromString(entry.value(QLatin1String("time")).toString(), Qt::ISODateWithMs);
}

QString timeText(const QJsonObject &entry)
{
    const QDateTime time = timeOf(entry);
    return time.isValid() ? QLocale().toString(time.time(), QLocale::ShortFormat) : QString();
}

QString agentLine(const WhaleAiConversation &conversation)
{
    QString agent = conversation.agentName;
    if (!conversation.localModel.isEmpty()) {
        agent = i18nc("@info %1 is an AI agent, %2 a language model", "%1 with %2", agent, conversation.localModel);
    }
    return agent;
}

QString periodText(const WhaleAiConversation &conversation)
{
    const QLocale locale;
    if (!conversation.created.isValid()) {
        return locale.toString(conversation.updated, QLocale::ShortFormat);
    }
    if (conversation.created.date() == conversation.updated.date()) {
        return i18nc("@info a period of time: %1 a date, %2 and %3 times",
                     "%1, %2 – %3",
                     locale.toString(conversation.created.date(), QLocale::ShortFormat),
                     locale.toString(conversation.created.time(), QLocale::ShortFormat),
                     locale.toString(conversation.updated.time(), QLocale::ShortFormat));
    }
    return i18nc("@info a period of time",
                 "%1 – %2",
                 locale.toString(conversation.created, QLocale::ShortFormat),
                 locale.toString(conversation.updated, QLocale::ShortFormat));
}

QString toolTitle(const QJsonObject &toolCall)
{
    const QString title = toolCall.value(QLatin1String("title")).toString();
    return title.isEmpty() ? toolCall.value(QLatin1String("kind")).toString() : title;
}

QString statusText(const QString &status)
{
    if (status == QLatin1String("completed")) {
        return i18nc("@info status of a tool call", "done");
    }
    if (status == QLatin1String("failed")) {
        return i18nc("@info status of a tool call", "failed");
    }
    if (status == QLatin1String("cancelled")) {
        return i18nc("@info status of a tool call", "cancelled");
    }
    return i18nc("@info status of a tool call", "not finished");
}

QString permissionAnswer(const QJsonObject &entry)
{
    const QString answer = entry.value(QLatin1String("answer")).toString();
    if (!answer.isEmpty()) {
        return i18nc("@info the option the user chose for a permission request", "Answered: %1", answer);
    }
    return i18nc("@info a permission request of an AI agent", "No longer needed.");
}

/** Markdown of a plain text: line breaks kept, nothing taken as markup. */
QString plainMarkdown(const QString &text)
{
    // What starts markup anywhere; the rest (".", "-", …) only at the start of a line, which is rare.
    static const QRegularExpression special(QStringLiteral("([\\\\`*_\\[\\]<>#|~])"));
    QStringList lines = text.split(QLatin1Char('\n'));
    for (QString &line : lines) {
        line.replace(special, QStringLiteral("\\\\1"));
    }
    return lines.join(QStringLiteral("  \n"));
}

QString fence(const QString &text)
{
    // A fence longer than any run of backticks inside.
    int longest = 2;
    static const QRegularExpression ticks(QStringLiteral("`+"));
    for (auto it = ticks.globalMatch(text); it.hasNext();) {
        longest = std::max<int>(longest, it.next().capturedLength());
    }
    const QString marks(longest + 1, QLatin1Char('`'));
    return marks + QLatin1Char('\n') + text + QLatin1Char('\n') + marks;
}

QString html(const QString &text)
{
    return text.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
}

/** The body of the HTML QTextDocument makes of Markdown. */
QString markdownHtml(const QString &markdown)
{
    QTextDocument document;
    document.setMarkdown(markdown);
    const QString full = document.toHtml();
    const qsizetype start = full.indexOf(QLatin1Char('>'), full.indexOf(QLatin1String("<body"))) + 1;
    const qsizetype end = full.lastIndexOf(QLatin1String("</body>"));
    return start > 0 && end > start ? full.mid(start, end - start) : html(markdown);
}

QString who(const QString &name, const QJsonObject &entry, const Options &options)
{
    const QString time = options.times ? timeText(entry) : QString();
    return time.isEmpty() ? name : name + QStringLiteral(" · ") + time;
}
}

QString toMarkdown(const WhaleAiConversation &conversation, const Options &options)
{
    QStringList parts;
    parts.append(QStringLiteral("# ") + plainMarkdown(conversation.title));
    QStringList meta{agentLine(conversation), periodText(conversation)};
    if (!conversation.folder.isEmpty()) {
        meta.insert(1, QLatin1Char('`') + conversation.folder + QLatin1Char('`'));
    }
    parts.append(QLatin1Char('*') + meta.join(QStringLiteral(" · ")) + QLatin1Char('*'));
    parts.append(QStringLiteral("---"));

    const QString you = i18nc("@title the user in an exported AI conversation", "You");
    for (const QJsonValue &value : conversation.entries) {
        const QJsonObject entry = value.toObject();
        if (!included(entry, options)) {
            continue;
        }
        const QString kind = kindOf(entry);
        const QString text = entry.value(QLatin1String("text")).toString();
        const QString time = options.times ? timeText(entry) : QString();
        const QString stamp = time.isEmpty() ? QString() : QStringLiteral(" · ") + time;
        if (kind == QLatin1String("UserMessage")) {
            QString part = QStringLiteral("### ") + who(you, entry, options) + QStringLiteral("\n\n") + plainMarkdown(text);
            const QString context = entry.value(QLatin1String("context")).toString();
            if (!context.isEmpty()) {
                part += QStringLiteral("\n\n<sub>") + context.toHtmlEscaped() + QStringLiteral("</sub>");
            }
            parts.append(part);
        } else if (kind == QLatin1String("AgentMessage")) {
            parts.append(QStringLiteral("### ") + who(conversation.agentName, entry, options) + QStringLiteral("\n\n") + text.trimmed());
        } else if (kind == QLatin1String("Thought")) {
            parts.append(QStringLiteral("<details><summary>") + i18nc("@title an AI agent's thinking", "Thinking") + stamp.toHtmlEscaped()
                         + QStringLiteral("</summary>\n\n") + plainMarkdown(text.trimmed()) + QStringLiteral("\n\n</details>"));
        } else if (kind == QLatin1String("ToolCall")) {
            const QJsonObject toolCall = entry.value(QLatin1String("toolCall")).toObject();
            QString line = QStringLiteral("- `") + toolTitle(toolCall).replace(QLatin1Char('`'), QLatin1Char('\'')) + QStringLiteral("` — ")
                + statusText(toolCall.value(QLatin1String("status")).toString()) + stamp;
            parts.append(line);
        } else if (kind == QLatin1String("Permission")) {
            const QJsonObject toolCall = entry.value(QLatin1String("toolCall")).toObject();
            parts.append(QStringLiteral("> **") + i18nc("@title an AI agent asks the user", "Permission") + QStringLiteral("**") + stamp + QStringLiteral(": ")
                         + plainMarkdown(toolTitle(toolCall)) + QStringLiteral("  \n> ") + plainMarkdown(permissionAnswer(entry)));
        } else if (kind == QLatin1String("Error")) {
            QString part = QStringLiteral("> **") + i18nc("@title", "Error") + QStringLiteral("**") + stamp + QStringLiteral(": ") + plainMarkdown(text);
            const QString details = entry.value(QLatin1String("details")).toString();
            if (!details.isEmpty()) {
                part += QStringLiteral("\n\n") + fence(details);
            }
            parts.append(part);
        } else {
            // Notices, the agent's actions in the window, memory notes (with what became of them).
            const QString status = entry.value(QLatin1String("status")).toString();
            parts.append(QLatin1Char('*') + plainMarkdown(text) + QLatin1Char('*') + stamp
                         + (status.isEmpty() ? QString() : QStringLiteral(" — ") + plainMarkdown(status)));
        }
    }
    return parts.join(QStringLiteral("\n\n")) + QLatin1Char('\n');
}

QString toHtml(const WhaleAiConversation &conversation, const Options &options)
{
    QString body;
    body += QStringLiteral("<h1 style=\"color:%1\">").arg(QLatin1String(Accent)) + conversation.title.toHtmlEscaped() + QStringLiteral("</h1>");
    QStringList meta{agentLine(conversation).toHtmlEscaped()};
    if (!conversation.folder.isEmpty()) {
        meta.append(QStringLiteral("<tt>") + conversation.folder.toHtmlEscaped() + QStringLiteral("</tt>"));
    }
    meta.append(periodText(conversation).toHtmlEscaped());
    body += QStringLiteral("<p style=\"color:%1\">").arg(QLatin1String(Minor)) + meta.join(QStringLiteral(" · ")) + QStringLiteral("</p>");
    // A fine grey line: QTextDocument draws <hr> black and takes no colour for it.
    body += QStringLiteral(
                "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\" border=\"0\" style=\"background-color:%1; margin-top:4px; margin-bottom:6px\">"
                "<tr><td style=\"font-size:1pt\">&nbsp;</td></tr></table>")
                .arg(QLatin1String(UserBorder));

    const QString you = i18nc("@title the user in an exported AI conversation", "You");
    for (const QJsonValue &value : conversation.entries) {
        const QJsonObject entry = value.toObject();
        if (!included(entry, options)) {
            continue;
        }
        const QString kind = kindOf(entry);
        const QString text = entry.value(QLatin1String("text")).toString();
        const QString time = options.times ? timeText(entry) : QString();
        const QString stamp = time.isEmpty() ? QString() : QStringLiteral(" · ") + time.toHtmlEscaped();
        if (kind == QLatin1String("UserMessage")) {
            body += QStringLiteral(
                        "<table width=\"100%\" cellpadding=\"8\" cellspacing=\"0\" border=\"1\" style=\"border-color:%1; border-style:solid; "
                        "background-color:%2; margin-top:14px\"><tr><td>")
                        .arg(QLatin1String(UserBorder), QLatin1String(UserBackground));
            body +=
                QStringLiteral("<p style=\"color:%1\"><b>").arg(QLatin1String(Accent)) + who(you, entry, options).toHtmlEscaped() + QStringLiteral("</b></p>");
            body += QStringLiteral("<p>") + html(text) + QStringLiteral("</p>");
            const QString context = entry.value(QLatin1String("context")).toString();
            if (!context.isEmpty()) {
                body += QStringLiteral("<p style=\"color:%1; font-size:small\">").arg(QLatin1String(Minor)) + html(context) + QStringLiteral("</p>");
            }
            body += QStringLiteral("</td></tr></table>");
        } else if (kind == QLatin1String("AgentMessage")) {
            body += QStringLiteral("<p style=\"color:%1; margin-top:14px\"><b>").arg(QLatin1String(Accent))
                + who(conversation.agentName, entry, options).toHtmlEscaped() + QStringLiteral("</b></p>");
            body += markdownHtml(text);
        } else if (kind == QLatin1String("Thought")) {
            body += QStringLiteral("<p style=\"color:%1; font-size:small; margin-left:16px\"><i>").arg(QLatin1String(Minor))
                + i18nc("@title an AI agent's thinking", "Thinking").toHtmlEscaped() + stamp + QStringLiteral(":</i> ") + html(text.trimmed())
                + QStringLiteral("</p>");
        } else if (kind == QLatin1String("ToolCall")) {
            const QJsonObject toolCall = entry.value(QLatin1String("toolCall")).toObject();
            body += QStringLiteral("<p style=\"color:%1; font-size:small; margin-left:16px\">▸ <tt>").arg(QLatin1String(Minor))
                + toolTitle(toolCall).toHtmlEscaped() + QStringLiteral("</tt> — ")
                + statusText(toolCall.value(QLatin1String("status")).toString()).toHtmlEscaped() + stamp + QStringLiteral("</p>");
        } else if (kind == QLatin1String("Permission")) {
            const QJsonObject toolCall = entry.value(QLatin1String("toolCall")).toObject();
            body += QStringLiteral("<p style=\"margin-left:16px\"><b>") + i18nc("@title an AI agent asks the user", "Permission").toHtmlEscaped()
                + QStringLiteral("</b>") + stamp + QStringLiteral(": ") + toolTitle(toolCall).toHtmlEscaped()
                + QStringLiteral("<br><span style=\"color:%1\">").arg(QLatin1String(Minor)) + permissionAnswer(entry).toHtmlEscaped()
                + QStringLiteral("</span></p>");
        } else if (kind == QLatin1String("Error")) {
            body += QStringLiteral("<p style=\"color:%1\"><b>").arg(QLatin1String(ErrorColor)) + i18nc("@title", "Error").toHtmlEscaped()
                + QStringLiteral("</b>") + stamp + QStringLiteral(": ") + html(text) + QStringLiteral("</p>");
            const QString details = entry.value(QLatin1String("details")).toString();
            if (!details.isEmpty()) {
                body += QStringLiteral("<pre>") + details.toHtmlEscaped() + QStringLiteral("</pre>");
            }
        } else {
            const QString status = entry.value(QLatin1String("status")).toString();
            body += QStringLiteral("<p style=\"color:%1; font-size:small\"><i>").arg(QLatin1String(Minor)) + html(text) + QStringLiteral("</i>") + stamp
                + (status.isEmpty() ? QString() : QStringLiteral(" — ") + status.toHtmlEscaped()) + QStringLiteral("</p>");
        }
    }
    return QStringLiteral("<html><head><meta charset=\"utf-8\"><title>") + conversation.title.toHtmlEscaped() + QStringLiteral("</title></head><body>") + body
        + QStringLiteral("</body></html>");
}

bool write(const WhaleAiConversation &conversation, Format format, const Options &options, const QString &path, QString *error)
{
    if (format == Format::Markdown) {
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            if (error) {
                *error = file.errorString();
            }
            return false;
        }
        file.write(toMarkdown(conversation, options).toUtf8());
        if (!file.commit()) {
            if (error) {
                *error = file.errorString();
            }
            return false;
        }
        return true;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    {
        QPdfWriter pdf(&file);
        pdf.setTitle(conversation.title);
        pdf.setCreator(QStringLiteral("Orcinus"));
        pdf.setResolution(300);
        pdf.setPageLayout(QPageLayout(QPageSize(QPageSize::A4), QPageLayout::Portrait, QMarginsF(18, 16, 18, 16), QPageLayout::Millimeter));

        QTextDocument document;
        QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
        font.setPointSizeF(10);
        document.setDefaultFont(font);
        const QString mono = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
        document.setDefaultStyleSheet(QStringLiteral("body { color: #1f2328; }"
                                                     "h1 { font-size: 18pt; } h2 { font-size: 14pt; } h3 { font-size: 12pt; }"
                                                     "pre, code, tt { font-family: '%1'; font-size: 9pt; }"
                                                     "pre { background-color: #f5f6f8; }"
                                                     "a { color: %2; }")
                                          .arg(mono, QLatin1String(Accent)));
        document.setHtml(toHtml(conversation, options));
        // Paginated by print(), which also puts the page numbers at the bottom.
        document.print(&pdf);
    }
    if (!file.commit()) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    return true;
}

QString fileName(const WhaleAiConversation &conversation, Format format)
{
    static const QRegularExpression forbidden(QStringLiteral("[/\\\\:*?\"<>|\\x00-\\x1f]+"));
    QString name = conversation.title;
    name.replace(forbidden, QStringLiteral(" "));
    name = name.simplified();
    if (name.endsWith(QStringLiteral("…"))) {
        name.chop(1);
    }
    name = name.left(60).trimmed();
    if (name.isEmpty()) {
        name = i18nc("@info file name of an exported AI conversation", "Conversation");
    }
    return name + (format == Format::Pdf ? QStringLiteral(".pdf") : QStringLiteral(".md"));
}

bool exportInteractively(const WhaleAiConversation &conversation, Format format, QWidget *parent)
{
    WhaleAiExportDialog dialog(format, parent);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    QString folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (folder.isEmpty()) {
        folder = QDir::homePath();
    }
    const QString filter = format == Format::Pdf ? i18nc("@item:inlistbox file type", "PDF (*.pdf)") : i18nc("@item:inlistbox file type", "Markdown (*.md)");
    QString path = QFileDialog::getSaveFileName(parent,
                                                format == Format::Pdf ? i18nc("@title:window", "Export Conversation as PDF")
                                                                      : i18nc("@title:window", "Export Conversation as Markdown"),
                                                folder + QLatin1Char('/') + fileName(conversation, format),
                                                filter);
    if (path.isEmpty()) {
        return false;
    }
    const QString suffix = format == Format::Pdf ? QStringLiteral(".pdf") : QStringLiteral(".md");
    if (!path.endsWith(suffix, Qt::CaseInsensitive)) {
        path += suffix;
    }
    QString error;
    if (!write(conversation, format, dialog.options(), path, &error)) {
        KMessageBox::error(parent, i18nc("@info %1 is a file, %2 the reason", "The conversation could not be written to %1: %2", path, error));
        return false;
    }
    return true;
}
}

WhaleAiExportDialog::WhaleAiExportDialog(WhaleAiConversationExport::Format format, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(format == WhaleAiConversationExport::Format::Pdf ? i18nc("@title:window", "Export as PDF") : i18nc("@title:window", "Export as Markdown"));
    auto *label = new QLabel(i18nc("@info", "What goes in besides the messages and answers:"), this);
    m_actions = new QCheckBox(i18nc("@option:check what an AI agent did", "Actions"), this);
    m_actions->setObjectName(QStringLiteral("exportActions"));
    m_actions->setToolTip(i18nc("@info:tooltip", "The commands and tools the agent used, its thinking, and the panel's notices."));
    m_actions->setChecked(WhaleAiAgents::exportActions());
    m_times = new QCheckBox(i18nc("@option:check", "Times"), this);
    m_times->setObjectName(QStringLiteral("exportTimes"));
    m_times->setToolTip(i18nc("@info:tooltip", "When each message, action and request came."));
    m_times->setChecked(WhaleAiAgents::exportTimes());
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(i18nc("@action:button", "Export…"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(label);
    layout->addWidget(m_actions);
    layout->addWidget(m_times);
    layout->addWidget(buttons);
}

WhaleAiConversationExport::Options WhaleAiExportDialog::options() const
{
    return {m_actions->isChecked(), m_times->isChecked()};
}

void WhaleAiExportDialog::accept()
{
    WhaleAiAgents::setExportActions(m_actions->isChecked());
    WhaleAiAgents::setExportTimes(m_times->isChecked());
    QDialog::accept();
}
