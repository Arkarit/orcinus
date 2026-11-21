/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaiwindowtools.h"

#include "config-dolphin.h"
#include "dolphinmainwindow.h"
#include "dolphintabpage.h"
#include "dolphintabwidget.h"
#include "dolphinviewcontainer.h"
#include "filterbar/filterbar.h"
#include "views/dolphinview.h"
#include "whalestylemanager.h"
#include "whalestyles.h"
#include "whaleviewer.h"
#include "whaleviewerlist.h"
#if HAVE_MPV
#include "whalevideoplayer.h"
#endif

#include <KLocalizedString>
#include <KMessageWidget>

#include <QDir>
#include <QDockWidget>
#include <QFileInfo>
#include <QJsonArray>
#include <QPointer>
#include <QScreen>
#include <QSplitter>

#include <cmath>

namespace
{
constexpr int DefaultItemLimit = 200;
constexpr int MaxItemLimit = 500;

QJsonObject property(const QString &type, const QString &description, const QStringList &choices = {})
{
    QJsonObject result{{QStringLiteral("type"), type}, {QStringLiteral("description"), description}};
    if (!choices.isEmpty()) {
        result.insert(QStringLiteral("enum"), QJsonArray::fromStringList(choices));
    }
    return result;
}

QJsonObject failed(bool *error, const QString &message)
{
    *error = true;
    return {{QStringLiteral("error"), message}};
}

struct Panel {
    QString id;
    QString dockName;
};

const QList<Panel> &panels()
{
    static const QList<Panel> list{
        {QStringLiteral("places"), QStringLiteral("placesDock")},
        {QStringLiteral("information"), QStringLiteral("infoDock")},
        {QStringLiteral("folders"), QStringLiteral("foldersDock")},
        {QStringLiteral("terminal"), QStringLiteral("terminalDock")},
        {QStringLiteral("ai"), QStringLiteral("aiDock")},
    };
    return list;
}

QStringList panelIds()
{
    QStringList ids;
    for (const Panel &panel : panels()) {
        ids.append(panel.id);
    }
    return ids;
}

DolphinTabPage *currentTabPage(DolphinMainWindow *window)
{
    auto *tabs = window->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"));
    return tabs ? tabs->currentTabPage() : nullptr;
}

/** The views of the current tab: one, or left and right. */
QList<DolphinViewContainer *> views(DolphinMainWindow *window)
{
    DolphinTabPage *page = currentTabPage(window);
    if (!page) {
        return {};
    }
    QList<DolphinViewContainer *> result{page->primaryViewContainer()};
    if (page->splitViewEnabled() && page->secondaryViewContainer()) {
        result.append(page->secondaryViewContainer());
    }
    return result;
}

QString viewName(DolphinMainWindow *window, DolphinViewContainer *container)
{
    const QList<DolphinViewContainer *> all = views(window);
    if (all.size() < 2) {
        return QStringLiteral("main");
    }
    return all.indexOf(container) == 0 ? QStringLiteral("left") : QStringLiteral("right");
}

/** The messages shown above @p container's view that the user can close. */
QList<KMessageWidget *> messages(DolphinViewContainer *container)
{
    QList<KMessageWidget *> result;
    const auto widgets = container->findChildren<KMessageWidget *>();
    for (KMessageWidget *widget : widgets) {
        if (widget->isVisible() && !widget->isHideAnimationRunning() && widget->isCloseButtonVisible()) {
            result.append(widget);
        }
    }
    return result;
}

QString messageType(KMessageWidget::MessageType type)
{
    switch (type) {
    case KMessageWidget::Positive:
        return QStringLiteral("positive");
    case KMessageWidget::Information:
        return QStringLiteral("information");
    case KMessageWidget::Warning:
        return QStringLiteral("warning");
    case KMessageWidget::Error:
        return QStringLiteral("error");
    }
    return QString();
}

QString windowState(const QWidget *window)
{
    if (window->isFullScreen()) {
        return QStringLiteral("fullscreen");
    }
    return window->isMaximized() ? QStringLiteral("maximized") : QStringLiteral("normal");
}

QJsonObject viewerState()
{
    WhaleViewer *viewer = WhaleViewer::instance();
    if (!viewer) {
        return {{QStringLiteral("open"), false}};
    }
    QJsonObject state{{QStringLiteral("open"), true},
                      {QStringLiteral("file"), viewer->list().currentItem().localPath()},
                      {QStringLiteral("index"), viewer->list().currentIndex()},
                      {QStringLiteral("count"), viewer->list().count()},
                      {QStringLiteral("video"), viewer->isVideo()}};
#if HAVE_MPV
    if (viewer->isVideo() && viewer->videoPlayer()) {
        state.insert(QStringLiteral("paused"), viewer->videoPlayer()->isPaused());
    }
#endif
    return state;
}

QString sortFieldName(const QByteArray &role)
{
    // The names of the Sort By menu, as the agent knows them from list_actions.
    if (role == "text") {
        return QStringLiteral("name");
    }
    if (role == "modificationtime") {
        return QStringLiteral("modified");
    }
    if (role == "creationtime") {
        return QStringLiteral("created");
    }
    if (role == "accesstime") {
        return QStringLiteral("accessed");
    }
    return QString::fromLatin1(role);
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
    case DolphinView::ColumnsView:
        return QStringLiteral("columns");
    }
    return QString();
}

