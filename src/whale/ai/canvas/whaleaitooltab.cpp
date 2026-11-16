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
#include <QApplication>
#include <QJsonArray>
#include <QKeySequence>
#include <QMouseEvent>
#include <QMutex>
#include <QPointer>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QTimer>
#include <QVBoxLayout>

#include <functional>

namespace
{
// console.log of a tool goes into its log (CV3): the QML engine tells the file it comes from.
QMutex s_tabsMutex;
QList<std::pair<QString, QPointer<WhaleAiToolTab>>> s_tabs;
QtMessageHandler s_previousHandler = nullptr;

void routeConsole(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    const QByteArray category(context.category ? context.category : "");
    if (context.file && (category == "js" || category == "qml")) {
        const QString file = QString::fromUtf8(context.file);
        QMutexLocker locker(&s_tabsMutex);
        for (const auto &[prefix, tab] : std::as_const(s_tabs)) {
            if (tab && file.startsWith(prefix)) {
                const QString line = QStringLiteral("console: %1 (%2:%3)").arg(message, file.mid(prefix.size())).arg(context.line);
                QMetaObject::invokeMethod(tab.data(), "addLog", Qt::QueuedConnection, Q_ARG(QString, line));
                break;
            }
        }
    }
    if (s_previousHandler) {
        s_previousHandler(type, context, message);
    }
}

void watchConsole(WhaleAiToolTab *tab, const QString &folder)
{
    QMutexLocker locker(&s_tabsMutex);
    if (!s_previousHandler) {
        s_previousHandler = qInstallMessageHandler(routeConsole);
    }
    s_tabs.removeIf([tab](const auto &entry) {
        return !entry.second || entry.second == tab;
    });
    s_tabs.append({QUrl::fromLocalFile(folder).toString() + QLatin1Char('/'), tab});
}

void unwatchConsole(WhaleAiToolTab *tab)
{
    QMutexLocker locker(&s_tabsMutex);
    s_tabs.removeIf([tab](const auto &entry) {
        return !entry.second || entry.second == tab;
    });
}

/** @p value for JSON, shortened: long strings and lists are cut. */
QJsonValue shortJson(const QVariant &value)
{
    QVariant plain = value;
    if (value.canConvert<QJSValue>() && value.metaType() == QMetaType::fromType<QJSValue>()) {
        plain = qvariant_cast<QJSValue>(value).toVariant();
    }
    QJsonValue json = QJsonValue::fromVariant(plain);
    if (json.isString() && json.toString().size() > 500) {
        return QJsonValue(QString(json.toString().left(500) + QStringLiteral("…")));
    }
    if (json.isArray() && json.toArray().size() > 50) {
        QJsonArray cut;
        const QJsonArray all = json.toArray();
        for (int i = 0; i < 50; ++i) {
            cut.append(all.at(i));
        }
        cut.append(QStringLiteral("… %1 in all").arg(all.size()));
        return cut;
    }
    return json;
}

QString typeName(const QObject *object)
{
    QString name = QString::fromLatin1(object->metaObject()->className());
    for (const QLatin1String suffix : {QLatin1String("_QMLTYPE_"), QLatin1String("_QML_")}) {
        const int at = name.indexOf(suffix);
        if (at > 0) {
            name.truncate(at);
        }
    }
    if (name.startsWith(QLatin1String("QQuick"))) {
        name = name.mid(6);
    }
    return name;
}

QQuickItem *findNamed(QQuickItem *item, const QString &objectName)
{
    if (!item) {
        return nullptr;
    }
    if (item->objectName() == objectName) {
        return item;
    }
    const auto children = item->childItems();
    for (QQuickItem *child : children) {
        if (QQuickItem *found = findNamed(child, objectName)) {
            return found;
        }
    }
    return nullptr;
}
}

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
    unwatchConsole(this);
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
    // The log is the one of this load: what the agent reads after a change.
    m_log.clear();
    Q_EMIT logChanged();

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
    // Warnings go into the log only (below), console.log too.
    engine->setOutputWarningsToStandardError(false);
    watchConsole(this, m_tool.folder);
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

