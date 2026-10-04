/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
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
 * Shows a greeting and the location of the view area.
 */
class WhaleHelloView : public WhaleView
{
public:
    WhaleHelloView(WhaleViewContext *context, QObject *parent)
        : WhaleView(context, parent)
        , m_label(new QLabel)
    {
        m_label->setAlignment(Qt::AlignCenter);
        updateText();
        connect(context, &WhaleViewContext::urlChanged, this, &WhaleHelloView::updateText);
    }

    ~WhaleHelloView() override
    {
        delete m_label;
    }

    QWidget *widget() override
    {
        return m_label;
    }

private:
    void updateText()
    {
        m_label->setText(QStringLiteral("Hello from a Whale view plugin.\n%1").arg(context()->url().toDisplayString(QUrl::PreferLocalFile)));
    }

    QPointer<QLabel> m_label;
};
}

class WhaleHelloPlugin : public WhaleViewPlugin
{
    Q_OBJECT

public:
    explicit WhaleHelloPlugin(QObject *parent)
        : WhaleViewPlugin(parent)
    {
    }

    WhaleView *createView(WhaleViewContext *context, QObject *parent) override
    {
        return new WhaleHelloView(context, parent);
    }
};

K_PLUGIN_CLASS_WITH_JSON(WhaleHelloPlugin, "whale_hello.json")

#include "whalehelloplugin.moc"
