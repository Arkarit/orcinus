/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaledevautomation.h"

#include "config-dolphin.h"
#include "dolphin_generalsettings.h"
#include "dolphinmainwindow.h"
#include "dolphintabpage.h"
#include "dolphintabwidget.h"
#include "dolphinurlnavigator.h"
#include "dolphinviewbridge.h"
#include "dolphinviewcontainer.h"
#include "filterbar/filterbar.h"
#include "kitemviews/kfileitemmodel.h"
#include "kitemviews/kitemlistcontainer.h"
#include "kitemviews/kitemlistcontroller.h"
#include "kitemviews/kitemlistview.h"
#include "kitemviews/private/kitemlistroleeditor.h"
#include "search/bar.h"
#include "statusbar/dolphinstatusbar.h"
#include "views/dolphinview.h"
#include "whalecommandrouter.h"
#include "whaledevautomationdbus.h"
#include "whaledevtoolbars.h"
#include "whalefileoperations.h"
#include "whalefileoperationsimpl.h"
#include "whalefileviewtoolbar.h"
#include "whalelocationbar.h"
#include "whalepreviewproviderimpl.h"
#include "whalerecursivecontent.h"
#include "whalesplitlock.h"
#include "whalesplitviewstartup.h"
#include "whalestyleelement.h"
#include "whalestyleelements.h"
#include "whalestylemanager.h"
#include "whalestyles.h"
#include "whaletoolbardescriptor.h"
#include "whaletoolbarsettings.h"
#include "whaletransientmessage.h"
#include "whaletreecache.h"
#include "whaleview.h"
#include "whaleviewactions.h"
#include "whaleviewcommandscope.h"
#include "whaleviewcontextimpl.h"
#include "whaleviewdebug.h"
#include "whaleviewer.h"
#include "whaleviewercache.h"
#if HAVE_MPV
#include "whalevideoplayer.h"
#endif
#include "whaleviewersettings.h"
#include "whaleviewhost.h"
#include "whaleviewpreferences.h"
#include "whaleviewregistry.h"
#include "whaleviewservices.h"

#include <KActionCollection>
#include <KActionMenu>
#include <KCoreDirLister>
#include <KIO/Paste>
#include <KIO/RenameFileDialog>
#include <KLocalizedString>
#include <KMessageWidget>
#include <KPropertiesDialog>
#include <KSelectAction>
#include <KSqueezedTextLabel>
#include <KStandardAction>
#include <KToolBar>
#include <KUrlMimeData>

#include <QAbstractItemView>
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDBusConnection>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDrag>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QGraphicsView>
#include <QJsonArray>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMetaEnum>
#include <QMimeData>
#include <QPixmap>
#include <QPushButton>
#include <QRegion>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QVariantAnimation>
#include <QWheelEvent>

#include <cmath>

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

// Action IDs of the automation and the names of the actions in Whale, triggered as
// the menus, toolbar and shortcuts do (since T2 also on a plugin view's own selection).
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
        {QStringLiteral("properties"), QStringLiteral("properties")},
        {QStringLiteral("new_tab"), QStringLiteral("new_tab")},
        {QStringLiteral("close_tab"), KStandardAction::name(KStandardAction::Close)},
        // TB5: "Show File View Toolbars" of the View menu.
        {QStringLiteral("whale_show_file_view_toolbars"), QStringLiteral("whale_show_file_view_toolbars")},
        {QStringLiteral("activate_next_tab"), QStringLiteral("activate_next_tab")},
        {QStringLiteral("activate_prev_tab"), QStringLiteral("activate_prev_tab")},
        {QStringLiteral("show_preview"), QStringLiteral("show_preview")},
        {QStringLiteral("reload"), KStandardAction::name(KStandardAction::Redisplay)},
        {QStringLiteral("show_information_panel"), QStringLiteral("show_information_panel")},
        // F1: the file list's view modes (the toolbar's and View menu's actions).
        {QStringLiteral("view_icons"), QStringLiteral("icons")},
        {QStringLiteral("view_compact"), QStringLiteral("compact")},
        {QStringLiteral("view_details"), QStringLiteral("details")},
        // Only with WHALE_DEV_MCP_FILE_OPERATIONS=1, see fileOperations().
        {QStringLiteral("copy"), KStandardAction::name(KStandardAction::Copy)},
        {QStringLiteral("cut"), KStandardAction::name(KStandardAction::Cut)},
        {QStringLiteral("paste"), KStandardAction::name(KStandardAction::Paste)},
        {QStringLiteral("trash"), KStandardAction::name(KStandardAction::MoveToTrash)},
        {QStringLiteral("delete"), KStandardAction::name(KStandardAction::DeleteFile)},
        {QStringLiteral("duplicate"), QStringLiteral("duplicate")},
        {QStringLiteral("copy_location"), QStringLiteral("copy_location")},
        {QStringLiteral("copy_to_other_pane"), QStringLiteral("copy_to_inactive_split_view")},
        {QStringLiteral("move_to_other_pane"), QStringLiteral("move_to_inactive_split_view")},
    };
    return list;
}

// Actions that change files or the user's clipboard. They are refused unless the
// Whale process was started for a test profile with WHALE_DEV_MCP_FILE_OPERATIONS=1.
const QStringList &fileOperations()
{
    static const QStringList ids = {QStringLiteral("copy"),
                                    QStringLiteral("cut"),
                                    QStringLiteral("paste"),
                                    QStringLiteral("trash"),
                                    QStringLiteral("delete"),
                                    QStringLiteral("duplicate"),
                                    QStringLiteral("copy_location"),
                                    QStringLiteral("copy_to_other_pane"),
                                    QStringLiteral("move_to_other_pane")};
    return ids;
}

bool fileOperationsAllowed()
{
    return qgetenv("WHALE_DEV_MCP_FILE_OPERATIONS") == "1";
}

QString fileOperationsRefusal(const QString &what)
{
    return QStringLiteral("\"%1\" changes files or the clipboard and needs a Whale started with WHALE_DEV_MCP_FILE_OPERATIONS=1 (test profiles only)")
        .arg(what);
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

// GeneralSettings::splitViewAtStartup() (PI3).
QString splitViewAtStartupName()
{
    switch (GeneralSettings::splitViewAtStartup()) {
    case GeneralSettings::EnumSplitViewAtStartup::AlwaysSplit:
        return QStringLiteral("always-split");
    case GeneralSettings::EnumSplitViewAtStartup::AlwaysSingle:
        return QStringLiteral("always-single");
    default:
        return QStringLiteral("last-state");
    }
}

// A condition is the expected value, or {"$contains": x}: a string containing the
// string x, or an array containing the value x.
bool holds(const QJsonValue &expected, const QJsonValue &actual)
{
    const QJsonObject object = expected.toObject();
    if (expected.isObject() && object.size() == 1 && object.contains(QStringLiteral("$contains"))) {
        const QJsonValue part = object.value(QStringLiteral("$contains"));
        if (actual.isString()) {
            return actual.toString().contains(part.toString());
        }
        return actual.isArray() && actual.toArray().contains(part);
    }
    return actual == expected;
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
    return QStringLiteral("/" WHALE_COMPONENT "/DevAutomation");
}

QString WhaleDevAutomation::interfaceName()
{
    return QStringLiteral(WHALE_APP_ID ".DevAutomation");
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
    // The information panel listens to exactly this signal.
    // ... and to this one.
    connect(window, &DolphinMainWindow::requestItemInfo, this, [this](const KFileItem &item) {
        m_itemInfo = item.isNull() ? QString() : item.text();
        m_itemInfoUrl = item.url();
        m_itemInfoIsDir = !item.isNull() && item.isDir();
        ++m_itemInfoSignals;
    });
    connect(window, &DolphinMainWindow::selectionChanged, this, [this](const KFileItemList &selection) {
        m_shellSelection.clear();
        for (const KFileItem &item : selection) {
            m_shellSelection.append(item.text());
        }
        ++m_shellSelectionSignals;
    });
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
    QJsonArray selectedNames;
    const KFileItemList selected = view->selectedItems();
    for (int i = 0; i < selected.count() && i < MaxListedItems; ++i) {
        selection.append(selected.at(i).url().toString());
        selectedNames.append(selected.at(i).text());
    }
    const QUrl currentUrl = host->bridge()->selection().current();

    // What a plugin item view shows as selected and current.
    QJsonValue pluginSelection = QJsonValue::Null;
    QJsonValue pluginCurrent = QJsonValue::Null;
    if (auto itemView = qobject_cast<QAbstractItemView *>(pluginWidget); itemView && itemView->selectionModel()) {
        QModelIndexList indexes = itemView->selectionModel()->selectedIndexes();
        std::sort(indexes.begin(), indexes.end());
        QJsonArray names;
        for (const QModelIndex &index : std::as_const(indexes)) {
            names.append(index.data().toString());
        }
        pluginSelection = names;
        pluginCurrent = itemView->currentIndex().isValid() ? QJsonValue(itemView->currentIndex().data().toString()) : QJsonValue::Null;
    }

    // The item a plugin item view shows at its top (its scroll position), the rows it
    // shows, and the rows that show a preview (a QPixmap instead of the icon).
    QJsonValue pluginTopItem = QJsonValue::Null;
    QJsonValue pluginVisibleRows = QJsonValue::Null;
    QJsonValue pluginPreviewRows = QJsonValue::Null;
    if (auto itemView = qobject_cast<QAbstractItemView *>(pluginWidget)) {
        const QModelIndex top = itemView->indexAt(QPoint(1, 1));
        if (top.isValid()) {
            pluginTopItem = top.data().toString();
        }
        const QRect area = itemView->viewport()->rect();
        const QModelIndex bottom = itemView->indexAt(QPoint(1, area.bottom() - 1));
        const int rowCount = itemView->model()->rowCount();
        if (top.isValid()) {
            const int last = bottom.isValid() ? bottom.row() : rowCount - 1;
            pluginVisibleRows = QJsonObject{
                {QStringLiteral("first"), top.data().toString()},
                {QStringLiteral("last"), itemView->model()->index(last, 0).data().toString()},
                {QStringLiteral("count"), last - top.row() + 1},
            };
        }
        QJsonArray previewRows;
        for (int row = 0; row < rowCount; ++row) {
            const QModelIndex index = itemView->model()->index(row, 0);
            if (index.data(Qt::DecorationRole).typeId() == QMetaType::QPixmap) {
                previewRows.append(index.data().toString());
            }
        }
        pluginPreviewRows = previewRows;
    }
    // What a treemap view shows (whale_treemap describes itself through its widget).
    QJsonValue treemap = QJsonValue::Null;
    QVariantMap treemapState;
    if (pluginWidget && QMetaObject::invokeMethod(pluginWidget, "treemapState", Qt::DirectConnection, Q_RETURN_ARG(QVariantMap, treemapState))) {
        treemap = QJsonObject::fromVariantMap(treemapState);
    }
    // Which views have a stored state at this location (the states themselves are the
    // views' own business), and how the last restore went.
    const WhaleViewsState views = host->viewsState();
    QStringList storedStates = views.states.keys();
    storedStates.sort();
    for (QString &id : storedStates) {
        id = semanticViewId(id);
    }
    const WhaleViewHost::RestoreInfo restore = host->lastRestore();
    const QString preferredViewId = WhaleViewPreferences::instance()->preferredView(container->url());
    const QJsonValue restoreState = restore.source.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                             : QJsonObject{
                                                                   {QStringLiteral("source"), restore.source},
                                                                   {QStringLiteral("requested"), semanticViewId(restore.requestedViewId)},
                                                                   {QStringLiteral("shown"), semanticViewId(restore.shownViewId)},
                                                                   {QStringLiteral("fallback"), restore.fallback},
                                                               };

    // The message shown in the view area (errors of operations), not those of dialogs.
    QString message;
    const auto messageWidgets = container->findChildren<KMessageWidget *>();
    for (const KMessageWidget *widget : messageWidgets) {
        bool inDialog = false;
        for (const QWidget *parent = widget->parentWidget(); parent && parent != container; parent = parent->parentWidget()) {
            inDialog = inDialog || parent->isWindow();
        }
        if (!inDialog && widget->isVisible()) {
            message = widget->text();
        }
    }

    QString statusText;
    if (auto statusBar = container->findChild<DolphinStatusBar *>()) {
        if (auto label = statusBar->findChild<KSqueezedTextLabel *>()) {
            statusText = label->fullText();
        }
    }

    QString sortField = QStringLiteral("role:") + QString::fromLatin1(view->sortRole());
    for (const auto &[field, role] : sortFields()) {
        if (role == view->sortRole()) {
            sortField = field;
        }
    }

    // The listers, and (F1) the files below the location the file list still waits for.
    const bool loading = host->bridge()->isLoading();

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
        {QStringLiteral("panelRole"), view->panelRole() == WhalePanelRole::Left ? QStringLiteral("left") : QStringLiteral("right")},
        {QStringLiteral("active"), container->isActive()},
        {QStringLiteral("url"), container->url().toString()},
        {QStringLiteral("viewId"), semanticViewId(host->currentViewId())},
        {QStringLiteral("dolphinViewVisible"), view->isVisible()},
        {QStringLiteral("pluginWidgetVisible"), pluginWidget ? QJsonValue(pluginWidget->isVisible()) : QJsonValue::Null},
        {QStringLiteral("viewMode"), viewModeName(view->viewMode())},
        {QStringLiteral("ownViewMode"), view->ownStyleViewMode() ? QJsonValue(viewModeName(*view->ownStyleViewMode())) : QJsonValue::Null},
        {QStringLiteral("loading"), loading},
        {QStringLiteral("itemCount"), fileItems.count()},
        {QStringLiteral("items"), items},
        {QStringLiteral("pluginRows"), pluginRows},
        {QStringLiteral("pluginTopItem"), pluginTopItem},
        {QStringLiteral("pluginVisibleRows"), pluginVisibleRows},
        {QStringLiteral("pluginPreviewRows"), pluginPreviewRows},
        {QStringLiteral("treemap"), treemap},
        {QStringLiteral("fileOperations"), paneFileOperations(container)},
        {QStringLiteral("command"), commandState(container)},
        {QStringLiteral("message"), message},
        // LK: the floating information of the view area (also in "message" while shown).
        {QStringLiteral("transientMessage"),
         WhaleTransientMessage::of(container) && WhaleTransientMessage::of(container)->isVisible() ? WhaleTransientMessage::of(container)->text() : QString()},
        {QStringLiteral("previews"), panePreviews(container)},
        {QStringLiteral("storedViewStates"), QJsonArray::fromStringList(storedStates)},
        {QStringLiteral("restore"), restoreState},
        // O3: the view preferred at the location, and what decided about the shown view.
        {QStringLiteral("preferredView"), preferredViewId.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(semanticViewId(preferredViewId))},
        {QStringLiteral("viewOrigin"), host->viewOrigin()},
        {QStringLiteral("contentScope"), contentScopeState(container)},
        {QStringLiteral("fileListPoints"), fileListPoints(container)},
        {QStringLiteral("selectionCount"), selected.count()},
        {QStringLiteral("selection"), selection},
        {QStringLiteral("selectedNames"), selectedNames},
        {QStringLiteral("current"), currentUrl.toString()},
        {QStringLiteral("currentName"), currentUrl.fileName()},
        {QStringLiteral("pluginSelection"), pluginSelection},
        {QStringLiteral("pluginCurrent"), pluginCurrent},
        {QStringLiteral("statusText"), statusText},
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
             // FB2: what the filter applies to
             {QStringLiteral("scope"),
              view->filterScope() == KFileItemModelFilter::Scope::Files         ? QStringLiteral("files")
                  : view->filterScope() == KFileItemModelFilter::Scope::Folders ? QStringLiteral("folders")
                                                                                : QStringLiteral("files-and-folders")},
         }},
        {QStringLiteral("selectionMode"), container->isSelectionModeEnabled()},
        {QStringLiteral("focusProxy"), focusProxy},
        {QStringLiteral("hiddenDolphinViewHasFocus"), pluginWidget != nullptr && contains(view, focus)},
        // TB6: the File View Toolbar of this view.
        {QStringLiteral("toolbar"), WhaleDevToolbars::fileViewState(container, semanticViewId(host->currentViewId()))},
    };
}

