/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaiuiactions.h"

#include <QHash>
#include <QStringList>

namespace
{
const QHash<QString, WhaleAiUiActions::Access> &blocked()
{
    using A = WhaleAiUiActions::Access;
    static const QHash<QString, A> names = [] {
        QHash<QString, A> result;
        auto add = [&result](A access, const QStringList &list) {
            for (const QString &name : list) {
                result.insert(name, access);
            }
        };
        add(A::FileOperation,
            {QStringLiteral("edit_cut"),
             QStringLiteral("edit_copy"),
             QStringLiteral("edit_paste"),
             // Undo and redo take back file operations, the user's ones too.
             QStringLiteral("edit_undo"),
             QStringLiteral("edit_redo"),
             QStringLiteral("copy_to_inactive_split_view"),
             QStringLiteral("move_to_inactive_split_view"),
             QStringLiteral("create_dir"),
             QStringLiteral("create_subdir"),
             QStringLiteral("create_file"),
             QStringLiteral("new_menu"),
             QStringLiteral("renamefile"),
             QStringLiteral("movetotrash"),
             QStringLiteral("deletefile"),
             QStringLiteral("delete_shortcut"),
             QStringLiteral("duplicate"),
             QStringLiteral("add_to_places")});
        add(A::WayOut,
            {QStringLiteral("act_as_admin"),
             QStringLiteral("open_terminal"),
             QStringLiteral("open_terminal_here"),
             QStringLiteral("show_terminal_panel"),
             QStringLiteral("focus_terminal_panel"),
             QStringLiteral("compare_files"),
             QStringLiteral("manage_disk_space"),
             QStringLiteral("open_preferred_search_tool"),
             // Opens the bookmark editor, another program.
             QStringLiteral("edit_bookmarks")});
        add(A::Dialog,
            {QStringLiteral("options_configure"),
             QStringLiteral("options_configure_toolbars"),
             QStringLiteral("options_configure_keybinding"),
             QStringLiteral("whale_configure_file_view_toolbar"),
             QStringLiteral("view_properties"),
             QStringLiteral("properties"),
             QStringLiteral("whale_save_style"),
             QStringLiteral("open_kcommand_bar"),
             QStringLiteral("hamburger_menu"),
             QStringLiteral("add_bookmarks_list"),
             QStringLiteral("switch_application_language")});
        add(A::Window, {QStringLiteral("file_quit"), QStringLiteral("file_close"), QStringLiteral("show_ai_panel")});
        // Overwrites a profile; allowed once the agent's changes can be undone (AI9e).
        add(A::SavedData, {QStringLiteral("whale_save_last_style"), QStringLiteral("add_bookmark")});
        return result;
    }();
    return names;
}
}

WhaleAiUiActions::Access WhaleAiUiActions::access(const QString &name)
{
    // Test fixtures of the build tree, never part of the product.
    if (name.startsWith(QLatin1String("whale_test"))) {
        return Access::Window;
    }
    // Service menus and other plugins of the file item actions ("servicemenu_x.desktop::run",
    // "plugin::action") run programs on files: "Run In Konsole", "Install Font", ...
    if (name.startsWith(QLatin1String("servicemenu_")) || name.contains(QLatin1String("::"))) {
        return Access::WayOut;
    }
    // Help: dialogs, the handbook and the web browser, the "What's This?" mode.
    if (name.startsWith(QLatin1String("help_"))) {
        return Access::Dialog;
    }
    return blocked().value(name, Access::Allowed);
}

QString WhaleAiUiActions::reason(Access access)
{
    switch (access) {
    case Access::Allowed:
        return QString();
    case Access::FileOperation:
        return QStringLiteral("It changes files; use your own tools for files.");
    case Access::WayOut:
        return QStringLiteral("It starts another program or changes rights; it is not available to agents.");
    case Access::Dialog:
        return QStringLiteral("It opens a dialog, which agents cannot answer; ask the user to do it.");
    case Access::Window:
        return QStringLiteral("It would close or hide this conversation.");
    case Access::SavedData:
        return QStringLiteral("It overwrites saved data that cannot be restored yet; ask the user to do it.");
    }
    return QString();
}
