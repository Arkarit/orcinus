/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whalefocusorder.h"

#include "dolphinviewcontainer.h"
#include "whalefileviewtoolbar.h"
#include "whalelocationbar.h"

#include <QApplication>
#include <QHash>
#include <QSet>
#include <QWidget>

#include <algorithm>

namespace
{
DolphinViewContainer *containerOf(QWidget *widget)
{
    for (QWidget *parent = widget->parentWidget(); parent && !parent->isWindow(); parent = parent->parentWidget()) {
        if (auto container = qobject_cast<DolphinViewContainer *>(parent)) {
            return container;
        }
    }
    return nullptr;
}

// The widgets Tab stops at, in the order described in the header.
QList<QWidget *> tabStops(QWidget *window)
{
    // In Qt's order: each widget that takes the focus, at the place of the first widget that
    // passes it on to it (a focus proxy).
    QList<QWidget *> stops;
    QSet<QWidget *> seen;
    for (QWidget *widget = window->nextInFocusChain(); widget != window; widget = widget->nextInFocusChain()) {
        QWidget *target = widget;
        while (target->focusProxy()) {
            target = target->focusProxy();
        }
        if (target->window() == window && (target->focusPolicy() & Qt::TabFocus) && target->isVisibleTo(window) && target->isEnabled()
            && !seen.contains(target)) {
            seen.insert(target);
            stops.append(target);
        }
    }

    QHash<DolphinViewContainer *, QList<QWidget *>> toolBarStops;
    QHash<DolphinViewContainer *, QList<QWidget *>> otherStops;
    QList<QWidget *> order; // a file view stands for its widgets, at the place of its first one
    for (QWidget *stop : std::as_const(stops)) {
        DolphinViewContainer *container = containerOf(stop);
        if (!container) {
            order.append(stop);
            continue;
        }
        if (!toolBarStops.contains(container) && !otherStops.contains(container)) {
            order.append(container);
        }
        auto toolBar = container->findChild<WhaleFileViewToolBar *>(QString(), Qt::FindDirectChildrenOnly);
        if (toolBar && toolBar->isAncestorOf(stop)) {
            toolBarStops[container].append(stop);
        } else {
            otherStops[container].append(stop);
        }
    }

    QList<QWidget *> result;
    for (QWidget *widget : std::as_const(order)) {
        auto container = qobject_cast<DolphinViewContainer *>(widget);
        if (!container) {
            result.append(widget);
            continue;
        }
        QList<QWidget *> toolBar = toolBarStops.value(container);
        auto bar = container->findChild<WhaleFileViewToolBar *>(QString(), Qt::FindDirectChildrenOnly);
        // The row of the buttons, then the location bar if it is in the second row.
        const auto row = [bar](QWidget *widget) {
            return bar && bar->isLocationBarWrapped() && bar->locationBar()->isAncestorOf(widget) ? 1 : 0;
        };
        const bool rightToLeft = container->layoutDirection() == Qt::RightToLeft;
        std::stable_sort(toolBar.begin(), toolBar.end(), [container, rightToLeft, &row](QWidget *a, QWidget *b) {
            if (row(a) != row(b)) {
                return row(a) < row(b);
            }
            const int xA = a->mapTo(container, QPoint(rightToLeft ? a->width() : 0, 0)).x();
            const int xB = b->mapTo(container, QPoint(rightToLeft ? b->width() : 0, 0)).x();
            return rightToLeft ? xA > xB : xA < xB;
        });
        result += toolBar + otherStops.value(container);
    }
    return result;
}
}

bool WhaleFocusOrder::focusNextPrevChild(QWidget *window, bool next)
{
    QWidget *focus = QApplication::focusWidget();
    if (!window || !focus || focus->window() != window) {
        return false;
    }
    const QList<QWidget *> stops = tabStops(window);
    const int index = stops.indexOf(focus);
    if (index < 0 || stops.count() < 2) {
        return false;
    }
    QWidget *target = stops.at((index + (next ? 1 : stops.count() - 1)) % stops.count());
    target->setFocus(next ? Qt::TabFocusReason : Qt::BacktabFocusReason);
    return true;
}
