/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEDENSITYLINE_H
#define WHALEDENSITYLINE_H

#include <QWidget>

/**
 * @brief A separator line along one edge of a bar, shown with the toolbar density Very compact.
 *
 * Very compact draws the location bar and the filter field without frames; the lines close the
 * file view off against the file view toolbar above and the filter bar below. They are drawn in
 * the color Breeze uses for separators (window and text color mixed), so they follow the color
 * scheme. An overlay child of the bar, transparent for the mouse.
 */
class WhaleDensityLine : public QWidget
{
public:
    /**
     * Adds a line along @p edge (top or bottom) of @p bar.
     */
    static WhaleDensityLine *attach(QWidget *bar, Qt::Edge edge);

    /**
     * @return The color of the line for @p palette.
     */
    static QColor color(const QPalette &palette);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    WhaleDensityLine(QWidget *bar, Qt::Edge edge);
    void updateLine();

    Qt::Edge m_edge;
};

#endif
