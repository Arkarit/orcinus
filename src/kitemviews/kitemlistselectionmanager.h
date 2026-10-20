/*
 * SPDX-FileCopyrightText: 2011 Peter Penz <peter.penz19@gmail.com>
 *
 * Based on the Itemviews NG project from Trolltech Labs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KITEMLISTSELECTIONMANAGER_H
#define KITEMLISTSELECTIONMANAGER_H

#include "dolphin_export.h"
#include "kitemviews/kitemmodelbase.h"
#include "kitemviews/kitemset.h"

#include <QObject>

#include <functional>

class KItemModelBase;

/**
 * @brief Allows to select and deselect items of a KItemListView.
 */
class DOLPHIN_EXPORT KItemListSelectionManager : public QObject
{
    Q_OBJECT

    enum RangesRemovingBehaviour {
        DiscardRemovedIndex,
        AdjustRemovedIndex
    };

public:
    enum SelectionMode {
        Select,
        Deselect,
        Toggle
    };

    explicit KItemListSelectionManager(QObject *parent = nullptr);
    ~KItemListSelectionManager() override;

    void setCurrentItem(int current);
    int currentItem() const;

    void setSelectedItems(const KItemSet &items);
    KItemSet selectedItems() const;
    bool isSelected(int index) const;
    bool hasSelection() const;

    void setSelected(int index, int count = 1, SelectionMode mode = Select);
    /**
     * Equivalent to:
     * clearSelection();
     * setSelected(index, count);
     * but emitting once only selectionChanged signal
     */
    void replaceSelection(int index, int count = 1);
    void clearSelection();

    void beginAnchoredSelection(int anchor);
    void endAnchoredSelection();
    bool isAnchoredSelectionActive() const;

    KItemModelBase *model() const;

    /**
     * Whale: limits the selection to items of one group, e.g. of one folder while the views of a
     * split view are linked. @p sameGroup tells whether two items belong to the same group; an
     * empty function removes the limit. Adding one item of another group to the selection is
     * refused, a range (also an anchored one) is limited to the group of the selection or the
     * anchor. selectionRestricted() tells about what was refused or left out. A selection of
     * several groups is reduced to the group of the current item at once.
     */
    void setSameGroup(const std::function<bool(int, int)> &sameGroup);
    bool isGroupLimited() const;

Q_SIGNALS:
    void currentChanged(int current, int previous);
    void selectionChanged(const KItemSet &current, const KItemSet &previous);
    /// Whale: items were left out of the selection because of setSameGroup().
    void selectionRestricted();

private:
    void setModel(KItemModelBase *model);
    void itemsInserted(const KItemRangeList &itemRanges);
    void itemsRemoved(const KItemRangeList &itemRanges);
    void itemsMoved(const KItemRange &itemRange, const QList<int> &movedToIndexes);

    /**
     * Helper method for itemsRemoved. Returns the changed index after removing
     * the given range. If the index is part of the range, -1 will be returned.
     */
    int indexAfterRangesRemoving(int index, const KItemRangeList &itemRanges, const RangesRemovingBehaviour behaviour) const;

    /// Whale: whether the anchored range may include @p index (setSameGroup()).
    bool isInAnchoredGroup(int index) const;
    /// Whale: @p items without those of another group than @p reference.
    KItemSet itemsOfGroup(const KItemSet &items, int reference) const;

private:
    int m_currentItem;
    int m_anchorItem;
    KItemSet m_selectedItems;
    bool m_isAnchoredSelectionActive;

    KItemModelBase *m_model;
    std::function<bool(int, int)> m_sameGroup; // Whale: setSameGroup()

    friend class KItemListController; // Calls setModel()
    friend class KItemListView; // Calls itemsInserted(), itemsRemoved() and itemsMoved()
    friend class KItemListSelectionManagerTest;
};

#endif