QString folderOf(DolphinViewContainer *container)
{
    return container->url().isLocalFile() ? container->url().toLocalFile() : container->rootItem().localPath();
}

/** What a view shows and how: its folder, mode, sorting and hidden files. */
QJsonObject viewState(DolphinMainWindow *window, DolphinViewContainer *container)
{
    const DolphinView *view = container->view();
    const QString folder = folderOf(container);
    return {{QStringLiteral("view"), viewName(window, container)},
            {QStringLiteral("folder"), folder.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(folder)},
            {QStringLiteral("mode"), viewModeName(view->viewMode())},
            {QStringLiteral("sort"),
             QJsonObject{{QStringLiteral("by"), sortFieldName(view->sortRole())},
                         {QStringLiteral("order"), view->sortOrder() == Qt::DescendingOrder ? QStringLiteral("descending") : QStringLiteral("ascending")},
                         {QStringLiteral("foldersFirst"), view->sortFoldersFirst()}}},
            {QStringLiteral("hiddenFiles"), view->hiddenFilesShown()},
            {QStringLiteral("grouped"), view->groupedSorting()},
            {QStringLiteral("previews"), view->previewsShown()},
            {QStringLiteral("filter"), QJsonObject{{QStringLiteral("shown"), container->isFilterBarVisible()}, {QStringLiteral("text"), view->nameFilter()}}},
            {QStringLiteral("itemCount"), int(view->items().count())}};
}

QJsonObject getWindowState(DolphinMainWindow *window)
{
    QJsonObject windowObject{{QStringLiteral("width"), window->width()},
                             {QStringLiteral("height"), window->height()},
                             {QStringLiteral("state"), windowState(window)}};
    if (const QScreen *screen = window->screen()) {
        const QRect available = screen->availableGeometry();
        windowObject.insert(QStringLiteral("screen"),
                            QJsonObject{{QStringLiteral("width"), available.width()}, {QStringLiteral("height"), available.height()}});
    }

    QJsonArray panelArray;
    for (const Panel &panel : panels()) {
        const auto *dock = window->findChild<QDockWidget *>(panel.dockName);
        if (!dock) {
            continue;
        }
        QJsonObject entry{{QStringLiteral("id"), panel.id},
                          {QStringLiteral("title"), KLocalizedString::removeAcceleratorMarker(dock->windowTitle())},
                          {QStringLiteral("visible"), dock->isVisible()}};
        if (dock->isVisible()) {
            entry.insert(QStringLiteral("width"), dock->width());
            entry.insert(QStringLiteral("height"), dock->height());
            if (dock->isFloating()) {
                entry.insert(QStringLiteral("floating"), true);
            }
        }
        panelArray.append(entry);
    }

    const QList<DolphinViewContainer *> all = views(window);
    QJsonObject split{{QStringLiteral("enabled"), all.size() > 1}};
    if (all.size() > 1) {
        const int left = all.at(0)->width();
        const int right = all.at(1)->width();
        split.insert(QStringLiteral("leftShare"), left + right > 0 ? std::round(100.0 * left / (left + right)) / 100 : 0.5);
        split.insert(QStringLiteral("activeView"), viewName(window, window->activeViewContainer()));
    }

    QJsonArray messageArray;
    for (DolphinViewContainer *container : all) {
        for (KMessageWidget *message : messages(container)) {
            messageArray.append(QJsonObject{{QStringLiteral("view"), viewName(window, container)},
                                            {QStringLiteral("type"), messageType(message->messageType())},
                                            {QStringLiteral("text"), message->text()}});
        }
    }

    QJsonArray viewArray;
    for (DolphinViewContainer *container : all) {
        viewArray.append(viewState(window, container));
    }

    // The tabs: the views above are those of the current one.
    QJsonArray tabArray;
    int currentTab = -1;
    if (auto *tabs = window->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"))) {
        currentTab = tabs->currentIndex();
        for (int i = 0; i < tabs->count(); ++i) {
            DolphinTabPage *page = tabs->tabPageAt(i);
            if (!page) {
                continue;
            }
            QJsonArray folders{folderOf(page->primaryViewContainer())};
            if (page->splitViewEnabled() && page->secondaryViewContainer()) {
                folders.append(folderOf(page->secondaryViewContainer()));
            }
            tabArray.append(QJsonObject{{QStringLiteral("index"), i}, {QStringLiteral("folders"), folders}});
        }
    }

    // The profile applied or saved last; the window may have been changed since.
    QJsonValue profile = QJsonValue::Null;
    const QString profileId = WhaleStyleManager::ensure(window)->lastStyleId();
    for (const WhaleStyle &style : WhaleStyles::self()->store().styles()) {
        if (style.id == profileId) {
            profile = QJsonObject{{QStringLiteral("id"), style.id}, {QStringLiteral("name"), style.name}};
        }
    }

    return {{QStringLiteral("window"), windowObject},
            {QStringLiteral("tabs"), QJsonObject{{QStringLiteral("current"), currentTab}, {QStringLiteral("list"), tabArray}}},
            {QStringLiteral("views"), viewArray},
            {QStringLiteral("lastProfile"), profile},
            {QStringLiteral("panels"), panelArray},
            {QStringLiteral("splitView"), split},
            {QStringLiteral("messages"), messageArray},
            {QStringLiteral("viewer"), viewerState()}};
}

