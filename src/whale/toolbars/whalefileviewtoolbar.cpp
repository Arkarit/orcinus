/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whalefileviewtoolbar.h"
#include "whaledensityline.h"

#include "dolphintabpage.h"
#include "dolphinviewcontainer.h"
#include "whalecontainercommands.h"
#include "whalelocationbar.h"
#include "whaletoolbarconfigurator.h"
#include "whaletoolbarmanager.h"
#include "whaletoolbarsettings.h"
#include "whaleviewcommandscope.h"

#include <KConfig>
#include <KConfigGroup>
#include <KIconLoader>
#include <KLocalizedString>

#include <QActionEvent>
#include <QApplication>
#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QStyle>
#include <QTimer>
#include <QToolButton>

#include <algorithm>

WhaleFileViewToolBar::WhaleFileViewToolBar(DolphinViewContainer *container)
    : KToolBar(container, false, false)
    , m_container(container)
{
    Q_ASSERT(container);
    setObjectName(QStringLiteral("whaleFileViewToolBar"));
    // Closes the file view off with the flat location bar of Very compact (TD).
    WhaleDensityLine::attach(this, Qt::BottomEdge);
    setMovable(false);
    // Small icons without text, as the user's settings of this toolbar (they stay when KDE's
    // toolbar style changes). Every File View Toolbar looks the same.
    KConfig settings(QString(), KConfig::SimpleConfig);
    KConfigGroup group(&settings, QStringLiteral("File View Toolbar"));
    group.writeEntry("IconSize", int(KIconLoader::SizeSmall));
    group.writeEntry("ToolButtonStyle", QStringLiteral("IconOnly"));
    applySettings(group);

    // Shown or not: one setting for all File View Toolbars. A hidden one creates its actions
    // when it is shown.
    WhaleToolbarSettings *toolbarSettings = WhaleToolbarSettings::self();
    if (!toolbarSettings->fileViewToolBarsVisible()) {
        hide();
    }
    connect(toolbarSettings, &WhaleToolbarSettings::fileViewToolBarsVisibleChanged, this, &QWidget::setVisible);

    // Now, not when shown: the window asks every view for its visible navigator.
    m_locationBar = new WhaleLocationBar(container, this);

    m_wrapTimer = new QTimer(this);
    m_wrapTimer->setSingleShot(true);
    connect(m_wrapTimer, &QTimer::timeout, this, &WhaleFileViewToolBar::updateWrapOfTab);

    // The focus in this toolbar (Tab, F6) makes it the active view, as a click does.
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now) {
        if (!now || !m_container || m_container->isActive() || !isAncestorOf(now)) {
            return;
        }
        // Activating a view gives the focus to its file list: back to where it went.
        const QPointer<QWidget> focus = now;
        m_container->setActive(true);
        if (focus) {
            focus->setFocus(Qt::OtherFocusReason);
        }
    });
}

WhaleFileViewToolBar::~WhaleFileViewToolBar()
{
    // Not this toolbar's.
    for (QWidget *widget : {m_trailingWidget.data(), m_leadingWidget.data()}) {
        if (widget && widget->parentWidget() == this) {
            widget->hide();
            widget->setParent(nullptr);
        }
    }
}

WhaleViewCommandScope *WhaleFileViewToolBar::scope() const
{
    return m_scope;
}

WhaleLocationBar *WhaleFileViewToolBar::locationBar() const
{
    return m_locationBar;
}

bool WhaleFileViewToolBar::isLocationBarWrapped() const
{
    return m_locationBarWrapped;
}

int WhaleFileViewToolBar::minimumLocationBarWidth()
{
    return 200;
}

void WhaleFileViewToolBar::setTrailingWidget(QWidget *widget)
{
    setExtraWidget(m_trailingWidget, widget);
}

QWidget *WhaleFileViewToolBar::trailingWidget() const
{
    return m_trailingWidget;
}

void WhaleFileViewToolBar::setLeadingWidget(QWidget *widget)
{
    if (widget == m_leadingWidget) {
        return;
    }
    setExtraWidget(m_leadingWidget, widget);
    // With or without its placeholder.
    rebuild();
}

