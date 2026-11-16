/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaiqmlguard.h"

#include <QQmlEngine>

void WhaleAiQmlGuard::install(QQmlEngine *engine)
{
    static WhaleAiQmlGuard guard;
    engine->addUrlInterceptor(&guard);
}

bool WhaleAiQmlGuard::allows(const QString &path)
{
    static const QStringList denied{
        QStringLiteral("/Qt/labs/"),
        QStringLiteral("/QtQuick/LocalStorage/"),
        QStringLiteral("/QtCore/"),
        QStringLiteral("/QtNetwork/"),
        QStringLiteral("/QtWebEngine/"),
        QStringLiteral("/QtWebView/"),
        QStringLiteral("/QtWebChannel/"),
        QStringLiteral("/QtWebSockets/"),
        QStringLiteral("/QtMultimedia/"),
        QStringLiteral("/QtTest/"),
        QStringLiteral("/Qt5Compat/"),
        QStringLiteral("/QtQml/XmlListModel/"),
    };
    for (const QString &part : denied) {
        if (path.contains(part)) {
            return false;
        }
    }
    return true;
}

QUrl WhaleAiQmlGuard::intercept(const QUrl &url, DataType type)
{
    if (type != QmldirFile) {
        return url;
    }
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.path();
    // A qmldir that does not exist: the import fails with "module is not installed".
    return allows(path) ? url : QUrl(QStringLiteral("file:///nonexistent/orcinus-blocked/qmldir"));
}
