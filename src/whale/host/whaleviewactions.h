/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEVIEWACTIONS_H
#define WHALEVIEWACTIONS_H

#include <QHash>
#include <QMetaObject>
#include <QObject>
#include <QPointer>

class KActionCollection;
class KSelectAction;
class QAction;
class QActionGroup;
class WhaleViewHost;
class WhaleViewRegistry;

/**
 * @brief The view actions of a main window: the view switcher and the gating of
 * Dolphin's actions for plugin views.
 *
 * Owns the WhaleViewRegistry of the window and switches the view of the
 * active view area between the built-in file list and the plugin views.
 * Views are identified by their plugin ID, never by their display name.
 *
 * Gating: while the active view area shows a plugin view, Dolphin's actions that
 * the view cannot honor (WhaleViewCapabilities) are disabled, so they never act on
 * state the user cannot see. Each gateable action gets an action group of its own
 * that serves as the gate: Dolphin keeps enabling and disabling the action as
 * usual, a disabled group keeps it disabled, and opening the gate restores exactly
 * Dolphin's latest state (QActionGroup semantics). Actions that work correctly on
 * the mirrored selection (e.g. Copy, Move to Trash with a selection) stay as they are.
 *
 * Rename (F2) in a plugin view: views with InlineRename rename themselves, the
 * others get Dolphin's rename dialog; the DolphinView's inline editor is never used.
 */
class WhaleViewActions : public QObject
{
    Q_OBJECT

public:
    /**
     * Adds the view switcher to @p actionCollection and to its "view_settings" menu,
     * so it must be created after the actions of the DolphinViewActionHandler.
     */
    WhaleViewActions(KActionCollection *actionCollection, QObject *parent);
    ~WhaleViewActions() override;

    /**
     * Sets the host of the active view area. The switcher shows and changes its view.
     */
    void setCurrentHost(WhaleViewHost *host);

    WhaleViewRegistry *registry() const;

    /**
     * @return Why the gating disables the action @p actionName for the shown view, or
     *         an empty string if it does not. For tests and development tools.
     */
    QString gateReason(const QString &actionName) const;

    /**
     * @return The names of the actions that the gating may disable.
     */
    static QStringList gateableActions();

    /**
     * Called by the view areas of the window when they show another view, so the
     * session (which records the shown views) is saved again.
     */
    void notifyViewChanged();

Q_SIGNALS:
    /**
     * Is emitted when a view area of the window shows another view. Like a changed
     * location, this is a reason to save the session.
     */
    void sessionChanged();

private:
    void slotViewSelected(QAction *action);
    void updateCheckedView();
    void updateGates();
    void slotRename();
    void slotViewModeTriggered();

    WhaleViewRegistry *m_registry;
    KSelectAction *m_switcher;
    KActionCollection *const m_actionCollection;
    QPointer<WhaleViewHost> m_host;
    QMetaObject::Connection m_hostConnection;
    QMetaObject::Connection m_selectionConnection;
    QMetaObject::Connection m_capabilitiesConnection;
    QHash<QString, QActionGroup *> m_gates;
    QHash<QString, QString> m_gateReasons;
};

#endif