QWidget *WhaleFileViewToolBar::leadingWidget() const
{
    return m_leadingWidget;
}

bool WhaleFileViewToolBar::isLeadingPlaceholder(const QAction *action)
{
    return action && action->objectName() == QLatin1String("whale_leading_placeholder");
}

void WhaleFileViewToolBar::setExtraWidget(QPointer<QWidget> &slot, QWidget *widget)
{
    if (widget == slot) {
        return;
    }
    if (slot && slot->parentWidget() == this) {
        slot->hide();
        slot->setParent(nullptr);
    }
    slot = widget;
    if (widget) {
        widget->setParent(this);
    }
    layOutExtras();
    // The room for the location bar has changed.
    scheduleWrapUpdate();
}

void WhaleFileViewToolBar::showEvent(QShowEvent *event)
{
    if (!m_scope && m_container) {
        // Shown means part of its window by now: the window's services and toolbar manager.
        m_scope = new WhaleViewCommandScope(m_container, this);
        // Middle click on Back, Forward, Up or Home: in a new tab.
        installEventFilter(m_scope->commands()->middleClickFilter());
        connect(WhaleToolbarManager::ensure(window()), &WhaleToolbarManager::fileViewLayoutChanged, this, &WhaleFileViewToolBar::rebuild);
        rebuild();
    }
    KToolBar::showEvent(event);
    scheduleWrapUpdate();
}

void WhaleFileViewToolBar::resizeEvent(QResizeEvent *event)
{
    KToolBar::resizeEvent(event);
    layOutExtras();
    // Wrapping changes the height only: no new decision then. While the width changes (the
    // animation of a new split view, dragging the splitter), the toolbars would wrap and unwrap
    // on the way.
    if (event->size().width() != event->oldSize().width()) {
        scheduleWrapUpdate(true);
    }
}