QJsonObject setWindowState(DolphinMainWindow *window, const QJsonObject &arguments, bool *error, QString *notice)
{
    const QString state = arguments.value(QLatin1String("state")).toString();
    const QJsonValue widthValue = arguments.value(QLatin1String("width"));
    const QJsonValue heightValue = arguments.value(QLatin1String("height"));
    if (!state.isEmpty() && state != QLatin1String("normal") && state != QLatin1String("maximized") && state != QLatin1String("fullscreen")) {
        return failed(error, QStringLiteral("\"state\" must be normal, maximized or fullscreen; minimizing is not available."));
    }
    const bool size = !widthValue.isUndefined() || !heightValue.isUndefined();
    if (size && (!widthValue.isDouble() || !heightValue.isDouble())) {
        return failed(error, QStringLiteral("Give both \"width\" and \"height\" in pixels."));
    }
    if (size && state != QLatin1String("normal") && !state.isEmpty()) {
        return failed(error, QStringLiteral("A size applies to the normal state only."));
    }
    if (state.isEmpty() && !size) {
        return failed(error, QStringLiteral("Give \"state\", or \"width\" and \"height\"."));
    }
    if (state == QLatin1String("maximized")) {
        window->showMaximized();
        *notice = i18nc("@info", "The agent maximized the window.");
    } else if (state == QLatin1String("fullscreen")) {
        window->showFullScreen();
        *notice = i18nc("@info", "The agent switched the window to full screen.");
    } else {
        if (window->isMaximized() || window->isFullScreen()) {
            window->showNormal();
        }
        *notice = i18nc("@info", "The agent restored the window to its normal size.");
    }
    if (size) {
        // Within the screen; a window manager may still adjust it.
        const QRect available = window->screen() ? window->screen()->availableGeometry() : QRect(0, 0, 8000, 8000);
        const int width = std::clamp(widthValue.toInt(), 400, std::max(400, available.width()));
        const int height = std::clamp(heightValue.toInt(), 300, std::max(300, available.height()));
        window->resize(width, height);
        *notice = i18nc("@info %1 and %2 are pixels", "The agent resized the window to %1 × %2.", width, height);
    }
    return {{QStringLiteral("requested"), state.isEmpty() ? QStringLiteral("normal") : state},
            {QStringLiteral("note"), QStringLiteral("The window manager applies it; read get_window_state for the result. Windows cannot be moved.")}};
}

