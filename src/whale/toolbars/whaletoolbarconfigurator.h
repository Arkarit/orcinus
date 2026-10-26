/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALETOOLBARCONFIGURATOR_H
#define WHALETOOLBARCONFIGURATOR_H

#include "whaletoolbardescriptor.h"

#include <QObject>
#include <QPointer>

#include <functional>
#include <memory>

class KEditToolBar;
class KXMLGUIClient;
class KXmlGuiWindow;
class QAction;
class QWidget;
class WhaleFileViewToolbarEditClient;

/**
 * @brief Configure Toolbars of a main window, for its main toolbar and the File View Toolbar.
 *
 * KDE's dialog (KEditToolBar) edits the toolbars of all clients of the window's XML GUI
 * factory and offers them in its "Toolbar:" list. So that the File View Toolbar is one of
 * them, a client with its configuration file is part of the factory while the dialog is
 * open, and only then. Its actions are prototypes: what the dialog shows as "Available
 * actions" of the File View Toolbar (WhaleViewCommandScope::allowedActions()), with the
 * texts and icons of the window's actions of the same names. They do nothing and have no
 * shortcuts; the toolbars of the file views show actions of their own.
 *
 * Exactly one per main window, a direct child of the window.
 *
 * Lifetime of the client: created and added to the factory when the dialog opens, removed
 * and deleted when the dialog is finished (OK, Cancel, closed). If the window is destroyed
 * with the dialog open, the factory is gone already and is not touched.
 */
class WhaleToolbarConfigurator : public QObject
{
    Q_OBJECT

public:
    /**
     * Makes "Configure Toolbars…" of @p window open the dialog of the configurator.
     * @p saveNewToolbarConfig is what the window does after its toolbars were changed
     * (KXmlGuiWindow::saveNewToolbarConfig()).
     * @return The configurator of the window, created as its child if it has none.
     */
    static WhaleToolbarConfigurator *install(KXmlGuiWindow *window, const std::function<void()> &saveNewToolbarConfig);

    /**
     * @return The configurator of the window of @p widget, or nullptr.
     */
    static WhaleToolbarConfigurator *of(const QWidget *widget);

    ~WhaleToolbarConfigurator() override;

    /**
     * Opens Configure Toolbars with the toolbar of the kind @p descriptorId chosen
     * (WhaleToolbarDescriptor::globalId() or fileViewId()). If the dialog is open already,
     * it is brought to the front as it is.
     */
    void configure(const QString &descriptorId = WhaleToolbarDescriptor::globalId());

    /**
     * @return The dialog while it is open, otherwise nullptr.
     */
    KEditToolBar *dialog() const;

private:
    WhaleToolbarConfigurator(KXmlGuiWindow *window, const std::function<void()> &saveNewToolbarConfig);

    void slotNewToolBarConfig();
    void slotFinished();
    void slotClientAdded(KXMLGUIClient *client);
    void slotClientRemoved(KXMLGUIClient *client);
    void polishToolBarNames();

    KXmlGuiWindow *m_window;
    std::function<void()> m_saveNewToolbarConfig;
    QPointer<KEditToolBar> m_dialog;
    std::unique_ptr<WhaleFileViewToolbarEditClient> m_editClient;
    QPointer<QAction> m_findAction;
    bool m_findActionWasEnabled = true;
};

#endif
