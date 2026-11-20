/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaiconversationstore.h"
#include "whaleaidebug.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>

namespace
{
const int FormatVersion = 1;
const int TitleLength = 80;

QString titleSourceName(WhaleAiConversation::TitleSource source)
{
    switch (source) {
    case WhaleAiConversation::TitleSource::Agent:
        return QStringLiteral("agent");
    case WhaleAiConversation::TitleSource::User:
        return QStringLiteral("user");
    case WhaleAiConversation::TitleSource::Auto:
        break;
    }
    return QStringLiteral("auto");
}

WhaleAiConversation::TitleSource titleSourceFrom(const QString &name)
{
    if (name == QLatin1String("agent")) {
        return WhaleAiConversation::TitleSource::Agent;
    }
    if (name == QLatin1String("user")) {
        return WhaleAiConversation::TitleSource::User;
    }
    return WhaleAiConversation::TitleSource::Auto;
}
}

WhaleAiConversation WhaleAiConversation::create()
{
    WhaleAiConversation conversation;
    conversation.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    conversation.created = QDateTime::currentDateTime();
    conversation.updated = conversation.created;
    return conversation;
}

QString WhaleAiConversation::titleFor(const QJsonArray &entries)
{
    QString title;
    for (const QJsonValue &value : entries) {
        const QJsonObject entry = value.toObject();
        if (entry.value(QLatin1String("kind")).toString() != QLatin1String("UserMessage")) {
            continue;
        }
        const QString text = entry.value(QLatin1String("text")).toString().simplified();
        if (text.isEmpty()) {
            continue;
        }
        if (!title.isEmpty()) {
            title += QStringLiteral(" · ");
        }
        title += text;
        if (title.size() > TitleLength) {
            return title.left(TitleLength - 1).trimmed() + QStringLiteral("…");
        }
    }
    return title;
}

QJsonObject WhaleAiConversation::toJson() const
{
    return QJsonObject{
        {QStringLiteral("version"), FormatVersion},
        {QStringLiteral("id"), id},
        {QStringLiteral("title"), title},
        {QStringLiteral("titleSource"), titleSourceName(titleSource)},
        {QStringLiteral("created"), created.toString(Qt::ISODateWithMs)},
        {QStringLiteral("updated"), updated.toString(Qt::ISODateWithMs)},
        {QStringLiteral("agent"),
         QJsonObject{{QStringLiteral("id"), agentId},
                     {QStringLiteral("name"), agentName},
                     {QStringLiteral("commandLine"), commandLine},
                     {QStringLiteral("localProvider"), localProvider},
                     {QStringLiteral("localModel"), localModel}}},
        {QStringLiteral("folder"), folder},
        {QStringLiteral("sessionId"), sessionId},
        {QStringLiteral("mode"), mode},
        {QStringLiteral("entries"), entries},
    };
}

std::optional<WhaleAiConversation> WhaleAiConversation::fromJson(const QJsonObject &object)
{
    const int version = object.value(QLatin1String("version")).toInt();
    if (version < 1 || version > FormatVersion) {
        return std::nullopt;
    }
    WhaleAiConversation conversation;
    conversation.id = object.value(QLatin1String("id")).toString();
    if (conversation.id.isEmpty()) {
        return std::nullopt;
    }
    conversation.title = object.value(QLatin1String("title")).toString();
    conversation.entries = object.value(QLatin1String("entries")).toArray();
    const QJsonValue source = object.value(QLatin1String("titleSource"));
    conversation.titleSource = titleSourceFrom(source.toString());
    if (source.isUndefined()) {
        // Saved with the first line of the first message: the title it gets now.
        if (const QString title = titleFor(conversation.entries); !title.isEmpty()) {
            conversation.title = title;
        }
    }
    conversation.created = QDateTime::fromString(object.value(QLatin1String("created")).toString(), Qt::ISODateWithMs);
    conversation.updated = QDateTime::fromString(object.value(QLatin1String("updated")).toString(), Qt::ISODateWithMs);
    const QJsonObject agent = object.value(QLatin1String("agent")).toObject();
    conversation.agentId = agent.value(QLatin1String("id")).toString();
    conversation.agentName = agent.value(QLatin1String("name")).toString();
    conversation.commandLine = agent.value(QLatin1String("commandLine")).toString();
    conversation.localProvider = agent.value(QLatin1String("localProvider")).toString();
    conversation.localModel = agent.value(QLatin1String("localModel")).toString();
    conversation.folder = object.value(QLatin1String("folder")).toString();
    conversation.sessionId = object.value(QLatin1String("sessionId")).toString();
    conversation.mode = object.value(QLatin1String("mode")).toString();
    return conversation;
}