void WhaleFileViewToolBar::rebuild()
{
    WhaleToolbarManager *manager = WhaleToolbarManager::of(this);
    if (!m_scope || !manager) {
        return;
    }
    clear();
    if (m_leadingWidget) {
        // Keeps the room of the leading widget at the start of the row.
        m_leadingPlaceholder = new QWidget(this);
        m_leadingPlaceholder->setFixedWidth(m_leadingWidget->sizeHint().width());
        addWidget(m_leadingPlaceholder)->setObjectName(QStringLiteral("whale_leading_placeholder"));
    }
    const WhaleToolbarLayout layout = manager->fileViewLayout();
    // What the user changed about the actions in Configure Toolbars, for this view's actions.
    m_scope->applyActionProperties(layout.actionProperties());
    const auto entries = layout.entries();
    m_hasLocationBar = std::any_of(entries.cbegin(), entries.cend(), [](const WhaleToolbarLayout::Entry &entry) {
        return entry.type == WhaleToolbarLayout::Entry::Type::Action && entry.actionId == WhaleLocationBar::actionName();
    });
    // A spacer next to the location bar would take half of the room the location bar
    // expands into: then it does not expand. In the second row, the location bar is not next to
    // it.
    const bool hasLocationBar = m_hasLocationBar && !m_locationBarWrapped;
    for (const WhaleToolbarLayout::Entry &entry : entries) {
        switch (entry.type) {
        case WhaleToolbarLayout::Entry::Type::Separator:
            addSeparator();
            break;
        case WhaleToolbarLayout::Entry::Type::Spacer: {
            // As KXMLGUIBuilder builds <Spacer/>.
            auto spacer = new QWidget(this);
            spacer->setSizePolicy(hasLocationBar ? QSizePolicy::Minimum : QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
            addWidget(spacer);
            break;
        }
        case WhaleToolbarLayout::Entry::Type::Action:
            if (entry.actionId == WhaleLocationBar::actionName()) {
                // One widget: at its first place only (a hand-edited file may name it twice).
                if (hasLocationBar && !actions().contains(m_locationBar->action())) {
                    addAction(m_locationBar->action());
                }
                break;
            }
            // Only what a File View Toolbar may show (WhaleViewCommandScope::allowedActions()).
            if (QAction *action = m_scope->action(entry.actionId)) {
                addAction(action);
            }
            break;
        }
    }
    if (!m_hasLocationBar && m_locationBarWrapped) {
        m_locationBarWrapped = false;
    }
    layOutExtras();
    scheduleWrapUpdate();
}

bool WhaleFileViewToolBar::needsWrap() const
{
    if (!m_scope || !m_hasLocationBar || !isVisible()) {
        return false;
    }
    // What the other widgets of the row take, as QToolBarLayout lays them out.
    const int spacing = style()->pixelMetric(QStyle::PM_ToolBarItemSpacing, nullptr, this);
    const int margin = style()->pixelMetric(QStyle::PM_ToolBarItemMargin, nullptr, this) + style()->pixelMetric(QStyle::PM_ToolBarFrameWidth, nullptr, this);
    int others = 2 * margin;
    const auto actions = this->actions();
    for (QAction *action : actions) {
        QWidget *widget = widgetForAction(action);
        if (action == m_locationBar->action() || !action->isVisible() || !widget) {
            continue;
        }
        others += std::max(widget->sizeHint().width(), 0) + spacing;
    }
    return contentsRect().width() - others < minimumLocationBarWidth();
}

void WhaleFileViewToolBar::scheduleWrapUpdate(bool settle)
{
    if (settle) {
        m_wrapTimer->start(25);
    } else if (!m_wrapTimer->isActive()) {
        m_wrapTimer->start(0);
    }
}

void WhaleFileViewToolBar::updateWrapOfTab()
{
    // All views of the tab together: their file lists start at the same height.
    QList<WhaleFileViewToolBar *> toolBars{this};
    for (QWidget *parent = parentWidget(); parent; parent = parent->parentWidget()) {
        if (auto page = qobject_cast<DolphinTabPage *>(parent)) {
            toolBars = page->findChildren<WhaleFileViewToolBar *>();
            break;
        }
    }
    const bool wrap = std::any_of(toolBars.cbegin(), toolBars.cend(), [](WhaleFileViewToolBar *toolBar) {
        return toolBar->needsWrap();
    });
    for (WhaleFileViewToolBar *toolBar : std::as_const(toolBars)) {
        toolBar->setLocationBarWrapped(wrap);
    }
}

void WhaleFileViewToolBar::setLocationBarWrapped(bool wrapped)
{
    wrapped = wrapped && m_hasLocationBar && m_scope;
    if (wrapped == m_locationBarWrapped) {
        return;
    }
    m_locationBarWrapped = wrapped;
    // Out of the row (taking it out hides it) or back into it.
    rebuild();
}

void WhaleFileViewToolBar::layOutExtras()
{
    // All outside of the area QToolBarLayout lays the buttons out in (contentsRect()): the
    // second row below it, the leading widget at its start, the trailing widget at its end.
    const int margin = style()->pixelMetric(QStyle::PM_ToolBarItemMargin, nullptr, this) + style()->pixelMetric(QStyle::PM_ToolBarFrameWidth, nullptr, this);
    const int spacing = style()->pixelMetric(QStyle::PM_ToolBarItemSpacing, nullptr, this);
    const int rowHeight = m_locationBarWrapped ? m_locationBar->sizeHint().height() : 0;
    const bool trailing = m_trailingWidget && m_trailingWidget->parentWidget() == this;
    const int trailingWidth = trailing ? m_trailingWidget->sizeHint().width() : 0;
    const int end = trailing ? trailingWidth + spacing : 0;
    const bool rightToLeft = layoutDirection() == Qt::RightToLeft;
    const QMargins margins(rightToLeft ? end : 0, 0, rightToLeft ? 0 : end, m_locationBarWrapped ? rowHeight + margin : 0);
    if (contentsMargins() != margins) {
        setContentsMargins(margins);
    }
    if (m_locationBarWrapped) {
        m_locationBar->setGeometry(margin, height() - rowHeight - margin, width() - 2 * margin, rowHeight);
        m_locationBar->show();
    }
    if (trailing) {
        const int rowOneHeight = height() - margins.bottom();
        const int widgetHeight = std::min(m_trailingWidget->sizeHint().height(), rowOneHeight);
        const int x = rightToLeft ? margin : width() - margin - trailingWidth;
        m_trailingWidget->setGeometry(x, (rowOneHeight - widgetHeight) / 2, trailingWidth, widgetHeight);
        m_trailingWidget->show();
        m_trailingWidget->raise();
    }
    // Over its placeholder, the first item of the row (QToolBarLayout ignores a margin at the start).
    if (m_leadingWidget && m_leadingWidget->parentWidget() == this && m_leadingPlaceholder) {
        const int rowOneHeight = height() - margins.bottom();
        const int widgetHeight = std::min(m_leadingWidget->sizeHint().height(), rowOneHeight);
        const QRect place = m_leadingPlaceholder->geometry();
        m_leadingWidget->setGeometry(place.x(), (rowOneHeight - widgetHeight) / 2, place.width(), widgetHeight);
        m_leadingWidget->setVisible(m_leadingPlaceholder->isVisible());
        m_leadingWidget->raise();
    }
}

void WhaleFileViewToolBar::actionEvent(QActionEvent *event)
{
    KToolBar::actionEvent(event);
    if (event->type() == QEvent::ActionAdded) {
        if (QWidget *button = widgetForAction(event->action())) {
            button->installEventFilter(this);
        }
    }
}

void WhaleFileViewToolBar::contextMenuEvent(QContextMenuEvent *event)
{
    // A menu of its own: KToolBar's needs a main window as parent, and QToolBar would pass the
    // event on to the window, which answers with the menu of its own toolbars and panels.
    event->accept();
    auto menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    if (WhaleToolbarConfigurator *configurator = WhaleToolbarConfigurator::of(this)) {
        QAction *configure = menu->addAction(QIcon::fromTheme(QStringLiteral("configure-toolbars")), i18nc("@action:inmenu", "Configure File View Toolbar…"));
        configure->setObjectName(QStringLiteral("whale_configure_file_view_toolbar"));
        connect(configure, &QAction::triggered, configurator, [configurator]() {
            configurator->configure(WhaleToolbarDescriptor::fileViewId());
        });
    }
    // All of them; "Show File View Toolbars" shows them again (View menu, hamburger menu, the
    // main toolbar's context menu and the menu of a view's background).
    QAction *hide = menu->addAction(i18nc("@action:inmenu", "Hide File View Toolbars"));
    hide->setObjectName(QStringLiteral("whale_hide_file_view_toolbars"));
    connect(hide, &QAction::triggered, WhaleToolbarSettings::self(), [] {
        WhaleToolbarSettings::self()->setFileViewToolBarsVisible(false);
    });
    menu->popup(event->globalPos());
}

bool WhaleFileViewToolBar::eventFilter(QObject *watched, QEvent *event)
{
    // The leading widget follows its placeholder (also into the overflow menu: hidden).
    if (watched == m_leadingPlaceholder) {
        switch (event->type()) {
        case QEvent::Move:
        case QEvent::Resize:
        case QEvent::Show:
        case QEvent::Hide:
            layOutExtras();
            break;
        default:
            break;
        }
    }
    // Working with the toolbar of a view makes it the active view, as clicking into the view
    // or its location bar does. The actions act on this view in any case.
    if (event->type() == QEvent::MouseButtonPress && m_container) {
        if (!m_container->isActive()) {
            m_container->setActive(true);
        }
        // A disabled button: tell why, in this view's message area. (The window does that for
        // the shortcut of a disabled action, in the active view; the actions here have none.)
        auto button = qobject_cast<QToolButton *>(watched);
        if (button && m_scope && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
            const QString reason = m_scope->disabledReason(button->defaultAction());
            if (!reason.isEmpty()) {
                m_container->showMessage(reason, KMessageWidget::Warning);
            }
        }
    }
    return KToolBar::eventFilter(watched, event);
}

#include "moc_whalefileviewtoolbar.cpp"
