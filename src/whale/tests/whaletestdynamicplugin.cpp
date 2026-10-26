/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleview.h"
#include "whaleviewcontext.h"
#include "whaleviewplugin.h"

#include <KPluginFactory>

#include <QLabel>
#include <QPointer>

namespace
{
/**
 * A test view that offers Zoom in folders whose name ends with "-zoom", or when its
 * state says so (state "zoom"). It reports every change with capabilitiesChanged().
 */
class DynamicView : public WhaleView
{
public:
    DynamicView(WhaleViewContext *context, QObject *parent)
        : WhaleView(context, parent)
        , m_label(new QLabel)
    {
        m_label->setObjectName(QStringLiteral("whaleTestDynamicView"));
        m_label->setFocusPolicy(Qt::StrongFocus);
        m_label->setText(QStringLiteral("Dynamic test view"));
        connect(context, &WhaleViewContext::urlChanged, this, &DynamicView::update);
        m_zoomable = isZoomable();
    }

    ~DynamicView() override
    {
        delete m_label;
    }

    QWidget *widget() override
    {
        return m_label;
    }

    WhaleViewCapabilities capabilities() const override
    {
        using C = WhaleViewCapability;
        return C::Selection | C::MultiSelection | C::HostOrder | (m_zoomable ? WhaleViewCapabilities(C::Zoom) : WhaleViewCapabilities());
    }

    QByteArray saveState() const override
    {
        return m_zoomState ? QByteArrayLiteral("zoom") : QByteArray();
    }

    void restoreState(const QByteArray &state) override
    {
        m_zoomState = state == "zoom";
        update();
    }

private:
    bool isZoomable() const
    {
        return m_zoomState || context()->url().path().endsWith(QLatin1String("-zoom"));
    }

    void update()
    {
        if (isZoomable() != m_zoomable) {
            m_zoomable = !m_zoomable;
            Q_EMIT capabilitiesChanged();
        }
    }

    QPointer<QLabel> m_label;
    bool m_zoomState = false;
    bool m_zoomable = false;
};
}

class WhaleTestDynamicPlugin : public WhaleViewPlugin
{
    Q_OBJECT

public:
    explicit WhaleTestDynamicPlugin(QObject *parent)
        : WhaleViewPlugin(parent)
    {
    }

    WhaleView *createView(WhaleViewContext *context, QObject *parent) override
    {
        return new DynamicView(context, parent);
    }
};

K_PLUGIN_CLASS_WITH_JSON(WhaleTestDynamicPlugin, "whale_test_dynamic.json")

#include "whaletestdynamicplugin.moc"
