/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaitooltab.h"

#include "whaleaicanvasmedia.h"
#include "whaleaiqmlguard.h"
#include "whaleaiquickwidget.h"

#include <KLocalizedString>
#include <KMessageWidget>

#include <QAction>
#include <QQmlContext>
#include <QQmlEngine>
#include <QTimer>
#include <QVBoxLayout>

WhaleAiToolTab::WhaleAiToolTab(const WhaleAiTool &tool, const WhaleAiToolStore &store, const WhaleAiToolHost &host, QWidget *parent)
    : QWidget(parent)
    , m_tool(tool)
    , m_store(store)
    , m_host(host)
    , m_layout(new QVBoxLayout(this))
    , m_message(new KMessageWidget(this))
    , m_messageTimer(new QTimer(this))
{
    setObjectName(QStringLiteral("aiTool_%1").arg(tool.id));
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);
    m_message->setWordWrap(true);
    m_message->setCloseButtonVisible(true);
    m_message->hide();
    m_layout->addWidget(m_message);
    m_messageTimer->setSingleShot(true);
    m_messageTimer->setInterval(6000);
    connect(m_messageTimer, &QTimer::timeout, m_message, &KMessageWidget::animatedHide);
}

WhaleAiToolTab::~WhaleAiToolTab()
{
    unload();
}

QString WhaleAiToolTab::id() const
{
    return m_tool.id;
}

const WhaleAiTool &WhaleAiToolTab::tool() const
{
    return m_tool;
}

bool WhaleAiToolTab::isLoaded() const
{
    return m_loaded;
}

QStringList WhaleAiToolTab::log() const
{
    return m_log;
}

WhaleAiQuickWidget *WhaleAiToolTab::quickWidget() const
{
    return m_quick;
}

WhaleAiToolApi *WhaleAiToolTab::api() const
{
    return m_api;
}

bool WhaleAiToolTab::isBlocked() const
{
    return m_blocked;
}

void WhaleAiToolTab::addLog(const QString &line)
{
    m_log.append(line);
    while (m_log.size() > MaxLog) {
        m_log.removeFirst();
    }
    Q_EMIT logChanged();
}

void WhaleAiToolTab::showMessage(const QString &text, int type, const QList<QAction *> &actions)
{
    m_messageTimer->stop();
    for (QAction *action : std::as_const(m_actions)) {
        m_message->removeAction(action);
        action->deleteLater();
    }
    m_actions = actions;
    for (QAction *action : actions) {
        m_message->addAction(action);
    }
    m_message->setMessageType(KMessageWidget::MessageType(type));
    m_message->setText(text);
    m_message->animatedShow();
    // Information goes away by itself; errors and questions stay.
    if (type == KMessageWidget::Information && actions.isEmpty()) {
        m_messageTimer->start();
    }
}

void WhaleAiToolTab::unload()
{
    m_loaded = false;
    // The QML refers to the API: it goes first.
    delete m_quick;
    m_quick = nullptr;
    delete m_api;
    m_api = nullptr;
}

void WhaleAiToolTab::load()
{
    m_triedLoading = true;
    m_blocked = false;
    unload();
    m_message->hide();

    QString error;
    const auto tool = m_store.tool(m_tool.id, &error);
    if (!tool) {
        addLog(error);
        showMessage(i18nc("@info %1 is an error message in English", "The tool cannot be loaded: %1", error), KMessageWidget::Error);
        Q_EMIT loaded(false);
        return;
    }
    m_tool = *tool;

    m_api = new WhaleAiToolApi(m_tool, m_store, m_host, this, this);
    connect(m_api, &WhaleAiToolApi::notified, this, [this](const QString &text) {
        showMessage(text, KMessageWidget::Information);
    });
    connect(m_api, &WhaleAiToolApi::logged, this, &WhaleAiToolTab::addLog);

    m_quick = new WhaleAiQuickWidget(this);
    QQmlEngine *engine = m_quick->engine();
    WhaleAiQmlGuard::install(engine);
    connect(engine, &QQmlEngine::warnings, this, [this](const QList<QQmlError> &warnings) {
        for (const QQmlError &warning : warnings) {
            addLog(warning.toString());
        }
    });
    m_quick->rootContext()->setContextProperty(QStringLiteral("orcinus"), m_api);
    m_api->setEngine(engine);
    m_layout->addWidget(m_quick, 1);
    m_quick->setSource(QUrl::fromLocalFile(m_tool.entryPath()));

    QStringList errors;
    for (const QQmlError &qmlError : m_quick->errors()) {
        errors.append(qmlError.toString());
    }
    m_loaded = errors.isEmpty() && m_quick->status() == QQuickWidget::Ready;
    if (!m_loaded) {
        for (const QString &line : std::as_const(errors)) {
            addLog(line);
        }
        auto reload = new QAction(QIcon::fromTheme(QStringLiteral("view-refresh")), i18nc("@action:button", "Reload"), this);
        connect(reload, &QAction::triggered, this, &WhaleAiToolTab::load);
        showMessage(i18nc("@info %1 is a list of QML errors", "The tool has errors:\n%1", errors.mid(0, 5).join(QLatin1Char('\n'))),
                    KMessageWidget::Error,
                    {reload});
    } else {
        m_quick->setFocus();
    }
    Q_EMIT loaded(m_loaded);
}

void WhaleAiToolTab::block(const QString &message)
{
    unload();
    m_blocked = true;
    auto trust = new QAction(QIcon::fromTheme(QStringLiteral("window-pin")), i18nc("@action:button", "Keep Again…"), this);
    connect(trust, &QAction::triggered, this, &WhaleAiToolTab::trustRequested);
    auto remove = new QAction(QIcon::fromTheme(QStringLiteral("edit-delete-remove")), i18nc("@action:button", "Remove from Canvas"), this);
    connect(remove, &QAction::triggered, this, &WhaleAiToolTab::removeRequested);
    showMessage(message, KMessageWidget::Warning, {trust, remove});
}

void WhaleAiToolTab::notifyContextChanged()
{
    if (m_api) {
        Q_EMIT m_api->contextChanged();
    }
}

void WhaleAiToolTab::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // Loaded when first seen: kept tools cost nothing until then.
    if (!m_triedLoading && !m_blocked) {
        load();
    }
}

void WhaleAiToolTab::hideEvent(QHideEvent *event)
{
    // Nothing plays unseen (another tab, the panel hidden).
    if (m_api) {
        m_api->player()->stop();
    }
    QWidget::hideEvent(event);
}

#include "moc_whaleaitooltab.cpp"