QJsonObject setPanelWidth(DolphinMainWindow *window, const QJsonObject &arguments, bool *error, QString *notice)
{
    const QString id = arguments.value(QLatin1String("panel")).toString();
    const QJsonValue widthValue = arguments.value(QLatin1String("width"));
    QDockWidget *dock = nullptr;
    for (const Panel &panel : panels()) {
        if (panel.id == id) {
            dock = window->findChild<QDockWidget *>(panel.dockName);
        }
    }
    if (!dock) {
        return failed(error, QStringLiteral("\"panel\" must be one of %1.").arg(panelIds().join(QStringLiteral(", "))));
    }
    if (!widthValue.isDouble()) {
        return failed(error, QStringLiteral("\"width\" must be a number of pixels."));
    }
    if (!dock->isVisible()) {
        return failed(error, QStringLiteral("The panel is hidden; show it first (list_actions \"panels\")."));
    }
    const int width = std::clamp(widthValue.toInt(), 80, std::max(80, window->width() - 200));
    if (dock->isFloating()) {
        dock->resize(width, dock->height());
    } else {
        window->resizeDocks({dock}, {width}, Qt::Horizontal);
    }
    *notice = i18nc("@info %1 is a panel, %2 pixels",
                    "The agent set the width of the %1 panel to %2 pixels.",
                    KLocalizedString::removeAcceleratorMarker(dock->windowTitle()),
                    width);
    return {{QStringLiteral("panel"), id}, {QStringLiteral("width"), width}};
}

QJsonObject setSplitRatio(DolphinMainWindow *window, const QJsonObject &arguments, bool *error, QString *notice)
{
    const QList<DolphinViewContainer *> all = views(window);
    auto *splitter = all.size() > 1 ? qobject_cast<QSplitter *>(all.at(0)->parentWidget()) : nullptr;
    if (!splitter || splitter->count() < 2) {
        return failed(error, QStringLiteral("There is no split view (see list_actions \"split\")."));
    }
    const QJsonValue shareValue = arguments.value(QLatin1String("leftShare"));
    if (!shareValue.isDouble() || shareValue.toDouble() < 0.1 || shareValue.toDouble() > 0.9) {
        return failed(error, QStringLiteral("\"leftShare\" must be between 0.1 and 0.9."));
    }
    const double share = shareValue.toDouble();
    const QList<int> sizes = splitter->sizes();
    const int total = sizes.at(0) + sizes.at(1);
    splitter->setSizes({int(total * share), total - int(total * share)});
    const int left = int(std::round(share * 100));
    *notice = i18nc("@info %1 and %2 are percentages", "The agent divided the split view %1 % : %2 %.", left, 100 - left);
    return {{QStringLiteral("leftShare"), share}};
}

DolphinViewContainer *chosenView(DolphinMainWindow *window, const QJsonObject &arguments, bool *error, QJsonObject *failure)
{
    const QString view = arguments.value(QLatin1String("view")).toString();
    const QList<DolphinViewContainer *> all = views(window);
    if (view.isEmpty()) {
        return window->activeViewContainer();
    }
    if ((view != QLatin1String("left") && view != QLatin1String("right")) || all.size() < 2) {
        *failure = failed(error, QStringLiteral("\"view\" is left or right, and only in split view."));
        return nullptr;
    }
    return all.at(view == QLatin1String("left") ? 0 : 1);
}

