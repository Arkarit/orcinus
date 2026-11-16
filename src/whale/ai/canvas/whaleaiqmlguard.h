/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAIQMLGUARD_H
#define WHALEAIQMLGUARD_H

#include <QQmlAbstractUrlInterceptor>

class QQmlEngine;

/**
 * @brief Keeps QML tools to the modules meant for them (CV0, CV2).
 *
 * Imports of modules that read or write files, the network or a database on their own (Qt.labs,
 * QtQuick.LocalStorage, QtCore, QtNetwork, QtWebEngine, ...) fail, so tools use the "orcinus"
 * object for files. A guard rail, not a sandbox: plain QtQuick can still write an image file
 * (grabToImage). See .privatedoc/ai-canvas-plan.md.
 */
class WhaleAiQmlGuard : public QQmlAbstractUrlInterceptor
{
public:
    /** Guards @p engine (one guard for all engines; it keeps no state). */
    static void install(QQmlEngine *engine);
    /** @return Whether the module of the qmldir at @p path may be imported. */
    static bool allows(const QString &path);

    QUrl intercept(const QUrl &url, DataType type) override;
};

#endif
