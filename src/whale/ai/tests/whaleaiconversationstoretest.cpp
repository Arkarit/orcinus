/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaiconversationstore.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

/**
 * The saved conversations of the AI panel (AI14a).
 */
class WhaleAiConversationStoreTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void roundTrip();
    void listNewestFirst();
    void brokenAndForeign();
    void prune();
    void title();
    void titleSource();
};

namespace
{
WhaleAiConversation sample(const QString &title, const QDateTime &updated)
{
    WhaleAiConversation conversation = WhaleAiConversation::create();
    conversation.title = title;
    conversation.updated = updated;
    conversation.agentId = QStringLiteral("claude");
    conversation.agentName = QStringLiteral("Claude");
    conversation.commandLine = QStringLiteral("npx -y @agentclientprotocol/claude-agent-acp");
    conversation.folder = QStringLiteral("/home/someone/Pictures");
    conversation.sessionId = QStringLiteral("s-1");
    conversation.mode = QStringLiteral("default");
    conversation.entries = QJsonArray{QJsonObject{{QStringLiteral("kind"), QStringLiteral("UserMessage")}, {QStringLiteral("text"), title}}};
    return conversation;
}
}

void WhaleAiConversationStoreTest::roundTrip()
{
    QTemporaryDir dir;
    const WhaleAiConversationStore store(dir.filePath(QStringLiteral("conversations")));
    WhaleAiConversation conversation = sample(QStringLiteral("Sort my pictures"), QDateTime::currentDateTime());
    conversation.localProvider = QStringLiteral("strata");
    conversation.localModel = QStringLiteral("qwen3.8-flash-next-iq2_xs");
    // Milliseconds are kept; the rest is compared as it is.
    QVERIFY(store.save(conversation));
    const std::optional<WhaleAiConversation> loaded = store.load(conversation.id);
    QVERIFY(loaded);
    QCOMPARE(*loaded, conversation);

    // Only the user may read it.
    const QFileInfo file(store.folder() + QLatin1Char('/') + conversation.id + QStringLiteral(".json"));
    QVERIFY(file.exists());
    QCOMPARE(file.permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther), QFileDevice::Permissions());

    // Saved again: replaced, not a second one.
    conversation.title = QStringLiteral("Sort my pictures by date");
    QVERIFY(store.save(conversation));
    QCOMPARE(store.list().size(), 1);
    QCOMPARE(store.list().first().title, conversation.title);
    // Without entries unless asked.
    QVERIFY(store.list().first().entries.isEmpty());
    QCOMPARE(store.list(true).first().entries, conversation.entries);

    QVERIFY(store.remove(conversation.id));
    QVERIFY(!store.load(conversation.id));
    QVERIFY(store.list().isEmpty());
}

void WhaleAiConversationStoreTest::listNewestFirst()
{
    QTemporaryDir dir;
    const WhaleAiConversationStore store(dir.path());
    const QDateTime now = QDateTime::currentDateTime();
    QVERIFY(store.save(sample(QStringLiteral("old"), now.addDays(-2))));
    QVERIFY(store.save(sample(QStringLiteral("new"), now)));
    QVERIFY(store.save(sample(QStringLiteral("middle"), now.addDays(-1))));
    QStringList titles;
    for (const WhaleAiConversation &conversation : store.list()) {
        titles.append(conversation.title);
    }
    QCOMPARE(titles, (QStringList{QStringLiteral("new"), QStringLiteral("middle"), QStringLiteral("old")}));
}

