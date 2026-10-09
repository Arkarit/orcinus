/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whalefileviewtoolbarclient.h"

#include <QDomDocument>

static void initResources()
{
    // The resources of a static library are only linked in when something refers to them.
    Q_INIT_RESOURCE(whaletoolbars);
}

QString WhaleFileViewToolbarClient::rcFile()
{
    return QStringLiteral("whalefileviewtoolbarui.rc");
}

QString WhaleFileViewToolbarClient::toolBarName()
{
    return QStringLiteral("whaleFileViewToolBar");
}

WhaleFileViewToolbarClient::WhaleFileViewToolbarClient()
{
    initResources();
    setXMLFile(rcFile());
}

WhaleFileViewToolbarClient::~WhaleFileViewToolbarClient() = default;

WhaleToolbarLayout WhaleFileViewToolbarClient::layout() const
{
    return WhaleToolbarLayout::fromDocument(domDocument(), toolBarName());
}

void WhaleFileViewToolbarClient::reload()
{
    reloadXML();
}
