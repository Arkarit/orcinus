/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEFILEVIEWTOOLBARCLIENT_H
#define WHALEFILEVIEWTOOLBARCLIENT_H

#include "whaletoolbarlayout.h"

#include <KXMLGUIClient>

/**
 * @brief The XML GUI client of the File View Toolbar: its configuration file.
 *
 * Reads "whalefileviewtoolbarui.rc" the XML GUI way: the installed file (from the
 * resources) or the user's copy in kxmlgui5/<component>/, whichever is newer, keeping the
 * user's toolbar on a newer installed version. One configuration for the toolbars of all
 * file views.
 *
 * Lifetime: owned by WhaleToolbarManager (never on the stack). It is not part of the
 * window's XML GUI factory; Configure Toolbars (TB4) will add it to the factory while the
 * dialog is open and must remove it again before the client or the window goes away.
 */
class WhaleFileViewToolbarClient : public KXMLGUIClient
{
public:
    static QString rcFile();
    static QString toolBarName();

    WhaleFileViewToolbarClient();
    ~WhaleFileViewToolbarClient() override;

    /**
     * @return The layout of the File View Toolbar in the client's current document.
     */
    WhaleToolbarLayout layout() const;

    /**
     * Reads the configuration file again (e.g. after it was changed).
     */
    void reload();
};

#endif