QJsonObject WhaleDevAutomation::contentScopeState(DolphinViewContainer *container) const
{
    WhaleViewHost *host = container->viewHost();
    const WhaleRecursiveContent *content = host->recursiveContent();
    // The shown items as paths relative to the location (a sample, in the file list's order).
    QJsonArray relativeItems;
    const QString prefix = container->url().adjusted(QUrl::StripTrailingSlash).path() + QLatin1Char('/');
    const KFileItemList fileItems = container->view()->items();
    for (int i = 0; i < fileItems.count() && i < MaxListedItems; ++i) {
        const QString path = fileItems.at(i).url().path();
        relativeItems.append(path.startsWith(prefix) ? path.mid(prefix.length()) : fileItems.at(i).url().toString());
    }
    const WhaleRecursiveContent::Timing timing = content->timing();
    auto textOrNull = [](const QString &text) {
        return text.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(text);
    };
    return {
        {QStringLiteral("scope"), WhaleContentScopes::name(host->contentScope())},
        {QStringLiteral("shown"), host->isRecursiveContentShown()},
        {QStringLiteral("supported"), host->supportsRecursiveContent()},
        {QStringLiteral("origin"), host->contentScopeOrigin()},
        {QStringLiteral("preferred"), WhaleContentScopes::name(WhaleViewPreferences::instance()->contentScope(container->url()))},
        {QStringLiteral("busy"), content->isBusy()},
        {QStringLiteral("fileCount"), content->fileCount()},
        {QStringLiteral("relativeItems"), relativeItems},
        {QStringLiteral("timing"),
         QJsonObject{
             {QStringLiteral("scanMs"), timing.scanMs},
             {QStringLiteral("projectionMs"), timing.projectionMs},
             {QStringLiteral("modelMs"), timing.modelMs},
             {QStringLiteral("updates"), timing.updates},
         }},
        {QStringLiteral("pasteRefusal"), textOrNull(content->pasteRefusal())},
        {QStringLiteral("lastRefusal"), textOrNull(content->lastRefusal())},
        {QStringLiteral("refusals"), content->refusalCount()},
    };
}

QJsonValue WhaleDevAutomation::fileListPoints(DolphinViewContainer *container) const
{
    // Where real input reaches the file list's items (their icon and name) and its background,
    // in the main window's coordinates (e.g. for ydotool on Wayland); only items shown completely.
    if (!container->viewHost()->isDolphinViewShown() || !container->view()->isVisible()) {
        return QJsonValue::Null;
    }
    KItemListContainer *itemListContainer = container->view()->itemListContainer();
    auto graphicsView = qobject_cast<QGraphicsView *>(itemListContainer->viewport());
    KItemListView *itemListView = itemListContainer->controller()->view();
    auto model = qobject_cast<KFileItemModel *>(itemListContainer->controller()->model());
    if (!graphicsView || !itemListView || !model) {
        return QJsonValue::Null;
    }
    QWidget *viewport = graphicsView->viewport();
    const QString prefix = container->url().adjusted(QUrl::StripTrailingSlash).path() + QLatin1Char('/');
    QJsonArray items;
    QRegion covered;
    for (int index = 0; index < model->count() && items.count() < MaxListedItems; ++index) {
        const QRectF rect = itemListView->itemRect(index);
        // Details rows may be wider than the view: shown completely in height, the point is
        // the middle of the visible part of the row.
        const QRect inViewport = graphicsView->mapFromScene(itemListView->mapToScene(rect)).boundingRect();
        const QRect visible = inViewport.intersected(viewport->rect());
        if (rect.isEmpty() || visible.top() != inViewport.top() || visible.bottom() != inViewport.bottom() || visible.width() < 8) {
            continue;
        }
        covered += inViewport;
        // The point on the item's icon and name (a press elsewhere in a Details row starts a
        // rubber band, not a drag), if that is visible.
        const QRectF contextRect = itemListView->itemContextRect(index);
        const QRect context = graphicsView->mapFromScene(itemListView->mapToScene(contextRect)).boundingRect();
        const QPoint center = viewport->mapTo(m_window, visible.contains(context.center()) ? context.center() : visible.center());
        const QString path = model->fileItem(index).url().path();
        items.append(QJsonObject{
            {QStringLiteral("path"), path.startsWith(prefix) ? path.mid(prefix.length()) : path},
            {QStringLiteral("x"), center.x()},
            {QStringLiteral("y"), center.y()},
        });
    }
    // A point of the background: the lowest free spot near the bottom right corner.
    QJsonValue background = QJsonValue::Null;
    for (int dy = 12; dy < viewport->height() / 2 && background.isNull(); dy += 12) {
        const QPoint candidate(viewport->width() - 24, viewport->height() - dy);
        if (!covered.contains(candidate) && !itemListView->itemAt(itemListView->mapFromScene(graphicsView->mapToScene(candidate)))) {
            const QPoint point = viewport->mapTo(m_window, candidate);
            background = QJsonObject{{QStringLiteral("x"), point.x()}, {QStringLiteral("y"), point.y()}};
        }
    }
    return QJsonObject{{QStringLiteral("items"), items}, {QStringLiteral("background"), background}};
}

QJsonObject WhaleDevAutomation::setContentScope(int paneIndexArg, const QString &scopeName)
{
    QString error;
    DolphinViewContainer *container = pane(paneIndexArg, &error);
    if (!container) {
        return failure(error);
    }
    const std::optional<WhaleContentScope> scope = WhaleContentScopes::fromName(scopeName);
    if (!scope) {
        return failure(QStringLiteral("unknown scope \"%1\", expected current-folder or recursive-files").arg(scopeName));
    }
    // As the user does: the pane becomes active, then "Show flat (no folders)".
    container->setActive(true);
    QAction *action = m_window->actionCollection()->action(QStringLiteral("whale_include_subfolders"));
    if (!action || !action->isEnabled()) {
        return failure(QStringLiteral("\"Show flat (no folders)\" is not available in this pane (file list at a local folder only)"));
    }
    const bool recursive = *scope == WhaleContentScope::RecursiveFiles;
    if (action->isChecked() != recursive) {
        action->trigger();
    }
    return ok();
}

