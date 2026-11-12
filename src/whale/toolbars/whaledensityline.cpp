/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaledensityline.h"

#include "whaledensitystyle.h"

#include <QEvent>
#include <QPainter>

WhaleDensityLine *WhaleDensityLine::attach(QWidget *bar, Qt::Edge edge)
{
    Q_ASSERT(bar);
    Q_ASSERT(edge == Qt::TopEdge || edge == Qt::BottomEdge);
    return new WhaleDensityLine(bar, edge);
}

WhaleDensityLine::WhaleDensityLine(QWidget *bar, Qt::Edge edge)
    : QWidget(bar)
    , m_edge(edge)
{
    setObjectName(edge == Qt::TopEdge ? QStringLiteral("whaleDensityLineTop") : QStringLiteral("whaleDensityLineBottom"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);
    bar->installEventFilter(this);
    updateLine();
}

QColor WhaleDensityLine::color(const QPalette &palette)
{
    // As Breeze's separator color (e.g. of the status bar): 20 % of the text in the window color.
    const QColor window = palette.color(QPalette::Window);
    const QColor text = palette.color(QPalette::WindowText);
    const qreal bias = 0.2;
    return QColor::fromRgbF(window.redF() * (1 - bias) + text.redF() * bias,
                            window.greenF() * (1 - bias) + text.greenF() * bias,
                            window.blueF() * (1 - bias) + text.blueF() * bias);
}

bool WhaleDensityLine::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parentWidget()) {
        switch (event->type()) {
        case QEvent::Resize:
        case QEvent::StyleChange:
        case QEvent::PaletteChange:
        case QEvent::ChildAdded:
        case QEvent::Show:
            updateLine();
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void WhaleDensityLine::updateLine()
{
    QWidget *bar = parentWidget();
    const auto style = qobject_cast<const WhaleDensityStyle *>(bar->style());
    const bool shown = style && style->density() == WhaleToolbarSettings::Density::VeryCompact;
    if (!shown) {
        hide();
        return;
    }
    setGeometry(0, m_edge == Qt::TopEdge ? 0 : bar->height() - 1, bar->width(), 1);
    // Above the contents, also those added later.
    raise();
    show();
    update();
}

void WhaleDensityLine::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.fillRect(rect(), color(palette()));
}
