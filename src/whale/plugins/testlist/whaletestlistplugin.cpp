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
#include <QIcon>
#include <QListView>
#include <QPointer>

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
        beginResetModel();
        m_items = m_list->items();
        endResetModel();
    }

    void slotItemsInserted(int index, const KFileItemList &items)
    {
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
        if (items.isEmpty()) {
            return;
        }
        beginRemoveRows(QModelIndex(), index, index + items.count() - 1);
        m_items.remove(index, items.count());
        endRemoveRows();
    }

    void slotItemsMoved(int index, const QList<int> &movedToIndexes)
    {
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
        if (items.isEmpty()) {
            return;
        }
        std::copy(items.begin(), items.end(), m_items.begin() + index);
        Q_EMIT dataChanged(this->index(index), this->index(index + items.count() - 1));
    }

    WhaleItemList *const m_list;
    KFileItemList m_items;
};

/**
 * Shows the names of the items of the location in the host's order, so the
 * host's sorting, hidden files and name filter apply.
 *
 * The view only displays the items. It has no selection of its own, and it does
 * not open, rename, drag or delete items or offer a context menu: until views
 * can share the host's selection, such a selection or action could not tell the
 * user which items the host's file actions would affect.
 */
class WhaleTestListView : public WhaleView
{
public:
    WhaleTestListView(WhaleViewContext *context, QObject *parent)
        : WhaleView(context, parent)
        , m_listView(new QListView)
    {
        m_listView->setObjectName(QStringLiteral("whaleTestListView"));
        m_listView->setModel(new ItemListModel(context->items(), m_listView));
        m_listView->setUniformItemSizes(true);
        m_listView->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_listView->setSelectionMode(QAbstractItemView::NoSelection);
        m_listView->setDragEnabled(false);
        m_listView->setAcceptDrops(false);
        // No menu, and none from a parent widget either.
        m_listView->setContextMenuPolicy(Qt::PreventContextMenu);
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
        return WhaleViewCapability::HostOrder;
    }

private:
    QPointer<QListView> m_listView;
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
