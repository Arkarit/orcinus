/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEVIEWACTIONS_H
#define WHALEVIEWACTIONS_H

#include <QObject>
#include <QPointer>

class KActionCollection;
class KSelectAction;
class QAction;
class WhaleViewHost;
class WhaleViewRegistry;

/**
 * @brief The view actions of a main window: the view switcher.
 *
 * Owns the WhaleViewRegistry of the window and switches the view of the
 * active view area between the built-in file list and the plugin views.
 * Views are identified by their plugin ID, never by their display name.
 */
class WhaleViewActions : public QObject
{
    Q_OBJECT

public:
    WhaleViewActions(KActionCollection *actionCollection, QObject *parent);
    ~WhaleViewActions() override;

    /**
     * Sets the host of the active view area. The switcher shows and changes its view.
     */
    void setCurrentHost(WhaleViewHost *host);

    WhaleViewRegistry *registry() const;

private:
    void slotViewSelected(QAction *action);
    void updateCheckedView();

    WhaleViewRegistry *m_registry;
    KSelectAction *m_switcher;
    QPointer<WhaleViewHost> m_host;
    QMetaObject::Connection m_hostConnection;
};

#endif