QJsonObject listItems(DolphinMainWindow *window, const QJsonObject &arguments, bool *error, QString *)
{
    QJsonObject failure;
    DolphinViewContainer *container = chosenView(window, arguments, error, &failure);
    if (!container) {
        return failure.isEmpty() ? failed(error, QStringLiteral("There is no view.")) : failure;
    }
    const QJsonValue offsetValue = arguments.value(QLatin1String("offset"));
    const QJsonValue limitValue = arguments.value(QLatin1String("limit"));
    if ((!offsetValue.isUndefined() && !offsetValue.isDouble()) || (!limitValue.isUndefined() && !limitValue.isDouble())) {
        return failed(error, QStringLiteral("\"offset\" and \"limit\" are numbers."));
    }
    const int offset = qMax(0, offsetValue.toInt(0));
    const int limit = qBound(1, limitValue.toInt(DefaultItemLimit), MaxItemLimit);

    const QString folder = folderOf(container);
    const KFileItemList items = container->view()->items();
    QJsonArray entries;
    for (int i = offset; i < items.count() && entries.size() < limit; ++i) {
        const KFileItem &item = items.at(i);
        // Expanded folders (details view) show their content too: relative to the view's folder.
        const QString path = item.localPath();
        const QString name = !folder.isEmpty() && !path.isEmpty() ? QDir(folder).relativeFilePath(path) : item.text();
        QJsonObject entry{{QStringLiteral("name"), name}, {QStringLiteral("type"), item.isDir() ? QStringLiteral("folder") : item.mimeComment()}};
        if (!item.isDir()) {
            entry.insert(QStringLiteral("size"), double(item.size()));
        }
        const QDateTime modified = item.time(KFileItem::ModificationTime);
        if (modified.isValid()) {
            entry.insert(QStringLiteral("modified"), modified.toString(Qt::ISODate));
        }
        entries.append(entry);
    }
    QJsonObject result = viewState(window, container);
    result.insert(QStringLiteral("offset"), offset);
    result.insert(QStringLiteral("items"), entries);
    result.insert(QStringLiteral("truncated"), offset + entries.size() < items.count());
    return result;
}

QJsonObject setFilter(DolphinMainWindow *window, const QJsonObject &arguments, bool *error, QString *notice)
{
    QJsonObject failure;
    DolphinViewContainer *container = chosenView(window, arguments, error, &failure);
    if (!container) {
        return failure.isEmpty() ? failed(error, QStringLiteral("There is no view.")) : failure;
    }
    const QJsonValue textValue = arguments.value(QLatin1String("text"));
    if (!textValue.isString()) {
        return failed(error, QStringLiteral("\"text\" is the text to filter by, or empty to clear the filter."));
    }
    const QString text = textValue.toString();
    auto *filterBar = container->findChild<FilterBar *>();
    if (!filterBar) {
        return failed(error, QStringLiteral("This view has no filter bar."));
    }
    if (text.isEmpty()) {
        container->clearFilterBar();
        container->setFilterBarVisible(false);
        *notice = i18nc("@info", "The agent cleared the filter.");
    } else {
        // As the user types it: the bar opens and filters while the text changes.
        container->setFilterBarVisible(true);
        filterBar->setText(text);
        *notice = i18nc("@info %1 is a text the view is filtered by", "The agent filtered the view by “%1”.", text);
    }
    return viewState(window, container);
}

QJsonObject dismissMessage(DolphinMainWindow *window, const QJsonObject &arguments, bool *error, QString *notice)
{
    QJsonObject failure;
    DolphinViewContainer *container = chosenView(window, arguments, error, &failure);
    if (!container) {
        return failure.isEmpty() ? failed(error, QStringLiteral("There is no view.")) : failure;
    }
    const QList<KMessageWidget *> shown = messages(container);
    if (shown.isEmpty()) {
        return failed(error, QStringLiteral("No message that can be closed is shown there."));
    }
    for (KMessageWidget *message : shown) {
        message->animatedHide();
    }
    *notice = i18ncp("@info", "The agent closed a message.", "The agent closed %1 messages.", shown.size());
    return {{QStringLiteral("closed"), int(shown.size())}};
}

QJsonObject openInViewer(DolphinMainWindow *window, const QJsonObject &arguments, bool *error, QString *notice)
{
    DolphinViewContainer *container = window->activeViewContainer();
    if (!container) {
        return failed(error, QStringLiteral("There is no view."));
    }
    const QString text = arguments.value(QLatin1String("item")).toString();
    const QString folder = container->url().isLocalFile() ? container->url().toLocalFile() : container->rootItem().localPath();
    if (text.isEmpty() || folder.isEmpty()) {
        return failed(error, QStringLiteral("\"item\" must name a file in the folder of the active view (see get_location)."));
    }
    const QString path = QDir::isAbsolutePath(text) ? QDir::cleanPath(text) : QDir::cleanPath(QDir(folder).filePath(text));
    KFileItem found;
    const KFileItemList items = container->view()->items();
    for (const KFileItem &item : items) {
        if (item.localPath() == path) {
            found = item;
            break;
        }
    }
    if (found.isNull()) {
        return failed(error, QStringLiteral("Not shown in the active view: %1").arg(path));
    }
    if (found.isDir() || !WhaleViewer::openItem(found, container->view(), window)) {
        return failed(error, QStringLiteral("The viewer cannot show %1 (images and videos only, as set in the settings).").arg(found.name()));
    }
    *notice = i18nc("@info %1 is a file name", "The agent opened “%1” in the viewer.", found.name());
    return viewerState();
}

