/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whalefileoperations.h"
#include "whaleitemlist.h"
#include "whalepreviewprovider.h"
#include "whaleview.h"
#include "whaleviewcontext.h"
#include "whaleviewplugin.h"

#include <KPluginFactory>

#include <QAbstractListModel>
#include <QDataStream>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QHash>
#include <QIODevice>
#include <QIcon>
#include <QKeyEvent>
#include <QListView>
#include <QPointer>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QSet>
#include <QTimer>

#include <functional>

namespace
{
/**
 * Presents a WhaleItemList as a Qt list model. It keeps its own copy of the items,
 * because the list reports its steps after they happened, while a Qt model must
 * announce them before.
 */
class ItemListModel : public QAbstractListModel
{
public:
    ItemListModel(WhaleItemList *list, QObject *parent)
        : QAbstractListModel(parent)
        , m_list(list)
        , m_items(list->items())
    {
        connect(list, &WhaleItemList::itemsReset, this, &ItemListModel::slotItemsReset);
        connect(list, &WhaleItemList::itemsInserted, this, &ItemListModel::slotItemsInserted);
        connect(list, &WhaleItemList::itemsRemoved, this, &ItemListModel::slotItemsRemoved);
        connect(list, &WhaleItemList::itemsMoved, this, &ItemListModel::slotItemsMoved);
        connect(list, &WhaleItemList::itemsChanged, this, &ItemListModel::slotItemsChanged);
    }

    /**
     * @return True while the model follows a change of the item list. Selection
     *         changes of the list view in that time are consequences, not user input.
     */
    bool isUpdating() const
    {
        return m_updating;
    }

    QUrl url(int row) const
    {
        return m_items.value(row).url();
    }

    KFileItem item(int row) const
    {
        return m_items.value(row);
    }

    /**
     * Shows @p preview instead of the icon of the item @p url; a null pixmap shows the
     * icon again.
     */
    void setPreview(const QUrl &url, const QPixmap &preview)
    {
        if (preview.isNull()) {
            if (!m_previews.remove(url)) {
                return;
            }
        } else {
            m_previews.insert(url, preview);
        }
        const int row = rowOf(url);
        if (row >= 0) {
            Q_EMIT dataChanged(index(row), index(row), {Qt::DecorationRole});
        }
    }

    bool hasPreview(const QUrl &url) const
    {
        return m_previews.contains(url);
    }

    QList<QUrl> previewUrls() const
    {
        return m_previews.keys();
    }

    void clearPreviews()
    {
        if (m_previews.isEmpty()) {
            return;
        }
        m_previews.clear();
        if (!m_items.isEmpty()) {
            Q_EMIT dataChanged(index(0), index(m_items.count() - 1), {Qt::DecorationRole});
        }
    }

    int rowOf(const QUrl &url) const
    {
        for (int row = 0; row < m_items.count(); ++row) {
            if (m_items.at(row).url() == url) {
                return row;
            }
        }
        return -1;
    }

    QHash<QUrl, int> rowsByUrl() const
    {
        QHash<QUrl, int> rows;
        rows.reserve(m_items.count());
        for (int row = 0; row < m_items.count(); ++row) {
            rows.insert(m_items.at(row).url(), row);
        }
        return rows;
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : m_items.count();
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
            return {};
        }
        const KFileItem &item = m_items.at(index.row());
        switch (role) {
        case Qt::DisplayRole:
            return item.text();
        case Qt::DecorationRole: {
            // A preview is a QPixmap, the icon a QIcon (development tools tell them apart).
            const auto preview = m_previews.constFind(item.url());
            if (preview != m_previews.constEnd()) {
                return *preview;
            }
            return QIcon::fromTheme(item.isDir() ? QStringLiteral("folder") : QStringLiteral("text-x-generic"));
        }
        default:
            return {};
        }
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        // Lets QAbstractItemView recognize the drag gesture; the drag itself is Whale's.
        return QAbstractListModel::flags(index) | (index.isValid() ? Qt::ItemIsDragEnabled : Qt::NoItemFlags);
    }

private:
    void slotItemsReset()
    {
        QScopedValueRollback updating(m_updating, true);
        beginResetModel();
        m_items = m_list->items();
        m_previews.clear();
        endResetModel();
    }

    void slotItemsInserted(int index, const KFileItemList &items)
    {
        QScopedValueRollback updating(m_updating, true);
        if (items.isEmpty()) {
            return;
        }
        beginInsertRows(QModelIndex(), index, index + items.count() - 1);
        m_items.insert(index, items.count(), KFileItem());
        std::copy(items.begin(), items.end(), m_items.begin() + index);
        endInsertRows();
    }

