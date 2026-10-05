/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaledevautomation.h"

#include "dolphinmainwindow.h"
#include "dolphintabpage.h"
#include "dolphintabwidget.h"
#include "dolphinurlnavigator.h"
#include "dolphinviewcontainer.h"
#include "filterbar/filterbar.h"
#include "kitemviews/private/kitemlistroleeditor.h"
#include "search/bar.h"
#include "views/dolphinview.h"
#include "whaledevautomationdbus.h"
#include "whaleview.h"
#include "whaleviewactions.h"
#include "whaleviewdebug.h"
#include "whaleviewhost.h"
#include "whaleviewregistry.h"

#include <KActionCollection>
#include <KCoreDirLister>
#include <KSelectAction>
#include <KStandardAction>
#include <KToolBar>

#include <QAbstractItemView>
#include <QApplication>
#include <QBuffer>
#include <QDBusConnection>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QTimer>

namespace
{
// Lists in the state are cut off here; the counts are always complete.
constexpr int MaxListedItems = 200;

const QString dolphinId = QStringLiteral("dolphin");

QString semanticViewId(const QString &hostViewId)
{
    return hostViewId == WhaleViewHost::dolphinViewId() ? dolphinId : hostViewId;
}

// Semantic sort fields and the KFileItemModel roles behind them.
const QList<std::pair<QString, QByteArray>> &sortFields()
{
    static const QList<std::pair<QString, QByteArray>> fields = {
        {QStringLiteral("name"), "text"},
        {QStringLiteral("size"), "size"},
        {QStringLiteral("modified"), "modificationtime"},
        {QStringLiteral("created"), "creationtime"},
        {QStringLiteral("type"), "type"},
        {QStringLiteral("extension"), "extension"},
    };
    return fields;
}

// Action IDs of the automation and the names of the actions in Whale. Only
// actions that change no files: Delete, Trash, Cut, Copy, Paste and the like
// stay out until plugin views share the host's selection (view plugins M4/M5).
const QList<std::pair<QString, QString>> &actions()
{
    static const QList<std::pair<QString, QString>> list = {
        {QStringLiteral("focus_other_view"), QStringLiteral("focus_inactive_split_view")},
        {QStringLiteral("go_up"), KStandardAction::name(KStandardAction::Up)},
        {QStringLiteral("go_back"), KStandardAction::name(KStandardAction::Back)},
        {QStringLiteral("go_forward"), KStandardAction::name(KStandardAction::Forward)},
        {QStringLiteral("edit_location"), QStringLiteral("replace_location")},
        {QStringLiteral("select_all"), KStandardAction::name(KStandardAction::SelectAll)},
        {QStringLiteral("invert_selection"), QStringLiteral("invert_selection")},
        {QStringLiteral("rename"), KStandardAction::name(KStandardAction::RenameFile)},
    };
    return list;
}

QJsonObject ok()
{
    return {{QStringLiteral("ok"), true}};
}

QJsonObject failure(const QString &error)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
}

bool contains(const QWidget *widget, const QWidget *focus)
{
    return widget && focus && (widget == focus || widget->isAncestorOf(focus));
}

QString viewModeName(DolphinView::Mode mode)
{
    switch (mode) {
    case DolphinView::IconsView:
        return QStringLiteral("icons");
    case DolphinView::CompactView:
        return QStringLiteral("compact");
    case DolphinView::DetailsView:
        return QStringLiteral("details");
    }
    return QStringLiteral("unknown");
}

QJsonValue valueAt(const QJsonValue &root, const QString &path)
{
    QJsonValue value = root;
    const QStringList segments = path.split(QLatin1Char('.'));
    for (const QString &segment : segments) {
        bool isIndex = false;
        const int index = segment.toInt(&isIndex);
        if (isIndex && value.isArray()) {
            value = value.toArray().at(index);
        } else if (value.isObject()) {
            value = value.toObject().value(segment);
        } else {
            return QJsonValue(QJsonValue::Undefined);
        }
    }
    return value;
}
}

QString WhaleDevAutomation::objectPath()
{
    return QStringLiteral("/whale/DevAutomation");
}

