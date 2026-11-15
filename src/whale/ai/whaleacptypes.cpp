/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleacptypes.h"

WhaleAcpAgentInfo WhaleAcpAgentInfo::fromInitializeResult(const QJsonObject &result)
{
    WhaleAcpAgentInfo info;
    info.raw = result;
    info.protocolVersion = result.value(QLatin1String("protocolVersion")).toInt();

    const QJsonObject agentInfo = result.value(QLatin1String("agentInfo")).toObject();
    info.name = agentInfo.value(QLatin1String("name")).toString();
    info.title = agentInfo.value(QLatin1String("title")).toString();
    info.version = agentInfo.value(QLatin1String("version")).toString();

    const QJsonObject capabilities = result.value(QLatin1String("agentCapabilities")).toObject();
    info.loadSession = capabilities.value(QLatin1String("loadSession")).toBool();
    const QJsonObject prompt = capabilities.value(QLatin1String("promptCapabilities")).toObject();
    info.imagePrompts = prompt.value(QLatin1String("image")).toBool();
    info.embeddedContext = prompt.value(QLatin1String("embeddedContext")).toBool();
    const QJsonObject mcp = capabilities.value(QLatin1String("mcpCapabilities")).toObject();
    info.mcpHttp = mcp.value(QLatin1String("http")).toBool();
    info.mcpSse = mcp.value(QLatin1String("sse")).toBool();

    const QJsonArray methods = result.value(QLatin1String("authMethods")).toArray();
    for (const QJsonValue &value : methods) {
        const QJsonObject method = value.toObject();
        info.authMethods.append({method.value(QLatin1String("id")).toString(),
                                 method.value(QLatin1String("name")).toString(),
                                 method.value(QLatin1String("description")).toString()});
    }
    return info;
}

const WhaleAcpConfigOption::Choice *WhaleAcpConfigOption::choice(const QString &value) const
{
    for (const Choice &choice : choices) {
        if (choice.value == value) {
            return &choice;
        }
    }
    return nullptr;
}

static void appendChoices(QList<WhaleAcpConfigOption::Choice> &choices, const QJsonArray &options, const QString &group)
{
    for (const QJsonValue &value : options) {
        const QJsonObject option = value.toObject();
        if (option.contains(QLatin1String("group"))) {
            appendChoices(choices, option.value(QLatin1String("options")).toArray(), option.value(QLatin1String("name")).toString());
            continue;
        }
        choices.append({option.value(QLatin1String("value")).toString(),
                        option.value(QLatin1String("name")).toString(),
                        option.value(QLatin1String("description")).toString(),
                        group});
    }
}

QList<WhaleAcpConfigOption> WhaleAcpConfigOption::listFromJson(const QJsonArray &array)
{
    QList<WhaleAcpConfigOption> list;
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        if (object.value(QLatin1String("type")).toString() != QLatin1String("select")) {
            continue;
        }
        WhaleAcpConfigOption option;
        option.id = object.value(QLatin1String("id")).toString();
        option.name = object.value(QLatin1String("name")).toString();
        option.description = object.value(QLatin1String("description")).toString();
        option.category = object.value(QLatin1String("category")).toString();
        option.currentValue = object.value(QLatin1String("currentValue")).toString();
        appendChoices(option.choices, object.value(QLatin1String("options")).toArray(), QString());
        if (!option.id.isEmpty()) {
            list.append(option);
        }
    }
    return list;
}

WhaleAcpConfigOption WhaleAcpConfigOption::fromModes(const QJsonObject &modes)
{
    WhaleAcpConfigOption option;
    option.id = modesOptionId();
    option.category = QStringLiteral("mode");
    option.currentValue = modes.value(QLatin1String("currentModeId")).toString();
    const QJsonArray available = modes.value(QLatin1String("availableModes")).toArray();
    for (const QJsonValue &value : available) {
        const QJsonObject mode = value.toObject();
        option.choices.append({mode.value(QLatin1String("id")).toString(),
                               mode.value(QLatin1String("name")).toString(),
                               mode.value(QLatin1String("description")).toString(),
                               QString()});
    }
    return option;
}

WhaleAcpToolCall::Status WhaleAcpToolCall::statusFromString(const QString &status)
{
    if (status == QLatin1String("in_progress")) {
        return Status::InProgress;
    }
    if (status == QLatin1String("completed")) {
        return Status::Completed;
    }
    if (status == QLatin1String("failed")) {
        return Status::Failed;
    }
    return Status::Pending;
}

void WhaleAcpToolCall::merge(const QJsonObject &update)
{
    if (id.isEmpty()) {
        id = update.value(QLatin1String("toolCallId")).toString();
    }
    if (update.contains(QLatin1String("title"))) {
        title = update.value(QLatin1String("title")).toString();
    }
    if (update.contains(QLatin1String("kind"))) {
        kind = update.value(QLatin1String("kind")).toString();
    }
    if (update.contains(QLatin1String("status"))) {
        status = statusFromString(update.value(QLatin1String("status")).toString());
    }
    if (update.contains(QLatin1String("locations"))) {
        locations.clear();
        const QJsonArray array = update.value(QLatin1String("locations")).toArray();
        for (const QJsonValue &location : array) {
            locations.append(location.toObject().value(QLatin1String("path")).toString());
        }
    }
    if (update.contains(QLatin1String("content"))) {
        content = update.value(QLatin1String("content")).toArray();
    }
    if (update.contains(QLatin1String("rawInput"))) {
        rawInput = update.value(QLatin1String("rawInput"));
    }
    if (update.contains(QLatin1String("rawOutput"))) {
        rawOutput = update.value(QLatin1String("rawOutput"));
    }
    if (update.contains(QLatin1String("_meta"))) {
        meta = update.value(QLatin1String("_meta")).toObject();
    }
}

QJsonObject WhaleAcpMcpServer::toJson() const
{
    QJsonArray env;
    for (const auto &[name, value] : environment) {
        env.append(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("value"), value}});
    }
    return QJsonObject{
        {QStringLiteral("name"), name},
        {QStringLiteral("command"), command},
        {QStringLiteral("args"), QJsonArray::fromStringList(arguments)},
        {QStringLiteral("env"), env},
    };
}

QJsonObject WhaleAcpContent::text(const QString &text)
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), text}};
}

QJsonObject WhaleAcpContent::resourceLink(const QUrl &url, const QString &name, const QString &mimeType)
{
    QJsonObject link{
        {QStringLiteral("type"), QStringLiteral("resource_link")},
        {QStringLiteral("uri"), url.toString(QUrl::FullyEncoded)},
        {QStringLiteral("name"), name},
    };
    if (!mimeType.isEmpty()) {
        link.insert(QStringLiteral("mimeType"), mimeType);
    }
    return link;
}
