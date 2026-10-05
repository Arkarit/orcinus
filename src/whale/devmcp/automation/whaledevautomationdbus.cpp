/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaledevautomationdbus.h"

#include "whaledevautomation.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QJsonDocument>
#include <QJsonObject>

namespace
{
QString toJson(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}
}

WhaleDevAutomationDBus::WhaleDevAutomationDBus(WhaleDevAutomation *automation)
    : QObject(automation)
    , m_automation(automation)
{
}

QString WhaleDevAutomationDBus::UiState()
{
    return toJson(m_automation->uiState());
}

QString WhaleDevAutomationDBus::FocusState()
{
    return toJson(m_automation->focusState());
}

QStringList WhaleDevAutomationDBus::ActionIds()
{
    return WhaleDevAutomation::actionIds();
}

QString WhaleDevAutomationDBus::SetView(int pane, const QString &viewId)
{
    return toJson(m_automation->setView(pane, viewId));
}

QString WhaleDevAutomationDBus::ActivatePane(int pane)
{
    return toJson(m_automation->activatePane(pane));
}

QString WhaleDevAutomationDBus::SetSplitView(bool enabled)
{
    return toJson(m_automation->setSplitView(enabled));
}

QString WhaleDevAutomationDBus::SetLocation(int pane, const QString &url)
{
    return toJson(m_automation->setLocation(pane, url));
}

QString WhaleDevAutomationDBus::SetFilterText(int pane, const QString &text)
{
    return toJson(m_automation->setFilterText(pane, text));
}

QString WhaleDevAutomationDBus::ClearFilter(int pane)
{
    return toJson(m_automation->clearFilter(pane));
}

QString WhaleDevAutomationDBus::SetHiddenFiles(int pane, bool shown)
{
    return toJson(m_automation->setHiddenFiles(pane, shown));
}

QString WhaleDevAutomationDBus::SetSort(int pane, const QString &field, const QString &order, const QString &foldersFirst)
{
    std::optional<bool> folders;
    if (foldersFirst == QLatin1String("true")) {
        folders = true;
    } else if (foldersFirst == QLatin1String("false")) {
        folders = false;
    }
    return toJson(m_automation->setSort(pane, field, order, folders));
}

QString WhaleDevAutomationDBus::TriggerAction(const QString &actionId)
{
    return toJson(m_automation->triggerAction(actionId));
}

QString WhaleDevAutomationDBus::WaitForState(const QString &conditions, int timeoutMs)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(conditions.toUtf8(), &parseError);
    if (!document.isObject()) {
        return toJson({{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("conditions must be a JSON object")}});
    }
    setDelayedReply(true);
    const QDBusMessage request = message();
    m_automation->waitForState(document.object(), qBound(0, timeoutMs, 60000), [request](const QJsonObject &result) {
        QDBusConnection::sessionBus().send(request.createReply(toJson(result)));
    });
    return {};
}

QByteArray WhaleDevAutomationDBus::Screenshot()
{
    return m_automation->screenshot();
}

#include "moc_whaledevautomationdbus.cpp"
