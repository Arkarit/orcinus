/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// A view plugin whose metadata claims a view API version this Whale does not
// provide. The registry must reject it from its metadata alone. If the library
// gets loaded anyway, its static initializer marks that in the environment.

#include "whaleviewplugin.h"

#include <KPluginFactory>

namespace
{
[[maybe_unused]] const bool s_markLoaded = qputenv("WHALE_TEST_WRONGAPI_LOADED", "1");
}

class WhaleTestWrongApiPlugin : public WhaleViewPlugin
{
    Q_OBJECT

public:
    explicit WhaleTestWrongApiPlugin(QObject *parent)
        : WhaleViewPlugin(parent)
    {
    }

    WhaleView *createView(WhaleViewContext *, QObject *) override
    {
        return nullptr;
    }
};

K_PLUGIN_CLASS_WITH_JSON(WhaleTestWrongApiPlugin, "whale_test_wrongapi.json")

#include "whaletestwrongapiplugin.moc"