    void slotItemsRemoved(int index, const KFileItemList &items)
    {
        QScopedValueRollback updating(m_updating, true);
        if (items.isEmpty()) {
            return;
        }
        beginRemoveRows(QModelIndex(), index, index + items.count() - 1);
        for (const KFileItem &item : items) {
            m_previews.remove(item.url());
        }
        m_items.remove(index, items.count());
        endRemoveRows();
    }

    void slotItemsMoved(int index, const QList<int> &movedToIndexes)
    {
        QScopedValueRollback updating(m_updating, true);
        Q_EMIT layoutAboutToBeChanged();
        const KFileItemList old = m_items.mid(index, movedToIndexes.count());
        for (int i = 0; i < old.count(); ++i) {
            m_items[movedToIndexes.at(i)] = old.at(i);
        }
        // Keep the current item of the list view on the same file.
        const QModelIndexList persistent = persistentIndexList();
        QModelIndexList moved;
        for (const QModelIndex &from : persistent) {
            const int offset = from.row() - index;
            moved.append(offset >= 0 && offset < movedToIndexes.count() ? this->index(movedToIndexes.at(offset)) : from);
        }
        changePersistentIndexList(persistent, moved);
        Q_EMIT layoutChanged();
    }

    void slotItemsChanged(int index, const KFileItemList &items)
    {
        QScopedValueRollback updating(m_updating, true);
        if (items.isEmpty()) {
            return;
        }
        // A changed file (or a renamed one) needs a new preview.
        for (int i = 0; i < items.count(); ++i) {
            m_previews.remove(m_items.at(index + i).url());
        }
        std::copy(items.begin(), items.end(), m_items.begin() + index);
        Q_EMIT dataChanged(this->index(index), this->index(index + items.count() - 1));
    }

    WhaleItemList *const m_list;
    KFileItemList m_items;
    QHash<QUrl, QPixmap> m_previews;
    bool m_updating = false;
};

/**
 * The list view of the test list. Drag and drop are handed to the view's callbacks:
 * QListView's own handling would move rows of the model.
 */
class TestListWidget : public QListView
{
public:
    std::function<void()> dragStarted;
    std::function<void(QDragMoveEvent *)> dragMoved;
    std::function<void(QDropEvent *)> dropped;

protected:
    void startDrag(Qt::DropActions supportedActions) override
    {
        Q_UNUSED(supportedActions)
        dragStarted();
    }

    void dragEnterEvent(QDragEnterEvent *event) override
    {
        dragMoved(event);
    }

    void dragMoveEvent(QDragMoveEvent *event) override
    {
        dragMoved(event);
    }

    void dragLeaveEvent(QDragLeaveEvent *event) override
    {
        event->accept();
    }

    void dropEvent(QDropEvent *event) override
    {
        dropped(event);
    }
};

/**
 * Shows the names of the items of the location in the host's order, so the
 * host's sorting, hidden files and name filter apply.
 *
 * The selection is the host's: the view shows the context's selection and reports
 * the user's changes back, so the host's actions, status bar and information panel
 * see it, and it survives switching views. Enter and a double click open items,
 * a right click shows the host's context menu, and hovering an item describes it in
 * the information panel, all through the context. Renaming (F2) is the host's
 * dialog: the view does not declare InlineRename.
 *
 * Previews: while the context's preview provider is enabled ("Show Previews"), the
 * rows are larger and show thumbnails. Only the visible rows and one page above and
 * below them are requested; rows that leave that range are canceled and lose their
 * thumbnail, so scrolling through a large folder never piles up work.
 *
 * Drag and drop go through the context's file operations: dragging the selection,
 * and dropping onto a folder row or onto the location (any other row, the background).
 *
 * Its state (saveState()) is the item scrolled to the top, so the history, view
 * switches and the session bring the user back to the same place.
 */
