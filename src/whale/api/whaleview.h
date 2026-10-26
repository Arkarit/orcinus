/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef WHALEVIEW_H
#define WHALEVIEW_H

#include "whaleviewapi_export.h"
#include "whaleviewcapabilities.h"

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QUrl>

#include <memory>

class QWidget;
class WhaleSelection;
class WhaleViewContext;
class WhaleViewPrivate;

/**
 * @brief Base class of a view shown in a Whale view area.
 *
 * A view is created by a WhaleViewPlugin for one view area. It presents the
 * content of that area in a widget and talks to Whale only through its
 * WhaleViewContext.
 *
 * All methods are called in the GUI thread.
 */
class WHALEVIEWAPI_EXPORT WhaleView : public QObject
{
    Q_OBJECT

public:
    /**
     * @param context The host interface of this view. It outlives the view.
     */
    explicit WhaleView(WhaleViewContext *context, QObject *parent = nullptr);
    ~WhaleView() override;

    /**
     * @return The widget that shows the view. The same widget must be returned
     *         for the lifetime of the view. The view owns it: Whale embeds it
     *         into its layout, takes it out again before destroying the view,
     *         and expects the view to delete the widget in its destructor.
     *
     *         The widget should accept focus (e.g. Qt::StrongFocus): Whale activates
     *         the view area, for example the half of a split view, when the focus
     *         enters the widget, and gives it the focus when the view area is activated.
     */
    virtual QWidget *widget() = 0;

    /**
     * @return The abilities of the view. They may depend on context()->url() or on the
     *         view's state; emit capabilitiesChanged() when they change.
     *         The default implementation returns no capabilities.
     */
    virtual WhaleViewCapabilities capabilities() const;

    /**
     * Is called after the view has been shown. The default implementation does nothing.
     */
    virtual void activate();

    /**
     * Is called before the view is hidden. Expensive work should be paused.
     * The default implementation does nothing.
     */
    virtual void deactivate();

    /**
     * @return An opaque state of the view at the current location (e.g. what is scrolled
     *         into view, a display option). Whale stores it per location in the history
     *         and per view area in the session, separately for each view ID, and hands it
     *         back to restoreState() of a view with the same ID. An empty state means none.
     *         The default implementation returns an empty state.
     */
    virtual QByteArray saveState() const;

    /**
     * Restores a state returned by saveState(), possibly by an older version of the
     * plugin: unknown or damaged data must be ignored. Called after the view has been
     * created with its context and before activate(), and again on the shown view when
     * the user returns to a location in the history (after the location has changed;
     * its items may still be loading). The default implementation does nothing.
     */
    virtual void restoreState(const QByteArray &state);

    /**
     * Starts renaming @p url inside the view. Only called if capabilities() contain
     * WhaleViewCapability::InlineRename, when the user renames exactly one selected
     * item (F2). Without that capability Whale renames with a dialog instead.
     * The default implementation does nothing.
     */
    virtual void startRename(const QUrl &url);

    /**
     * @return The items selected in the view, on which Whale's actions on selected items
     *         (Delete, F2, Copy, Properties, ...) act while the view is shown. Only asked of
     *         views with WhaleViewCapability::RecursiveContent: their selection may lie
     *         outside the listing and therefore cannot be Whale's selection; it stays the
     *         view's own and Whale routes the actions to it (WhaleFileOperations). Other
     *         views show Whale's selection, which the actions use directly. Emit
     *         selectedItemsChanged() when the result changes. The default implementation
     *         returns context()->selection().selected().
     */
    virtual QList<QUrl> selectedItems() const;

    /**
     * Shows the selection of the other view of a split view, while both are linked. Only
     * called if capabilities() contain WhaleViewCapability::LinkedSelection, while the
     * view is not the active one. The items are below the location: in the listing, or
     * deeper (from a view that shows more than the listing). Missing items (filtered,
     * hidden, not shown by the view) are left out. The selection is in one folder; the
     * current item is one of the selected items if anything is selected.
     *
     * The default implementation makes the items of the listing Whale's selection
     * (context()->setSelection()), which a view that shows Whale's selection follows.
     */
    virtual void showLinkedSelection(const WhaleSelection &selection);

Q_SIGNALS:
    /**
     * Is emitted when capabilities() changed, e.g. after the location or the state
     * changed; Whale then updates which actions apply.
     */
    void capabilitiesChanged();

    /**
     * Is emitted when selectedItems() changed, so Whale updates its actions at once.
     */
    void selectedItemsChanged();

protected:
    WhaleViewContext *context() const;

private:
    const std::unique_ptr<WhaleViewPrivate> d;
};

#endif
