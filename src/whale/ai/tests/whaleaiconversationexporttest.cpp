/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaiconversationexport.h"

#include <QFile>
#include <QLocale>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

/**
 * Markdown, text and PDF export of a saved AI conversation (AI15).
 */
class WhaleAiConversationExportTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void markdown();
    void markdownWithActions();
    void text();
    void markdownText();
    void pdf();
    void fileName();

private:
    static WhaleAiConversation sample();
};

WhaleAiConversation WhaleAiConversationExportTest::sample()
{
    WhaleAiConversation conversation = WhaleAiConversation::create();
    conversation.title = QStringLiteral("Sort my *pictures*");
    conversation.agentName = QStringLiteral("OpenCode");
    conversation.localModel = QStringLiteral("qwen3.8-27b");
    conversation.folder = QStringLiteral("/home/someone/Bilder");
    conversation.created = QDateTime(QDate(2026, 11, 19), QTime(10, 42));
    conversation.updated = QDateTime(QDate(2026, 11, 19), QTime(10, 57));
    auto entry = [](const QString &kind, const QTime &time, const QJsonObject &fields) {
        QJsonObject object = fields;
        object.insert(QStringLiteral("kind"), kind);
        object.insert(QStringLiteral("time"), QDateTime(QDate(2026, 11, 19), time).toString(Qt::ISODateWithMs));
        return object;
    };
    conversation.entries = {
        entry(QStringLiteral("Notice"), QTime(10, 42), {{QStringLiteral("text"), QStringLiteral("OpenCode works in /home/someone/Bilder.")}}),
        entry(QStringLiteral("UserMessage"),
              QTime(10, 42),
              {{QStringLiteral("text"), QStringLiteral("Sort my *pictures*\nby date")}, {QStringLiteral("context"), QStringLiteral("Folder: Bilder")}}),
        entry(QStringLiteral("Thought"), QTime(10, 43), {{QStringLiteral("text"), QStringLiteral("The user wants a sort.")}}),
        entry(QStringLiteral("ToolCall"),
              QTime(10, 43),
              {{QStringLiteral("toolCall"),
                QJsonObject{{QStringLiteral("title"), QStringLiteral("set_sort")}, {QStringLiteral("status"), QStringLiteral("completed")}}}}),
        entry(QStringLiteral("Permission"),
              QTime(10, 44),
              {{QStringLiteral("toolCall"), QJsonObject{{QStringLiteral("title"), QStringLiteral("Delete old.jpg")}}},
               {QStringLiteral("answer"), QStringLiteral("No")}}),
        entry(QStringLiteral("AgentMessage"),
              QTime(10, 45),
              {{QStringLiteral("text"), QStringLiteral("Done. **Sorted** by date:\n\n- a.jpg\n- b.jpg\n\n```\nls -t\n```")}}),
        entry(QStringLiteral("Error"),
              QTime(10, 57),
              {{QStringLiteral("text"), QStringLiteral("The agent stopped working.")}, {QStringLiteral("details"), QStringLiteral("exit 3")}}),
    };
    return conversation;
}