QString WhaleDevAutomation::interfaceName()
{
    return QStringLiteral("io.github.arkarit.whale.DevAutomation");
}

bool WhaleDevAutomation::isRequested()
{
    return qgetenv("WHALE_DEV_MCP") == "1";
}

void WhaleDevAutomation::startIfRequested(DolphinMainWindow *window)
{
    if (!isRequested()) {
        return;
    }
    auto automation = new WhaleDevAutomation(window);
    auto adaptor = new WhaleDevAutomationDBus(automation);
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerObject(objectPath(), adaptor, QDBusConnection::ExportScriptableSlots)) {
        qCWarning(WhaleViewDebug) << "Development automation: cannot register" << objectPath() << bus.lastError().message();
        return;
    }
    qCWarning(WhaleViewDebug) << "Development automation enabled on" << bus.baseService() << objectPath();
}

WhaleDevAutomation::WhaleDevAutomation(DolphinMainWindow *window)
    : QObject(window)
    , m_window(window)
{
    connect(tabWidget(), &DolphinTabWidget::activeViewChanged, this, &WhaleDevAutomation::recordActivation);
}

WhaleDevAutomation::~WhaleDevAutomation() = default;

DolphinTabWidget *WhaleDevAutomation::tabWidget() const
{
    return m_window->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"));
}

QList<DolphinViewContainer *> WhaleDevAutomation::panes() const
{
    QList<DolphinViewContainer *> result;
    DolphinTabPage *page = tabWidget()->currentTabPage();
    if (!page) {
        return result;
    }
    result.append(page->primaryViewContainer());
    if (page->splitViewEnabled() && page->secondaryViewContainer()) {
        result.append(page->secondaryViewContainer());
    }
    return result;
}

DolphinViewContainer *WhaleDevAutomation::pane(int index, QString *error) const
{
    const QList<DolphinViewContainer *> all = panes();
    if (index < 0) {
        return m_window->activeViewContainer();
    }
    if (index >= all.count()) {
        *error = QStringLiteral("no pane %1 (the current tab has %2)").arg(index).arg(all.count());
        return nullptr;
    }
    return all.at(index);
}

int WhaleDevAutomation::paneIndex(const DolphinViewContainer *container) const
{
    return panes().indexOf(const_cast<DolphinViewContainer *>(container));
}

QJsonObject WhaleDevAutomation::paneState(DolphinViewContainer *container, int index) const
{
    DolphinView *view = container->view();
    WhaleViewHost *host = container->viewHost();
    QWidget *pluginWidget = host->isDolphinViewShown() ? nullptr : host->currentView()->widget();
    QWidget *focus = m_window->focusWidget();

    QJsonArray items;
    const KFileItemList fileItems = view->items();
    for (int i = 0; i < fileItems.count() && i < MaxListedItems; ++i) {
        items.append(fileItems.at(i).text());
    }

    QJsonValue pluginRows = QJsonValue::Null;
    if (auto itemView = qobject_cast<QAbstractItemView *>(pluginWidget)) {
        QJsonArray rows;
        const QAbstractItemModel *model = itemView->model();
        for (int row = 0; model && row < model->rowCount() && row < MaxListedItems; ++row) {
            rows.append(model->index(row, 0).data().toString());
        }
        pluginRows = rows;
    }

    QJsonArray selection;
    const KFileItemList selected = view->selectedItems();
    for (int i = 0; i < selected.count() && i < MaxListedItems; ++i) {
        selection.append(selected.at(i).url().toString());
    }

    QString sortField = QStringLiteral("role:") + QString::fromLatin1(view->sortRole());
    for (const auto &[field, role] : sortFields()) {
        if (role == view->sortRole()) {
            sortField = field;
        }
    }

    bool loading = false;
    const auto listers = view->findChildren<KCoreDirLister *>();
    for (const KCoreDirLister *lister : listers) {
        loading = loading || !lister->isFinished();
    }

    QString focusProxy = QStringLiteral("none");
    if (QWidget *proxy = view->focusProxy()) {
        if (pluginWidget && proxy == pluginWidget) {
            focusProxy = QStringLiteral("plugin-widget");
        } else if (view->isAncestorOf(proxy)) {
            focusProxy = QStringLiteral("dolphin-item-view");
        } else {
            focusProxy = QStringLiteral("other");
        }
    }

    return {
        {QStringLiteral("index"), index},
        {QStringLiteral("side"), index == 0 ? QStringLiteral("left") : QStringLiteral("right")},
        {QStringLiteral("active"), container->isActive()},
        {QStringLiteral("url"), container->url().toString()},
        {QStringLiteral("viewId"), semanticViewId(host->currentViewId())},
        {QStringLiteral("dolphinViewVisible"), view->isVisible()},
        {QStringLiteral("pluginWidgetVisible"), pluginWidget ? QJsonValue(pluginWidget->isVisible()) : QJsonValue::Null},
        {QStringLiteral("viewMode"), viewModeName(view->viewMode())},
        {QStringLiteral("loading"), loading},
        {QStringLiteral("itemCount"), fileItems.count()},
        {QStringLiteral("items"), items},
        {QStringLiteral("pluginRows"), pluginRows},
        {QStringLiteral("selectionCount"), selected.count()},
        {QStringLiteral("selection"), selection},
        {QStringLiteral("hiddenFiles"), view->hiddenFilesShown()},
        {QStringLiteral("sort"),
         QJsonObject{
             {QStringLiteral("field"), sortField},
             {QStringLiteral("order"), view->sortOrder() == Qt::AscendingOrder ? QStringLiteral("ascending") : QStringLiteral("descending")},
             {QStringLiteral("foldersFirst"), view->sortFoldersFirst()},
         }},
        {QStringLiteral("filter"),
         QJsonObject{
             {QStringLiteral("text"), view->nameFilter()},
             {QStringLiteral("barVisible"), container->isFilterBarVisible()},
         }},
        {QStringLiteral("focusProxy"), focusProxy},
        {QStringLiteral("hiddenDolphinViewHasFocus"), pluginWidget != nullptr && contains(view, focus)},
    };
}

