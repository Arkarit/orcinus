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

class QMenuBar;

/**
 * @brief The menu bar as an element of the window's toolbars (TM, .privatedoc/whale-toolbar-menu-plan.md).
 *
 * Each widget it creates holds a menu bar of its own with the menus of the window's menu bar
 * (the same QMenu objects), and follows that one when KXmlGui builds it anew. It is as wide as
 * all titles together and does not grow (E3).
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
 */
class WhaleToolBarMenu : public QWidget
{
    Q_OBJECT

public:
    WhaleToolBarMenu(QMenuBar *source, QWidget *parent);
    ~WhaleToolBarMenu() override;

    /** @return The menu bar shown in the toolbar. */
    QMenuBar *menuBar() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    /** Takes the menus of the window's menu bar. */
    void sync();
    void scheduleSync();

    QPointer<QMenuBar> m_source;
    QMenuBar *m_menuBar;
    bool m_syncScheduled = false;
};

#endif
