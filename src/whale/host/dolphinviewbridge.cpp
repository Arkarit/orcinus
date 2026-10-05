/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "dolphinviewbridge.h"

#include "dolphin_generalsettings.h"
#include "kitemviews/kfileitemmodel.h"
#include "kitemviews/kitemlistcontainer.h"
#include "kitemviews/kitemlistcontroller.h"
#include "kitemviews/kitemlistselectionmanager.h"
#include "views/dolphinview.h"
#include "whaleitemlist.h"
#include "whaleviewdebug.h"

#include <QPoint>
#include <QPointer>
#include <QTimer>

namespace
{
/**
 * A WhaleItemList that follows the KFileItemModel of a DolphinView.
 *
 * Every range of a model signal becomes one step of the list, so the list never
 * passes model ranges or roles on. A step that does not fit the list (which would
 * mean that the list missed a change) replaces the whole content instead.
 */
class MirroredItemList : public WhaleItemList
{
public:
    MirroredItemList(KItemListController *controller, QObject *parent)
        : WhaleItemList(parent)
    {
        connect(controller, &KItemListController::modelChanged, this, [this](KItemModelBase *current) {
            setModel(current);
        });
        setModel(controller->model());
    }

private:
    void setModel(KItemModelBase *model)
    {
        if (m_model) {
            disconnect(m_model, nullptr, this, nullptr);
        }
        m_model = qobject_cast<KFileItemModel *>(model);
        if (m_model) {
            connect(m_model, &KFileItemModel::itemsInserted, this, &MirroredItemList::slotItemsInserted);
            connect(m_model, &KFileItemModel::itemsRemoved, this, &MirroredItemList::slotItemsRemoved);
            connect(m_model, &KFileItemModel::itemsMoved, this, &MirroredItemList::slotItemsMoved);
            connect(m_model, &KFileItemModel::itemsChanged, this, &MirroredItemList::slotItemsChanged);
        }
        resetItems(modelItems(0, modelCount()));
    }

    int modelCount() const
    {
        return m_model ? m_model->count() : 0;
    }

    KFileItemList modelItems(int index, int count) const
    {
        KFileItemList items;
        items.reserve(count);
        for (int i = index; i < index + count; ++i) {
            items.append(m_model->fileItem(i));
        }
        return items;
    }

    void resync(const char *reason)
    {
        qCWarning(WhaleViewDebug) << "Item list out of step with the DolphinView (" << reason << "), reloading all items";
        resetItems(modelItems(0, modelCount()));
    }

    void checkCount(const char *reason)
    {
        if (count() != modelCount()) {
            resync(reason);
        }
    }

    // The ranges refer to the model before the insertion, so each range starts
    // behind the items inserted for the previous ranges.
    void slotItemsInserted(const KItemRangeList &ranges)
    {
        int inserted = 0;
        for (const KItemRange &range : ranges) {
            const int index = range.index + inserted;
            if (index < 0 || index > count() || index + range.count > modelCount()) {
                resync("insert");
                return;
            }
            insertItems(index, modelItems(index, range.count));
            inserted += range.count;
        }
        checkCount("insert");
    }

    // The ranges refer to the model before the removal.
    void slotItemsRemoved(const KItemRangeList &ranges)
    {
        int removed = 0;
        for (const KItemRange &range : ranges) {
            const int index = range.index - removed;
            if (index < 0 || range.count < 0 || index + range.count > count()) {
                resync("remove");
                return;
            }
            removeItems(index, range.count);
            removed += range.count;
        }
        checkCount("remove");
    }

    void slotItemsMoved(const KItemRange &range, const QList<int> &movedToIndexes)
    {
        const int end = range.index + range.count;
        bool valid = range.index >= 0 && movedToIndexes.count() == range.count && end <= count() && count() == modelCount();
        QList<bool> taken(valid ? range.count : 0, false);
        for (int i = 0; valid && i < movedToIndexes.count(); ++i) {
            const int to = movedToIndexes.at(i);
            valid = to >= range.index && to < end && !taken.at(to - range.index);
            if (valid) {
                taken[to - range.index] = true;
            }
        }
        if (!valid) {
            resync("move");
            return;
        }
        moveItems(range.index, movedToIndexes);

        // The model is sorted already, so the moved range must match it now.
        for (int i = range.index; i < end; ++i) {
            if (item(i).url() != m_model->fileItem(i).url()) {
                resync("move");
                return;
            }
        }
    }

