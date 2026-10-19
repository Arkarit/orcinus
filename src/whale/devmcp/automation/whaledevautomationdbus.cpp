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

QString WhaleDevAutomationDBus::SetView(int pane, const QString &viewId, bool remember)
{
    return toJson(m_automation->setView(pane, viewId, remember));
}

QString WhaleDevAutomationDBus::PreferredView(const QString &url)
{
    return toJson(m_automation->preferredView(url));
}

QString WhaleDevAutomationDBus::SetPreferredView(const QString &url, const QString &viewId)
{
    return toJson(m_automation->setPreferredView(url, viewId));
}

QString WhaleDevAutomationDBus::SetViewPluginAvailable(const QString &viewId, bool available)
{
    return toJson(m_automation->setViewPluginAvailable(viewId, available));
}

QString WhaleDevAutomationDBus::ActivatePane(int pane)
{
    return toJson(m_automation->activatePane(pane));
}

QString WhaleDevAutomationDBus::SetSplitView(bool enabled)
{
    return toJson(m_automation->setSplitView(enabled));
}

QString WhaleDevAutomationDBus::SetSplitRatio(double leftShare)
{
    return toJson(m_automation->setSplitRatio(leftShare));
}

QString WhaleDevAutomationDBus::SetWindowSize(int width, int height)
{
    return toJson(m_automation->setWindowSize(width, height));
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

QString WhaleDevAutomationDBus::SetContentScope(int pane, const QString &scope)
{
    return toJson(m_automation->setContentScope(pane, scope));
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

QString WhaleDevAutomationDBus::SetSelection(int pane, const QStringList &urls, const QString &current)
{
    return toJson(m_automation->setSelection(pane, urls, current));
}

QString WhaleDevAutomationDBus::ActivateItems(int pane, const QStringList &urls)
{
    return toJson(m_automation->activateItems(pane, urls));
}

QString WhaleDevAutomationDBus::OpenContextMenu(int pane, const QString &url)
{
    return toJson(m_automation->openContextMenu(pane, url));
}

QString WhaleDevAutomationDBus::ScrollToItem(int pane, const QString &url)
{
    return toJson(m_automation->scrollToItem(pane, url));
}

QString WhaleDevAutomationDBus::Quit()
{
    return toJson(m_automation->quit());
}

QString WhaleDevAutomationDBus::ClosePopups()
{
    return toJson(m_automation->closePopups());
}

QString WhaleDevAutomationDBus::RequestItemInfo(int pane, const QString &url)
{
    return toJson(m_automation->requestItemInfo(pane, url));
}

QString WhaleDevAutomationDBus::AnswerRenameDialog(const QString &newName)
{
    return toJson(m_automation->answerRenameDialog(newName));
}

QString WhaleDevAutomationDBus::CloseDialogs()
{
    return toJson(m_automation->closeDialogs());
}

QString WhaleDevAutomationDBus::ActionState()
{
    return toJson(m_automation->actionState());
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

QString WhaleDevAutomationDBus::PreviewState()
{
    return toJson(m_automation->previewState());
}

QString WhaleDevAutomationDBus::RequestPreview(int pane, const QStringList &urls, int size)
{
    return toJson(m_automation->requestPreview(pane, urls, size));
}

QString WhaleDevAutomationDBus::PerformDragDrop(int sourcePane, int targetPane, const QString &targetUrl, const QString &action)
{
    return toJson(m_automation->performDragDrop(sourcePane, targetPane, targetUrl, action));
}

QString WhaleDevAutomationDBus::ChoosePopupEntry(const QString &text)
{
    return toJson(m_automation->choosePopupEntry(text));
}

QString WhaleDevAutomationDBus::TreemapInput(int pane, const QString &action, const QString &url, const QString &key)
{
    return toJson(m_automation->treemapInput(pane, action, url, key));
}

QString WhaleDevAutomationDBus::TreemapSetOptions(int pane, const QString &colorMode, const QString &labels)
{
    return toJson(m_automation->treemapSetOptions(pane, colorMode, labels));
}

QString WhaleDevAutomationDBus::FileOperation(int pane, const QString &operation, const QStringList &urls)
{
    return toJson(m_automation->fileOperation(pane, operation, urls));
}

QString WhaleDevAutomationDBus::AnswerDialog(const QString &text)
{
    return toJson(m_automation->answerDialog(text));
}

QString WhaleDevAutomationDBus::TriggerToolbarAction(const QString &toolBar, const QString &actionId, const QString &menuEntry)
{
    return toJson(m_automation->triggerToolbarAction(toolBar, actionId, menuEntry));
}

QString WhaleDevAutomationDBus::ConfigureToolbars(const QString &request)
{
    return toJson(m_automation->configureToolbars(QJsonDocument::fromJson(request.toUtf8()).object()));
}

QString WhaleDevAutomationDBus::LocationBarInput(const QString &toolBar, const QString &text, bool submit)
{
    return toJson(m_automation->locationBarInput(toolBar, text, submit));
}

#include "moc_whaledevautomationdbus.cpp"
