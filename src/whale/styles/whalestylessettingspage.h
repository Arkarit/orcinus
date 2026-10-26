/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALESTYLESSETTINGSPAGE_H
#define WHALESTYLESSETTINGSPAGE_H

#include "settings/settingspagebase.h"
#include "whalestyle.h"

#include <QPointer>

class DolphinMainWindow;
class KKeySequenceWidget;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

/**
 * @brief The page "Styles" of the settings dialog (ST4).
 *
 * Lists the styles in their order and edits a copy of them: add one from the window, rename,
 * delete, reorder (buttons or dragging), the shortcut, and which elements a style sets (E2: on or
 * off; values only by capturing again). Apply or OK of the dialog saves the copy; "Apply to
 * Window" works at once, also with changes not saved yet.
 *
 * It captures from and applies to the window the dialog was opened from, as
 * FoldersTabsSettingsPage reads the location of the active window. Without a window (it was
 * closed meanwhile), capturing and applying are disabled.
 */
class WhaleStylesSettingsPage : public SettingsPageBase
{
    Q_OBJECT

public:
    WhaleStylesSettingsPage(DolphinMainWindow *window, QWidget *parent);
    ~WhaleStylesSettingsPage() override;

    /** Saves the edited styles. */
    void applySettings() override;

    /** Styles have no defaults; the user's styles stay. */
    void restoreDefaults() override;

private:
    void loadStyles();
    void fillList(const QString &currentId);
    void showCurrentStyle();
    void updateButtons();
    int currentIndex() const;
    WhaleStyle *currentStyle();
    void markChanged();

    void addFromWindow();
    void removeCurrent();
    void moveCurrent(int step);
    void takeListOrder();
    void renameCurrent();
    void changeShortcut(const QKeySequence &shortcut);
    void elementToggled(QTreeWidgetItem *item, int column);
    void captureAgain();
    void applyToWindow();

    /** @return Who else uses @p shortcut (another style or an action of the window), or empty. */
    QString shortcutUser(const QKeySequence &shortcut) const;

    QPointer<DolphinMainWindow> m_window;
    QList<WhaleStyle> m_styles;
    bool m_filling = false;

    QListWidget *m_list = nullptr;
    QPushButton *m_addButton = nullptr;
    QPushButton *m_removeButton = nullptr;
    QPushButton *m_upButton = nullptr;
    QPushButton *m_downButton = nullptr;
    QWidget *m_details = nullptr;
    QLineEdit *m_name = nullptr;
    KKeySequenceWidget *m_shortcut = nullptr;
    QTreeWidget *m_elements = nullptr;
    QPushButton *m_captureButton = nullptr;
    QPushButton *m_applyButton = nullptr;
    QLabel *m_hint = nullptr;
};

#endif
