/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALESTYLEMANAGER_H
#define WHALESTYLEMANAGER_H

#include "whalestyle.h"

#include <QObject>
#include <QStringList>

class DolphinMainWindow;
class KActionCategory;
class KActionMenu;
class QAction;

/**
 * @brief Applies and captures the styles of one main window (ST3).
 *
 * Exactly one per main window, a direct child of it (ensure()). It offers:
 *
 * - "whale_styles": the menu View → Styles with one entry per style and "Save as Style…"
 *   ("whale_save_style"). Both are created before the window's setupGUI(), so the XML GUI file
 *   can place them (setupActions()).
 * - "whale_style_<id>": one action per style with the style's shortcut, in the category
 *   "Styles" of Configure Shortcuts. They are created after setupGUI() (plugStyles()):
 *   XML GUI would otherwise give them shortcuts it saved itself. whalestylesrc is the only place
 *   of these shortcuts; takeShortcuts() writes changes made in Configure Shortcuts back to it.
 *
 * Applying sets the enabled elements in the order of the registry (WhaleStyleElements); an
 * element not available in the window is skipped. Afterwards everything can be changed as usual;
 * no style is "active" (F3).
 */
class WhaleStyleManager : public QObject
{
    Q_OBJECT

public:
    /** @return The manager of @p window, created on the first call. */
    static WhaleStyleManager *ensure(DolphinMainWindow *window);

    /** @return The name of the action of the style with @p id. */
    static QString actionName(const QString &id);

    static QString menuActionName();
    static QString saveActionName();

    ~WhaleStyleManager() override;

    /** Creates the menu and "Save as Style…"; before setupGUI(). */
    void setupActions();

    /** Creates the actions of the styles and keeps them up to date; after setupGUI(). */
    void plugStyles();

    /**
     * Applies the style with @p id to the window.
     * @return Whether there is such a style.
     */
    bool applyStyle(const QString &id);

    /** @return The current state of the window: all elements available in it, enabled. */
    WhaleStyle captureStyle() const;

    /**
     * @return @p style with the current values of the window. Elements keep whether they are
     *         enabled; elements it did not have are added, enabled if available.
     */
    WhaleStyle recaptureStyle(const WhaleStyle &style) const;

    /**
     * Saves the current state as a style named @p name. A style of that name (ignoring case)
     * is captured again and keeps its ID and shortcut; else a new one is added.
     * @return The ID of the style, or an empty string for an empty name.
     */
    QString saveCurrentAsStyle(const QString &name);

    /**
     * Writes shortcuts changed in Configure Shortcuts back to the styles and removes the copies
     * XML GUI saved itself; after the dialog was closed (it holds the actions while open, and
     * a change of the styles recreates them). After Cancel nothing differs.
     */
    void takeShortcuts();

private:
    explicit WhaleStyleManager(DolphinMainWindow *window);

    void rebuildStyleActions();
    void askAndSaveCurrentAsStyle();
    void removeSavedActionProperties();

    DolphinMainWindow *m_window;
    KActionMenu *m_menu = nullptr;
    QAction *m_saveAction = nullptr;
    KActionCategory *m_category = nullptr;
    QStringList m_styleActionNames;
    bool m_plugged = false;
};

#endif