class WhaleTestListView : public WhaleView
{
public:
    WhaleTestListView(WhaleViewContext *context, QObject *parent)
        : WhaleView(context, parent)
        , m_listView(new TestListWidget)
        , m_model(new ItemListModel(context->items(), m_listView))
        , m_previews(context->previews())
        , m_previewTimer(new QTimer(this))
    {
        m_listView->setObjectName(QStringLiteral("whaleTestListView"));
        m_listView->setModel(m_model);
        m_listView->setUniformItemSizes(true);
        m_listView->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_listView->setSelectionMode(QAbstractItemView::ExtendedSelection);
        m_listView->setDragEnabled(true);
        m_listView->setAcceptDrops(true);
        m_listView->setDragDropMode(QAbstractItemView::DragDrop);
        m_listView->dragStarted = [this]() {
            startDrag();
        };
        m_listView->dragMoved = [this](QDragMoveEvent *event) {
            this->context()->fileOperations()->updateDropAction(event, dropTarget(event->position().toPoint()));
        };
        m_listView->dropped = [this](QDropEvent *event) {
            this->context()->fileOperations()->drop(event, dropTarget(event->position().toPoint()), m_listView->viewport());
        };
        m_listView->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(m_listView, &QWidget::customContextMenuRequested, this, &WhaleTestListView::requestContextMenu);
        m_listView->setMouseTracking(true);
        connect(m_listView, &QAbstractItemView::entered, this, [this](const QModelIndex &index) {
            requestItemInfo(m_model->url(index.row()));
        });
        connect(m_listView, &QAbstractItemView::viewportEntered, this, [this]() {
            requestItemInfo(QUrl());
        });
        m_listView->installEventFilter(this);
        m_listView->viewport()->installEventFilter(this);

        applySelection(context->selection());
        connect(context, &WhaleViewContext::selectionChanged, this, &WhaleTestListView::applySelection);
        QItemSelectionModel *selectionModel = m_listView->selectionModel();
        connect(selectionModel, &QItemSelectionModel::selectionChanged, this, &WhaleTestListView::reportSelection);
        connect(selectionModel, &QItemSelectionModel::currentChanged, this, &WhaleTestListView::reportSelection);
        connect(m_listView, &QAbstractItemView::doubleClicked, this, [this](const QModelIndex &index) {
            this->context()->activateItems({m_model->url(index.row())});
        });

        // A restored top item may not be listed yet; it is applied once it is.
        connect(context, &WhaleViewContext::urlChanged, this, [this]() {
            m_pendingTopItem.clear();
        });
        connect(m_model, &QAbstractItemModel::rowsInserted, this, &WhaleTestListView::schedulePendingTopItem);
        connect(m_model, &QAbstractItemModel::modelReset, this, &WhaleTestListView::schedulePendingTopItem);
        connect(context, &WhaleViewContext::loadingCompleted, this, &WhaleTestListView::schedulePendingTopItem);

        // Previews of the rows in sight, updated once the event loop is idle after
        // scrolling, resizing or changes of the items.
        m_previewTimer->setSingleShot(true);
        m_previewTimer->setInterval(0);
        connect(m_previewTimer, &QTimer::timeout, this, &WhaleTestListView::updatePreviews);
        connect(m_previews, &WhalePreviewProvider::previewAvailable, this, &WhaleTestListView::slotPreviewAvailable);
        connect(m_previews, &WhalePreviewProvider::previewFailed, this, &WhaleTestListView::slotPreviewFailed);
        connect(m_previews, &WhalePreviewProvider::enabledChanged, this, &WhaleTestListView::applyPreviewsEnabled);
        connect(m_listView->verticalScrollBar(), &QScrollBar::valueChanged, this, &WhaleTestListView::schedulePreviews);
        connect(m_model, &QAbstractItemModel::rowsInserted, this, &WhaleTestListView::schedulePreviews);
        connect(m_model, &QAbstractItemModel::rowsRemoved, this, &WhaleTestListView::schedulePreviews);
        connect(m_model, &QAbstractItemModel::modelReset, this, &WhaleTestListView::schedulePreviews);
        connect(m_model, &QAbstractItemModel::layoutChanged, this, &WhaleTestListView::schedulePreviews);
        connect(context->items(), &WhaleItemList::itemsChanged, this, [this](int index, const KFileItemList &items) {
            Q_UNUSED(index)
            // Changed files need new previews; the model has dropped the old ones.
            QList<QUrl> urls;
            for (const KFileItem &item : items) {
                urls.append(item.url());
                m_noPreview.remove(item.url());
            }
            m_previews->cancel(urls);
            schedulePreviews();
        });
        connect(context, &WhaleViewContext::urlChanged, this, [this]() {
            // The provider has canceled the requests of the previous location.
            m_requested.clear();
            m_noPreview.clear();
        });
        applyPreviewsEnabled(m_previews->isEnabled());
    }

    QByteArray saveState() const override
    {
        const QModelIndex top = m_listView->indexAt(QPoint(1, 1));
        if (!top.isValid() && m_pendingTopItem.isEmpty()) {
            return {};
        }
        QByteArray state;
        QDataStream stream(&state, QIODevice::WriteOnly);
        stream << quint8(1) << (top.isValid() ? m_model->url(top.row()) : m_pendingTopItem);
        return state;
    }