QJsonObject controlViewer(const QJsonObject &arguments, bool *error, QString *notice)
{
    WhaleViewer *viewer = WhaleViewer::instance();
    if (!viewer) {
        return failed(error, QStringLiteral("The viewer is not open (see open_in_viewer)."));
    }
    const QString action = arguments.value(QLatin1String("action")).toString();
    if (action == QLatin1String("next") || action == QLatin1String("previous")) {
        viewer->step(action == QLatin1String("next") ? 1 : -1);
    } else if (action == QLatin1String("first")) {
        viewer->showIndex(0);
    } else if (action == QLatin1String("last")) {
        viewer->showIndex(viewer->list().count() - 1);
    } else if (action == QLatin1String("close")) {
        viewer->close();
        *notice = i18nc("@info", "The agent closed the viewer.");
        return {{QStringLiteral("open"), false}};
    } else if (action == QLatin1String("play") || action == QLatin1String("pause")) {
#if HAVE_MPV
        if (!viewer->isVideo() || !viewer->videoPlayer()) {
            return failed(error, QStringLiteral("The viewer shows no video."));
        }
        viewer->videoPlayer()->setPaused(action == QLatin1String("pause"));
        *notice = action == QLatin1String("pause") ? i18nc("@info", "The agent paused the video.") : i18nc("@info", "The agent played the video.");
        return viewerState();
#else
        return failed(error, QStringLiteral("This Orcinus plays no videos."));
#endif
    } else {
        return failed(error, QStringLiteral("\"action\" must be next, previous, first, last, play, pause or close."));
    }
    *notice = i18nc("@info %1 is a file name", "The agent showed “%1” in the viewer.", viewer->list().currentItem().name());
    return viewerState();
}
}