void WhaleAiConversationStoreTest::brokenAndForeign()
{
    QTemporaryDir dir;
    const WhaleAiConversationStore store(dir.path());
    const WhaleAiConversation good = sample(QStringLiteral("good"), QDateTime::currentDateTime());
    QVERIFY(store.save(good));
    auto write = [&dir](const QString &name, const QByteArray &content) {
        QFile file(dir.filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(content);
    };
    write(QStringLiteral("0000-broken.json"), "{ not json");
    // Of a newer Orcinus: left alone.
    QJsonObject newer = good.toJson();
    newer.insert(QStringLiteral("version"), 99);
    newer.insert(QStringLiteral("id"), QStringLiteral("1111-newer"));
    write(QStringLiteral("1111-newer.json"), QJsonDocument(newer).toJson());
    // A file whose id is not its name.
    QJsonObject other = good.toJson();
    write(QStringLiteral("2222-other.json"), QJsonDocument(other).toJson());

    QCOMPARE(store.list().size(), 1);
    QCOMPARE(store.list().first().id, good.id);
    QVERIFY(!store.load(QStringLiteral("0000-broken")));
    QVERIFY(!store.load(QStringLiteral("1111-newer")));
    QVERIFY(!store.load(QStringLiteral("2222-other")));
    // No way out of the folder.
    QVERIFY(!store.load(QStringLiteral("../x")));
    QVERIFY(!store.remove(QStringLiteral("../x")));
    WhaleAiConversation evil = good;
    evil.id = QStringLiteral("../evil");
    QVERIFY(!store.save(evil));
}

void WhaleAiConversationStoreTest::prune()
{
    QTemporaryDir dir;
    const WhaleAiConversationStore store(dir.path());
    const QDateTime now = QDateTime::currentDateTime();
    QVERIFY(store.save(sample(QStringLiteral("ancient"), now.addDays(-100))));
    QVERIFY(store.save(sample(QStringLiteral("recent"), now.addDays(-10))));
    QCOMPARE(store.prune(0), 0); // always kept
    QCOMPARE(store.list().size(), 2);
    QCOMPARE(store.prune(90), 1);
    QCOMPARE(store.list().size(), 1);
    QCOMPARE(store.list().first().title, QStringLiteral("recent"));
}

void WhaleAiConversationStoreTest::title()
{
    auto user = [](const QString &text) {
        return QJsonObject{{QStringLiteral("kind"), QStringLiteral("UserMessage")}, {QStringLiteral("text"), text}};
    };
    const QJsonObject answer{{QStringLiteral("kind"), QStringLiteral("AgentMessage")}, {QStringLiteral("text"), QStringLiteral("Hello!")}};
    // The user's messages one after the other, lines as spaces; the agent's answers not.
    QCOMPARE(WhaleAiConversation::titleFor({user(QStringLiteral("  Hello Elfie ")), answer, user(QStringLiteral("Sort my\tpictures\nby date"))}),
             QStringLiteral("Hello Elfie · Sort my pictures by date"));
    QCOMPARE(WhaleAiConversation::titleFor({answer}), QString());
    const QString title = WhaleAiConversation::titleFor({user(QString(50, QLatin1Char('a'))), user(QString(50, QLatin1Char('b')))});
    QCOMPARE(title.size(), 80);
    QVERIFY(title.startsWith(QString(50, QLatin1Char('a')) + QStringLiteral(" · b")));
    QVERIFY(title.endsWith(QStringLiteral("…")));
}

void WhaleAiConversationStoreTest::titleSource()
{
    QTemporaryDir dir;
    const WhaleAiConversationStore store(dir.path());
    WhaleAiConversation conversation = sample(QStringLiteral("Hello"), QDateTime::currentDateTime());
    conversation.entries.append(QJsonObject{{QStringLiteral("kind"), QStringLiteral("UserMessage")}, {QStringLiteral("text"), QStringLiteral("Sort them")}});
    conversation.title = QStringLiteral("Sorting pictures");
    conversation.titleSource = WhaleAiConversation::TitleSource::Agent;
    QVERIFY(store.save(conversation));
    QCOMPARE(store.load(conversation.id)->titleSource, WhaleAiConversation::TitleSource::Agent);
    QCOMPARE(store.load(conversation.id)->title, QStringLiteral("Sorting pictures"));

    // Saved before titles had a source: the title it gets now.
    QJsonObject old = conversation.toJson();
    old.remove(QStringLiteral("titleSource"));
    old.insert(QStringLiteral("title"), QStringLiteral("Hello"));
    const std::optional<WhaleAiConversation> migrated = WhaleAiConversation::fromJson(old);
    QVERIFY(migrated);
    QCOMPARE(migrated->titleSource, WhaleAiConversation::TitleSource::Auto);
    QCOMPARE(migrated->title, QStringLiteral("Hello · Sort them"));
}

QTEST_GUILESS_MAIN(WhaleAiConversationStoreTest)

#include "whaleaiconversationstoretest.moc"
