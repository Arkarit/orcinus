/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEVIEWSERVICES_H
#define WHALEVIEWSERVICES_H

#include <QObject>

class QWidget;
class WhaleTreeCache;
class WhaleViewRegistry;

/**
 * @brief The view services of one main window, shared by everything that shows or acts on
 *        its views.
 *
 * Exactly one per main window, a direct child of the window (no singleton: each window has
 * its own plugin registry and tree cache). It owns
 * - the view plugin registry (plugins are found and loaded once per window), and
 * - the tree cache (the tree scans of the window's views, WhaleTreeScanner).
 *
 * The view action sets (WhaleViewActions: the window's one for the active view, later
 * also one per view) and the view areas (WhaleViewHost) use these services; none of them
 * owns them.
 */
class WhaleViewServices : public QObject
{
    Q_OBJECT

public:
    /**
     * @return The services of the main window @p window, created as its child if it has
     *         none yet.
     */
    static WhaleViewServices *ensure(QWidget *window);

    /**
     * @return The services of the window of @p widget (QWidget::window()), or nullptr if
     *         it has none (e.g. a view area outside of a main window).
     */
    static WhaleViewServices *of(const QWidget *widget);

    ~WhaleViewServices() override;

    WhaleViewRegistry *registry() const;

    /**
     * @return The tree scans shared by the views of the window.
     */
    WhaleTreeCache *treeCache() const;

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
    void viewChanged();

private:
    explicit WhaleViewServices(QWidget *window);

    WhaleViewRegistry *const m_registry;
    WhaleTreeCache *const m_treeCache;
};

#endif
