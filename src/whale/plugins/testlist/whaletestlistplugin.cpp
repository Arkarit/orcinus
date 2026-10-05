/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleitemlist.h"
#include "whaleview.h"
#include "whaleviewcontext.h"
#include "whaleviewplugin.h"

#include <KPluginFactory>

#include <QAbstractListModel>
#include <QEvent>
#include <QHash>
#include <QIcon>
#include <QKeyEvent>
#include <QListView>
#include <QPointer>
#include <QScopedValueRollback>

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
        case Qt::DecorationRole:
            return QIcon::fromTheme(item.isDir() ? QStringLiteral("folder") : QStringLiteral("text-x-generic"));
        default:
            return {};
        }
    }

private:
    void slotItemsReset()
    {
        QScopedValueRollback updating(m_updating, true);
        beginResetModel();
        m_items = m_list->items();
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
        std::copy(items.begin(), items.end(), m_items.begin() + index);
        Q_EMIT dataChanged(this->index(index), this->index(index + items.count() - 1));
    }

    WhaleItemList *const m_list;
    KFileItemList m_items;
    bool m_updating = false;
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
 * dialog: the view does not declare InlineRename. No drag and drop, no previews.
 */
class WhaleTestListView : public WhaleView
{
public:
    WhaleTestListView(WhaleViewContext *context, QObject *parent)
        : WhaleView(context, parent)
        , m_listView(new QListView)
        , m_model(new ItemListModel(context->items(), m_listView))
    {
        m_listView->setObjectName(QStringLiteral("whaleTestListView"));
        m_listView->setModel(m_model);
        m_listView->setUniformItemSizes(true);
        m_listView->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_listView->setSelectionMode(QAbstractItemView::ExtendedSelection);
        m_listView->setDragEnabled(false);
        m_listView->setAcceptDrops(false);
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
        return C::HostOrder | C::Selection | C::MultiSelection | C::KeyboardNavigation;
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_listView->viewport() && event->type() == QEvent::Leave) {
            requestItemInfo(QUrl());
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
        const int currentRow = rows.value(selection.current(), -1);
        selectionModel->setCurrentIndex(currentRow >= 0 ? m_model->index(currentRow) : QModelIndex(), QItemSelectionModel::NoUpdate);
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

    QPointer<QListView> m_listView;
    ItemListModel *const m_model;
    bool m_applying = false;
    QUrl m_infoUrl;
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
