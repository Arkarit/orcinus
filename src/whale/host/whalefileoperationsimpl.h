/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEFILEOPERATIONSIMPL_H
#define WHALEFILEOPERATIONSIMPL_H

#include "whalefileoperations.h"

#include <functional>

class DolphinViewBridge;
class QDrag;

/**
 * @brief The WhaleFileOperations of one view, carried out by the DolphinView of its
 * view area through the DolphinViewBridge.
 *
 * Drag data and drop handling are the DolphinView's own (see DolphinViewBridge), so a
 * plugin view drags and drops exactly like the file list.
 */
class WhaleFileOperationsImpl : public WhaleFileOperations
{
    Q_OBJECT

public:
    explicit WhaleFileOperationsImpl(DolphinViewBridge *bridge, QObject *parent = nullptr);
    ~WhaleFileOperationsImpl() override;

    Qt::DropAction startDrag(const QList<QUrl> &urls, const QPixmap &pixmap = QPixmap()) override;
    bool updateDropAction(QDragMoveEvent *event, const QUrl &url) override;
    bool drop(QDropEvent *event, const QUrl &url, QWidget *widget) override;

    using DragRunner = std::function<Qt::DropAction(QDrag *drag)>;

    /**
     * Development tools only (the dev MCP automation): runs the drags of all views with
     * @p runner instead of QDrag::exec(), which needs a real pointer. The runner gets
     * the drag as Whale built it and returns the drop action. An empty runner restores
     * QDrag::exec().
     */
    static void setDragRunner(const DragRunner &runner);

private:
    DolphinViewBridge *const m_bridge;
};

#endif
