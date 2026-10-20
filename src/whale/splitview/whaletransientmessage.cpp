/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaletransientmessage.h"

#include "dolphinviewcontainer.h"
#include "whaleviewhost.h"

#include <QEvent>
#include <QPainter>
#include <QStyle>
#include <QTimer>

namespace
{
const QString messageName = QStringLiteral("whale_transient_message");
constexpr int shownMs = 4000;

QWidget *areaOf(DolphinViewContainer *container)
{
    // Above the view, whichever is shown (file list or plugin view), not the bars around it.
    return container->viewHost() ? static_cast<QWidget *>(container->viewHost()) : container;
}
}

WhaleTransientMessage::WhaleTransientMessage(DolphinViewContainer *container)
    // A child of the view area, not of the view host: switching the view raises the shown
    // view among the children of the host.
    : KMessageWidget(container)
    , m_area(areaOf(container))
    , m_timer(new QTimer(this))
{
    setObjectName(messageName);
    setMessageType(KMessageWidget::Information);
    setCloseButtonVisible(false); // it goes by itself, and lets the mouse through
    setWordWrap(true);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);
    hide();
    m_timer->setSingleShot(true);
    m_timer->setInterval(shownMs);
    connect(m_timer, &QTimer::timeout, this, &QWidget::hide);
    m_area->installEventFilter(this);
}

void WhaleTransientMessage::showIn(DolphinViewContainer *container, const QString &text)
{
    if (!container || text.isEmpty()) {
        return;
    }
    WhaleTransientMessage *message = of(container);
    if (!message) {
        message = new WhaleTransientMessage(container);
    }
    message->showText(text);
}

WhaleTransientMessage *WhaleTransientMessage::of(const DolphinViewContainer *container)
{
    return container ? container->findChild<WhaleTransientMessage *>(messageName, Qt::FindDirectChildrenOnly) : nullptr;
}

void WhaleTransientMessage::showText(const QString &text)
{
    if (!isVisible() || this->text() != text) {
        setText(text);
        place();
        QWidget::show();
    }
    raise();
    m_timer->start();
}

bool WhaleTransientMessage::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_area && (event->type() == QEvent::Resize || event->type() == QEvent::Move) && isVisible()) {
        place();
    }
    return KMessageWidget::eventFilter(watched, event);
}

void WhaleTransientMessage::paintEvent(QPaintEvent *event)
{
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(palette().window());
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
    }
    KMessageWidget::paintEvent(event);
}

void WhaleTransientMessage::place()
{
    const int margin = style()->pixelMetric(QStyle::PM_LayoutBottomMargin, nullptr, this);
    const int available = qMax(0, m_area->width() - 2 * margin);
    // As wide as the text in one line, at most the width of the area; then wrapped.
    setWordWrap(false);
    const int width = qMin(sizeHint().width(), available);
    setWordWrap(true);
    const int height = hasHeightForWidth() ? heightForWidth(width) : sizeHint().height();
    const QPoint origin = m_area == parentWidget() ? QPoint() : m_area->mapTo(parentWidget(), QPoint());
    setGeometry(origin.x() + (m_area->width() - width) / 2, origin.y() + m_area->height() - margin - height, width, height);
}

#include "moc_whaletransientmessage.cpp"