QJsonObject WhaleDevAutomation::uiState() const
{
    QJsonArray paneStates;
    const QList<DolphinViewContainer *> all = panes();
    for (int i = 0; i < all.count(); ++i) {
        paneStates.append(paneState(all.at(i), i));
    }

    QJsonArray views{dolphinId};
    if (auto viewActions = m_window->findChild<WhaleViewActions *>()) {
        const auto plugins = viewActions->registry()->plugins();
        for (const KPluginMetaData &metaData : plugins) {
            views.append(metaData.pluginId());
        }
    }

    QJsonArray recent;
    for (const Activation &activation : m_recentActivations) {
        recent.append(QJsonObject{
            {QStringLiteral("seq"), activation.seq},
            {QStringLiteral("tab"), activation.tab},
            {QStringLiteral("pane"), activation.pane},
            {QStringLiteral("focusRole"), activation.focusRole},
        });
    }

    DolphinViewContainer *active = m_window->activeViewContainer();
    DolphinTabWidget *tabs = tabWidget();
    return {
        {QStringLiteral("schema"), 1},
        {QStringLiteral("pid"), QCoreApplication::applicationPid()},
        {QStringLiteral("window"),
         QJsonObject{
             {QStringLiteral("visible"), m_window->isVisible()},
             {QStringLiteral("active"), m_window->isActiveWindow()},
         }},
        {QStringLiteral("tabs"), QJsonObject{{QStringLiteral("count"), tabs->count()}, {QStringLiteral("current"), tabs->currentIndex()}}},
        {QStringLiteral("splitView"), all.count() > 1},
        {QStringLiteral("paneCount"), all.count()},
        {QStringLiteral("activePane"), paneIndex(active)},
        {QStringLiteral("url"), active ? active->url().toString() : QString()},
        {QStringLiteral("availableViews"), views},
        {QStringLiteral("panes"), paneStates},
        {QStringLiteral("focus"), focusState()},
        {QStringLiteral("activations"), QJsonObject{{QStringLiteral("count"), m_activationCount}, {QStringLiteral("recent"), recent}}},
    };
}

