/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALETRANSIENTMESSAGE_H
#define WHALETRANSIENTMESSAGE_H

#include <KMessageWidget>

class DolphinViewContainer;
class QTimer;

/**
 * @brief Information that floats above the bottom of a view area and hides itself after a
 *        few seconds (A8 of the split lock, .privatedoc/whale-split-lock-plan.md).
 *
 * Unlike the message bar of DolphinViewContainer it is not part of the layout, so the items
 * of the view do not move while the user clicks them, and it lets the mouse through to them.
 * One per view area, created on first use; the message bar of the view area stays as it is.
 */
class WhaleTransientMessage : public KMessageWidget
{
    Q_OBJECT

public:
    /**
     * Shows @p text in the view area @p container. The same text again only restarts the time.
     */
    static void showIn(DolphinViewContainer *container, const QString &text);

    /**
     * @return The message of @p container, or null if none has been shown there yet.
     */
    static WhaleTransientMessage *of(const DolphinViewContainer *container);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    /// Opaque below the tinted background of KMessageWidget, so the items do not show through.
    void paintEvent(QPaintEvent *event) override;

private:
    explicit WhaleTransientMessage(DolphinViewContainer *container);
    void showText(const QString &text);
    /// At the bottom of the area, centered, as wide as the text needs.
    void place();

    QWidget *const m_area; ///< the view host: where the view is
    QTimer *const m_timer;
};

#endif
