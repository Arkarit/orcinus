/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEMENUBARACTION_H
#define WHALEMENUBARACTION_H

#include <QPointer>
#include <QWidget>
#include <QWidgetAction>

class QMenu;
class QMenuBar;
class QToolBar;
class QToolButton;

/**
 * @brief The menu bar as an element of the window's toolbars (TM, .privatedoc/whale-toolbar-menu-plan.md).
 *
 * Each widget it creates holds a menu bar of its own with the menus of the window's menu bar
 * (the same QMenu objects), and follows that one when KXmlGui builds it anew. It is as wide as
 * all titles together and does not grow (E3). Where that width is not left in the toolbar, it
 * shows a single button with the menus instead (E2, TM2). A KHamburgerMenu button in the same
 * toolbar would be a second one; it is left out there as long as the element is in it (F2).
 *
 * It does not depend on the window's menu bar being shown (E1): "Show Menubar" only changes
 * the classic one.
 */
class WhaleMenuBarAction : public QWidgetAction
{
    Q_OBJECT

public:
    /** @p source is the window's menu bar. */
    WhaleMenuBarAction(QMenuBar *source, QObject *parent);
    ~WhaleMenuBarAction() override;

    /** @return The name of the action in the window's action collection. */
    static QString actionName();

protected:
    QWidget *createWidget(QWidget *parent) override;

private:
    QPointer<QMenuBar> m_source;
};

/**
 * @brief The widget of WhaleMenuBarAction in one toolbar.
 *
 * QToolBar never shrinks a widget but moves it to its overflow menu, so the widget works out
 * itself whether all menus fit: the width of the toolbar minus the other elements, the
 * stretching ones (spacers, location bars) at their minimum width. That depends only on the
 * toolbar and the others, not on what the widget shows, so the choice cannot flicker.
 */
class WhaleToolBarMenu : public QWidget
{
    Q_OBJECT

public:
    WhaleToolBarMenu(QMenuBar *source, QWidget *parent);
    ~WhaleToolBarMenu() override;

    /** @return The menu bar shown in the toolbar. */
    QMenuBar *menuBar() const;

    /** @return The button shown instead of the menu bar where it does not fit. */
    QToolButton *burgerButton() const;

    /** @return Whether the button is shown instead of the menu bar. */
    bool isBurger() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    /** Takes the menus of the window's menu bar. */
    void sync();
    void scheduleSync();

    /** Shows the menu bar or the button, as the toolbar has room. */
    void updateMode();
    void scheduleUpdateMode();
    void setBurger(bool burger);
    /** @return The width the toolbar leaves for this widget. */
    int availableWidth() const;
    void fillBurgerMenu();

    /** Leaves out (or brings back) the KHamburgerMenu button of the toolbar (F2). */
    void suppressHamburger(bool suppress);
    static void restoreHamburger(QWidget *hamburger);

    QPointer<QMenuBar> m_source;
    QPointer<QToolBar> m_toolBar;
    QMenuBar *m_menuBar;
    QToolButton *m_burgerButton;
    QMenu *m_burgerMenu;
    bool m_burger = false;
    bool m_syncScheduled = false;
    bool m_updateScheduled = false;

    QPointer<QWidget> m_hamburger; // the suppressed button
};

#endif