QList<WhaleAiWindowTool> WhaleAiWindowTools::create(DolphinMainWindow *window)
{
    QPointer<DolphinMainWindow> guard(window);
    auto bind = [guard](auto function) {
        return [guard, function](const QJsonObject &arguments, bool *error, QString *notice) -> QJsonObject {
            if (!guard) {
                return failed(error, QStringLiteral("The window is gone."));
            }
            return function(guard.data(), arguments, error, notice);
        };
    };
    const QString pane = QStringLiteral("Split view only: left or right; else the active view");
    return {
        {QStringLiteral("get_window_state"),
         QStringLiteral("Get the state of the Orcinus window"),
         QStringLiteral("Returns the Orcinus window the user is chatting from: size and state (normal, maximized, fullscreen), the tabs "
                        "(folders of each), the views of the current tab (folder, mode, sorting, grouping, previews, hidden files, "
                        "filter), the profile applied last, the panels "
                        "(id, visible, width), the split view (left share, active view), the messages shown above the views, and the "
                        "built-in viewer (open, file, index)."),
         true,
         {},
         {},
         bind([](DolphinMainWindow *window, const QJsonObject &, bool *, QString *) {
             return getWindowState(window);
         })},
        {QStringLiteral("list_items"),
         QStringLiteral("List what a view of Orcinus shows"),
         QStringLiteral("Returns the files and folders a view shows, in the order the user sees them, with type, size in bytes (files) "
                        "and modification date, plus the view's folder, mode and sorting. Read-only: use it to answer questions about "
                        "the folder (largest file, newest file, ...) or to check the result of a command. Pages with offset and limit."),
         true,
         {{QStringLiteral("view"), property(QStringLiteral("string"), pane, {QStringLiteral("left"), QStringLiteral("right")})},
          {QStringLiteral("offset"), property(QStringLiteral("integer"), QStringLiteral("First item, default 0"))},
          {QStringLiteral("limit"), property(QStringLiteral("integer"), QStringLiteral("At most this many items, default 200, at most 500"))}},
         {},
         bind(listItems),
         {QStringLiteral("view"), QStringLiteral("offset"), QStringLiteral("limit")}},
        {QStringLiteral("set_window_state"),
         QStringLiteral("Resize or maximize the Orcinus window"),
         QStringLiteral("Sets the Orcinus window to normal, maximized or fullscreen, or resizes it (width and height in pixels, normal "
                        "state). It cannot be minimized or moved."),
         false,
         {{QStringLiteral("state"),
           property(QStringLiteral("string"),
                    QStringLiteral("normal, maximized or fullscreen"),
                    {QStringLiteral("normal"), QStringLiteral("maximized"), QStringLiteral("fullscreen")})},
          {QStringLiteral("width"), property(QStringLiteral("integer"), QStringLiteral("Width in pixels (with height)"))},
          {QStringLiteral("height"), property(QStringLiteral("integer"), QStringLiteral("Height in pixels (with width)"))}},
         {},
         bind(setWindowState),
         {QStringLiteral("state"), QStringLiteral("width"), QStringLiteral("height")}},
        {QStringLiteral("set_panel_width"),
         QStringLiteral("Set the width of a panel"),
         QStringLiteral("Sets the width of a visible panel of the Orcinus window (Places, Information, Folders, Terminal, AI). Show a "
                        "hidden panel first with trigger_action."),
         false,
         {{QStringLiteral("panel"), property(QStringLiteral("string"), QStringLiteral("Panel id"), panelIds())},
          {QStringLiteral("width"), property(QStringLiteral("integer"), QStringLiteral("Width in pixels"))}},
         {QStringLiteral("panel"), QStringLiteral("width")},
         bind(setPanelWidth)},
        {QStringLiteral("set_split_ratio"),
         QStringLiteral("Divide the split view"),
         QStringLiteral("Sets how much of the split view the left view takes (0.1 to 0.9)."),
         false,
         {{QStringLiteral("leftShare"), property(QStringLiteral("number"), QStringLiteral("Share of the left view, e.g. 0.5"))}},
         {QStringLiteral("leftShare")},
         bind(setSplitRatio)},
        {QStringLiteral("set_filter"),
         QStringLiteral("Filter a view of Orcinus by name"),
         QStringLiteral("Shows only the items whose names contain the text (as the filter bar does; it opens), or clears the filter "
                        "with an empty text. Only what is shown changes, nothing is saved. list_items lists what is left."),
         false,
         {{QStringLiteral("text"), property(QStringLiteral("string"), QStringLiteral("Text to filter by; empty clears the filter"))},
          {QStringLiteral("view"), property(QStringLiteral("string"), pane, {QStringLiteral("left"), QStringLiteral("right")})}},
         {QStringLiteral("text")},
         bind(setFilter)},
        {QStringLiteral("dismiss_message"),
         QStringLiteral("Close a message in Orcinus"),
         QStringLiteral("Closes the messages shown above a view (as their close button does), e.g. \"Current location changed …\". "
                        "get_window_state lists them."),
         false,
         {{QStringLiteral("view"), property(QStringLiteral("string"), pane, {QStringLiteral("left"), QStringLiteral("right")})}},
         {},
         bind(dismissMessage)},
        {QStringLiteral("open_in_viewer"),
         QStringLiteral("Show a file in the built-in viewer"),
         QStringLiteral("Opens an image or video of the active view's folder in Orcinus' built-in viewer, as if the user opened it. "
                        "The viewer covers the screen; control_viewer pages on and closes it."),
         false,
         {{QStringLiteral("item"), property(QStringLiteral("string"), QStringLiteral("Name in the folder of the active view, or its absolute path"))}},
         {QStringLiteral("item")},
         bind(openInViewer)},
        {QStringLiteral("control_viewer"),
         QStringLiteral("Control the built-in viewer"),
         QStringLiteral("Pages the open viewer to the next, previous, first or last file, plays or pauses a video, or closes the viewer."),
         false,
         {{QStringLiteral("action"),
           property(QStringLiteral("string"),
                    QStringLiteral("What to do"),
                    {QStringLiteral("next"),
                     QStringLiteral("previous"),
                     QStringLiteral("first"),
                     QStringLiteral("last"),
                     QStringLiteral("play"),
                     QStringLiteral("pause"),
                     QStringLiteral("close")})}},
         {QStringLiteral("action")},
         bind([](DolphinMainWindow *, const QJsonObject &arguments, bool *error, QString *notice) {
             return controlViewer(arguments, error, notice);
         })},
    };
}

QJsonObject WhaleAiWindowTools::state(DolphinMainWindow *window)
{
    return getWindowState(window);
}

QJsonObject WhaleAiWindowTools::activeViewState(DolphinMainWindow *window)
{
    DolphinViewContainer *container = window->activeViewContainer();
    return container ? viewState(window, container) : QJsonObject();
}