QJsonObject WhaleDevAutomation::uiState() const
{
    QJsonArray paneStates;
    const QList<DolphinViewContainer *> all = panes();
    for (int i = 0; i < all.count(); ++i) {
        paneStates.append(paneState(all.at(i), i));
    }

    QJsonArray views{dolphinId};
    if (WhaleViewServices *services = WhaleViewServices::of(m_window)) {
        const auto plugins = services->registry()->plugins();
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

    // Opening and closing the split view animate the width of a view (DolphinTabPage's
    // expand animation, a direct child of the splitter); until it ends, a view may be 1 px wide.
    bool splitAnimating = false;
    if (DolphinTabPage *page = tabs->currentTabPage()) {
        if (auto splitter = qobject_cast<QSplitter *>(page->primaryViewContainer()->parentWidget())) {
            const auto animations = splitter->findChildren<QVariantAnimation *>(QString(), Qt::FindDirectChildrenOnly);
            splitAnimating = std::any_of(animations.cbegin(), animations.cend(), [](const QVariantAnimation *animation) {
                return animation->state() == QAbstractAnimation::Running;
            });
        }
    }

    return {
        {QStringLiteral("schema"), 1},
        {QStringLiteral("pid"), QCoreApplication::applicationPid()},
        {QStringLiteral("window"),
         QJsonObject{
             {QStringLiteral("visible"), m_window->isVisible()},
             {QStringLiteral("active"), m_window->isActiveWindow()},
             {QStringLiteral("width"), m_window->width()},
             {QStringLiteral("height"), m_window->height()},
         }},
        {QStringLiteral("tabs"), QJsonObject{{QStringLiteral("count"), tabs->count()}, {QStringLiteral("current"), tabs->currentIndex()}}},
        {QStringLiteral("splitView"), all.count() > 1},
        {QStringLiteral("splitAnimating"), splitAnimating},
        {QStringLiteral("splitLock"), splitLockState()},
        {QStringLiteral("splitViewAtStartup"),
         QJsonObject{
             {QStringLiteral("choice"), splitViewAtStartupName()},
             {QStringLiteral("openInSplitView"), WhaleSplitViewStartup::openInSplitView()},
         }},
        {QStringLiteral("paneCount"), all.count()},
        {QStringLiteral("activePane"), paneIndex(active)},
        {QStringLiteral("url"), active ? active->url().toString() : QString()},
        {QStringLiteral("availableViews"), views},
        {QStringLiteral("panes"), paneStates},
        {QStringLiteral("focus"), focusState()},
        {QStringLiteral("activations"), QJsonObject{{QStringLiteral("count"), m_activationCount}, {QStringLiteral("recent"), recent}}},
        {QStringLiteral("shell"),
         QJsonObject{
             {QStringLiteral("selectedNames"), QJsonArray::fromStringList(m_shellSelection)},
             {QStringLiteral("selectionSignals"), m_shellSelectionSignals},
             {QStringLiteral("itemInfo"), m_itemInfo.isNull() ? QJsonValue::Null : QJsonValue(m_itemInfo)},
             {QStringLiteral("itemInfoSignals"), m_itemInfoSignals},
             {QStringLiteral("itemInfoUrl"), m_itemInfoUrl.toString()},
             {QStringLiteral("itemInfoIsDir"), m_itemInfoIsDir},
         }},
        {QStringLiteral("actions"), actionState()},
        // T2: what Whale's actions on selected items act on now (the active pane's source).
        {QStringLiteral("command"), active ? commandState(active) : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("clipboard"), clipboardState()},
        {QStringLiteral("popupMenu"), popupMenu()},
        {QStringLiteral("dialogs"), dialogs()},
        {QStringLiteral("treeCache"), treeCacheState()},
        {QStringLiteral("viewMenu"), viewMenuState()},
        // TB6: the main toolbar, the File View Toolbars of all tabs, Configure Toolbars.
        {QStringLiteral("toolbars"),
         QJsonObject{
             {QStringLiteral("global"), WhaleDevToolbars::globalState(m_window)},
             {QStringLiteral("fileViewsVisible"), WhaleToolbarSettings::self()->fileViewToolBarsVisible()},
             {QStringLiteral("instances"), WhaleDevToolbars::instancesState(m_window)},
         }},
        {QStringLiteral("toolbarDialog"), WhaleDevToolbars::dialogState(m_window)},
        {QStringLiteral("styles"), stylesState()},
        {QStringLiteral("viewer"), viewerState()},
    };
}

QJsonObject WhaleDevAutomation::stylesState() const
{
    QJsonArray styles;
    const QList<WhaleStyle> all = WhaleStyles::self()->store().styles();
    for (const WhaleStyle &style : all) {
        // The enabled elements and what they set, as the settings page will describe them.
        QJsonObject elements;
        for (auto it = style.elements.cbegin(); it != style.elements.cend(); ++it) {
            const WhaleStyleElement *element = WhaleStyleElements::self().element(it.key());
            elements.insert(it.key(),
                            QJsonObject{
                                {QStringLiteral("enabled"), it->enabled},
                                {QStringLiteral("known"), element != nullptr},
                                {QStringLiteral("description"), element ? element->describe(it->value) : QString()},
                            });
        }
        const QAction *action = m_window->actionCollection()->action(WhaleStyleManager::actionName(style.id));
        styles.append(QJsonObject{
            {QStringLiteral("id"), style.id},
            {QStringLiteral("name"), style.name},
            {QStringLiteral("shortcut"), style.shortcut.toString(QKeySequence::PortableText)},
            {QStringLiteral("action"), action ? action->objectName() : QString()},
            {QStringLiteral("actionShortcut"), action ? action->shortcut().toString(QKeySequence::PortableText) : QString()},
            {QStringLiteral("elements"), elements},
        });
    }
    // The menu View > Styles as the user sees it: object names, "-" for a separator.
    QJsonArray menu;
    if (auto menuAction = qobject_cast<KActionMenu *>(m_window->actionCollection()->action(WhaleStyleManager::menuActionName()))) {
        const auto entries = menuAction->menu()->actions();
        for (const QAction *entry : entries) {
            menu.append(entry->isSeparator() ? QStringLiteral("-") : entry->objectName());
        }
    }
    // What the window shows now, element by element, as "Save as Style…" would capture it.
    QJsonObject current;
    const auto elements = WhaleStyleElements::self().elements();
    for (const WhaleStyleElement *element : elements) {
        current.insert(element->key(), element->isAvailable(m_window) ? QJsonValue(element->capture(m_window)) : QJsonValue::Null);
    }
    return {
        {QStringLiteral("list"), styles},
        {QStringLiteral("menu"), menu},
        {QStringLiteral("current"), current},
        // The menu bar itself, which follows its action a moment later.
        {QStringLiteral("menuBarVisible"), !m_window->menuBar()->isHidden()},
        // The style applied or saved last, which "Save Style" saves.
        {QStringLiteral("lastStyle"), WhaleStyleManager::ensure(m_window)->lastStyleId()},
    };
}

QJsonValue WhaleDevAutomation::viewMenuState() const
{
    auto viewActions = windowViewActions();
    if (!viewActions) {
        return QJsonValue::Null;
    }
    auto entryState = [](const QAction *action) {
        return QJsonObject{
            {QStringLiteral("text"), KLocalizedString::removeAcceleratorMarker(action->text())},
            {QStringLiteral("checked"), action->isChecked()},
            {QStringLiteral("enabled"), action->isEnabled()},
        };
    };
    QJsonArray modes;
    const auto modeActions = viewActions->viewModeActions();
    for (const QAction *action : modeActions) {
        QJsonObject mode = entryState(action);
        mode.insert(QStringLiteral("id"), viewActions->viewModeId(action));
        modes.append(mode);
    }
    // The toolbar's view settings dropdown as the user sees it: "-" for a separator,
    // "name >" for a submenu.
    QJsonArray viewSettings;
    if (QAction *dropdown = m_window->actionCollection()->action(QStringLiteral("view_settings")); dropdown && dropdown->menu()) {
        const auto entries = dropdown->menu()->actions();
        for (const QAction *entry : entries) {
            if (entry->isVisible()) {
                const QString text = KLocalizedString::removeAcceleratorMarker(entry->text());
                viewSettings.append(entry->isSeparator() ? QStringLiteral("-") : entry->menu() ? text + QStringLiteral(" >") : text);
            }
        }
    }
    const bool submenu = viewActions->viewModeMenuEntries() != modeActions;
    QJsonObject options;
    for (const auto &[key, name] : {std::pair{QStringLiteral("flat"), QStringLiteral("whale_include_subfolders")},
                                    std::pair{QStringLiteral("previews"), QStringLiteral("show_preview")},
                                    std::pair{QStringLiteral("hidden"), QStringLiteral("show_hidden_files")}}) {
        if (const QAction *action = m_window->actionCollection()->action(name)) {
            QJsonObject option = entryState(action);
            option.insert(QStringLiteral("gateReason"), viewActions->gateReason(name));
            options.insert(key, option);
        }
    }
    return QJsonObject{
        {QStringLiteral("placement"), submenu ? QStringLiteral("submenu") : QStringLiteral("direct")},
        {QStringLiteral("modes"), modes},
        {QStringLiteral("viewSettings"), viewSettings},
        {QStringLiteral("options"), options},
    };
}

WhaleViewActions *WhaleDevAutomation::windowViewActions() const
{
    // The window's action set (acting on the active view) is a direct child of the window;
    // action sets bound to one view area live inside their view area.
    return m_window->findChild<WhaleViewActions *>(QString(), Qt::FindDirectChildrenOnly);
}

QJsonValue WhaleDevAutomation::treeCacheState() const
{
    WhaleViewServices *services = WhaleViewServices::of(m_window);
    if (!services) {
        return QJsonValue::Null;
    }
    const WhaleTreeCache::Statistics statistics = services->treeCache()->statistics();
    return QJsonObject{
        {QStringLiteral("hits"), statistics.hits},
        {QStringLiteral("misses"), statistics.misses},
        {QStringLiteral("joins"), statistics.joins},
        {QStringLiteral("invalidations"), statistics.invalidations},
        {QStringLiteral("evictions"), statistics.evictions},
        {QStringLiteral("notKept"), statistics.notKept},
        {QStringLiteral("entries"), statistics.entries},
        {QStringLiteral("bytes"), double(statistics.bytes)},
        {QStringLiteral("watchedFolders"), statistics.watchedFolders},
        {QStringLiteral("runningScans"), statistics.runningScans},
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

QJsonObject WhaleDevAutomation::setView(int paneIndexArg, const QString &viewId, bool remember)
{
    QString error;
    DolphinViewContainer *container = pane(paneIndexArg, &error);
    if (!container) {
        return failure(error);
    }
    WhaleViewHost *host = container->viewHost();
    if (remember) {
        // The user's path: the entry of the view mode, which acts on the active pane. For
        // the file list, the entry of its current mode.
        const QString hostViewId = viewId == dolphinId ? WhaleViewHost::dolphinViewId() : viewId;
        auto viewActions = windowViewActions();
        container->setActive(true);
        QAction *entry = viewActions ? viewActions->viewModeAction(hostViewId) : nullptr;
        if (!entry) {
            return failure(QStringLiteral("unknown view \"%1\"").arg(viewId));
        }
        if (!entry->isEnabled()) {
            return failure(QStringLiteral("the view \"%1\" is not offered at this location").arg(viewId));
        }
        entry->trigger();
        if (host->currentViewId() != hostViewId) {
            return failure(QStringLiteral("the view \"%1\" could not be shown").arg(viewId));
        }
        return ok();
    }
    if (viewId == dolphinId) {
        host->showDolphinView();
        return ok();
    }
    WhaleViewServices *services = WhaleViewServices::of(m_window);
    WhaleViewPlugin *plugin = services ? services->registry()->plugin(viewId) : nullptr;
    if (!plugin) {
        return failure(QStringLiteral("unknown view \"%1\"").arg(viewId));
    }
    if (!host->showPluginView(viewId, plugin)) {
        return failure(QStringLiteral("the view \"%1\" could not be shown").arg(viewId));
    }
    return ok();
}

QJsonObject WhaleDevAutomation::preferredView(const QString &url) const
{
    const QUrl location = QUrl::fromUserInput(url, QString(), QUrl::AssumeLocalFile);
    const QString viewId = WhaleViewPreferences::instance()->preferredView(location);
    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("url"), location.toString()},
        {QStringLiteral("key"), WhaleViewPreferences::locationKey(location)},
        {QStringLiteral("view"), viewId.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(semanticViewId(viewId))},
        {QStringLiteral("count"), WhaleViewPreferences::instance()->count()},
    };
}

QJsonObject WhaleDevAutomation::setPreferredView(const QString &url, const QString &viewId)
{
    const QUrl location = QUrl::fromUserInput(url, QString(), QUrl::AssumeLocalFile);
    if (WhaleViewPreferences::locationKey(location).isEmpty()) {
        return failure(QStringLiteral("\"%1\" is no location").arg(url));
    }
    if (viewId.isEmpty()) {
        WhaleViewPreferences::instance()->clearPreferredView(location);
    } else {
        WhaleViewPreferences::instance()->setPreferredView(location, viewId == dolphinId ? WhaleViewHost::dolphinViewId() : viewId);
    }
    return preferredView(url);
}

QJsonObject WhaleDevAutomation::setViewPluginAvailable(const QString &viewId, bool available)
{
    WhaleViewServices *services = WhaleViewServices::of(m_window);
    if (!services) {
        return failure(QStringLiteral("no view services"));
    }
    services->registry()->setPluginAvailable(viewId, available);
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

QJsonObject WhaleDevAutomation::setSplitRatio(double leftShare)
{
    DolphinTabPage *page = tabWidget()->currentTabPage();
    auto splitter = page->splitViewEnabled() ? qobject_cast<QSplitter *>(page->primaryViewContainer()->parentWidget()) : nullptr;
    if (!splitter || splitter->count() < 2) {
        return failure(QStringLiteral("the current tab is not split"));
    }
    if (leftShare < 0.1 || leftShare > 0.9) {
        return failure(QStringLiteral("the share must be between 0.1 and 0.9"));
    }
    const QList<int> sizes = splitter->sizes();
    const int total = sizes.at(0) + sizes.at(1);
    splitter->setSizes({int(total * leftShare), total - int(total * leftShare)});
    return ok();
}

QJsonValue WhaleDevAutomation::splitLockState() const
{
    DolphinTabPage *page = tabWidget()->currentTabPage();
    WhaleSplitLock *lock = WhaleSplitLock::of(page);
    if (!lock) {
        return QJsonValue::Null;
    }
    QJsonArray lockability;
    const QList<DolphinViewContainer *> all = panes();
    for (DolphinViewContainer *container : all) {
        const WhaleSplitLock::Lockability state = WhaleSplitLock::lockability(container);
        lockability.append(QJsonObject{
            {QStringLiteral("lockable"), state.lockable},
            {QStringLiteral("reason"), state.reason},
            {QStringLiteral("selectionLimited"), container->view()->isSelectionLimitedToOneFolder()},
        });
    }
    const QToolButton *button = lock->button();
    // Where real input reaches the button, in the main window's coordinates.
    const QPoint center = button->isVisible() ? button->mapTo(m_window, button->rect().center()) : QPoint();
    return QJsonObject{
        {QStringLiteral("buttonX"), button->isVisible() ? QJsonValue(center.x()) : QJsonValue::Null},
        {QStringLiteral("buttonY"), button->isVisible() ? QJsonValue(center.y()) : QJsonValue::Null},
        {QStringLiteral("locked"), lock->isLocked()},
        {QStringLiteral("buttonVisible"), button->isVisible()},
        {QStringLiteral("buttonEnabled"), button->isEnabled()},
        {QStringLiteral("buttonChecked"), button->isChecked()},
        {QStringLiteral("buttonIcon"), button->icon().name()},
        {QStringLiteral("toolTip"), button->toolTip()},
        {QStringLiteral("panes"), lockability},
    };
}

QJsonObject WhaleDevAutomation::setSplitLock(bool locked)
{
    WhaleSplitLock *lock = WhaleSplitLock::of(tabWidget()->currentTabPage());
    if (!lock) {
        return failure(QStringLiteral("the current tab has no split lock"));
    }
    if (lock->isLocked() != locked) {
        QToolButton *button = lock->button();
        if (!button->isVisible()) {
            return failure(QStringLiteral("the lock button is not shown (no split view)"));
        }
        if (!button->isEnabled()) {
            return failure(QStringLiteral("the lock button is disabled: %1").arg(button->toolTip()));
        }
        // The same path as a click on the button.
        button->click();
    }
    QJsonObject reply = ok();
    reply.insert(QStringLiteral("locked"), lock->isLocked());
    return reply;
}

QJsonObject WhaleDevAutomation::applyStyle(const QString &id)
{
    QAction *action = m_window->actionCollection()->action(WhaleStyleManager::actionName(id));
    if (!action) {
        return failure(QStringLiteral("no style \"%1\" in this window, styles: %2").arg(id, WhaleStyles::self()->store().ids().join(QStringLiteral(", "))));
    }
    action->trigger();
    return ok();
}

QJsonObject WhaleDevAutomation::saveStyle(const QString &name)
{
    if (name.isEmpty()) {
        // "Save Style": the style applied or saved last, through its action.
        QAction *action = m_window->actionCollection()->action(WhaleStyleManager::saveLastActionName());
        if (!action || !action->isEnabled()) {
            return failure(QStringLiteral("no style applied or saved yet in this window (\"Save Style\" is disabled)"));
        }
        action->trigger();
        QJsonObject reply = ok();
        reply.insert(QStringLiteral("id"), WhaleStyleManager::ensure(m_window)->lastStyleId());
        return reply;
    }
    const QString id = WhaleStyleManager::ensure(m_window)->saveCurrentAsStyle(name);
    if (id.isEmpty()) {
        return failure(QStringLiteral("a style needs a name"));
    }
    QJsonObject reply = ok();
    reply.insert(QStringLiteral("id"), id);
    return reply;
}

QJsonObject WhaleDevAutomation::setWindowSize(int width, int height)
{
    if (width < 300 || height < 200 || width > 8000 || height > 8000) {
        return failure(QStringLiteral("unreasonable window size %1x%2").arg(width).arg(height));
    }
    if (m_window->isMaximized() || m_window->isFullScreen()) {
        m_window->showNormal();
    }
    m_window->resize(width, height);
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

QJsonObject WhaleDevAutomation::setSelection(int paneIndexArg, const QStringList &urls, const QString &current)
{
    QString error;
    DolphinViewContainer *container = pane(paneIndexArg, &error);
    if (!container) {
        return failure(error);
    }
    QList<QUrl> selected;
    for (const QString &url : urls) {
        selected.append(QUrl::fromUserInput(url, QString(), QUrl::AssumeLocalFile));
    }
    const QUrl currentUrl = current.isEmpty() ? QUrl() : QUrl::fromUserInput(current, QString(), QUrl::AssumeLocalFile);
    const WhaleSelection result = container->viewHost()->bridge()->setSelection(WhaleSelection(selected, currentUrl));

    QJsonArray applied;
    const QList<QUrl> resultUrls = result.selected();
    for (const QUrl &url : resultUrls) {
        applied.append(url.toString());
    }
    QJsonArray ignored;
    for (const QUrl &url : std::as_const(selected)) {
        if (!result.contains(url)) {
            ignored.append(url.toString());
        }
    }
    if (!currentUrl.isEmpty() && result.current() != currentUrl) {
        ignored.append(currentUrl.toString());
    }
    QJsonObject reply = ok();
    reply.insert(QStringLiteral("selection"), applied);
    reply.insert(QStringLiteral("current"), result.current().toString());
    reply.insert(QStringLiteral("ignored"), ignored);
    return reply;
}

QJsonObject WhaleDevAutomation::activateItems(int paneIndexArg, const QStringList &urls)
{
    QString error;
    DolphinViewContainer *container = pane(paneIndexArg, &error);
    if (!container) {
        return failure(error);
    }
    QList<QUrl> items;
    for (const QString &url : urls) {
        items.append(QUrl::fromUserInput(url, QString(), QUrl::AssumeLocalFile));
    }
    container->viewHost()->bridge()->activateItems(items);
    return ok();
}

QJsonObject WhaleDevAutomation::actionState() const
{
    auto viewActions = windowViewActions();
    QStringList names = WhaleViewActions::gateableActions();
    names << QStringLiteral("properties");
    QJsonObject state;
    for (const QString &name : std::as_const(names)) {
        if (QAction *action = m_window->actionCollection()->action(name)) {
            const QString gate = viewActions ? viewActions->gateReason(name) : QString();
            state.insert(name,
                         QJsonObject{
                             {QStringLiteral("enabled"), action->isEnabled()},
                             {QStringLiteral("gate"), gate.isEmpty() ? QJsonValue::Null : QJsonValue(gate)},
                         });
        }
    }
    return state;
}

QJsonValue WhaleDevAutomation::clipboardState() const
{
    // Outside of test profiles the clipboard is the user's own; it stays private.
    if (!fileOperationsAllowed()) {
        return QJsonValue::Null;
    }
    const QMimeData *mimeData = QApplication::clipboard()->mimeData();
    QJsonArray urls;
    if (mimeData) {
        const QList<QUrl> clipboardUrls = KUrlMimeData::urlsFromMimeData(mimeData);
        for (const QUrl &url : clipboardUrls) {
            urls.append(url.toString());
        }
    }
    return QJsonObject{{QStringLiteral("urls"), urls}, {QStringLiteral("cut"), mimeData && KIO::isClipboardDataCut(mimeData)}};
}

QJsonValue WhaleDevAutomation::popupMenu() const
{
    auto menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
    if (!menu || !menu->isVisible()) {
        return QJsonValue::Null;
    }
    QJsonArray entries;
    QJsonArray names;
    QJsonArray texts;
    const auto actions = menu->actions();
    for (QAction *action : actions) {
        if (action->isSeparator()) {
            continue;
        }
        const QString text = action->text().remove(QLatin1Char('&'));
        entries.append(QJsonObject{
            {QStringLiteral("name"), action->objectName()},
            {QStringLiteral("text"), text},
            {QStringLiteral("enabled"), action->isEnabled()},
        });
        if (!action->objectName().isEmpty()) {
            names.append(action->objectName());
        }
        texts.append(text);
    }
    // The entry the keyboard is on (Up/Down), by name or text.
    QJsonValue active = QJsonValue::Null;
    if (const QAction *action = menu->activeAction()) {
        active = action->objectName().isEmpty() ? action->text().remove(QLatin1Char('&')) : action->objectName();
    }
    return QJsonObject{{QStringLiteral("entries"), entries},
                       {QStringLiteral("names"), names},
                       {QStringLiteral("texts"), texts},
                       {QStringLiteral("active"), active}};
}

QJsonArray WhaleDevAutomation::dialogs() const
{
    QJsonArray result;
    const auto windows = QApplication::topLevelWidgets();
    for (QWidget *window : windows) {
        auto dialog = qobject_cast<QDialog *>(window);
        if (!dialog || !dialog->isVisible()) {
            continue;
        }
        QString type = QStringLiteral("other");
        QJsonArray urls;
        if (qobject_cast<KIO::RenameFileDialog *>(dialog)) {
            type = QStringLiteral("rename");
        } else if (auto properties = qobject_cast<KPropertiesDialog *>(dialog)) {
            type = QStringLiteral("properties");
            const KFileItemList items = properties->items();
            for (const KFileItem &item : items) {
                urls.append(item.url().toString());
            }
        }
        QJsonArray buttons;
        const auto boxes = dialog->findChildren<QDialogButtonBox *>();
        for (const QDialogButtonBox *box : boxes) {
            const auto boxButtons = box->buttons();
            for (const QAbstractButton *button : boxButtons) {
                if (button->isVisible()) {
                    buttons.append(button->text().remove(QLatin1Char('&')));
                }
            }
        }
        // The text of a message box (e.g. KIO's confirmation).
        QString text;
        const auto labels = dialog->findChildren<QLabel *>();
        for (const QLabel *label : labels) {
            if (label->isVisible() && label->text().length() > text.length()) {
                text = label->text();
            }
        }
        result.append(QJsonObject{{QStringLiteral("type"), type},
                                  {QStringLiteral("class"), QString::fromLatin1(dialog->metaObject()->className())},
                                  {QStringLiteral("title"), dialog->windowTitle()},
                                  {QStringLiteral("text"), text},
                                  {QStringLiteral("buttons"), buttons},
                                  {QStringLiteral("urls"), urls}});
    }
    return result;
}

QJsonObject WhaleDevAutomation::openContextMenu(int paneIndexArg, const QString &url)
{
    QString error;
    DolphinViewContainer *container = pane(paneIndexArg, &error);
    if (!container) {
        return failure(error);
    }
    const QUrl item = url.isEmpty() ? QUrl() : QUrl::fromUserInput(url, QString(), QUrl::AssumeLocalFile);
    if (!item.isEmpty()) {
        const KFileItemList items = container->view()->items();
        const bool listed = std::any_of(items.cbegin(), items.cend(), [&item](const KFileItem &fileItem) {
            return fileItem.url() == item;
        });
        if (!listed) {
            return failure(QStringLiteral("\"%1\" is not in the listing of the pane").arg(url));
        }
    }
    WhaleViewHost *host = container->viewHost();
    QWidget *shown = host->isDolphinViewShown() ? static_cast<QWidget *>(container->view()) : host->currentView()->widget();
    const QPoint position = shown->mapToGlobal(shown->rect().center());
    // The menu runs its own event loop until it closes; this call must answer first.
    QTimer::singleShot(0, host, [host, position, item]() {
        host->bridge()->requestContextMenu(position, item);
    });
    return ok();
}

QJsonObject WhaleDevAutomation::scrollToItem(int paneIndexArg, const QString &url)
{
    QString error;
    DolphinViewContainer *container = pane(paneIndexArg, &error);
    if (!container) {
        return failure(error);
    }
    WhaleViewHost *host = container->viewHost();
    auto itemView = host->isDolphinViewShown() ? nullptr : qobject_cast<QAbstractItemView *>(host->currentView()->widget());
    if (!itemView) {
        return failure(QStringLiteral("the pane does not show a plugin item view"));
    }
    const QAbstractItemModel *model = itemView->model();
    const QString name = QUrl::fromUserInput(url, QString(), QUrl::AssumeLocalFile).fileName();
    for (int row = 0; row < model->rowCount(); ++row) {
        if (model->index(row, 0).data().toString() == name) {
            itemView->scrollTo(model->index(row, 0), QAbstractItemView::PositionAtTop);
            return ok();
        }
    }
    return failure(QStringLiteral("\"%1\" is not shown in the pane").arg(url));
}

QJsonObject WhaleDevAutomation::quit()
{
    // Like closing the window: Whale saves its session ("remember open tabs") and quits.
    // Deferred, so that this call is answered first.
    QTimer::singleShot(0, m_window, &QWidget::close);
    return ok();
}

QJsonObject WhaleDevAutomation::closePopups()
{
    while (QWidget *popup = QApplication::activePopupWidget()) {
        popup->close();
    }
    return ok();
}

QJsonObject WhaleDevAutomation::requestItemInfo(int paneIndexArg, const QString &url)
{
    QString error;
    DolphinViewContainer *container = pane(paneIndexArg, &error);
    if (!container) {
        return failure(error);
    }
    container->viewHost()->bridge()->requestItemInfo(url.isEmpty() ? QUrl() : QUrl::fromUserInput(url, QString(), QUrl::AssumeLocalFile));
    return ok();
}

QJsonObject WhaleDevAutomation::answerRenameDialog(const QString &newName)
{
    if (!fileOperationsAllowed()) {
        return failure(fileOperationsRefusal(QStringLiteral("rename_dialog")));
    }
    const auto windows = QApplication::topLevelWidgets();
    for (QWidget *window : windows) {
        auto dialog = qobject_cast<KIO::RenameFileDialog *>(window);
        if (!dialog || !dialog->isVisible()) {
            continue;
        }
        auto edit = dialog->findChild<QLineEdit *>();
        auto buttons = dialog->findChild<QDialogButtonBox *>();
        QPushButton *accept = buttons ? buttons->button(QDialogButtonBox::Ok) : nullptr;
        if (!edit || !accept) {
            return failure(QStringLiteral("unexpected layout of the rename dialog"));
        }
        edit->setText(newName);
        accept->click();
        return ok();
    }
    return failure(QStringLiteral("no rename dialog is open"));
}

QJsonObject WhaleDevAutomation::closeDialogs()
{
    const auto windows = QApplication::topLevelWidgets();
    for (QWidget *window : windows) {
        if (auto dialog = qobject_cast<QDialog *>(window); dialog && dialog->isVisible()) {
            dialog->reject();
        }
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
        if (fileOperations().contains(id) && !fileOperationsAllowed()) {
            return failure(fileOperationsRefusal(id));
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

DolphinViewContainer *WhaleDevAutomation::fileView(const QString &argument, QString *error) const
{
    bool isIndex = false;
    const int index = argument.toInt(&isIndex);
    if (argument == QLatin1String("active") || (isIndex && index < 0)) {
        return m_window->activeViewContainer();
    }
    if (isIndex) {
        return pane(index, error);
    }
    const auto all = WhaleDevToolbars::instances(m_window);
    for (const WhaleDevToolbars::Instance &instance : all) {
        if (WhaleDevToolbars::instanceId(instance.container) == argument) {
            return instance.container;
        }
    }
    *error = QStringLiteral("no file view with the instanceId \"%1\" in this window (closed, or another window?)").arg(argument);
    return nullptr;
}

QJsonObject WhaleDevAutomation::locationBarInput(const QString &toolBarArg, const QString &text, bool submit)
{
    if (toolBarArg == WhaleToolbarDescriptor::globalId()) {
        return failure(QStringLiteral("the main toolbar has no location bar; each file view has its own (a pane index or instanceId)"));
    }
    QString error;
    DolphinViewContainer *container = fileView(toolBarArg, &error);
    if (!container) {
        return failure(error);
    }
    return WhaleDevToolbars::locationBarInput(container, text, submit);
}

QJsonObject WhaleDevAutomation::triggerToolbarAction(const QString &toolBarArg, const QString &actionId, const QString &menuEntry)
{
    KToolBar *toolBar = nullptr;
    WhaleFileViewToolBar *fileViewToolBar = nullptr;
    if (toolBarArg == WhaleToolbarDescriptor::globalId()) {
        toolBar = m_window->findChild<KToolBar *>(QStringLiteral("mainToolBar"), Qt::FindDirectChildrenOnly);
        if (!toolBar) {
            return failure(QStringLiteral("the window has no main toolbar"));
        }
    } else {
        QString error;
        DolphinViewContainer *container = fileView(toolBarArg, &error);
        if (!container) {
            return failure(error);
        }
        fileViewToolBar = container ? container->findChild<WhaleFileViewToolBar *>(QString(), Qt::FindDirectChildrenOnly) : nullptr;
        if (!fileViewToolBar) {
            return failure(QStringLiteral("the view has no File View Toolbar"));
        }
        toolBar = fileViewToolBar;
    }

    // The same IDs as the actions in Whale.
    const bool known = fileViewToolBar
        ? WhaleViewCommandScope::allowedActions().contains(actionId) || actionId == WhaleLocationBar::actionName() // a widget: the answer says so
        : m_window->actionCollection()->action(actionId) != nullptr;
    if (!known) {
        return failure(QStringLiteral("unknown action \"%1\" for this toolbar").arg(actionId));
    }
    if (WhaleDevToolbars::changesFiles(actionId) && !fileOperationsAllowed()) {
        return failure(fileOperationsRefusal(actionId));
    }
    if (fileViewToolBar && !fileViewToolBar->scope()) {
        // Never shown: it has no actions yet, and the automation does not create them.
        return failure(QStringLiteral("the File View Toolbar has not been shown yet (hidden, or a tab never shown)"));
    }
    QAction *action = nullptr;
    const auto actions = toolBar->actions();
    for (QAction *candidate : actions) {
        if (candidate->objectName() == actionId) {
            action = candidate;
        }
    }
    if (!action) {
        QStringList layout;
        for (const QAction *candidate : actions) {
            if (!candidate->objectName().isEmpty()) {
                layout.append(candidate->objectName());
            }
        }
        return failure(QStringLiteral("\"%1\" is not in this toolbar (it has: %2)").arg(actionId, layout.join(QStringLiteral(", "))));
    }
    QString reason;
    if (fileViewToolBar) {
        WhaleViewCommandScope *scope = fileViewToolBar->scope();
        reason = scope->disabledReason(action);
        if (reason.isEmpty() && scope->viewActions()) {
            reason = scope->viewActions()->gateReason(actionId);
        }
    }
    return WhaleDevToolbars::click(toolBar, action, menuEntry, reason);
}

QJsonObject WhaleDevAutomation::configureToolbars(const QJsonObject &request)
{
    return WhaleDevToolbars::configure(m_window, request);
}

QJsonObject WhaleDevAutomation::evaluate(const QJsonObject &conditions) const
{
    const QJsonValue state = uiState();
    QJsonArray mismatches;
    for (auto it = conditions.begin(); it != conditions.end(); ++it) {
        const QJsonValue actual = valueAt(state, it.key());
        if (!holds(it.value(), actual)) {
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

QJsonValue WhaleDevAutomation::panePreviews(DolphinViewContainer *container) const
{
    WhaleViewHost *host = container->viewHost();
    const WhaleViewContextImpl *context = host->pluginContext();
    const WhalePreviewProviderImpl *provider = context ? context->previewProviderIfCreated() : nullptr;
    QJsonObject state{
        // Dolphin's "Show Previews" at the location, and the view's capability.
        {QStringLiteral("shown"), container->view()->previewsShown()},
        {QStringLiteral("capability"), bool(host->currentView()->capabilities() & WhaleViewCapability::Previews)},
        {QStringLiteral("provider"), provider != nullptr},
    };
    if (!provider) {
        return state;
    }
    QStringList pending;
    const QList<QUrl> urls = provider->pendingUrls();
    for (const QUrl &url : urls) {
        pending.append(url.fileName());
    }
    pending.sort();
    const WhalePreviewProviderImpl::Statistics statistics = provider->statistics();
    state.insert(QStringLiteral("enabled"), provider->isEnabled());
    state.insert(QStringLiteral("pendingCount"), pending.count());
    state.insert(QStringLiteral("pending"), QJsonArray::fromStringList(pending.mid(0, MaxListedItems)));
    state.insert(QStringLiteral("runningJobs"), provider->runningJobs());
    state.insert(QStringLiteral("requested"), statistics.requested);
    state.insert(QStringLiteral("generated"), statistics.generated);
    state.insert(QStringLiteral("failed"), statistics.failed);
    state.insert(QStringLiteral("canceled"), statistics.canceled);
    state.insert(QStringLiteral("jobsStarted"), statistics.jobsStarted);
    state.insert(QStringLiteral("jobsKilled"), statistics.jobsKilled);
    state.insert(QStringLiteral("maxPending"), statistics.maxPending);
    return state;
}

QJsonObject WhaleDevAutomation::previewState() const
{
    QJsonArray panesState;
    const QList<DolphinViewContainer *> all = panes();
    for (DolphinViewContainer *container : all) {
        panesState.append(panePreviews(container));
    }
    return {{QStringLiteral("panes"), panesState}, {QStringLiteral("results"), m_previewResults}};
}

QJsonObject WhaleDevAutomation::requestPreview(int paneIndexArg, const QStringList &urls, int size)
{
    QString error;
    DolphinViewContainer *container = pane(paneIndexArg, &error);
    if (!container) {
        return failure(error);
    }
    WhaleViewContextImpl *context = container->viewHost()->pluginContext();
    if (!context) {
        return failure(QStringLiteral("the pane shows the file list; previews are for plugin views"));
    }
    WhalePreviewProvider *provider = context->previews();
    if (!m_watchedProviders.contains(provider)) {
        m_watchedProviders.append(provider);
        const int index = paneIndex(container);
        auto record = [this, index](const KFileItem &item, const QPixmap &preview) {
            m_previewResults.append(QJsonObject{
                {QStringLiteral("pane"), index},
                {QStringLiteral("name"), item.url().fileName()},
                {QStringLiteral("preview"), !preview.isNull()},
                {QStringLiteral("width"), preview.width()},
                {QStringLiteral("height"), preview.height()},
            });
            while (m_previewResults.count() > 50) {
                m_previewResults.removeFirst();
            }
        };
        connect(provider, &WhalePreviewProvider::previewAvailable, this, record);
        connect(provider, &WhalePreviewProvider::previewFailed, this, [record](const KFileItem &item) {
            record(item, QPixmap());
        });
    }
    KFileItemList items;
    QJsonArray ignored;
    for (const QString &url : urls) {
        const KFileItem item = container->viewHost()->bridge()->item(QUrl::fromUserInput(url, QString(), QUrl::AssumeLocalFile));
        if (item.isNull()) {
            ignored.append(url);
        } else {
            items.append(item);
        }
    }
    provider->requestPreviews(items, QSize(size, size), m_window->devicePixelRatioF());
    QJsonObject reply = ok();
    reply.insert(QStringLiteral("enabled"), provider->isEnabled());
    reply.insert(QStringLiteral("ignored"), ignored);
    return reply;
}

namespace
{
QString tooSmallToDrop(const QWidget *widget)
{
    return QStringLiteral("the target view is too small to drop onto (%1x%2 px; is the split view still opening? wait for splitAnimating false)")
        .arg(widget->width())
        .arg(widget->height());
}
}

QJsonObject WhaleDevAutomation::deliverDrag(DolphinViewContainer *target, const QUrl &item, const QMimeData *data, Qt::KeyboardModifiers modifiers)
{
    WhaleViewHost *host = target->viewHost();
    QWidget *widget = nullptr;
    QPoint position;
    if (!host->isDolphinViewShown()) {
        auto itemView = qobject_cast<QAbstractItemView *>(host->currentView()->widget());
        if (!itemView) {
            return failure(QStringLiteral("the target pane's view is no item view"));
        }
        widget = itemView->viewport();
        if (item.isEmpty()) {
            // The background below the rows.
            position = QPoint(widget->width() / 2, widget->height() - 5);
            if (!widget->rect().contains(position)) {
                return failure(tooSmallToDrop(widget));
            }
            if (itemView->indexAt(position).isValid()) {
                return failure(QStringLiteral("the target view shows no background to drop onto"));
            }
        } else {
            const QAbstractItemModel *model = itemView->model();
            for (int row = 0; row < model->rowCount() && widget && position.isNull(); ++row) {
                if (model->index(row, 0).data().toString() == item.fileName()) {
                    itemView->scrollTo(model->index(row, 0));
                    position = itemView->visualRect(model->index(row, 0)).center();
                }
            }
            if (position.isNull()) {
                return failure(QStringLiteral("\"%1\" is not shown in the target view").arg(item.toString()));
            }
        }
    } else {
        // The file list's own item view (KItemListContainer's QGraphicsView).
        KItemListContainer *container = target->view()->itemListContainer();
        auto graphicsView = qobject_cast<QGraphicsView *>(container->viewport());
        KItemListView *itemListView = container->controller()->view();
        widget = graphicsView->viewport();
        if (item.isEmpty()) {
            position = QPoint(widget->width() - 20, widget->height() - 20);
            if (!widget->rect().contains(position)) {
                return failure(tooSmallToDrop(widget));
            }
            if (itemListView->itemAt(itemListView->mapFromScene(graphicsView->mapToScene(position)))) {
                return failure(QStringLiteral("the target file list shows no background to drop onto"));
            }
        } else {
            auto model = qobject_cast<KFileItemModel *>(container->controller()->model());
            const int index = model->index(item);
            const QRectF rect = index >= 0 ? itemListView->itemRect(index) : QRectF();
            position = graphicsView->mapFromScene(itemListView->mapToScene(rect.center()));
            if (rect.isEmpty() || !widget->rect().contains(position)) {
                return failure(QStringLiteral("\"%1\" is not shown in the target file list").arg(item.toString()));
            }
        }
    }

    const Qt::DropActions actions = Qt::CopyAction | Qt::MoveAction | Qt::LinkAction;
    QDragEnterEvent enter(position, actions, data, Qt::LeftButton, modifiers);
    QCoreApplication::sendEvent(widget, &enter);
    QDragMoveEvent move(position, actions, data, Qt::LeftButton, modifiers);
    QCoreApplication::sendEvent(widget, &move);
    const bool accepted = move.isAccepted() && move.dropAction() != Qt::IgnoreAction;
    bool dropped = false;
    if (accepted) {
        QDropEvent drop(position, actions, data, Qt::LeftButton, modifiers);
        QCoreApplication::sendEvent(widget, &drop);
        dropped = drop.isAccepted();
    } else {
        QDragLeaveEvent leave;
        QCoreApplication::sendEvent(widget, &leave);
    }
    QJsonObject reply = ok();
    reply.insert(QStringLiteral("accepted"), accepted);
    reply.insert(QStringLiteral("dropped"), dropped);
    reply.insert(QStringLiteral("proposedAction"), int(move.proposedAction()));
    return reply;
}

QJsonObject WhaleDevAutomation::performDragDrop(int sourcePaneArg, int targetPaneArg, const QString &targetUrl, const QString &action)
{
    if (!fileOperationsAllowed()) {
        return failure(fileOperationsRefusal(QStringLiteral("perform_drag_drop")));
    }
    static const QHash<QString, Qt::KeyboardModifiers> modifiersOf = {
        {QStringLiteral("copy"), Qt::ControlModifier},
        {QStringLiteral("move"), Qt::ShiftModifier},
        {QStringLiteral("link"), Qt::ControlModifier | Qt::ShiftModifier},
        {QStringLiteral("ask"), Qt::NoModifier},
    };
    if (!modifiersOf.contains(action)) {
        return failure(QStringLiteral("unknown action \"%1\", expected copy, move, link or ask").arg(action));
    }
    QString error;
    DolphinViewContainer *source = pane(sourcePaneArg, &error);
    DolphinViewContainer *target = source ? pane(targetPaneArg, &error) : nullptr;
    if (!target) {
        return failure(error);
    }
    const QUrl item = targetUrl.isEmpty() ? QUrl() : QUrl::fromUserInput(targetUrl, QString(), QUrl::AssumeLocalFile);
    const Qt::KeyboardModifiers modifiers = modifiersOf.value(action);
    const QList<QUrl> selection = source->viewHost()->bridge()->selection().selected();
    if (selection.isEmpty()) {
        return failure(QStringLiteral("the source pane has no selection to drag"));
    }

    WhaleViewHost *sourceHost = source->viewHost();
    if (sourceHost->isDolphinViewShown()) {
        // The data of the file list's own drag (KItemListController::startDragging()).
        std::unique_ptr<QMimeData> data(sourceHost->bridge()->createDragMimeData(selection));
        QJsonObject reply = deliverDrag(target, item, data.get(), modifiers);
        reply.insert(QStringLiteral("source"), QStringLiteral("file-list-drag-data"));
        return reply;
    }

    auto itemView = qobject_cast<QAbstractItemView *>(sourceHost->currentView()->widget());
    if (!itemView) {
        return failure(QStringLiteral("the source pane's view is no item view"));
    }
    // The gesture on the first selected row the view shows.
    QModelIndex pressed;
    const QModelIndexList selectedRows = itemView->selectionModel()->selectedIndexes();
    for (const QModelIndex &index : selectedRows) {
        if (!pressed.isValid() || index.row() < pressed.row()) {
            pressed = index;
        }
    }
    if (!pressed.isValid()) {
        return failure(QStringLiteral("the source view shows no selected row"));
    }
    itemView->scrollTo(pressed);

    QJsonObject reply;
    int drags = 0;
    WhaleFileOperationsImpl::setDragRunner([&](QDrag *drag) {
        ++drags;
        QStringList dragged;
        const QList<QUrl> urls = KUrlMimeData::urlsFromMimeData(drag->mimeData(), KUrlMimeData::PreferKdeUrls);
        for (const QUrl &url : urls) {
            dragged.append(url.fileName());
        }
        reply = deliverDrag(target, item, drag->mimeData(), modifiers);
        reply.insert(QStringLiteral("dragged"), QJsonArray::fromStringList(dragged));
        return reply.value(QStringLiteral("dropped")).toBool() ? Qt::DropAction(reply.value(QStringLiteral("proposedAction")).toInt()) : Qt::IgnoreAction;
    });
    // Sent directly to the view: a running drag owns the pointer, so after it neither a
    // move nor the release reaches the view.
    QWidget *viewport = itemView->viewport();
    auto send = [viewport](QEvent::Type type, const QPoint &position) {
        const Qt::MouseButton button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
        const Qt::MouseButtons buttons = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton;
        QMouseEvent event(type, position, viewport->mapToGlobal(position), button, buttons, Qt::NoModifier);
        QCoreApplication::sendEvent(viewport, &event);
    };
    const QPoint start = itemView->visualRect(pressed).center();
    const QPoint step(QApplication::startDragDistance() + 2, 0);
    send(QEvent::MouseButtonPress, start);
    for (const QPoint &position : {start + QPoint(1, 0), start + step, start + step * 2}) {
        send(QEvent::MouseMove, position);
        if (drags > 0) {
            break;
        }
    }
    WhaleFileOperationsImpl::setDragRunner({});
    if (drags == 0) {
        send(QEvent::MouseButtonRelease, start + step * 2);
        return failure(QStringLiteral("the source view did not start a drag"));
    }
    reply.insert(QStringLiteral("source"), QStringLiteral("plugin-drag-gesture"));
    return reply;
}

QJsonObject WhaleDevAutomation::choosePopupEntry(const QString &text)
{
    if (!fileOperationsAllowed()) {
        return failure(fileOperationsRefusal(QStringLiteral("choose_popup_entry")));
    }
    auto menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
    if (!menu || !menu->isVisible()) {
        return failure(QStringLiteral("no popup menu is open"));
    }
    const auto actions = menu->actions();
    for (QAction *action : actions) {
        // The text, or only its label without the shortcut after a tab ("Copy Here\tCtrl").
        const QString entry = action->text().remove(QLatin1Char('&'));
        if (!action->isSeparator() && (entry == text || entry.section(QLatin1Char('\t'), 0, 0) == text)) {
            if (!action->isEnabled()) {
                return failure(QStringLiteral("the entry \"%1\" is disabled").arg(text));
            }
            // As a click: the menu closes, then the entry acts.
            menu->hide();
            action->trigger();
            return ok();
        }
    }
    return failure(QStringLiteral("the popup menu has no entry \"%1\"").arg(text));
}

// What QTest uses for mouse input on a window: the event takes the way of real input
// (the widget under the position gets it, a click focuses it, two clicks make a double click).
Q_GUI_EXPORT void qt_handleMouseEvent(QWindow *window,
                                      const QPointF &local,
                                      const QPointF &global,
                                      Qt::MouseButtons state,
                                      Qt::MouseButton button,
                                      QEvent::Type type,
                                      Qt::KeyboardModifiers mods,
                                      int timestamp);
// What QTest uses to let a synthesized key reach the window's shortcuts first.
Q_GUI_EXPORT bool qt_sendShortcutOverrideEvent(QObject *o, ulong timestamp, int k, Qt::KeyboardModifiers mods, const QString &text, bool autorep, ushort count);

namespace
{
/// Mouse input at @p position of @p widget, as real input of its window.
void sendMouse(QWidget *widget, QEvent::Type type, const QPoint &position, Qt::MouseButton button, Qt::MouseButtons buttons, int timestamp)
{
    QWidget *window = widget->window();
    const QPointF local = widget->mapTo(window, QPointF(position));
    qt_handleMouseEvent(window->windowHandle(), local, window->mapToGlobal(local), buttons, button, type, Qt::NoModifier, timestamp);
}

QVariantMap treemapStateOf(QWidget *view)
{
    QVariantMap state;
    QMetaObject::invokeMethod(view, "treemapState", Qt::DirectConnection, Q_RETURN_ARG(QVariantMap, state));
    return state;
}

QRect treemapCellRect(QWidget *view, const QUrl &url)
{
    QRect rect;
    QMetaObject::invokeMethod(view, "cellRect", Qt::DirectConnection, Q_RETURN_ARG(QRect, rect), Q_ARG(QUrl, url));
    return rect;
}
}

QJsonObject WhaleDevAutomation::treemapInput(int paneIndexArg, const QString &action, const QString &url, const QString &key)
{
    QString error;
    DolphinViewContainer *container = pane(paneIndexArg, &error);
    if (!container) {
        return failure(error);
    }
    WhaleViewHost *host = container->viewHost();
    QWidget *view = host->isDolphinViewShown() ? nullptr : host->currentView()->widget();
    QWidget *canvas = view ? view->findChild<QWidget *>(QStringLiteral("whaleTreemapCanvas")) : nullptr;
    if (!canvas || view->metaObject()->indexOfMethod("cellRect(QUrl)") < 0) {
        return failure(QStringLiteral("the pane does not show the treemap"));
    }
    const QUrl target = url.isEmpty() ? QUrl() : QUrl::fromUserInput(url, QString(), QUrl::AssumeLocalFile);
    auto cellCenter = [view, &target](QPoint *position) {
        const QRect rect = treemapCellRect(view, target);
        *position = rect.center();
        return !rect.isEmpty();
    };
    auto notShown = [&url]() {
        return failure(QStringLiteral("\"%1\" has no cell of its own in the treemap (not shown, or too small)").arg(url));
    };
    // Input times: steps of one input 10 ms apart, separate clicks a second apart (no double click).
    m_inputTime += 1000;
    auto pressKey = [canvas](const QString &name, bool *shortcut) {
        const QKeySequence sequence = QKeySequence::fromString(name);
        if (sequence.isEmpty()) {
            return false;
        }
        const QKeyCombination combination = sequence[0];
        const int keyCode = int(combination.key());
        const Qt::KeyboardModifiers modifiers = combination.keyboardModifiers();
        const QString text = (modifiers & ~Qt::ShiftModifier) || keyCode > 0xffff ? QString() : QKeySequence(combination.key()).toString().left(1);
        // As for a real key: the shortcuts of the window first, then the focused widget.
        *shortcut = qt_sendShortcutOverrideEvent(canvas, 0, keyCode, modifiers, text, false, 1);
        if (!*shortcut && keyCode == Qt::Key_Menu) {
            // For a real Menu key Qt sends a context menu event first, then the key press.
            QContextMenuEvent menuEvent(QContextMenuEvent::Keyboard, canvas->rect().center(), canvas->mapToGlobal(canvas->rect().center()), modifiers);
            QApplication::sendEvent(canvas, &menuEvent);
        }
        if (!*shortcut) {
            // A menu that opened has the keyboard.
            QWidget *receiver = QApplication::activePopupWidget() ? QApplication::activePopupWidget() : canvas;
            QKeyEvent press(QEvent::KeyPress, keyCode, modifiers, text);
            QApplication::sendEvent(receiver, &press);
            QKeyEvent release(QEvent::KeyRelease, keyCode, modifiers, text);
            QApplication::sendEvent(receiver, &release);
        }
        return true;
    };
    auto click = [this, canvas](const QPoint &position) {
        // A click into an inactive window activates it (the window manager does that; without
        // one, offscreen, the window stays inactive after a dialog or menu has closed).
        if (!canvas->window()->isActiveWindow()) {
            canvas->window()->activateWindow();
            QCoreApplication::processEvents();
        }
        m_inputTime += 1000;
        sendMouse(canvas, QEvent::MouseMove, position, Qt::NoButton, Qt::NoButton, m_inputTime);
        sendMouse(canvas, QEvent::MouseButtonPress, position, Qt::LeftButton, Qt::LeftButton, m_inputTime += 10);
        sendMouse(canvas, QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton, m_inputTime += 10);
    };
    // Keys go where real keys go: to the focus widget, which has to be the treemap.
    auto treemapHasFocus = [canvas]() {
        return QApplication::focusWidget() == canvas;
    };
    // Click into the cell, then go up (Up key) until the entry itself is selected: a folder
    // is covered by its entries, so a click always hits the deepest entry.
    auto select = [&]() -> QJsonObject {
        QPoint position;
        if (!cellCenter(&position)) {
            return notShown();
        }
        click(position);
        if (!treemapHasFocus()) {
            return failure(QStringLiteral("the click did not focus the treemap"));
        }
        for (int step = 0; step < 64; ++step) {
            const QUrl selected = QUrl(treemapStateOf(view).value(QStringLiteral("selected")).toString());
            if (selected == target) {
                return ok();
            }
            if (!target.isParentOf(selected)) {
                return failure(QStringLiteral("selecting \"%1\" ended at \"%2\"").arg(url, selected.toString()));
            }
            bool shortcut = false;
            pressKey(QStringLiteral("Up"), &shortcut);
        }
        return failure(QStringLiteral("could not select \"%1\"").arg(url));
    };

    if (action == QLatin1String("hover")) {
        QPoint position;
        if (target.isEmpty()) {
            // Onto the treemap's bar above the canvas: the pointer leaves the canvas.
            position = QPoint(canvas->width() / 2, -4);
        } else if (!cellCenter(&position)) {
            return notShown();
        }
        sendMouse(canvas, QEvent::MouseMove, position, Qt::NoButton, Qt::NoButton, m_inputTime);
        QJsonObject result = ok();
        // Where that is in the window, e.g. for real input from outside (tests on Wayland).
        const QPoint inWindow = canvas->mapTo(canvas->window(), position);
        result.insert(QStringLiteral("windowX"), inWindow.x());
        result.insert(QStringLiteral("windowY"), inWindow.y());
        return result;
    }
    if (action == QLatin1String("select")) {
        return select();
    }
    if (action == QLatin1String("activate")) {
        const QJsonObject selected = select();
        if (!selected.value(QStringLiteral("ok")).toBool()) {
            return selected;
        }
        bool shortcut = false;
        pressKey(QStringLiteral("Return"), &shortcut);
        return ok();
    }
    if (action == QLatin1String("context_menu")) {
        // A right click: the window makes the context menu event of the widget under the
        // pointer, so the pointer moves there first (from one pixel off: a move to the
        // position it is at already would be dropped).
        QPoint position;
        if (!cellCenter(&position)) {
            return notShown();
        }
        m_inputTime += 1000;
        sendMouse(canvas, QEvent::MouseMove, position + QPoint(1, 1), Qt::NoButton, Qt::NoButton, m_inputTime);
        sendMouse(canvas, QEvent::MouseMove, position, Qt::NoButton, Qt::NoButton, m_inputTime += 10);
        sendMouse(canvas, QEvent::MouseButtonPress, position, Qt::RightButton, Qt::RightButton, m_inputTime += 10);
        sendMouse(canvas, QEvent::MouseButtonRelease, position, Qt::RightButton, Qt::NoButton, m_inputTime += 10);
        QJsonObject result = ok();
        result.insert(QStringLiteral("menuShown"), qobject_cast<QMenu *>(QApplication::activePopupWidget()) != nullptr);
        return result;
    }
    if (action == QLatin1String("double_click")) {
        QPoint position;
        if (!cellCenter(&position)) {
            return notShown();
        }
        click(position);
        // The second click right after the first one at the same place: a double click.
        sendMouse(canvas, QEvent::MouseButtonPress, position, Qt::LeftButton, Qt::LeftButton, m_inputTime += 10);
        sendMouse(canvas, QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton, m_inputTime += 10);
        return ok();
    }
    if (action == QLatin1String("wheel_in") || action == QLatin1String("wheel_out")) {
        QPoint position = canvas->rect().center();
        if (!target.isEmpty() && !cellCenter(&position)) {
            return notShown();
        }
        const QPoint delta(0, action == QLatin1String("wheel_in") ? 120 : -120);
        QWheelEvent event(QPointF(position), QPointF(canvas->mapToGlobal(position)), QPoint(), delta, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(canvas, &event);
        return ok();
    }
    if (action == QLatin1String("key")) {
        if (!treemapHasFocus()) {
            return failure(QStringLiteral("the treemap does not have the keyboard focus (click into it first)"));
        }
        bool shortcut = false;
        if (!pressKey(key, &shortcut)) {
            return failure(QStringLiteral("unknown key \"%1\"").arg(key));
        }
        QJsonObject result = ok();
        result.insert(QStringLiteral("shortcut"), shortcut);
        return result;
    }
    return failure(QStringLiteral("unknown treemap action \"%1\"").arg(action));
}

QJsonObject WhaleDevAutomation::commandState(DolphinViewContainer *container) const
{
    QJsonArray urls;
    const QList<QUrl> selected = WhaleCommandRouter::selectedUrls(container->view());
    for (const QUrl &url : selected) {
        urls.append(url.toString());
    }
    return {
        {QStringLiteral("source"), WhaleCommandRouter::sourceName(WhaleCommandRouter::source(container->view()))},
        {QStringLiteral("urls"), urls},
    };
}

QJsonValue WhaleDevAutomation::paneFileOperations(DolphinViewContainer *container) const
{
    WhaleViewContextImpl *context = container->viewHost()->pluginContext();
    if (!context) {
        return QJsonValue::Null;
    }
    QObject *operations = context->fileOperations();
    watchFileOperations(operations);
    const FileOperationRecord record = m_fileOperations.value(operations);
    return QJsonObject{{QStringLiteral("finished"), record.count},
                       {QStringLiteral("last"), record.last.isEmpty() ? QJsonValue(QJsonValue::Null) : record.last}};
}

void WhaleDevAutomation::watchFileOperations(QObject *object) const
{
    auto operations = qobject_cast<WhaleFileOperations *>(object);
    if (!operations || m_fileOperations.contains(operations)) {
        return;
    }
    m_fileOperations.insert(operations, FileOperationRecord());
    connect(operations, &QObject::destroyed, this, [this, operations]() {
        m_fileOperations.remove(operations);
    });
    connect(
        operations,
        &WhaleFileOperations::operationFinished,
        this,
        [this, operations](WhaleFileOperations::Operation operation, const QList<QUrl> &urls, WhaleFileOperations::Result result, const QList<QUrl> &newUrls) {
            auto toArray = [](const QList<QUrl> &list) {
                QJsonArray array;
                for (const QUrl &url : list) {
                    array.append(url.toString());
                }
                return array;
            };
            FileOperationRecord &record = m_fileOperations[operations];
            ++record.count;
            record.last = QJsonObject{
                {QStringLiteral("seq"), record.count},
                {QStringLiteral("operation"), QString::fromLatin1(QMetaEnum::fromType<WhaleFileOperations::Operation>().valueToKey(int(operation))).toLower()},
                {QStringLiteral("result"), QString::fromLatin1(QMetaEnum::fromType<WhaleFileOperations::Result>().valueToKey(int(result))).toLower()},
                {QStringLiteral("urls"), toArray(urls)},
                {QStringLiteral("newUrls"), toArray(newUrls)},
            };
        });
}

QJsonObject WhaleDevAutomation::fileOperation(int paneIndexArg, const QString &operation, const QStringList &urlArgs)
{
    QString error;
    DolphinViewContainer *container = pane(paneIndexArg, &error);
    if (!container) {
        return failure(error);
    }
    WhaleViewContextImpl *context = container->viewHost()->pluginContext();
    if (!context) {
        return failure(QStringLiteral("the pane shows no plugin view"));
    }
    static const QStringList changing = {QStringLiteral("rename"),
                                         QStringLiteral("trash"),
                                         QStringLiteral("delete"),
                                         QStringLiteral("copy"),
                                         QStringLiteral("cut"),
                                         QStringLiteral("copy_location")};
    if (changing.contains(operation) && !fileOperationsAllowed()) {
        return failure(fileOperationsRefusal(QStringLiteral("file_operation ") + operation));
    }
    QList<QUrl> urls;
    for (const QString &url : urlArgs) {
        urls.append(QUrl::fromUserInput(url, QString(), QUrl::AssumeLocalFile));
    }
    WhaleFileOperations *operations = context->fileOperations();
    watchFileOperations(operations);

    if (operation == QLatin1String("supported")) {
        const WhaleFileOperations::Operations supported = operations->supportedOperations(urls);
        const QMetaEnum names = QMetaEnum::fromType<WhaleFileOperations::Operation>();
        QJsonArray list;
        for (int i = 0; i < names.keyCount(); ++i) {
            if (supported & WhaleFileOperations::Operation(names.value(i))) {
                list.append(QString::fromLatin1(names.key(i)));
            }
        }
        QJsonObject result = ok();
        result.insert(QStringLiteral("supported"), list);
        return result;
    }
    // Deferred: dialogs and menus of the operation must not block this answer.
    const QHash<QString, std::function<void()>> calls = {
        {QStringLiteral("open"),
         [operations, urls]() {
             operations->openItems(urls);
         }},
        {QStringLiteral("open_with"),
         [operations, urls]() {
             operations->openItemsWith(urls);
         }},
        {QStringLiteral("show_in_folder"),
         [operations, urls]() {
             operations->showItemInFolder(urls.value(0));
         }},
        {QStringLiteral("properties"),
         [operations, urls]() {
             operations->showProperties(urls);
         }},
        {QStringLiteral("rename"),
         [operations, urls]() {
             operations->renameItems(urls);
         }},
        {QStringLiteral("trash"),
         [operations, urls]() {
             operations->trashItems(urls);
         }},
        {QStringLiteral("delete"),
         [operations, urls]() {
             operations->deleteItems(urls);
         }},
        {QStringLiteral("copy"),
         [operations, urls]() {
             operations->copyItems(urls);
         }},
        {QStringLiteral("cut"),
         [operations, urls]() {
             operations->cutItems(urls);
         }},
        {QStringLiteral("copy_location"),
         [operations, urls]() {
             operations->copyLocations(urls);
         }},
    };
    const auto call = calls.constFind(operation);
    if (call == calls.constEnd()) {
        return failure(QStringLiteral("unknown file operation \"%1\"").arg(operation));
    }
    if (urls.isEmpty()) {
        return failure(QStringLiteral("no urls"));
    }
    QTimer::singleShot(0, operations, *call);
    return ok();
}

QJsonObject WhaleDevAutomation::answerDialog(const QString &text)
{
    if (!fileOperationsAllowed()) {
        return failure(fileOperationsRefusal(QStringLiteral("answer_dialog")));
    }
    const auto windows = QApplication::topLevelWidgets();
    for (QWidget *window : windows) {
        auto dialog = qobject_cast<QDialog *>(window);
        if (!dialog || !dialog->isVisible()) {
            continue;
        }
        const auto buttons = dialog->findChildren<QAbstractButton *>();
        for (QAbstractButton *button : buttons) {
            if (button->isVisible() && button->text().remove(QLatin1Char('&')) == text) {
                if (!button->isEnabled()) {
                    return failure(QStringLiteral("the button \"%1\" is disabled").arg(text));
                }
                button->click();
                return ok();
            }
        }
    }
    return failure(QStringLiteral("no open dialog has a button \"%1\"").arg(text));
}

QJsonObject WhaleDevAutomation::treemapSetOptions(int paneIndexArg, const QString &colorMode, const QString &labels)
{
    QString error;
    DolphinViewContainer *container = pane(paneIndexArg, &error);
    if (!container) {
        return failure(error);
    }
    WhaleViewHost *host = container->viewHost();
    QWidget *view = host->isDolphinViewShown() ? nullptr : host->currentView()->widget();
    auto colorBox = view ? view->findChild<QComboBox *>(QStringLiteral("colorMode")) : nullptr;
    auto labelsBox = view ? view->findChild<QCheckBox *>(QStringLiteral("showLabels")) : nullptr;
    if (!colorBox || !labelsBox) {
        return failure(QStringLiteral("the pane does not show the treemap"));
    }
    if (colorMode == QLatin1String("fileType") || colorMode == QLatin1String("topFolder")) {
        colorBox->setCurrentIndex(colorMode == QLatin1String("topFolder") ? 1 : 0);
    } else if (!colorMode.isEmpty()) {
        return failure(QStringLiteral("unknown color mode \"%1\"").arg(colorMode));
    }
    if (labels == QLatin1String("true") || labels == QLatin1String("false")) {
        if (labelsBox->isChecked() != (labels == QLatin1String("true"))) {
            labelsBox->click(); // as the user does
        }
    }
    return ok();
}

QJsonObject WhaleDevAutomation::viewerState() const
{
    QStringList mimeTypes = WhaleViewerSettings::mimeTypes();
    QJsonObject state{
        {QStringLiteral("settings"),
         QJsonObject{
             {QStringLiteral("mimeTypes"), QJsonArray::fromStringList(mimeTypes)},
             {QStringLiteral("wrapAround"), WhaleViewerSettings::wrapAround()},
             {QStringLiteral("upscale"), WhaleViewerSettings::upscale()},
             {QStringLiteral("smoothZoom"), WhaleViewerSettings::smoothZoom()},
             {QStringLiteral("videoSupported"), WhaleViewerSettings::videoSupported()},
             {QStringLiteral("videoAutoplay"), WhaleViewerSettings::videoAutoplay()},
             {QStringLiteral("videoSound"), WhaleViewerSettings::videoSound()},
             {QStringLiteral("videoLoop"), WhaleViewerSettings::videoLoop()},
             {QStringLiteral("multiSelection"),
              WhaleViewerSettings::multiSelection() == WhaleViewerSettings::MultiSelection::MoveSelection ? QStringLiteral("move") : QStringLiteral("keep")},
         }},
    };
    WhaleViewer *viewer = WhaleViewer::instance();
    state.insert(QStringLiteral("open"), viewer != nullptr);
    if (!viewer) {
        return state;
    }
    QStringList names;
    const KFileItemList items = viewer->list().items();
    for (const KFileItem &item : items) {
        names.append(item.name());
    }
    state.insert(QStringLiteral("url"), viewer->currentUrl().toString());
    state.insert(QStringLiteral("name"), viewer->list().currentItem().name());
    state.insert(QStringLiteral("index"), viewer->list().currentIndex());
    state.insert(QStringLiteral("count"), viewer->list().count());
    state.insert(QStringLiteral("files"), QJsonArray::fromStringList(names.mid(0, MaxListedItems)));
    state.insert(QStringLiteral("fromSelection"), viewer->list().isSelection());
    state.insert(QStringLiteral("movesSelection"), viewer->movesSelection());
    state.insert(QStringLiteral("loaded"), viewer->isLoaded());
    state.insert(QStringLiteral("loadError"), viewer->hasLoadError());
    state.insert(QStringLiteral("overlay"), viewer->overlayText());
    state.insert(QStringLiteral("visible"), viewer->isVisible());
    state.insert(QStringLiteral("fullScreen"), viewer->isFullScreen());
    state.insert(QStringLiteral("active"), viewer->isActiveWindow());
    state.insert(QStringLiteral("onWindowScreen"), viewer->screen() == m_window->screen());

    // V2e: zoom, the images decoded ahead, animation.
    const WhaleViewerCache::Entry *entry = viewer->cache().entry(viewer->currentUrl());
    state.insert(QStringLiteral("imageSize"), entry ? QJsonArray{entry->fullSize.width(), entry->fullSize.height()} : QJsonArray());
    state.insert(QStringLiteral("zoomed"), viewer->isZoomed());
    state.insert(QStringLiteral("zoom"), std::round(viewer->zoom() * 1000) / 1000);
    const QRect visible = viewer->visibleImageRect().toRect();
    state.insert(QStringLiteral("visibleRect"), QJsonArray{visible.x(), visible.y(), visible.width(), visible.height()});
    QStringList cached;
    const QList<QUrl> urls = viewer->cache().urls();
    for (const QUrl &url : urls) {
        cached.append(url.fileName());
    }
    cached.sort();
    state.insert(QStringLiteral("cached"), QJsonArray::fromStringList(cached));
    state.insert(QStringLiteral("animated"), entry && entry->animated);
    state.insert(QStringLiteral("animating"), viewer->isAnimating());
    state.insert(QStringLiteral("animationFrame"), viewer->animationFrame());

    // V3d: videos.
    state.insert(QStringLiteral("video"), viewer->isVideo());
    state.insert(QStringLiteral("controlsVisible"), viewer->controlsVisible());
#if HAVE_MPV
    if (const WhaleVideoPlayer *player = viewer->videoPlayer()) {
        state.insert(QStringLiteral("player"),
                     QJsonObject{
                         {QStringLiteral("file"), player->url().fileName()},
                         {QStringLiteral("loaded"), player->isLoaded()},
                         {QStringLiteral("error"), player->hasError()},
                         {QStringLiteral("position"), std::round(player->position() * 10) / 10},
                         {QStringLiteral("duration"), std::round(player->duration() * 10) / 10},
                         {QStringLiteral("paused"), player->isPaused()},
                         {QStringLiteral("muted"), player->isMuted()},
                         {QStringLiteral("atEnd"), player->isAtEnd()},
                         {QStringLiteral("size"), QJsonArray{player->videoSize().width(), player->videoSize().height()}},
                         {QStringLiteral("drawn"), player->rendersToWidget()},
                     });
    }
#endif
    return state;
}

QJsonObject WhaleDevAutomation::viewerInput(const QString &action, const QString &argument)
{
    WhaleViewer *viewer = WhaleViewer::instance();
    if (!viewer) {
        return failure(QStringLiteral("the viewer is not open"));
    }
    if (action == QLatin1String("key")) {
        // "+" separates keys in QKeySequence.
        const QKeySequence sequence = argument == QLatin1String("+") ? QKeySequence(Qt::Key_Plus) : QKeySequence::fromString(argument);
        if (sequence.isEmpty()) {
            return failure(QStringLiteral("unknown key \"%1\"").arg(argument));
        }
        const QKeyCombination combination = sequence[0];
        QKeyEvent press(QEvent::KeyPress, combination.key(), combination.keyboardModifiers());
        QApplication::sendEvent(viewer, &press);
        QKeyEvent release(QEvent::KeyRelease, combination.key(), combination.keyboardModifiers());
        QApplication::sendEvent(viewer, &release);
        return ok();
    }
    if (action == QLatin1String("wheel")) {
        bool valid = false;
        const int delta = argument.toInt(&valid);
        if (!valid) {
            return failure(QStringLiteral("the wheel needs an angle, e.g. -120"));
        }
        const QPoint position = viewer->rect().center();
        QWheelEvent event(QPointF(position),
                          QPointF(viewer->mapToGlobal(position)),
                          QPoint(),
                          QPoint(0, delta),
                          Qt::NoButton,
                          Qt::NoModifier,
                          Qt::NoScrollPhase,
                          false);
        QApplication::sendEvent(viewer, &event);
        return ok();
    }
    if (action == QLatin1String("click")) {
        const int y = viewer->height() / 2;
        QPoint position;
        if (argument == QLatin1String("left")) {
            position = QPoint(viewer->width() / 10, y);
        } else if (argument == QLatin1String("center")) {
            position = QPoint(viewer->width() / 2, y);
        } else if (argument == QLatin1String("right")) {
            position = QPoint(viewer->width() * 9 / 10, y);
        } else {
            return failure(QStringLiteral("click where? left, center or right, not \"%1\"").arg(argument));
        }
        sendMouse(viewer, QEvent::MouseButtonPress, position, Qt::LeftButton, Qt::LeftButton, m_inputTime += 10);
        sendMouse(viewer, QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton, m_inputTime += 10);
        return ok();
    }
    if (action == QLatin1String("zoom_wheel")) {
        // "delta" or "delta x y", x and y fractions of the window (default the middle).
        const QStringList parts = argument.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        bool valid = parts.count() == 1 || parts.count() == 3;
        const int delta = valid ? parts.at(0).toInt(&valid) : 0;
        QPointF position(viewer->width() / 2.0, viewer->height() / 2.0);
        if (valid && parts.count() == 3) {
            position = QPointF(parts.at(1).toDouble(&valid) * viewer->width(), position.y());
            position.setY(valid ? parts.at(2).toDouble(&valid) * viewer->height() : 0);
        }
        if (!valid) {
            return failure(QStringLiteral("the zoom wheel needs an angle, e.g. 120, and optionally where (fractions of the window)"));
        }
        QWheelEvent event(position, viewer->mapToGlobal(position), QPoint(), QPoint(0, delta), Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(viewer, &event);
        return ok();
    }
    if (action == QLatin1String("controls")) {
        // "play", or a fraction of the progress to seek to, e.g. "0.75" (V3d).
        if (!viewer->controlsVisible()) {
            return failure(QStringLiteral("the controls of a video do not show"));
        }
        QPoint position;
        bool fraction = false;
        const qreal value = argument.toDouble(&fraction);
        if (argument == QLatin1String("play")) {
            position = viewer->playButtonRect().center();
        } else if (fraction && value >= 0 && value <= 1) {
            const QRect area = viewer->seekAreaRect();
            const int line = viewer->fontMetrics().height();
            position = QPoint(area.left() + line / 2 + qRound(value * (area.width() - line)), area.center().y());
        } else {
            return failure(QStringLiteral("controls: \"play\" or a fraction from 0 to 1, not \"%1\"").arg(argument));
        }
        sendMouse(viewer, QEvent::MouseButtonPress, position, Qt::LeftButton, Qt::LeftButton, m_inputTime += 10);
        sendMouse(viewer, QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton, m_inputTime += 10);
        return ok();
    }
    if (action == QLatin1String("pointer")) {
        // The pointer moves over the viewer: the controls of a video show.
        sendMouse(viewer, QEvent::MouseMove, viewer->rect().center(), Qt::NoButton, Qt::NoButton, m_inputTime += 10);
        return ok();
    }
    if (action == QLatin1String("drag")) {
        // "dx dy" in window pixels, with the middle button from the middle of the window.
        const QStringList parts = argument.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        bool validX = false;
        bool validY = false;
        const int dx = parts.count() == 2 ? parts.at(0).toInt(&validX) : 0;
        const int dy = parts.count() == 2 ? parts.at(1).toInt(&validY) : 0;
        if (!validX || !validY) {
            return failure(QStringLiteral("drag needs dx and dy"));
        }
        const QPoint from = viewer->rect().center();
        const QPoint to = from + QPoint(dx, dy);
        sendMouse(viewer, QEvent::MouseButtonPress, from, Qt::MiddleButton, Qt::MiddleButton, m_inputTime += 10);
        sendMouse(viewer, QEvent::MouseMove, to, Qt::NoButton, Qt::MiddleButton, m_inputTime += 10);
        sendMouse(viewer, QEvent::MouseButtonRelease, to, Qt::MiddleButton, Qt::NoButton, m_inputTime += 10);
        return ok();
    }
    return failure(QStringLiteral("unknown viewer action \"%1\"").arg(action));
}

QJsonObject WhaleDevAutomation::setViewerOptions(const QJsonObject &options)
{
    for (auto it = options.begin(); it != options.end(); ++it) {
        if (it.key() == QLatin1String("wrapAround") && it.value().isBool()) {
            WhaleViewerSettings::setWrapAround(it.value().toBool());
        } else if (it.key() == QLatin1String("upscale") && it.value().isBool()) {
            WhaleViewerSettings::setUpscale(it.value().toBool());
        } else if (it.key() == QLatin1String("smoothZoom") && it.value().isBool()) {
            WhaleViewerSettings::setSmoothZoom(it.value().toBool());
        } else if (it.key() == QLatin1String("videoAutoplay") && it.value().isBool()) {
            WhaleViewerSettings::setVideoAutoplay(it.value().toBool());
        } else if (it.key() == QLatin1String("videoSound") && it.value().isBool()) {
            WhaleViewerSettings::setVideoSound(it.value().toBool());
        } else if (it.key() == QLatin1String("videoLoop") && it.value().isBool()) {
            WhaleViewerSettings::setVideoLoop(it.value().toBool());
        } else if (it.key() == QLatin1String("mimeTypes") && it.value().isArray()) {
            QStringList mimeTypes;
            const QJsonArray array = it.value().toArray();
            for (const QJsonValue &value : array) {
                mimeTypes.append(value.toString());
            }
            WhaleViewerSettings::setMimeTypes(mimeTypes);
        } else if (it.key() == QLatin1String("multiSelection") && (it.value() == QLatin1String("keep") || it.value() == QLatin1String("move"))) {
            WhaleViewerSettings::setMultiSelection(it.value() == QLatin1String("move") ? WhaleViewerSettings::MultiSelection::MoveSelection
                                                                                       : WhaleViewerSettings::MultiSelection::KeepSelection);
        } else {
            return failure(QStringLiteral("unknown or invalid viewer option \"%1\"").arg(it.key()));
        }
    }
    QJsonObject reply = ok();
    reply.insert(QStringLiteral("viewer"), viewerState());
    return reply;
}

QByteArray WhaleDevAutomation::viewerScreenshot() const
{
    WhaleViewer *viewer = WhaleViewer::instance();
    if (!viewer) {
        return {};
    }
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    viewer->grab().save(&buffer, "PNG");
    return png;
}

#include "moc_whaledevautomation.cpp"
