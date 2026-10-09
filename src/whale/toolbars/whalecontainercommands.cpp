/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whalecontainercommands.h"

#include "dolphinurlnavigator.h"
#include "dolphinviewcontainer.h"
#include "views/dolphinview.h"

#include <KActionCollection>
#include <KIO/Global>
#include <KLocalizedString>
#include <KStandardAction>
#include <KToolBarPopupAction>

#include <QMenu>

#include <memory>

namespace
{
// As many history entries as the window's Back and Forward menus show.
constexpr int MaxHistoryEntries = 12;

KToolBarPopupAction *createHistoryAction(KStandardAction::StandardAction id, KActionCollection *collection)
{
    // The window's Back/Forward are popup actions with the history as menu (DolphinMainWindow).
    const std::unique_ptr<QAction> standard(id == KStandardAction::Back ? KStandardAction::back(nullptr, nullptr, nullptr)
                                                                        : KStandardAction::forward(nullptr, nullptr, nullptr));
    auto action = new KToolBarPopupAction(standard->icon(), standard->text(), collection);
    action->setPopupMode(KToolBarPopupAction::DelayedPopup);
    collection->addAction(standard->objectName(), action);
    return action;
}
}

WhaleContainerCommands::WhaleContainerCommands(DolphinViewContainer *container, KActionCollection *actionCollection, QObject *parent)
    : QObject(parent)
    , m_container(container)
    , m_back(createHistoryAction(KStandardAction::Back, actionCollection))
    , m_forward(createHistoryAction(KStandardAction::Forward, actionCollection))
    , m_up(KStandardAction::up(this, &WhaleContainerCommands::goUp, actionCollection))
    , m_stop(actionCollection->addAction(QStringLiteral("stop")))
{
    Q_ASSERT(container);
    m_back->setToolTip(i18nc("@info", "Go back"));
    m_forward->setToolTip(i18nc("@info", "Go forward"));
    connect(m_back, &QAction::triggered, this, &WhaleContainerCommands::goBack);
    connect(m_forward, &QAction::triggered, this, &WhaleContainerCommands::goForward);
    connect(m_back->popupMenu(), &QMenu::aboutToShow, this, [this]() {
        fillHistoryMenu(m_back, +1);
    });
    connect(m_forward->popupMenu(), &QMenu::aboutToShow, this, [this]() {
        fillHistoryMenu(m_forward, -1);
    });

    KStandardAction::home(this, &WhaleContainerCommands::goHome, actionCollection);

    QAction *redisplay = KStandardAction::redisplay(this, &WhaleContainerCommands::reload, actionCollection);
    redisplay->setAutoRepeat(false);
    redisplay->setToolTip(i18nc("@info:tooltip", "Refresh view"));

    m_stop->setText(i18nc("@action:inmenu View", "Stop"));
    m_stop->setToolTip(i18nc("@info", "Stop loading"));
    m_stop->setIcon(QIcon::fromTheme(QStringLiteral("process-stop")));
    m_stop->setEnabled(false);
    connect(m_stop, &QAction::triggered, this, &WhaleContainerCommands::stop);

    // The state of this view only.
    connect(container->urlNavigatorInternalWithHistory(), &KUrlNavigator::historyChanged, this, &WhaleContainerCommands::updateHistory);
    DolphinView *view = container->view();
    connect(view, &DolphinView::urlChanged, this, &WhaleContainerCommands::updateUp);
    connect(view, &DolphinView::directoryLoadingStarted, m_stop, [this]() {
        m_stop->setEnabled(true);
    });
    connect(view, &DolphinView::directoryLoadingCompleted, m_stop, [this]() {
        m_stop->setEnabled(false);
    });
    updateHistory();
    updateUp();
}

WhaleContainerCommands::~WhaleContainerCommands() = default;

void WhaleContainerCommands::goBack()
{
    if (!m_container) {
        return;
    }
    // As DolphinMainWindow::goBack(): an empty location state is a redirection, skip it too.
    DolphinUrlNavigator *navigator = m_container->urlNavigatorInternalWithHistory();
    navigator->goBack();
    if (navigator->locationState().isEmpty()) {
        navigator->goBack();
    }
}

void WhaleContainerCommands::goForward()
{
    if (m_container) {
        m_container->urlNavigatorInternalWithHistory()->goForward();
    }
}

void WhaleContainerCommands::goUp()
{
    if (m_container) {
        m_container->urlNavigatorInternalWithHistory()->goUp();
    }
}

void WhaleContainerCommands::goHome()
{
    if (m_container) {
        m_container->urlNavigatorInternalWithHistory()->goHome();
    }
}

void WhaleContainerCommands::reload()
{
    if (m_container) {
        m_container->reload();
    }
}

void WhaleContainerCommands::stop()
{
    if (m_container) {
        m_container->view()->stopLoading();
    }
}

void WhaleContainerCommands::updateHistory()
{
    if (!m_container) {
        return;
    }
    const KUrlNavigator *navigator = m_container->urlNavigatorInternalWithHistory();
    const int index = navigator->historyIndex();
    m_back->setEnabled(index < navigator->historySize() - 1);
    m_forward->setEnabled(index > 0);
}

void WhaleContainerCommands::updateUp()
{
    if (m_container) {
        const QUrl url = m_container->url();
        m_up->setEnabled(KIO::upUrl(url) != url);
    }
}

void WhaleContainerCommands::fillHistoryMenu(KToolBarPopupAction *action, int direction)
{
    QMenu *menu = action->popupMenu();
    menu->clear();
    if (!m_container) {
        return;
    }
    const KUrlNavigator *navigator = m_container->urlNavigatorInternalWithHistory();
    const int current = navigator->historyIndex();
    for (int i = current + direction, entries = 0; i >= 0 && i < navigator->historySize() && entries < MaxHistoryEntries; i += direction, ++entries) {
        const QUrl url = navigator->locationUrl(i);
        QAction *entry = menu->addAction(QIcon::fromTheme(KIO::iconNameForUrl(url)), url.toDisplayString(QUrl::PreferLocalFile));
        const int steps = (i - current) * direction;
        connect(entry, &QAction::triggered, this, [this, steps, direction]() {
            for (int step = 0; step < steps; ++step) {
                direction > 0 ? goBack() : goForward();
            }
        });
    }
}

#include "moc_whalecontainercommands.cpp"
