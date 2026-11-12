/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEDENSITYSTYLE_H
#define WHALEDENSITYSTYLE_H

#include "whaletoolbarsettings.h"

#include <QProxyStyle>

/**
 * @brief Makes the toolbars lower (toolbar density, TD1).
 *
 * Wraps the application style. Only widgets inside a QToolBar (window toolbars, the File View
 * Toolbars with their location bars), the filter bar and the status bar are affected (TD3);
 * dialogs, menus and the views keep the
 * metrics of the application style. QWidget::setStyle() does not reach child widgets, hence
 * one proxy for the application rather than one per toolbar.
 *
 * Compact halves the margin of the toolbars and lowers the tool buttons; text fields and
 * combo boxes keep their height, so that the style still draws their frames. Very compact
 * also lowers those (Breeze then draws them flat, which is intended).
 *
 * With Normal no proxy is installed: the application style stays exactly Dolphin's.
 */
class WhaleDensityStyle : public QProxyStyle
{
    Q_OBJECT
    // KStyleExtensions asks only styles with this class info for custom elements (e.g. the
    // CE_CapacityBar of KCapacityBar in the status bar); QProxyStyle passes the requests on.
    Q_CLASSINFO("X-KDE-CustomElements", "true")

public:
    WhaleDensityStyle(QStyle *baseStyle, WhaleToolbarSettings::Density density);

    WhaleToolbarSettings::Density density() const;

    int pixelMetric(PixelMetric metric, const QStyleOption *option, const QWidget *widget) const override;
    QSize sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &size, const QWidget *widget) const override;

    /**
     * Installs the proxy for @p density around the application style, or removes it for
     * Normal. Recreating the application style lets Qt re-polish and re-layout all widgets.
     */
    static void apply(WhaleToolbarSettings::Density density);

    /**
     * Applies the density of WhaleToolbarSettings::self() now and whenever it changes, and
     * wraps a new application style again (e.g. after a change of the Plasma application style).
     */
    static void follow();

    /**
     * @return Whether @p widget is inside a bar whose metrics are changed: a toolbar, the
     *         filter bar or the status bar (TD3).
     */
    static bool isInBar(const QWidget *widget);

    /**
     * @return Whether @p widget is drawn with a lowered density (Compact or Very compact).
     */
    static bool isDense(const QWidget *widget);

private:
    WhaleToolbarSettings::Density m_density;
};

#endif