void WhaleAiConversationExportTest::markdown()
{
    const QString text = WhaleAiConversationExport::toMarkdown(sample(), {false, true});
    // The head: the title as text, agent, folder, time.
    QVERIFY2(text.startsWith(QLatin1String("# Sort my \\*pictures\\*\n")), qPrintable(text));
    QVERIFY(text.contains(QLatin1String("OpenCode with qwen3.8-27b")));
    QVERIFY(text.contains(QLatin1String("`/home/someone/Bilder`")));
    // The user's text as text, its lines kept; the context small.
    QVERIFY(text.contains(QLatin1String("Sort my \\*pictures\\*  \nby date")));
    QVERIFY(text.contains(QLatin1String("<sub>Folder: Bilder</sub>")));
    // The answer as the agent wrote it.
    QVERIFY(text.contains(QLatin1String("Done. **Sorted** by date:\n\n- a.jpg\n- b.jpg\n\n```\nls -t\n```")));
    // Requests and errors always; actions, thinking and notices not.
    QVERIFY(text.contains(QLatin1String("**Permission**")));
    QVERIFY(text.contains(QLatin1String("Answered: No")));
    QVERIFY(text.contains(QLatin1String("**Error**")));
    QVERIFY(text.contains(QLatin1String("exit 3")));
    QVERIFY(!text.contains(QLatin1String("set_sort")));
    QVERIFY(!text.contains(QLatin1String("The user wants a sort.")));
    QVERIFY(!text.contains(QLatin1String("works in")));
    // Times.
    const QString time = QLocale().toString(QTime(10, 45), QLocale::ShortFormat);
    QVERIFY(text.contains(QStringLiteral("### OpenCode · ") + time));
    const QString without = WhaleAiConversationExport::toMarkdown(sample(), {false, false});
    QVERIFY(without.contains(QLatin1String("### OpenCode\n")));
    QVERIFY(!without.contains(QStringLiteral(" · ") + time));
}

void WhaleAiConversationExportTest::markdownWithActions()
{
    const QString text = WhaleAiConversationExport::toMarkdown(sample(), {true, false});
    QVERIFY(text.contains(QStringLiteral("- `set_sort` — done")));
    QVERIFY(text.contains(QLatin1String("<details><summary>Thinking</summary>")));
    QVERIFY(text.contains(QLatin1String("The user wants a sort.")));
    QVERIFY(text.contains(QLatin1String("*OpenCode works in /home/someone/Bilder.*")));
    // In the order they came.
    QVERIFY(text.indexOf(QLatin1String("works in")) < text.indexOf(QLatin1String("### You")));
    QVERIFY(text.indexOf(QLatin1String("set_sort")) < text.indexOf(QLatin1String("### OpenCode")));
}

void WhaleAiConversationExportTest::text()
{
    const QString time = QLocale().toString(QTime(10, 45), QLocale::ShortFormat);
    const QString text = WhaleAiConversationExport::toText(sample(), {false, true});
    // The head as it is, no markup anywhere.
    QVERIFY2(text.startsWith(QStringLiteral("Sort my *pictures*\nOpenCode with qwen3.8-27b · /home/someone/Bilder · ")), qPrintable(text));
    QVERIFY(text.contains(QStringLiteral("You · ") + QLocale().toString(QTime(10, 42), QLocale::ShortFormat)
                          + QLatin1String(":\nSort my *pictures*\nby date\n(Folder: Bilder)")));
    // The answer as it reads: the list keeps its marks, the code its lines.
    QVERIFY2(text.contains(QStringLiteral("OpenCode · ") + time + QStringLiteral(":\nDone. Sorted by date:\n\n- a.jpg\n- b.jpg\n\nls -t")), qPrintable(text));
    QVERIFY(!text.contains(QLatin1String("**")));
    QVERIFY(!text.contains(QLatin1String("```")));
    QVERIFY(text.contains(QStringLiteral("Permission · ")));
    QVERIFY(text.contains(QLatin1String(": Delete old.jpg\nAnswered: No")));
    QVERIFY(text.contains(QLatin1String(": The agent stopped working.\nexit 3")));
    QVERIFY(!text.contains(QLatin1String("set_sort")));
    QVERIFY(text.endsWith(QLatin1String("exit 3\n")));

    // Only the entries, with actions and without times: for copying.
    const QString entries = WhaleAiConversationExport::entriesToText(sample().entries, QStringLiteral("Agent"), {true, false});
    QVERIFY2(entries.startsWith(QLatin1String("OpenCode works in /home/someone/Bilder.\n\nYou:\nSort my")), qPrintable(entries));
    QVERIFY(entries.contains(QLatin1String("Thinking:\nThe user wants a sort.")));
    QVERIFY(entries.contains(QStringLiteral("- set_sort — done")));
    QVERIFY(entries.contains(QLatin1String("Agent:\nDone.")));
    QVERIFY(!entries.contains(time));

    // Written as a file too.
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("conversation.txt"));
    QVERIFY(WhaleAiConversationExport::write(sample(), WhaleAiConversationExport::Format::Text, {}, path));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(file.readAll()), WhaleAiConversationExport::toText(sample(), {}));
}

