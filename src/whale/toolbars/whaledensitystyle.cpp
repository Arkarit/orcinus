/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaledensitystyle.h"

#include <QApplication>
#include <QEvent>
#include <QPointer>
#include <QStyleFactory>
#include <QTimer>
#include <QToolBar>

namespace
{
// The name of the wrapped application style: a proxy's own name is not the base style's.
QString &baseStyleName()
{
    static QString name;
    return name;
}

QStyle *createBaseStyle()
{
    QStyle *style = baseStyleName().isEmpty() ? nullptr : QStyleFactory::create(baseStyleName());
    return style ? style : QStyleFactory::create(QApplication::style()->name());
}

// Wraps a new application style again when something else replaced the proxy.
class StyleWatcher : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::StyleChange && !m_pending && watched->isWidgetType()) {
            const WhaleToolbarSettings::Density density = WhaleToolbarSettings::self()->toolbarDensity();
            if (density != WhaleToolbarSettings::Density::Normal && !dynamic_cast<WhaleDensityStyle *>(QApplication::style())) {
                m_pending = true;
                QTimer::singleShot(0, this, [this]() {
                    m_pending = false;
                    if (!dynamic_cast<WhaleDensityStyle *>(QApplication::style())) {
                        baseStyleName() = QApplication::style()->name();
                        WhaleDensityStyle::apply(WhaleToolbarSettings::self()->toolbarDensity());
                    }
                });
            }
        }
        return false;
    }

private:
    bool m_pending = false;
};
}

WhaleDensityStyle::WhaleDensityStyle(QStyle *baseStyle, WhaleToolbarSettings::Density density)
    : QProxyStyle(baseStyle)
    , m_density(density)
{
    // Code that tells styles apart by their object name (e.g. KCapacityBar, which draws its own
    // gradient for styles other than Breeze) must see the wrapped style.
    setObjectName(this->baseStyle()->objectName());
}

WhaleToolbarSettings::Density WhaleDensityStyle::density() const
{
    return m_density;
}

bool WhaleDensityStyle::isInToolBar(const QWidget *widget)
{
    for (const QWidget *w = widget; w; w = w->parentWidget()) {
        if (qobject_cast<const QToolBar *>(w)) {
            return true;
        }
        if (w->isWindow()) {
            break;
        }
    }
    return false;
}

int WhaleDensityStyle::pixelMetric(PixelMetric metric, const QStyleOption *option, const QWidget *widget) const
{
    const int value = QProxyStyle::pixelMetric(metric, option, widget);
    if (metric == PM_ToolBarItemMargin && isInToolBar(widget)) {
        return m_density == WhaleToolbarSettings::Density::Compact ? value / 2 : qMin(value, 1);
    }
    return value;
}

QSize WhaleDensityStyle::sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &size, const QWidget *widget) const
{
    QSize result = QProxyStyle::sizeFromContents(type, option, size, widget);
    if (!isInToolBar(widget)) {
        return result;
    }
    const bool veryCompact = m_density == WhaleToolbarSettings::Density::VeryCompact;
    int shrink = 0;
    switch (type) {
    case CT_ToolButton:
        shrink = veryCompact ? 8 : 4;
        break;
    case CT_LineEdit:
    case CT_ComboBox:
    case CT_PushButton:
        // Compact keeps the height of the fields: the style draws their frames only from it on.
        shrink = veryCompact ? 8 : 0;
        break;
    default:
        break;
    }
    if (shrink > 0) {
        // Never below the contents (icon, text).
        result.setHeight(qMax(size.height(), result.height() - shrink));
    }
    return result;
}

void WhaleDensityStyle::apply(WhaleToolbarSettings::Density density)
{
    auto current = dynamic_cast<WhaleDensityStyle *>(QApplication::style());
    if (!current) {
        baseStyleName() = QApplication::style()->name();
    } else if (current->density() == density) {
        return;
    }
    if (density == WhaleToolbarSettings::Density::Normal) {
        if (current) {
            QApplication::setStyle(createBaseStyle());
        }
        return;
    }
    QApplication::setStyle(new WhaleDensityStyle(createBaseStyle(), density));
}

void WhaleDensityStyle::follow()
{
    static QPointer<StyleWatcher> watcher;
    if (watcher) {
        return;
    }
    watcher = new StyleWatcher(qApp);
    qApp->installEventFilter(watcher);
    QObject::connect(WhaleToolbarSettings::self(), &WhaleToolbarSettings::toolbarDensityChanged, watcher, [](WhaleToolbarSettings::Density density) {
        apply(density);
    });
    apply(WhaleToolbarSettings::self()->toolbarDensity());
}

#include "moc_whaledensitystyle.cpp"
