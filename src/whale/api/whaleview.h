/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef WHALEVIEW_H
#define WHALEVIEW_H

#include "whaleviewapi_export.h"
#include "whaleviewcapabilities.h"

#include <QByteArray>
#include <QObject>

#include <memory>

class QWidget;
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
     */
    virtual QWidget *widget() = 0;

    /**
     * @return The abilities of the view. They may depend on context()->url().
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
     * @return An opaque state that is handed back to restoreState() when the
     *         user returns to the same location. The default implementation
     *         returns an empty state.
     */
    virtual QByteArray saveState() const;

    /**
     * Restores a state returned by saveState(). The default implementation does nothing.
     */
    virtual void restoreState(const QByteArray &state);

protected:
    WhaleViewContext *context() const;

private:
    const std::unique_ptr<WhaleViewPrivate> d;
};

#endif
