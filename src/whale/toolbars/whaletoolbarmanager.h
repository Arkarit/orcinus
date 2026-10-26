/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALETOOLBARMANAGER_H
#define WHALETOOLBARMANAGER_H

#include "whaletoolbardescriptor.h"
#include "whaletoolbarlayout.h"

#include <QList>
#include <QObject>
#include <QPointer>

#include <memory>

class KXmlGuiWindow;
class KToggleAction;
class QMenu;
class QWidget;
class WhaleFileViewToolbarClient;

/**
 * @brief The toolbars of one main window: their kinds and the shared configurations.
 *
 * Exactly one per main window, a direct child of the window (like WhaleViewServices; no
 * singleton). It knows the kinds of toolbars (descriptors) and the one layout of the File
 * View Toolbar. The File View Toolbars themselves belong to their file views
 * (WhaleFileViewToolBar in each DolphinViewContainer); the manager keeps no list of them,
 * they follow layoutChanged().
 *
 * Whether the File View Toolbars are shown is one setting for all of them
 * (WhaleToolbarSettings); the manager offers it as the window's action
 * "whale_show_file_view_toolbars", in the View menu and in the context menu of the window's
 * own toolbars. (DolphinMainWindow puts it into the hamburger menu, DolphinContextMenu into
 * the menu of a view's background while the File View Toolbars are hidden: without the menu
 * bar, these are the ways back.)
 */
class WhaleToolbarManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @return The manager of the main window @p window, created as its child if it has none.
     */
    static WhaleToolbarManager *ensure(QWidget *window);

    /**
     * @return The manager of the window of @p widget, or nullptr.
     */
    static WhaleToolbarManager *of(const QWidget *widget);

    ~WhaleToolbarManager() override;

    /**
     * Creates the window's action "Show File View Toolbars" in its action collection, before
     * the window builds its GUI (it may be on the main toolbar), and puts it into the View
     * menu and the context menus of the window's toolbars each time the window's GUI is built.
     * The window's XML GUI file is not changed.
     */
    void setupActions(KXmlGuiWindow *window);

    /**
     * @return The window's action "Show File View Toolbars", or nullptr before setupActions().
     */
    KToggleAction *showFileViewToolBarsAction() const;

    static QString showFileViewToolBarsActionName();

    /**
     * @return The kinds of toolbars: the main toolbar ("global") and the File View Toolbar
     *         ("file-view").
     */
    QList<WhaleToolbarDescriptor> descriptors() const;
    WhaleToolbarDescriptor descriptor(const QString &id) const;

    /**
     * @return The layout of the File View Toolbar, shared by the toolbars of all file views.
     */
    WhaleToolbarLayout fileViewLayout() const;

    /**
     * Reads the configuration of the File View Toolbar again.
     */
    void reloadFileViewLayout();

    /**
     * Reads the configuration of the File View Toolbar again in all windows: it is one
     * configuration for the application.
     */
    static void reloadAllFileViewLayouts();

Q_SIGNALS:
    void fileViewLayoutChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    explicit WhaleToolbarManager(QWidget *window);

    void plugIntoViewMenu();
    void watchToolBarMenus();
    void watchMenu(QMenu *menu);
    void plugIntoToolBarMenu();

    KXmlGuiWindow *m_window = nullptr;
    QPointer<KToggleAction> m_showFileViewToolBars; // owned by the window's action collection
    std::unique_ptr<WhaleFileViewToolbarClient> m_fileViewClient;
    WhaleToolbarLayout m_fileViewLayout;
};

#endif
