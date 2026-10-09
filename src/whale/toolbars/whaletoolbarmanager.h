/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALETOOLBARMANAGER_H
#define WHALETOOLBARMANAGER_H

#include "whaletoolbardescriptor.h"
#include "whaletoolbarlayout.h"

#include <QList>
#include <QObject>

#include <memory>

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

Q_SIGNALS:
    void fileViewLayoutChanged();

private:
    explicit WhaleToolbarManager(QWidget *window);

    std::unique_ptr<WhaleFileViewToolbarClient> m_fileViewClient;
    WhaleToolbarLayout m_fileViewLayout;
};

#endif