    void restoreState(const QByteArray &state) override
    {
        QDataStream stream(state);
        quint8 version = 0;
        QUrl topItem;
        stream >> version >> topItem;
        if (stream.status() != QDataStream::Ok || version != 1) {
            return; // unknown or damaged: keep the view as it is
        }
        m_pendingTopItem = topItem;
        schedulePendingTopItem();
    }

    ~WhaleTestListView() override
    {
        delete m_listView;
    }

    QWidget *widget() override
    {
        return m_listView;
    }

    WhaleViewCapabilities capabilities() const override
    {
        using C = WhaleViewCapability;
        return C::HostOrder | C::Selection | C::MultiSelection | C::KeyboardNavigation | C::Previews | C::Drag | C::Drop;
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_listView->viewport() && event->type() == QEvent::Leave) {
            requestItemInfo(QUrl());
        }
        if (watched == m_listView->viewport() && event->type() == QEvent::Resize) {
            schedulePendingTopItem(); // a list without a size cannot scroll yet
            schedulePreviews();
        }
        if (watched == m_listView && event->type() == QEvent::KeyPress) {
            const int key = static_cast<QKeyEvent *>(event)->key();
            if (key == Qt::Key_Return || key == Qt::Key_Enter) {
                // Like the file list: Enter opens the selected items, or the current one.
                QList<QUrl> urls = context()->selection().selected();
                if (urls.isEmpty() && m_listView->currentIndex().isValid()) {
                    urls.append(m_model->url(m_listView->currentIndex().row()));
                }
                context()->activateItems(urls);
                return true;
            }
        }
        return WhaleView::eventFilter(watched, event);
    }

