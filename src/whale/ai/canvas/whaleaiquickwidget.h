/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAIQUICKWIDGET_H
#define WHALEAIQUICKWIDGET_H

#include <QQuickWidget>

/**
 * @brief A QQuickWidget for the canvas and its tools that keeps plain keys to itself (CV0, CV1).
 *
 * While it has the focus, keys without Ctrl, Alt or Meta (letters, Space, arrows, Delete, ...)
 * never trigger the window's actions: Delete in a tool must not delete the files selected in the
 * view. F-keys and combinations with Ctrl, Alt or Meta still reach the window's actions, unless
 * the QML claims them itself (Keys.onShortcutOverride).
 *
 * Where the QML draws no background, the panel's colour (QPalette::Window) shows.
 */
class WhaleAiQuickWidget : public QQuickWidget
{
    Q_OBJECT

public:
    explicit WhaleAiQuickWidget(QWidget *parent = nullptr);

    /** @return Whether the window's shortcut for @p key with @p modifiers stays with the widget. */
    static bool keepsKey(int key, Qt::KeyboardModifiers modifiers);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    /** @return Whether @p object is this widget, its Qt Quick window or an item in it. */
    bool isOwn(QObject *object) const;
    void updateClearColor();
    bool m_delivering = false;
};

#endif
