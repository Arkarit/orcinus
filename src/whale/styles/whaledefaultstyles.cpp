/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaledefaultstyles.h"

#include "whalestylestore.h"

#include <KLocalizedString>

namespace
{
const QString HiddenFilterBar = QStringLiteral("visible=false,lock=true,case=false,mode=Glob,scope=Files,text=");
const QString ItemView = QStringLiteral("org.kde.dolphin.itemview");
const QString PlainViewStyle = QStringLiteral("[Dolphin]\nVersion=4\n");
const QString CompactViewStyle = QStringLiteral("[Dolphin]\nVersion=4\nViewMode=2\n");
const QString DetailsViewStyle = QStringLiteral("[Dolphin]\nVersion=4\nViewMode=1\nZoomLevel=0\n");
const QString StandardZoom = QStringLiteral("Icons=32,64;Compact=16,16;Details=16,16");

void set(WhaleStyle &style, const QString &key, const QString &value)
{
    style.elements.insert(key, {true, value});
}

// One view with the Places panel, the main toolbar and the full-width status bar.
WhaleStyle singleView(const QString &id, const QString &name, const QString &viewStyle)
{
    WhaleStyle style;
    style.id = id;
    style.name = name;
    set(style, QStringLiteral("fileViewToolBars"), QStringLiteral("true"));
    set(style, QStringLiteral("filterBar.left"), HiddenFilterBar);
    set(style, QStringLiteral("menuBar"), QStringLiteral("false"));
    set(style, QStringLiteral("panels"), QStringLiteral("places=true,folders=false,information=false,terminal=false"));
    set(style, QStringLiteral("split"), QStringLiteral("false"));
    set(style, QStringLiteral("splitLock"), QStringLiteral("false"));
    set(style, QStringLiteral("statusBar"), QStringLiteral("FullWidth"));
    set(style, QStringLiteral("toolBars"), QStringLiteral("mainToolBar=true"));
    set(style, QStringLiteral("view.left"), ItemView);
    set(style, QStringLiteral("viewStyle.left"), viewStyle);
    set(style, QStringLiteral("viewStyle.right"), PlainViewStyle);
    set(style, QStringLiteral("zoom"), StandardZoom);
    return style;
}

// singleView() with a second view.
WhaleStyle dualView(const QString &id, const QString &name, const QString &leftViewStyle, const QString &rightView, const QString &rightViewStyle)
{
    WhaleStyle style = singleView(id, name, leftViewStyle);
    set(style, QStringLiteral("filterBar.right"), HiddenFilterBar);
    set(style, QStringLiteral("split"), QStringLiteral("true"));
    set(style, QStringLiteral("view.right"), rightView);
    set(style, QStringLiteral("viewStyle.right"), rightViewStyle);
    return style;
}
}

namespace WhaleDefaultStyles
{
QList<WhaleStyle> styles()
{
    const QString compactLeft = QStringLiteral("[Dolphin]\nVersion=4\nViewMode=2\nZoomLevel=0\n");
    const QString detailsRight = QStringLiteral("[Dolphin]\nVersion=4\nViewMode=1\n");

    WhaleStyle imagesOnly = singleView(QStringLiteral("images-only"),
                                       i18nc("@item name of a shipped profile", "Images Only"),
                                       QStringLiteral("[Dolphin]\nVersion=4\nZoomLevel=16\n"));
    set(imagesOnly,
        QStringLiteral("filterBar.left"),
        QStringLiteral("visible=true,lock=true,case=false,mode=Glob,scope=Files,"
                       "text=*.png *.jpg *.jpeg *.gif *.bmp *.webp *.tif *.tiff *.svg *.ico *.avif *.heic *.heif"));
    set(imagesOnly, QStringLiteral("panels"), QStringLiteral("places=false,folders=false,information=false,terminal=false"));
    set(imagesOnly, QStringLiteral("statusBar"), QStringLiteral("Small"));
    set(imagesOnly, QStringLiteral("zoom"), QStringLiteral("Icons=32,256;Compact=16,16;Details=16,16"));

    WhaleStyle cushionMap = dualView(QStringLiteral("locked-cushion-map"),
                                     i18nc("@item name of a shipped profile", "Locked Cushion Map"),
                                     DetailsViewStyle,
                                     QStringLiteral("orcinus_treemap"),
                                     PlainViewStyle);
    set(cushionMap, QStringLiteral("splitLock"), QStringLiteral("true"));

    return {
        singleView(QStringLiteral("compact"), i18nc("@item name of a shipped profile", "Compact"), CompactViewStyle),
        dualView(QStringLiteral("dual-compact"), i18nc("@item name of a shipped profile", "Dual Compact"), compactLeft, ItemView, CompactViewStyle),
        singleView(QStringLiteral("details"), i18nc("@item name of a shipped profile", "Details"), DetailsViewStyle),
        dualView(QStringLiteral("dual-details"), i18nc("@item name of a shipped profile", "Dual Details"), DetailsViewStyle, ItemView, detailsRight),
        imagesOnly,
        cushionMap,
    };
}

QString startStyleId()
{
    return QStringLiteral("compact");
}

bool seed(WhaleStyleStore &store)
{
    if (!store.styles().isEmpty()) {
        return false;
    }
    return store.replaceStyles(styles());
}
}