private:
    void applySelection(const WhaleSelection &selection)
    {
        QScopedValueRollback applying(m_applying, true);
        const QHash<QUrl, int> rows = m_model->rowsByUrl();
        QItemSelection itemSelection;
        const QList<QUrl> urls = selection.selected();
        for (const QUrl &url : urls) {
            const int row = rows.value(url, -1);
            if (row >= 0) {
                itemSelection.select(m_model->index(row), m_model->index(row));
            }
        }
        QItemSelectionModel *selectionModel = m_listView->selectionModel();
        selectionModel->select(itemSelection, QItemSelectionModel::ClearAndSelect);
        // The host's current item does not move the view; what is scrolled into view is
        // the view's own state.
        const bool autoScroll = m_listView->hasAutoScroll();
        m_listView->setAutoScroll(false);
        const int currentRow = rows.value(selection.current(), -1);
        selectionModel->setCurrentIndex(currentRow >= 0 ? m_model->index(currentRow) : QModelIndex(), QItemSelectionModel::NoUpdate);
        m_listView->setAutoScroll(autoScroll);
    }

    void schedulePendingTopItem()
    {
        if (!m_pendingTopItem.isEmpty()) {
            // After the list view has laid out the new rows.
            QTimer::singleShot(0, this, &WhaleTestListView::applyPendingTopItem);
        }
    }

    void applyPendingTopItem()
    {
        const int row = m_model->rowsByUrl().value(m_pendingTopItem, -1);
        if (row < 0 || m_listView->viewport()->height() <= 0) {
            return;
        }
        m_listView->scrollTo(m_model->index(row), QAbstractItemView::PositionAtTop);
        // Items may still come in above it while the location loads; keep it until then.
        if (!context()->isLoading() && m_listView->indexAt(QPoint(1, 1)).row() == row) {
            m_pendingTopItem.clear();
        }
    }

    void requestContextMenu(const QPoint &position)
    {
        const QModelIndex index = m_listView->indexAt(position);
        if (!index.isValid()) {
            // As in the file list, a right click on the background is about the folder.
            m_listView->selectionModel()->clearSelection();
        }
        context()->requestContextMenu(m_listView->viewport()->mapToGlobal(position), index.isValid() ? m_model->url(index.row()) : QUrl());
    }

    void requestItemInfo(const QUrl &url)
    {
        if (url != m_infoUrl) {
            m_infoUrl = url;
            context()->requestItemInfo(url);
        }
    }

    void reportSelection()
    {
        // Changes made by applySelection() or by rows coming and going are not the user's.
        if (m_applying || m_model->isUpdating()) {
            return;
        }
        QModelIndexList selectedRows = m_listView->selectionModel()->selectedIndexes();
        std::sort(selectedRows.begin(), selectedRows.end());
        QList<QUrl> urls;
        for (const QModelIndex &index : std::as_const(selectedRows)) {
            urls.append(m_model->url(index.row()));
        }
        const QModelIndex current = m_listView->currentIndex();
        context()->setSelection(WhaleSelection(urls, current.isValid() ? m_model->url(current.row()) : QUrl()));
    }

    void startDrag()
    {
        const QList<QUrl> urls = context()->selection().selected();
        if (!urls.isEmpty()) {
            context()->fileOperations()->startDrag(urls);
        }
    }

    /// The item under @p position for a drop, empty for the background.
    QUrl dropTarget(const QPoint &position) const
    {
        const QModelIndex index = m_listView->indexAt(position);
        return index.isValid() ? m_model->url(index.row()) : QUrl();
    }

    void applyPreviewsEnabled(bool enabled)
    {
        m_noPreview.clear();
        if (!enabled) {
            m_requested.clear();
            m_model->clearPreviews();
        }
        // Thumbnails need larger rows than icons.
        m_listView->setIconSize(enabled ? QSize(PreviewSize, PreviewSize) : QSize());
        schedulePreviews();
    }

    void schedulePreviews()
    {
        if (m_previews->isEnabled()) {
            m_previewTimer->start();
        }
    }

    /**
     * Requests the previews of the rows in sight and one page above and below them,
     * and cancels (and forgets) those of all other rows.
     */
    void updatePreviews()
    {
        if (!m_previews->isEnabled()) {
            return;
        }
        const int rows = m_model->rowCount();
        QSet<QUrl> wanted;
        KFileItemList toRequest;
        if (rows > 0 && m_listView->viewport()->height() > 0) {
            const QRect area = m_listView->viewport()->rect();
            const QModelIndex first = m_listView->indexAt(area.topLeft() + QPoint(1, 1));
            const QModelIndex last = m_listView->indexAt(QPoint(1, area.bottom() - 1));
            const int firstRow = first.isValid() ? first.row() : 0;
            const int lastRow = last.isValid() ? last.row() : rows - 1;
            const int page = lastRow - firstRow + 1;
            for (int row = std::max(0, firstRow - page); row <= std::min(rows - 1, lastRow + page); ++row) {
                const KFileItem item = m_model->item(row);
                wanted.insert(item.url());
                if (!m_model->hasPreview(item.url()) && !m_noPreview.contains(item.url()) && !m_previews->isPending(item.url())) {
                    toRequest.append(item);
                }
            }
        }

        QList<QUrl> outOfSight;
        for (const QUrl &url : std::as_const(m_requested)) {
            if (!wanted.contains(url)) {
                outOfSight.append(url);
            }
        }
        m_previews->cancel(outOfSight);
        for (const QUrl &url : std::as_const(outOfSight)) {
            m_requested.remove(url);
        }
        const QList<QUrl> shown = m_model->previewUrls();
        for (const QUrl &url : shown) {
            if (!wanted.contains(url)) {
                m_model->setPreview(url, QPixmap());
            }
        }

        if (!toRequest.isEmpty()) {
            for (const KFileItem &item : std::as_const(toRequest)) {
                m_requested.insert(item.url());
            }
            m_previews->requestPreviews(toRequest, QSize(PreviewSize, PreviewSize), m_listView->devicePixelRatioF());
        }
    }

    void slotPreviewAvailable(const KFileItem &item, const QPixmap &preview)
    {
        if (m_requested.remove(item.url())) {
            m_model->setPreview(item.url(), preview);
        }
    }

    void slotPreviewFailed(const KFileItem &item)
    {
        if (m_requested.remove(item.url())) {
            m_noPreview.insert(item.url()); // keeps its icon; not asked again until it changes
        }
    }

    static constexpr int PreviewSize = 48;

    QPointer<TestListWidget> m_listView;
    ItemListModel *const m_model;
    WhalePreviewProvider *const m_previews;
    QTimer *const m_previewTimer;
    // Previews requested by this view and not answered yet.
    QSet<QUrl> m_requested;
    // Items without a preview (unsupported type, error).
    QSet<QUrl> m_noPreview;
    bool m_applying = false;
    QUrl m_infoUrl;
    QUrl m_pendingTopItem;
};
}

class WhaleTestListPlugin : public WhaleViewPlugin
{
    Q_OBJECT

public:
    explicit WhaleTestListPlugin(QObject *parent)
        : WhaleViewPlugin(parent)
    {
    }

    WhaleView *createView(WhaleViewContext *context, QObject *parent) override
    {
        return new WhaleTestListView(context, parent);
    }
};

K_PLUGIN_CLASS_WITH_JSON(WhaleTestListPlugin, "whale_testlist.json")

#include "whaletestlistplugin.moc"
