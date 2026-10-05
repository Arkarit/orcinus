/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whalefileoperationsimpl.h"

#include "dolphinviewbridge.h"

#include <KUrlMimeData>

#include <QApplication>
#include <QDrag>
#include <QIcon>
#include <QMimeData>
#include <QWidget>

namespace
{
WhaleFileOperationsImpl::DragRunner &dragRunner()
{
    static WhaleFileOperationsImpl::DragRunner runner;
    return runner;
}
}

WhaleFileOperationsImpl::WhaleFileOperationsImpl(DolphinViewBridge *bridge, QObject *parent)
    : WhaleFileOperations(parent)
    , m_bridge(bridge)
{
}

WhaleFileOperationsImpl::~WhaleFileOperationsImpl() = default;

void WhaleFileOperationsImpl::setDragRunner(const DragRunner &runner)
{
    dragRunner() = runner;
}

Qt::DropAction WhaleFileOperationsImpl::startDrag(const QList<QUrl> &urls, const QPixmap &pixmap)
{
    QMimeData *data = m_bridge->createDragMimeData(urls);
    if (!data) {
        return Qt::IgnoreAction;
    }

    // As KItemListController::startDragging(): the drag belongs to the active window.
    QObject *source = QApplication::activeWindow();
    auto drag = new QDrag(source ? source : this);
    drag->setMimeData(data);
    QPixmap image = pixmap;
    if (image.isNull()) {
        const QList<QUrl> dragged = KUrlMimeData::urlsFromMimeData(data, KUrlMimeData::PreferKdeUrls);
        const KFileItem first = dragged.isEmpty() ? KFileItem() : m_bridge->item(dragged.first());
        image = QIcon::fromTheme(first.isNull() ? QStringLiteral("unknown") : first.iconName()).pixmap(32);
    }
    drag->setPixmap(image);
    drag->setHotSpot(QPoint(image.width() / image.devicePixelRatio() / 2, 0));

    if (const DragRunner &runner = dragRunner()) {
        const Qt::DropAction action = runner(drag);
        drag->deleteLater();
        return action;
    }
    // QDrag deletes itself when the drag has ended.
    return drag->exec(Qt::MoveAction | Qt::CopyAction | Qt::LinkAction, Qt::CopyAction);
}

bool WhaleFileOperationsImpl::updateDropAction(QDragMoveEvent *event, const QUrl &url)
{
    return m_bridge->updateDropAction(event, url);
}

bool WhaleFileOperationsImpl::drop(QDropEvent *event, const QUrl &url, QWidget *widget)
{
    return m_bridge->drop(event, url, widget);
}

#include "moc_whalefileoperationsimpl.cpp"
