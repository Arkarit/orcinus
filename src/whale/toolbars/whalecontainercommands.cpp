/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whalecontainercommands.h"

#include "dolphinmainwindow.h"
#include "dolphinurlnavigator.h"
#include "dolphinviewcontainer.h"
#include "global.h"
#include "middleclickactioneventfilter.h"
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
    , m_home(KStandardAction::home(this, &WhaleContainerCommands::goHome, actionCollection))
    , m_stop(actionCollection->addAction(QStringLiteral("stop")))
    , m_toggleFilter(actionCollection->addAction(QStringLiteral("toggle_filter")))
    , m_toggleSearch(actionCollection->addAction(QStringLiteral("toggle_search")))
    , m_middleClickFilter(new MiddleClickActionEventFilter(this))
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

    // A middle click opens the location in a new tab (DolphinMainWindow does the same for
    // its toolbar and its history menus).
    connect(m_middleClickFilter, &MiddleClickActionEventFilter::actionMiddleClicked, this, &WhaleContainerCommands::openInNewTab);
    m_back->popupMenu()->installEventFilter(m_middleClickFilter);
    m_forward->popupMenu()->installEventFilter(m_middleClickFilter);

    QAction *redisplay = KStandardAction::redisplay(this, &WhaleContainerCommands::reload, actionCollection);
    redisplay->setAutoRepeat(false);
    redisplay->setToolTip(i18nc("@info:tooltip", "Refresh view"));

    m_stop->setText(i18nc("@action:inmenu View", "Stop"));
    m_stop->setToolTip(i18nc("@info", "Stop loading"));
    m_stop->setIcon(QIcon::fromTheme(QStringLiteral("process-stop")));
    m_stop->setEnabled(false);
    connect(m_stop, &QAction::triggered, this, &WhaleContainerCommands::stop);

    // The filter bar of this view: texts and icon as the window's actions.
    QAction *showFilter = actionCollection->addAction(QStringLiteral("show_filter_bar"));
    showFilter->setText(i18nc("@action:inmenu Tools", "Filter…"));
    showFilter->setToolTip(i18nc("@info:tooltip", "Show Filter Bar"));
    showFilter->setIcon(QIcon::fromTheme(QStringLiteral("view-filter")));
    connect(showFilter, &QAction::triggered, this, &WhaleContainerCommands::showFilterBar);

    m_toggleFilter->setText(i18nc("@action:inmenu", "Toggle Filter Bar"));
    m_toggleFilter->setIconText(i18nc("@action:intoolbar", "Filter"));
    m_toggleFilter->setIcon(showFilter->icon());
    m_toggleFilter->setToolTip(showFilter->toolTip());
    m_toggleFilter->setCheckable(true);
    m_toggleFilter->setChecked(container->isFilterBarVisible());
    connect(m_toggleFilter, &QAction::triggered, this, &WhaleContainerCommands::toggleFilterBar);
    connect(container, &DolphinViewContainer::showFilterBarChanged, m_toggleFilter, &QAction::setChecked);

    // The search bar of this view: texts and icon as the window's "toggle_search".
    const std::unique_ptr<QAction> find(KStandardAction::find(nullptr, nullptr, nullptr));
    m_toggleSearch->setText(i18nc("@action:inmenu", "Toggle Search Bar"));
    m_toggleSearch->setIconText(i18nc("@action:intoolbar", "Search"));
    m_toggleSearch->setIcon(find->icon());
    m_toggleSearch->setToolTip(i18nc("@info:tooltip", "Search for files and folders"));
    m_toggleSearch->setCheckable(true);
    m_toggleSearch->setChecked(container->isSearchBarVisible());
    connect(m_toggleSearch, &QAction::triggered, this, &WhaleContainerCommands::toggleSearchBar);
    connect(container, &DolphinViewContainer::searchBarVisibilityChanged, m_toggleSearch, &QAction::setChecked);

    // Selecting in this view. (The window's Select All selects the text of a focused
    // location bar instead; that stays with the window's action.)
    KStandardAction::selectAll(this, &WhaleContainerCommands::selectAll, actionCollection);
    QAction *invert = actionCollection->addAction(QStringLiteral("invert_selection"));
    invert->setText(i18nc("@action:inmenu Edit", "Invert Selection"));
    invert->setIcon(QIcon::fromTheme(QStringLiteral("edit-select-invert")));
    connect(invert, &QAction::triggered, this, &WhaleContainerCommands::invertSelection);

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
    if (!m_container) {
        return;
    }
    m_container->reload();
    // As DolphinMainWindow::reloadView(): the window announces a reload of its active view
    // (its terminal panel refreshes then). That signal is about the active view only, so a
    // reload of another view is not announced.
    auto window = qobject_cast<DolphinMainWindow *>(m_container->window());
    if (window && window->activeViewContainer() == m_container) {
        Q_EMIT window->urlRefreshed(m_container->url());
    }
}

void WhaleContainerCommands::stop()
{
    if (m_container) {
        m_container->view()->stopLoading();
    }
}

void WhaleContainerCommands::toggleFilterBar()
{
    if (m_container) {
        m_container->setFilterBarVisible(!m_container->isFilterBarVisible());
        m_toggleFilter->setChecked(m_container->isFilterBarVisible());
    }
}

void WhaleContainerCommands::showFilterBar()
{
    if (m_container) {
        m_container->setFilterBarVisible(true);
    }
}

void WhaleContainerCommands::toggleSearchBar(bool visible)
{
    if (!m_container) {
        return;
    }
    // As the window's action: opening also puts the focus into the search bar.
    m_container->setSearchBarVisible(visible);
    if (visible) {
        m_container->setFocusToSearchBar();
    }
    m_toggleSearch->setChecked(m_container->isSearchBarVisible());
}

void WhaleContainerCommands::selectAll()
{
    if (m_container) {
        m_container->view()->selectAll();
    }
}

void WhaleContainerCommands::invertSelection()
{
    if (m_container) {
        m_container->view()->invertSelection();
    }
}

QObject *WhaleContainerCommands::middleClickFilter() const
{
    return m_middleClickFilter;
}

void WhaleContainerCommands::openInNewTab(QAction *action)
{
    auto window = m_container ? qobject_cast<DolphinMainWindow *>(m_container->window()) : nullptr;
    if (!window || !action) {
        return;
    }
    // As DolphinMainWindow::slotToolBarActionMiddleClicked() and
    // slotBackForwardActionMiddleClicked(), with the history and location of this view.
    const KUrlNavigator *navigator = m_container->urlNavigatorInternalWithHistory();
    int historyIndex = -1;
    QUrl url;
    if (action == m_back) {
        historyIndex = navigator->historyIndex() + 1;
    } else if (action == m_forward) {
        historyIndex = navigator->historyIndex() - 1;
    } else if (action == m_up) {
        url = KIO::upUrl(m_container->url());
    } else if (action == m_home) {
        url = Dolphin::homeUrl();
    } else if (m_back->popupMenu()->actions().contains(action) || m_forward->popupMenu()->actions().contains(action)) {
        historyIndex = action->data().toInt();
    } else {
        return;
    }
    if (historyIndex >= 0 && historyIndex < navigator->historySize()) {
        url = navigator->locationUrl(historyIndex);
    }
    if (url.isValid()) {
        window->openNewTab(url);
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
        entry->setData(i); // the history index, for a middle click
        const int steps = (i - current) * direction;
        connect(entry, &QAction::triggered, this, [this, steps, direction]() {
            for (int step = 0; step < steps; ++step) {
                direction > 0 ? goBack() : goForward();
            }
        });
    }
}

#include "moc_whalecontainercommands.cpp"