void WhaleAiConversationExportTest::markdownText()
{
    using WhaleAiConversationExport::markdownText;
    QCOMPARE(markdownText(QStringLiteral("# Title\n\nOne *two*\nthree\n\nfour")), QStringLiteral("Title\n\nOne two three\n\nfour"));
    QCOMPARE(markdownText(QStringLiteral("1. a\n2. b\n   - c\n\nend")), QStringLiteral("1. a\n2. b\n  - c\n\nend"));
    QCOMPARE(markdownText(QStringLiteral("- [x] done\n- [ ] open")), QStringLiteral("- [x] done\n- [ ] open"));
    QCOMPARE(markdownText(QStringLiteral("See `code`:\n\n```sh\nls\npwd\n```")), QStringLiteral("See code:\n\nls\npwd"));
    QCOMPARE(markdownText(QStringLiteral("> quoted")), QStringLiteral("> quoted"));
}

void WhaleAiConversationExportTest::pdf()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("conversation.pdf"));
    QString error;
    QVERIFY2(WhaleAiConversationExport::write(sample(), WhaleAiConversationExport::Format::Pdf, {true, true}, path, &error), qPrintable(error));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(file.read(5) == "%PDF-");
    QVERIFY(file.size() > 2000);

    const QString pdftotext = QStandardPaths::findExecutable(QStringLiteral("pdftotext"));
    if (pdftotext.isEmpty()) {
        QSKIP("no pdftotext to read the PDF back");
    }
    QProcess process;
    process.start(pdftotext, {path, QStringLiteral("-")});
    QVERIFY(process.waitForFinished(10000));
    const QString text = QString::fromUtf8(process.readAllStandardOutput()).simplified();
    // The title as text, the answer's Markdown as formatting, not as characters.
    QVERIFY2(text.contains(QLatin1String("Sort my *pictures*")), qPrintable(text));
    QVERIFY(text.contains(QLatin1String("Done. Sorted by date:")));
    QVERIFY(!text.contains(QLatin1String("**Sorted**")));
    QVERIFY(text.contains(QLatin1String("set_sort")));
    QVERIFY(text.contains(QLatin1String("Answered: No")));

    // Markdown is written as well.
    const QString markdownPath = dir.filePath(QStringLiteral("conversation.md"));
    QVERIFY(WhaleAiConversationExport::write(sample(), WhaleAiConversationExport::Format::Markdown, {}, markdownPath));
    QFile markdown(markdownPath);
    QVERIFY(markdown.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(markdown.readAll()), WhaleAiConversationExport::toMarkdown(sample(), {}));

    // A folder that is not there: an error, no crash.
    QVERIFY(!WhaleAiConversationExport::write(sample(), WhaleAiConversationExport::Format::Pdf, {}, dir.filePath(QStringLiteral("no/such.pdf")), &error));
    QVERIFY(!error.isEmpty());
}

void WhaleAiConversationExportTest::fileName()
{
    WhaleAiConversation conversation = sample();
    conversation.title = QStringLiteral("What is in /etc: \"hosts\"?…");
    QCOMPARE(WhaleAiConversationExport::fileName(conversation, WhaleAiConversationExport::Format::Pdf), QStringLiteral("What is in etc hosts.pdf"));
    conversation.title.clear();
    QCOMPARE(WhaleAiConversationExport::fileName(conversation, WhaleAiConversationExport::Format::Markdown), QStringLiteral("Conversation.md"));
    QCOMPARE(WhaleAiConversationExport::fileName(conversation, WhaleAiConversationExport::Format::Text), QStringLiteral("Conversation.txt"));
}

QTEST_MAIN(WhaleAiConversationExportTest)

#include "whaleaiconversationexporttest.moc"