QJsonObject WhaleAiToolTab::inspect() const
{
    QQuickItem *root = m_quick ? m_quick->rootObject() : nullptr;
    if (!root) {
        return {{QStringLiteral("loaded"), false}};
    }
    // The root's own properties: those its QML declares, after the ones of its type.
    QJsonObject properties;
    const QMetaObject *meta = root->metaObject();
    const int first = meta->superClass() ? meta->superClass()->propertyCount() : meta->propertyOffset();
    for (int i = first; i < meta->propertyCount(); ++i) {
        const QMetaProperty property = meta->property(i);
        properties.insert(QString::fromLatin1(property.name()), shortJson(property.read(root)));
    }
    QJsonArray items;
    std::function<void(QQuickItem *)> walk = [&walk, &items](QQuickItem *item) {
        if (items.size() >= 200) {
            return;
        }
        if (!item->objectName().isEmpty()) {
            QJsonObject entry{{QStringLiteral("name"), item->objectName()},
                              {QStringLiteral("type"), typeName(item)},
                              {QStringLiteral("visible"), item->isVisible()},
                              {QStringLiteral("enabled"), item->isEnabled()}};
            for (const char *name : {"text", "checked", "value", "currentIndex", "count", "source"}) {
                const int index = item->metaObject()->indexOfProperty(name);
                if (index >= 0) {
                    entry.insert(QString::fromLatin1(name), shortJson(item->metaObject()->property(index).read(item)));
                }
            }
            items.append(entry);
        }
        const auto children = item->childItems();
        for (QQuickItem *child : children) {
            walk(child);
        }
    };
    walk(root);
    return {{QStringLiteral("loaded"), m_loaded}, {QStringLiteral("properties"), properties}, {QStringLiteral("items"), items}};
}

QJsonObject WhaleAiToolTab::invoke(const QJsonObject &arguments, QString *error)
{
    QQuickItem *root = m_quick ? m_quick->rootObject() : nullptr;
    if (!root) {
        *error = QStringLiteral("The tool is not loaded (see tool_log).");
        return {};
    }
    if (arguments.contains(QLatin1String("click"))) {
        const QString name = arguments.value(QLatin1String("click")).toString();
        QQuickItem *item = findNamed(root, name);
        if (!item) {
            *error = QStringLiteral("No item with the objectName \"%1\" (see tool_inspect).").arg(name);
            return {};
        }
        if (!item->isVisible() || !item->isEnabled()) {
            *error = QStringLiteral("\"%1\" is not visible or not enabled.").arg(name);
            return {};
        }
        // A real click where the item is, as the user would make it.
        const QPointF centre = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
        const QPointF global = m_quick->mapToGlobal(centre);
        QMouseEvent press(QEvent::MouseButtonPress, centre, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(m_quick, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, centre, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(m_quick, &release);
        return {{QStringLiteral("clicked"), name}};
    }
    if (arguments.contains(QLatin1String("keys"))) {
        const QKeySequence sequence(arguments.value(QLatin1String("keys")).toString());
        if (sequence.isEmpty()) {
            *error = QStringLiteral("\"keys\" must be a key sequence such as \"Right\", \"K\" or \"Ctrl+S\".");
            return {};
        }
        m_quick->setFocus();
        for (int i = 0; i < sequence.count(); ++i) {
            const QKeyCombination combination = sequence[i];
            const QString text = combination.keyboardModifiers() & ~Qt::ShiftModifier ? QString() : QKeySequence(combination.key()).toString().toLower();
            QKeyEvent press(QEvent::KeyPress, combination.key(), combination.keyboardModifiers(), text.size() == 1 ? text : QString());
            QCoreApplication::sendEvent(m_quick, &press);
            QKeyEvent release(QEvent::KeyRelease, combination.key(), combination.keyboardModifiers(), text.size() == 1 ? text : QString());
            QCoreApplication::sendEvent(m_quick, &release);
        }
        return {{QStringLiteral("pressed"), sequence.toString()}};
    }
    if (arguments.contains(QLatin1String("function"))) {
        const QString name = arguments.value(QLatin1String("function")).toString();
        QQmlEngine *engine = m_quick->engine();
        QQmlEngine::setObjectOwnership(root, QQmlEngine::CppOwnership);
        QJSValue self = engine->newQObject(root);
        QJSValue function = self.property(name);
        if (!function.isCallable()) {
            *error = QStringLiteral("The root has no function \"%1\".").arg(name);
            return {};
        }
        QJSValueList values;
        for (const QJsonValue &value : arguments.value(QLatin1String("arguments")).toArray()) {
            values.append(engine->toScriptValue(value.toVariant()));
        }
        const QJSValue result = function.callWithInstance(self, values);
        if (result.isError()) {
            *error = QStringLiteral("The function threw: %1").arg(result.toString());
            return {};
        }
        return {{QStringLiteral("called"), name}, {QStringLiteral("returned"), shortJson(result.toVariant())}};
    }
    *error = QStringLiteral("Give \"click\" (an objectName), \"keys\" or \"function\".");
    return {};
}

QImage WhaleAiToolTab::screenshot() const
{
    return m_quick && m_quick->rootObject() ? m_quick->grabFramebuffer() : QImage();
}

#include "moc_whaleaitooltab.cpp"
