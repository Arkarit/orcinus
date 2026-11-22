/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaiquickwidget.h"

#include <QApplication>
#include <QFileInfo>
#include <QKeyEvent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>

namespace
{
/**
 * Without QT_QUICK_CONTROLS_STYLE, Qt Quick Controls takes the widget style of QT_STYLE_OVERRIDE
 * (e.g. "breeze", common outside Plasma) as the name of its own style, finds no such QML module
 * and loads nothing. Then the canvas uses KDE's desktop style, which draws with the widget style
 * anyway, or Fusion. Once, before the first QML is loaded; a style the user set stays.
 */
void chooseControlsStyle(const QQmlEngine *engine)
{
    static bool chosen = false;
    if (chosen) {
        return;
    }
    chosen = true;
    if (!qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE") || qEnvironmentVariableIsEmpty("QT_STYLE_OVERRIDE")) {
        return;
    }
    const QStringList importPaths = engine->importPathList();
    for (const QString &path : importPaths) {
        if (QFileInfo::exists(path + QStringLiteral("/org/kde/desktop/qmldir"))) {
            QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
            return;
        }
    }
    QQuickStyle::setStyle(QStringLiteral("Fusion"));
}
}

WhaleAiQuickWidget::WhaleAiQuickWidget(QWidget *parent)
    : QQuickWidget(parent)
{
    chooseControlsStyle(engine());
    setResizeMode(QQuickWidget::SizeRootObjectToView);
    setFocusPolicy(Qt::StrongFocus);
    qApp->installEventFilter(this);
    updateClearColor();
}

void WhaleAiQuickWidget::updateClearColor()
{
    // The panel's colour where the QML leaves the background open (a tool's root is an Item).
    setClearColor(palette().color(QPalette::Window));
}

void WhaleAiQuickWidget::changeEvent(QEvent *event)
{
    QQuickWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange) {
        updateClearColor();
    }
}

bool WhaleAiQuickWidget::keepsKey(int key, Qt::KeyboardModifiers modifiers)
{
    if (modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
        return false;
    }
    // F-keys toggle panels, rename, reload, ...: they stay the window's.
    return !(key >= Qt::Key_F1 && key <= Qt::Key_F35);
}

bool WhaleAiQuickWidget::isOwn(QObject *object) const
{
    if (object == this || object == quickWindow()) {
        return true;
    }
    auto item = qobject_cast<QQuickItem *>(object);
    return item && item->window() == quickWindow();
}

bool WhaleAiQuickWidget::eventFilter(QObject *watched, QEvent *event)
{
    // Qt sends the ShortcutOverride of a QQuickWidget to the focused item of its Qt Quick window,
    // not to the widget, so it is caught on the way: delivered first (the QML may claim the key),
    // then the plain keys are kept.
    if (event->type() != QEvent::ShortcutOverride || m_delivering || !hasFocus() || !isOwn(watched)) {
        return false;
    }
    auto keyEvent = static_cast<QKeyEvent *>(event);
    m_delivering = true;
    QCoreApplication::sendEvent(watched, keyEvent);
    m_delivering = false;
    if (!keyEvent->isAccepted() && keepsKey(keyEvent->key(), keyEvent->modifiers())) {
        keyEvent->accept();
    }
    return true;
}

#include "moc_whaleaiquickwidget.cpp"