    // Most changes concern roles only (previews, folder sizes, …) and keep the
    // KFileItem. Only items that really differ are reported, in contiguous runs.
    //
    // This list is connected to the model after the DolphinView, so a change can
    // arrive before a structural step that caused it: KFileItemModelRolesUpdater
    // sets roles synchronously while it handles itemsInserted() or itemsMoved().
    // The indexes of such a change already refer to the model after that step.
    // These nested changes concern roles only, and the pending step takes its
    // items from the model anyway, so they are skipped:
    // - The count differs: an insertion or removal is pending.
    // - Another item is at the index, and that item is still in the model: a move
    //   is pending. (If it is no longer in the model, it was replaced at this
    //   index, which is how the model reports a rename.)
    void slotItemsChanged(const KItemRangeList &ranges, const QSet<QByteArray> &roles)
    {
        Q_UNUSED(roles)
        if (count() != modelCount()) {
            return;
        }
        for (const KItemRange &range : ranges) {
            int runStart = -1;
            KFileItemList run;
            const int end = std::min(range.index + range.count, count());
            for (int i = std::max(range.index, 0); i <= end; ++i) {
                const bool atEnd = i == end;
                const KFileItem modelItem = atEnd ? KFileItem() : m_model->fileItem(i);
                if (!atEnd && isChange(item(i), modelItem)) {
                    if (runStart < 0) {
                        runStart = i;
                    }
                    run.append(modelItem);
                } else if (runStart >= 0) {
                    changeItems(runStart, run);
                    runStart = -1;
                    run.clear();
                }
            }
        }
    }

    bool isChange(const KFileItem &listItem, const KFileItem &modelItem) const
    {
        if (listItem.url() == modelItem.url()) {
            return !listItem.cmp(modelItem);
        }
        return m_model->index(listItem.url()) < 0;
    }

    QPointer<KFileItemModel> m_model;
};
}

DolphinViewBridge::DolphinViewBridge(DolphinView *dolphinView, QObject *parent)
    : QObject(parent)
    , m_dolphinView(dolphinView)
    , m_selectionTimer(new QTimer(this))
{
    connect(dolphinView, &DolphinView::urlChanged, this, &DolphinViewBridge::urlChanged);
    connect(dolphinView, &DolphinView::directoryLoadingStarted, this, &DolphinViewBridge::loadingStarted);
    connect(dolphinView, &DolphinView::directoryLoadingCompleted, this, &DolphinViewBridge::loadingCompleted);
    connect(dolphinView, &DolphinView::directoryLoadingCanceled, this, &DolphinViewBridge::loadingCanceled);

    // The selection manager reports every step (a rubber band, the index shifts of an
    // insertion, ...). They are collected and compared by URL once the event loop is
    // idle; by then the item lists have followed the model as well. Renaming changes
    // a selected URL without any selection signal, hence itemsChanged too.
    m_selectionTimer->setSingleShot(true);
    m_selectionTimer->setInterval(0);
    connect(m_selectionTimer, &QTimer::timeout, this, &DolphinViewBridge::reportSelection);
    auto schedule = [this]() {
        m_selectionTimer->start();
    };
    KItemListSelectionManager *selectionManager = controller()->selectionManager();
    connect(selectionManager, &KItemListSelectionManager::selectionChanged, this, schedule);
    connect(selectionManager, &KItemListSelectionManager::selectionChanged, this, [this]() {
        if (hasSelection() != m_hasSelection) {
            m_hasSelection = !m_hasSelection;
            Q_EMIT hasSelectionChanged(m_hasSelection);
        }
    });
    connect(selectionManager, &KItemListSelectionManager::currentChanged, this, schedule);
    connect(model(), &KFileItemModel::itemsChanged, this, schedule);
    m_reportedSelection = selection();
    m_hasSelection = hasSelection();
}

DolphinViewBridge::~DolphinViewBridge() = default;

QUrl DolphinViewBridge::url() const
{
    return m_dolphinView->url();
}

WhaleItemList *DolphinViewBridge::createItemList(QObject *parent) const
{
    return new MirroredItemList(controller(), parent);
}