QString WhaleDevAutomation::focusRole(QWidget *focus, int *paneIndexOut) const
{
    *paneIndexOut = -1;
    if (!focus) {
        return QStringLiteral("none");
    }

    const QList<DolphinViewContainer *> all = panes();
    for (int i = 0; i < all.count(); ++i) {
        DolphinViewContainer *container = all.at(i);
        WhaleViewHost *host = container->viewHost();
        DolphinView *view = container->view();
        QWidget *pluginWidget = host->isDolphinViewShown() ? nullptr : host->currentView()->widget();
        *paneIndexOut = i;
        if (contains(pluginWidget, focus)) {
            return QStringLiteral("plugin:") + host->currentViewId();
        }
        if (contains(view, focus)) {
            if (pluginWidget) {
                return QStringLiteral("hidden-dolphin-view");
            }
            if (qobject_cast<KItemListRoleEditor *>(focus)) {
                return QStringLiteral("dolphin-rename-editor");
            }
            return QStringLiteral("dolphin-item-view");
        }
        // The location bar may live in the toolbar, outside of the container.
        if (contains(container->urlNavigator(), focus)) {
            return QStringLiteral("location-bar");
        }
        if (contains(container->findChild<Search::Bar *>(), focus)) {
            return QStringLiteral("search-field");
        }
        if (contains(container->findChild<FilterBar *>(), focus)) {
            return QStringLiteral("filter-bar");
        }
        if (contains(container, focus)) {
            return QStringLiteral("pane-other");
        }
    }
    *paneIndexOut = -1;

    static const QList<std::pair<QString, QString>> docks = {
        {QStringLiteral("placesDock"), QStringLiteral("places-panel")},
        {QStringLiteral("terminalDock"), QStringLiteral("terminal-panel")},
        {QStringLiteral("foldersDock"), QStringLiteral("folders-panel")},
        {QStringLiteral("infoDock"), QStringLiteral("information-panel")},
    };
    for (QWidget *widget = focus; widget; widget = widget->parentWidget()) {
        if (auto dock = qobject_cast<QDockWidget *>(widget)) {
            for (const auto &[name, role] : docks) {
                if (dock->objectName() == name) {
                    return role;
                }
            }
            return QStringLiteral("other-panel");
        }
        if (qobject_cast<KToolBar *>(widget)) {
            return QStringLiteral("toolbar");
        }
        if (qobject_cast<QMenu *>(widget) || qobject_cast<QMenuBar *>(widget)) {
            return QStringLiteral("menu");
        }
    }
    if (focus->window() != m_window) {
        return QStringLiteral("other-window");
    }
    return QStringLiteral("other");
}

QJsonObject WhaleDevAutomation::focusState() const
{
    // The focus widget of the window: it has the keyboard focus while the window is
    // active and gets it back on activation. (QApplication::focusWidget() is null while
    // another application is active, e.g. when the compositor did not activate Whale.)
    QWidget *focus = m_window->focusWidget();
    const bool keyboardFocus = m_window->isActiveWindow() && focus && focus == QApplication::focusWidget();
    int focusPane = -1;
    const QString role = focusRole(focus, &focusPane);
    const int activePane = paneIndex(m_window->activeViewContainer());

    QJsonArray anomalies;
    if (role == QLatin1String("hidden-dolphin-view")) {
        anomalies.append(QStringLiteral("focus-in-hidden-dolphin-view"));
    }
    const bool inView = role.startsWith(QLatin1String("plugin:")) || role.startsWith(QLatin1String("dolphin-"));
    if (inView && focusPane != activePane) {
        anomalies.append(QStringLiteral("focus-in-inactive-pane"));
    }
    const QList<DolphinViewContainer *> all = panes();
    for (int i = 0; i < all.count(); ++i) {
        WhaleViewHost *host = all.at(i)->viewHost();
        if (!host->isDolphinViewShown() && all.at(i)->view()->focusProxy() != host->currentView()->widget()) {
            anomalies.append(QStringLiteral("pane-%1-focus-proxy-not-plugin").arg(i));
        }
    }

    return {
        {QStringLiteral("role"), role},
        {QStringLiteral("pane"), focusPane >= 0 ? QJsonValue(focusPane) : QJsonValue::Null},
        {QStringLiteral("activePane"), activePane},
        {QStringLiteral("widgetClass"), focus ? QString::fromLatin1(focus->metaObject()->className()) : QString()},
        {QStringLiteral("windowActive"), m_window->isActiveWindow()},
        {QStringLiteral("keyboardFocus"), keyboardFocus},
        {QStringLiteral("anomalies"), anomalies},
    };
}

