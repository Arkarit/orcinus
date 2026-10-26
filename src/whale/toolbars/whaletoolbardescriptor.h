/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALETOOLBARDESCRIPTOR_H
#define WHALETOOLBARDESCRIPTOR_H

#include <QString>
#include <QStringList>

/**
 * @brief A kind of toolbar of a main window and where its configuration lives.
 *
 * One configuration per kind, shared by all its instances: there is one main toolbar, and
 * one File View Toolbar per file view, all with the same layout.
 */
struct WhaleToolbarDescriptor {
    enum class Target {
        ActiveView, ///< acts on the active view (the window's actions, with shortcuts)
        OwnFileView, ///< acts on the file view it belongs to (actions of its own, no shortcuts)
    };

    QString id; ///< stable: "global", "file-view"
    QString displayName;
    QString xmlGuiToolBar; ///< the <ToolBar> element of the XML GUI file
    QString rcFile; ///< the XML GUI file
    Target target = Target::ActiveView;
    QStringList allowedActions; ///< OwnFileView only: the actions its toolbars can show

    static QString globalId()
    {
        return QStringLiteral("global");
    }
    static QString fileViewId()
    {
        return QStringLiteral("file-view");
    }
};

#endif
