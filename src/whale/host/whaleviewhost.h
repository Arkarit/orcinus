/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEVIEWHOST_H
#define WHALEVIEWHOST_H

#include "whaleviewstate.h"

#include <QList>
#include <QPointer>
#include <QWidget>

#include <memory>

class DolphinView;
class DolphinViewAdapter;
class DolphinViewBridge;
class QStackedLayout;
class WhaleView;
class WhaleViewContextImpl;
class WhaleViewPlugin;

/**
 * @brief Shows the active view of one view area (DolphinViewContainer).
 *
 * The host takes the place of the DolphinView in the container's layout. It shows
 * either the DolphinView itself (through a DolphinViewAdapter) or the view of a
 * plugin. The DolphinView is never destroyed: while a plugin view is shown it stays
 * hidden, so the shell keeps working with it unchanged.
 *
 * While a plugin view is shown the host is the focus boundary of the view area:
 * - The interactive child widgets of the DolphinView are disabled, so neither
 *   internal setFocus() calls nor an inline editor can move the focus into the
 *   hidden view.
 * - The focus proxy of the DolphinView points to the plugin widget, so the shell's
 *   view()->setFocus() calls reach the visible view.
 * - Focus entering the plugin widget is reported by activationRequested(), which
 *   the container turns into the activation of its view area (split view).
 */
class WhaleViewHost : public QWidget
{
    Q_OBJECT

public:
    /**
     * @return The ID of the built-in file list view.
     */
    static QString dolphinViewId();

    WhaleViewHost(DolphinView *dolphinView, QWidget *parent);
    ~WhaleViewHost() override;

    /**
     * @return dolphinViewId() or the plugin ID of the shown view.
     */
    QString currentViewId() const;

    /**
     * @return The shown view: the DolphinViewAdapter or a plugin view.
     */
    WhaleView *currentView() const;

    bool isDolphinViewShown() const;

    /**
     * Shows the DolphinView and destroys the plugin view, if any.
     */
    void showDolphinView();

    /**
     * Shows a new view created by @p plugin instead of the current view.
     * @return False if the plugin does not support the location (WhaleViewPlugin::
     *         supportsUrl()) or did not provide a view with a widget; the current view
     *         stays in that case.
     */
    bool showPluginView(const QString &pluginId, WhaleViewPlugin *plugin);

    /**
     * History (called by DolphinViewContainer around its own location state):
     * @return Dolphin's @p dolphinState of the location being left, wrapped together
     *         with the shown view and the states of the views (WhalePersistence).
     */
    QByteArray wrapLocationState(const QByteArray &dolphinState);

    /**
     * History: the location has changed. Shows the view stored in @p locationState and
     * hands it its state; without Whale data (a new location, or an entry of an older
     * Whale/Dolphin) the shown view stays. A plugin view that does not support the new
     * location is replaced by the file list.
     * @return Dolphin's part of @p locationState for DolphinView::restoreState().
     */
    QByteArray unwrapLocationState(const QByteArray &locationState);

    /**
     * Session: @return the session data of the view areas @p hosts of one tab.
     */
    static QByteArray saveSessionState(const QList<WhaleViewHost *> &hosts);

    /**
     * Session: shows the views stored in @p data in the view areas @p hosts of one tab.
     * Missing, damaged or older data without Whale's part leaves the views as they are.
     */
    static void restoreSessionState(const QList<WhaleViewHost *> &hosts, const QByteArray &data);

    /**
     * @return The shown view and the states of the views at the current location.
     */
    WhaleViewsState viewsState() const;

    /**
     * How the last history or session restore went; for tests and development tools.
     */
    struct RestoreInfo {
        /// "history" or "session", or "location" when a view could not show a new location;
        /// empty if nothing was restored yet
        QString source;
        QString requestedViewId;
        QString shownViewId;
        bool fallback = false; ///< the requested view could not be shown
    };
    RestoreInfo lastRestore() const;

    /**
     * Tells the host whether its view area is the active one. On activation a shown
     * plugin view gets the focus, just as DolphinView::setActive() focuses itself.
     */
    void setActive(bool active);

    /**
     * @return The bridge to the DolphinView of this view area. For Whale's own
     *         development tools; views use their WhaleViewContext.
     */
    DolphinViewBridge *bridge() const;

    /**
     * @return The DolphinView of this view area (shown or hidden), e.g. to route Whale's
     *         actions (WhaleCommandRouter).
     */
    DolphinView *dolphinView() const;

    /**
     * @return The context of the shown plugin view, or null while the DolphinView is
     *         shown. For Whale's own development tools.
     */
    WhaleViewContextImpl *pluginContext() const;

Q_SIGNALS:
    void currentViewChanged(const QString &viewId);

    /**
     * Is emitted when the shown view reports changed capabilities.
     */
    void capabilitiesChanged();

    /**
     * Is emitted when the focus enters a shown plugin view.
     */
    void activationRequested();

    /**
     * Is emitted when the shown plugin view reports another selection of its own
     * (WhaleView::selectedItemsChanged()).
     */
    void viewSelectionChanged();

private:
    void applyViewsState(const WhaleViewsState &views, const QString &source);
    void keepPluginState();
    void notifyWindow();
    void destroyPluginView();
    void silenceDolphinView();
    void restoreDolphinView();
    void slotFocusChanged(QWidget *old, QWidget *now);
    void checkUrlSupported();

    DolphinView *const m_dolphinView;
    QStackedLayout *m_stack;
    bool m_active = false;

    DolphinViewBridge *m_bridge; // outlives the contexts, which use it
    WhaleViewContextImpl *m_dolphinViewContext;
    DolphinViewAdapter *m_dolphinViewAdapter;

    QString m_pluginId;
    WhaleViewPlugin *m_plugin = nullptr; // owned by the registry, never unloaded
    std::unique_ptr<WhaleViewContextImpl> m_pluginContext;
    std::unique_ptr<WhaleView> m_pluginView;

    // State of the DolphinView taken away while a plugin view is shown.
    QPointer<QWidget> m_dolphinViewFocusProxy;
    QList<QPointer<QWidget>> m_disabledDolphinViewChildren;

    // The views of the current location: which one is shown, and their states.
    WhaleViewsState m_views;
    // While a stored state is being applied, the outgoing view's state belongs to the
    // previous location and must not end up in m_views.
    bool m_applyingState = false;
    RestoreInfo m_lastRestore;
};

#endif
