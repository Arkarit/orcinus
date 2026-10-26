/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEFOCUSORDER_H
#define WHALEFOCUSORDER_H

class QWidget;

/**
 * @brief The order in which Tab moves the focus through the file views of a window.
 *
 * Qt follows the focus chain, the order in which the widgets were created: the File View
 * Toolbar is created after its view and status bar, and widgets created later (the buttons
 * of a location bar after navigating somewhere else) go to the end of the window. So Tab
 * reached a view's location bar after the view, and Shift+Tab from a view went into the
 * location bar of the view before it.
 *
 * The focus chain stays as it is (QWidget::setTabOrder() moves whole ranges around focus
 * proxies and cannot move widgets without focus, which Qt stops at as well). Instead the main
 * window asks here which widget is next: the widgets Tab stops at in Qt's order, but those of
 * each file view (DolphinViewContainer) together, at the place of its first one, its File
 * View Toolbar first (in the order it is seen, a wrapped location bar after the buttons) and then
 * the others in Qt's order.
 */
namespace WhaleFocusOrder
{
/**
 * Moves the focus of @p window to the next (@p next) or the previous widget in that order.
 *
 * @return false if the focus is on none of those widgets: Qt decides then.
 */
bool focusNextPrevChild(QWidget *window, bool next);
}

#endif
