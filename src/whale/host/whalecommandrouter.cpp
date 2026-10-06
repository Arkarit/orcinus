/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whalecommandrouter.h"

#include "dolphinviewbridge.h"
#include "views/dolphinview.h"
#include "whaleview.h"
#include "whaleviewcontextimpl.h"
#include "whaleviewdebug.h"
#include "whaleviewhost.h"

WhaleViewHost *WhaleCommandRouter::hostOf(const DolphinView *view)
{
    // The host shows the DolphinView in its stack (WhaleViewHost::WhaleViewHost()).
    return view ? qobject_cast<WhaleViewHost *>(view->parentWidget()) : nullptr;
}

WhaleCommandRouter::Source WhaleCommandRouter::source(const DolphinView *view)
{
    const WhaleViewHost *host = hostOf(view);
    if (!host || host->isDolphinViewShown()) {
        return Source::FileList;
    }
    return (host->currentView()->capabilities() & WhaleViewCapability::RecursiveContent) ? Source::PluginView : Source::MirroredPluginView;
}

QString WhaleCommandRouter::sourceName(Source source)
{
    switch (source) {
    case Source::FileList:
        return QStringLiteral("file-list");
    case Source::MirroredPluginView:
        return QStringLiteral("plugin-mirrored");
    case Source::PluginView:
        return QStringLiteral("plugin");
    }
    return {};
}

bool WhaleCommandRouter::routes(const DolphinView *view)
{
    return source(view) == Source::PluginView;
}

QList<QUrl> WhaleCommandRouter::selectedUrls(const DolphinView *view)
{
    if (routes(view)) {
        return hostOf(view)->currentView()->selectedItems();
    }
    return view ? view->selectedItems().urlList() : QList<QUrl>();
}

KFileItemList WhaleCommandRouter::selectedItems(const DolphinView *view)
{
    if (!routes(view)) {
        return view ? view->selectedItems() : KFileItemList();
    }
    const DolphinViewBridge *bridge = hostOf(view)->bridge();
    KFileItemList items;
    const QList<QUrl> urls = hostOf(view)->currentView()->selectedItems();
    for (const QUrl &url : urls) {
        const KFileItem listed = bridge->item(url);
        items.append(listed.isNull() ? KFileItem(url) : listed);
    }
    return items;
}

bool WhaleCommandRouter::perform(DolphinView *view, WhaleFileOperations::Operation operation)
{
    using Operation = WhaleFileOperations::Operation;
    if (!routes(view)) {
        return false;
    }
    WhaleViewHost *host = hostOf(view);
    const QList<QUrl> urls = host->currentView()->selectedItems();
    if (urls.isEmpty()) {
        // The actions are disabled then; Properties shows the location's, as in the file list.
        return operation != Operation::Properties;
    }
    WhaleFileOperations *operations = host->pluginContext()->fileOperations();
    if (!(operations->supportedOperations(urls) & operation)) {
        qCDebug(WhaleViewDebug) << "Not carried out (not supported for the selection):" << operation << urls;
        return true;
    }
    switch (operation) {
    case Operation::Open:
        operations->openItems(urls);
        break;
    case Operation::OpenWith:
        operations->openItemsWith(urls);
        break;
    case Operation::ShowInFolder:
        operations->showItemInFolder(urls.first());
        break;
    case Operation::Properties:
        operations->showProperties(urls);
        break;
    case Operation::Rename:
        operations->renameItems(urls);
        break;
    case Operation::Trash:
        operations->trashItems(urls);
        break;
    case Operation::Delete:
        operations->deleteItems(urls);
        break;
    case Operation::Copy:
        operations->copyItems(urls);
        break;
    case Operation::Cut:
        operations->cutItems(urls);
        break;
    case Operation::CopyLocation:
        operations->copyLocations(urls);
        break;
    case Operation::Duplicate:
        operations->duplicateItems(urls);
        break;
    }
    return true;
}