WhaleAiConversationStore::WhaleAiConversationStore(const QString &folder)
    : m_folder(folder.isEmpty() ? defaultFolder() : folder)
{
}

QString WhaleAiConversationStore::defaultFolder()
{
    QString folder = QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation);
    if (folder.isEmpty()) {
        folder = QDir::homePath() + QStringLiteral("/.local/state");
    }
    return folder + QStringLiteral("/orcinus/conversations");
}

QString WhaleAiConversationStore::folder() const
{
    return m_folder;
}

QString WhaleAiConversationStore::fileOf(const QString &id) const
{
    // Only ids of our own making: nothing may point out of the folder.
    static const QRegularExpression valid(QStringLiteral("^[0-9a-fA-F-]{1,64}$"));
    if (!valid.match(id).hasMatch()) {
        return QString();
    }
    return m_folder + QLatin1Char('/') + id + QStringLiteral(".json");
}

bool WhaleAiConversationStore::save(const WhaleAiConversation &conversation) const
{
    const QString path = fileOf(conversation.id);
    if (path.isEmpty() || !QDir().mkpath(m_folder)) {
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(WhaleAiDebug) << "Cannot save the conversation:" << file.errorString();
        return false;
    }
    // Only the user may read it: it holds paths and what the agent read.
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(QJsonDocument(conversation.toJson()).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        qCWarning(WhaleAiDebug) << "Cannot save the conversation:" << file.errorString();
        return false;
    }
    return true;
}

std::optional<WhaleAiConversation> WhaleAiConversationStore::load(const QString &id) const
{
    QFile file(fileOf(id));
    if (file.fileName().isEmpty() || !file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    std::optional<WhaleAiConversation> conversation = WhaleAiConversation::fromJson(QJsonDocument::fromJson(file.readAll()).object());
    if (!conversation || conversation->id != id) {
        return std::nullopt;
    }
    return conversation;
}

QList<WhaleAiConversation> WhaleAiConversationStore::list(bool withEntries) const
{
    QList<WhaleAiConversation> conversations;
    const QStringList files = QDir(m_folder).entryList({QStringLiteral("*.json")}, QDir::Files);
    for (const QString &file : files) {
        std::optional<WhaleAiConversation> conversation = load(file.chopped(5));
        if (!conversation) {
            continue; // broken, or of a newer Orcinus
        }
        if (!withEntries) {
            conversation->entries = QJsonArray();
        }
        conversations.append(*conversation);
    }
    std::sort(conversations.begin(), conversations.end(), [](const WhaleAiConversation &a, const WhaleAiConversation &b) {
        return a.updated > b.updated;
    });
    return conversations;
}

bool WhaleAiConversationStore::remove(const QString &id) const
{
    const QString path = fileOf(id);
    return !path.isEmpty() && QFile::remove(path);
}

int WhaleAiConversationStore::prune(int days) const
{
    if (days <= 0) {
        return 0;
    }
    const QDateTime limit = QDateTime::currentDateTime().addDays(-days);
    int removed = 0;
    for (const WhaleAiConversation &conversation : list()) {
        if (conversation.updated.isValid() && conversation.updated < limit && remove(conversation.id)) {
            ++removed;
        }
    }
    return removed;
}