void WhaleDevAutomation::recordActivation(DolphinViewContainer *container)
{
    int pane = -1;
    Activation activation{++m_activationCount, tabWidget()->currentIndex(), paneIndex(container), focusRole(m_window->focusWidget(), &pane)};
    m_recentActivations.append(activation);
    if (m_recentActivations.count() > 10) {
        m_recentActivations.removeFirst();
    }
}

QJsonObject WhaleDevAutomation::setView(int paneIndexArg, const QString &viewId)
{
    QString error;
    DolphinViewContainer *container = pane(paneIndexArg, &error);
    if (!container) {
        return failure(error);
    }
    WhaleViewHost *host = container->viewHost();
    if (viewId == dolphinId) {
        host->showDolphinView();
        return ok();
    }
    auto viewActions = m_window->findChild<WhaleViewActions *>();
    WhaleViewPlugin *plugin = viewActions ? viewActions->registry()->plugin(viewId) : nullptr;
    if (!plugin) {
        return failure(QStringLiteral("unknown view \"%1\"").arg(viewId));
    }
    if (!host->showPluginView(viewId, plugin)) {
        return failure(QStringLiteral("the view \"%1\" could not be shown").arg(viewId));
    }
    return ok();
}

QJsonObject WhaleDevAutomation::activatePane(int paneIndexArg)
{
    QString error;
    DolphinViewContainer *container = pane(paneIndexArg, &error);
    if (!container) {
        return failure(error);
    }
    container->setActive(true);
    return ok();
}

QJsonObject WhaleDevAutomation::setSplitView(bool enabled)
{
    DolphinTabPage *page = tabWidget()->currentTabPage();
    if (page->splitViewEnabled() != enabled) {
        // The same path as the "Split" toolbar button.
        page->setSplitViewEnabled(enabled, WithAnimation);
    }
    return ok();
}

QJsonObject WhaleDevAutomation::setLocation(int paneIndexArg, const QString &url)
{
    QString error;
    DolphinViewContainer *container = pane(paneIndexArg, &error);
    if (!container) {
        return failure(error);
    }
    const QUrl target = QUrl::fromUserInput(url, QString(), QUrl::AssumeLocalFile);
    if (!target.isValid()) {
        return failure(QStringLiteral("invalid URL \"%1\"").arg(url));
    }
    // Through the location bar, so history and focus behave as when the user navigates.
    container->setUrl(target);
    return ok();
}

QJsonObject WhaleDevAutomation::setFilterText(int paneIndexArg, const QString &text)
{
    QString error;
    DolphinViewContainer *container = pane(paneIndexArg, &error);
    if (!container) {
        return failure(error);
    }
    // Like typing into the filter bar: it opens with the focus.
    container->setFilterBarVisible(true);
    auto input = container->findChild<FilterBar *>()->findChild<QLineEdit *>();
    if (!input) {
        return failure(QStringLiteral("the filter bar has no input field"));
    }
    input->setText(text);
    return ok();
}

QJsonObject WhaleDevAutomation::clearFilter(int paneIndexArg)
{
    QString error;
    DolphinViewContainer *container = pane(paneIndexArg, &error);
    if (!container) {
        return failure(error);
    }
    // Closing the filter bar clears it and returns the focus to the view.
    container->setFilterBarVisible(false);
    return ok();
}

QJsonObject WhaleDevAutomation::setHiddenFiles(int paneIndexArg, bool shown)
{
    QString error;
    DolphinViewContainer *container = pane(paneIndexArg, &error);
    if (!container) {
        return failure(error);
    }
    container->view()->setHiddenFilesShown(shown);
    return ok();
}

