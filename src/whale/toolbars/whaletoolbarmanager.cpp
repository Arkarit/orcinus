/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaletoolbarmanager.h"

#include "whalefileviewtoolbarclient.h"
#include "whaleviewcommandscope.h"

#include <KLocalizedString>

#include <QWidget>

namespace
{
QList<WhaleToolbarManager *> s_managers;
}

WhaleToolbarManager::WhaleToolbarManager(QWidget *window)
    : QObject(window)
    , m_fileViewClient(std::make_unique<WhaleFileViewToolbarClient>())
    , m_fileViewLayout(m_fileViewClient->layout())
{
    s_managers.append(this);
}

WhaleToolbarManager::~WhaleToolbarManager()
{
    s_managers.removeOne(this);
}

WhaleToolbarManager *WhaleToolbarManager::ensure(QWidget *window)
{
    Q_ASSERT(window && window->isWindow());
    if (WhaleToolbarManager *manager = of(window)) {
        return manager;
    }
    return new WhaleToolbarManager(window);
}

WhaleToolbarManager *WhaleToolbarManager::of(const QWidget *widget)
{
    if (!widget) {
        return nullptr;
    }
    return widget->window()->findChild<WhaleToolbarManager *>(QString(), Qt::FindDirectChildrenOnly);
}

QList<WhaleToolbarDescriptor> WhaleToolbarManager::descriptors() const
{
    WhaleToolbarDescriptor global;
    global.id = WhaleToolbarDescriptor::globalId();
    global.displayName = i18nc("@title:menu", "Main Toolbar");
    global.xmlGuiToolBar = QStringLiteral("mainToolBar");
    global.rcFile = QStringLiteral("dolphinui.rc");
    global.target = WhaleToolbarDescriptor::Target::ActiveView;

    WhaleToolbarDescriptor fileView;
    fileView.id = WhaleToolbarDescriptor::fileViewId();
    fileView.displayName = i18nc("@title:menu", "File View Toolbar");
    fileView.xmlGuiToolBar = WhaleFileViewToolbarClient::toolBarName();
    fileView.rcFile = WhaleFileViewToolbarClient::rcFile();
    fileView.target = WhaleToolbarDescriptor::Target::OwnFileView;
    fileView.allowedActions = WhaleViewCommandScope::allowedActions();
    return {global, fileView};
}

WhaleToolbarDescriptor WhaleToolbarManager::descriptor(const QString &id) const
{
    const auto all = descriptors();
    for (const WhaleToolbarDescriptor &descriptor : all) {
        if (descriptor.id == id) {
            return descriptor;
        }
    }
    return {};
}

WhaleToolbarLayout WhaleToolbarManager::fileViewLayout() const
{
    return m_fileViewLayout;
}

void WhaleToolbarManager::reloadFileViewLayout()
{
    m_fileViewClient->reload();
    const WhaleToolbarLayout layout = m_fileViewClient->layout();
    if (layout != m_fileViewLayout) {
        m_fileViewLayout = layout;
        Q_EMIT fileViewLayoutChanged();
    }
}

void WhaleToolbarManager::reloadAllFileViewLayouts()
{
    const auto managers = s_managers;
    for (WhaleToolbarManager *manager : managers) {
        manager->reloadFileViewLayout();
    }
}

#include "moc_whaletoolbarmanager.cpp"