void DolphinViewBridge::requestContextMenu(const QPoint &globalPos, const QUrl &url)
{
    int index = -1;
    if (!url.isEmpty()) {
        index = model()->index(url);
        if (index < 0) {
            return;
        }
    }

    m_dolphinView->setActive(true);
    KItemListController *itemListController = controller();
    if (index >= 0) {
        KItemListSelectionManager *selectionManager = itemListController->selectionManager();
        if (!selectionManager->isSelected(index)) {
            setSelection(WhaleSelection({url}, url));
        }
        // Request signals of the controller (plan 3.2): the DolphinView first sends a
        // pending selection change, so the actions in the menu are up to date, then
        // asks the main window for the menu.
        Q_EMIT itemListController->itemContextMenuRequested(index, globalPos);
    } else {
        Q_EMIT itemListController->viewContextMenuRequested(globalPos);
    }
}

void DolphinViewBridge::requestItemInfo(const QUrl &url)
{
    const KFileItemModel *fileItemModel = model();
    const int index = url.isEmpty() ? -1 : fileItemModel->index(url);
    // The DolphinView's request signal itself: its hover handling would also show a
    // tooltip at the position of the hidden view.
    Q_EMIT m_dolphinView->requestItemInfo(index >= 0 ? fileItemModel->fileItem(index) : KFileItem());
}

void DolphinViewBridge::renameSelectedItemsWithDialog()
{
    // DolphinView::renameSelectedItems() renames a single item inline when the user
    // prefers that, otherwise with the dialog, and afterwards selects the renamed
    // items. Its dialog path is used as it is: the setting is a plain value without
    // notifications and is only read synchronously here.
    const bool renameInline = GeneralSettings::renameInline();
    GeneralSettings::setRenameInline(false);
    m_dolphinView->renameSelectedItems();
    GeneralSettings::setRenameInline(renameInline);
}

KItemListController *DolphinViewBridge::controller() const
{
    return m_dolphinView->itemListContainer()->controller();
}

KFileItemModel *DolphinViewBridge::model() const
{
    return qobject_cast<KFileItemModel *>(controller()->model());
}

WhaleSelection DolphinViewBridge::selection() const
{
    const KFileItemModel *fileItemModel = model();
    const KItemListSelectionManager *selectionManager = controller()->selectionManager();
    QList<QUrl> urls;
    const KItemSet selected = selectionManager->selectedItems();
    urls.reserve(selected.count());
    for (int index : selected) {
        urls.append(fileItemModel->fileItem(index).url());
    }
    const int current = selectionManager->currentItem();
    return WhaleSelection(urls, current >= 0 ? fileItemModel->fileItem(current).url() : QUrl());
}

bool DolphinViewBridge::hasSelection() const
{
    return controller()->selectionManager()->hasSelection();
}

WhaleSelection DolphinViewBridge::setSelection(const WhaleSelection &selection)
{
    const KFileItemModel *fileItemModel = model();
    KItemListSelectionManager *selectionManager = controller()->selectionManager();

    KItemSet indexes;
    const QList<QUrl> urls = selection.selected();
    for (const QUrl &url : urls) {
        const int index = fileItemModel->index(url);
        if (index >= 0) {
            indexes.insert(index);
        }
    }
    const int current = selection.current().isEmpty() ? -1 : fileItemModel->index(selection.current());

    // A shift selection in progress would add its range to selectedItems().
    selectionManager->endAnchoredSelection();
    if (current >= 0) {
        selectionManager->setCurrentItem(current);
    }
    selectionManager->setSelectedItems(indexes);

    // The selection manager has emitted its signals: the DolphinView follows as usual,
    // and reportSelection() tells all views (the caller's context skips its own echo).
    return this->selection();
}

void DolphinViewBridge::reportSelection()
{
    const WhaleSelection current = selection();
    if (m_reportedSelection && *m_reportedSelection == current) {
        return;
    }
    m_reportedSelection = current;
    Q_EMIT selectionChanged(current);
}

void DolphinViewBridge::activateItems(const QList<QUrl> &urls)
{
    const KFileItemModel *fileItemModel = model();
    KItemSet indexes;
    for (const QUrl &url : urls) {
        const int index = fileItemModel->index(url);
        if (index >= 0) {
            indexes.insert(index);
        }
    }
    if (indexes.isEmpty()) {
        return;
    }

    // The main window only listens to the active view area (plan 14.8).
    m_dolphinView->setActive(true);
    // Request signals of the controller, see plan 3.2: the DolphinView handles them
    // exactly as a double click or Enter in its own view (archives, folders, files,
    // Alt for properties, confirmation for many items).
    KItemListController *itemListController = controller();
    if (indexes.count() == 1) {
        Q_EMIT itemListController->itemActivated(indexes.first());
    } else {
        Q_EMIT itemListController->itemsActivated(indexes);
    }
}

#include "moc_dolphinviewbridge.cpp"
