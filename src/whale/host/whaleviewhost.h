/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEVIEWHOST_H
#define WHALEVIEWHOST_H

#include <QList>
#include <QPointer>
#include <QWidget>

#include <memory>

class DolphinView;
class DolphinViewAdapter;
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
     * @return False if the plugin did not provide a view with a widget; the
     *         current view stays in that case.
     */
    bool showPluginView(const QString &pluginId, WhaleViewPlugin *plugin);

    /**
     * Tells the host whether its view area is the active one. On activation a shown
     * plugin view gets the focus, just as DolphinView::setActive() focuses itself.
     */
    void setActive(bool active);

Q_SIGNALS:
    void currentViewChanged(const QString &viewId);

    /**
     * Is emitted when the focus enters a shown plugin view.
     */
    void activationRequested();

private:
    void destroyPluginView();
    void silenceDolphinView();
    void restoreDolphinView();
    void slotFocusChanged(QWidget *old, QWidget *now);

    DolphinView *const m_dolphinView;
    QStackedLayout *m_stack;
    bool m_active = false;

    WhaleViewContextImpl *m_dolphinViewContext;
    DolphinViewAdapter *m_dolphinViewAdapter;

    QString m_pluginId;
    std::unique_ptr<WhaleViewContextImpl> m_pluginContext;
    std::unique_ptr<WhaleView> m_pluginView;

    // State of the DolphinView taken away while a plugin view is shown.
    QPointer<QWidget> m_dolphinViewFocusProxy;
    QList<QPointer<QWidget>> m_disabledDolphinViewChildren;
};

#endif