QJsonObject WhaleDevAutomation::setSort(int paneIndexArg, const QString &field, const QString &order, std::optional<bool> foldersFirst)
{
    QString error;
    DolphinViewContainer *container = pane(paneIndexArg, &error);
    if (!container) {
        return failure(error);
    }
    QByteArray role;
    QStringList known;
    for (const auto &[name, modelRole] : sortFields()) {
        known.append(name);
        if (name == field) {
            role = modelRole;
        }
    }
    if (!field.isEmpty() && role.isEmpty()) {
        return failure(QStringLiteral("unknown sort field \"%1\", expected one of %2").arg(field, known.join(QStringLiteral(", "))));
    }
    if (!order.isEmpty() && order != QLatin1String("ascending") && order != QLatin1String("descending")) {
        return failure(QStringLiteral("unknown sort order \"%1\", expected ascending or descending").arg(order));
    }
    DolphinView *view = container->view();
    // setSortRole() switches to the preferred order of the role, so the order comes after it.
    if (!role.isEmpty()) {
        view->setSortRole(role);
    }
    if (!order.isEmpty()) {
        view->setSortOrder(order == QLatin1String("ascending") ? Qt::AscendingOrder : Qt::DescendingOrder);
    }
    if (foldersFirst) {
        view->setSortFoldersFirst(*foldersFirst);
    }
    return ok();
}

QStringList WhaleDevAutomation::actionIds()
{
    QStringList ids;
    for (const auto &[id, name] : actions()) {
        ids.append(id);
    }
    return ids;
}

QJsonObject WhaleDevAutomation::triggerAction(const QString &actionId)
{
    for (const auto &[id, name] : actions()) {
        if (id != actionId) {
            continue;
        }
        QAction *action = m_window->actionCollection()->action(name);
        if (!action) {
            return failure(QStringLiteral("the action \"%1\" does not exist in this window").arg(actionId));
        }
        if (!action->isEnabled()) {
            return failure(QStringLiteral("the action \"%1\" is disabled").arg(actionId));
        }
        action->trigger();
        return ok();
    }
    return failure(QStringLiteral("unknown or not allowed action \"%1\", allowed: %2").arg(actionId, actionIds().join(QStringLiteral(", "))));
}

QJsonObject WhaleDevAutomation::evaluate(const QJsonObject &conditions) const
{
    const QJsonValue state = uiState();
    QJsonArray mismatches;
    for (auto it = conditions.begin(); it != conditions.end(); ++it) {
        const QJsonValue actual = valueAt(state, it.key());
        if (actual != it.value()) {
            mismatches.append(QJsonObject{
                {QStringLiteral("path"), it.key()},
                {QStringLiteral("expected"), it.value()},
                {QStringLiteral("actual"), actual.isUndefined() ? QJsonValue(QStringLiteral("<missing>")) : actual},
            });
        }
    }
    return {{QStringLiteral("matched"), mismatches.isEmpty()}, {QStringLiteral("mismatches"), mismatches}};
}

void WhaleDevAutomation::waitForState(const QJsonObject &conditions, int timeoutMs, std::function<void(const QJsonObject &)> done)
{
    // The conditions are checked whenever the event loop is idle for a moment, so
    // nothing waits longer than needed. No fixed delay decides the result.
    auto timer = new QTimer(this);
    auto clock = std::make_shared<QElapsedTimer>();
    auto finished = std::make_shared<bool>(false);
    clock->start();
    auto check = [this, timer, clock, finished, conditions, timeoutMs, done]() {
        if (*finished) {
            return;
        }
        QJsonObject result = evaluate(conditions);
        const bool matched = result.value(QStringLiteral("matched")).toBool();
        const bool timedOut = !matched && clock->elapsed() >= timeoutMs;
        if (!matched && !timedOut) {
            return;
        }
        *finished = true;
        timer->stop();
        timer->deleteLater();
        result.insert(QStringLiteral("timedOut"), timedOut);
        result.insert(QStringLiteral("elapsedMs"), qint64(clock->elapsed()));
        result.insert(QStringLiteral("state"), uiState());
        done(result);
    };
    connect(timer, &QTimer::timeout, this, check);
    timer->start(20);
    QTimer::singleShot(0, timer, check);
}

QByteArray WhaleDevAutomation::screenshot() const
{
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    m_window->grab().save(&buffer, "PNG");
    return png;
}

#include "moc_whaledevautomation.cpp"
