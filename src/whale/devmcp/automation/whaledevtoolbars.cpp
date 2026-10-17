/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaledevtoolbars.h"

#include "dolphinmainwindow.h"
#include "dolphintabpage.h"
#include "dolphintabwidget.h"
#include "dolphinurlnavigator.h"
#include "dolphinviewcontainer.h"
#include "whalefileviewtoolbar.h"
#include "whalelocationbar.h"
#include "whaletoolbarconfigurator.h"
#include "whaletoolbarmanager.h"
#include "whaletoolbarsettings.h"
#include "whaleviewactions.h"
#include "whaleviewcommandscope.h"

#include <KEditToolBar>
#include <KLocalizedString>
#include <KToolBar>
#include <KUrlComboBox>

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QDataStream>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QPushButton>
#include <QStyleOptionToolButton>
#include <QTimer>
#include <QToolButton>
#include <QWidgetAction>
#include <QWindow>

#include <memory>

// What QTest uses for mouse input on a window: the event takes the way of real input.
Q_GUI_EXPORT void qt_handleMouseEvent(QWindow *window,
                                      const QPointF &local,
                                      const QPointF &global,
                                      Qt::MouseButtons state,
                                      Qt::MouseButton button,
                                      QEvent::Type type,
                                      Qt::KeyboardModifiers mods,
                                      int timestamp);

namespace
{
const char instanceIdProperty[] = "whaleDevInstanceId";
const char actionKeyProperty[] = "whaleDevActionKey";
const char widgetKeyProperty[] = "whaleDevWidgetKey";
// Menus are listed down to this depth (view_settings > Sort By > its entries).
constexpr int MaxMenuDepth = 3;
// How long a click may wait for a menu to open (a delayed popup opens after a delay).
constexpr int MenuTimeoutMs = 3000;

QJsonObject ok()
{
    return {{QStringLiteral("ok"), true}};
}

QJsonObject failure(const QString &error)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
}

QString plainText(const QAction *action)
{
    // Without "&" and without the shortcut after a tab ("Copy Here\tCtrl").
    return KLocalizedString::removeAcceleratorMarker(action->text()).section(QLatin1Char('\t'), 0, 0);
}

/// A key of the action object for the state: equal for the same object, different for
/// different objects (no addresses in the state).
QString actionKey(QAction *action)
{
    static int count = 0;
    QString key = action->property(actionKeyProperty).toString();
    if (key.isEmpty()) {
        key = QStringLiteral("action-%1").arg(++count);
        action->setProperty(actionKeyProperty, key);
    }
    return key;
}

/// The same for a widget: "location-bar-N".
QString widgetKey(QWidget *widget, const QString &prefix)
{
    static int count = 0;
    QString key = widget->property(widgetKeyProperty).toString();
    if (key.isEmpty()) {
        key = QStringLiteral("%1-%2").arg(prefix).arg(++count);
        widget->setProperty(widgetKeyProperty, key);
    }
    return key;
}

bool isSpacer(const QAction *action)
{
    // As KXMLGUIBuilder and WhaleFileViewToolBar build <Spacer/>: an unnamed widget.
    return qobject_cast<const QWidgetAction *>(action) && action->objectName().isEmpty();
}

QJsonArray layoutOf(const KToolBar *toolBar)
{
    QJsonArray layout;
    const auto actions = toolBar->actions();
    for (const QAction *action : actions) {
        layout.append(action->isSeparator() ? QStringLiteral("-") : isSpacer(action) ? QStringLiteral("|") : action->objectName());
    }
    return layout;
}

/// The menu a button opens: its own (a popup action's) or that of its action (QToolButton
/// shows the menu of its default action without making it its own).
QMenu *buttonMenu(const QToolButton *button, QAction *action)
{
    if (button && button->menu()) {
        return button->menu();
    }
    return action->menu();
}

QJsonArray menuState(QMenu *menu, int depth)
{
    QJsonArray entries;
    const auto actions = menu->actions();
    for (QAction *action : actions) {
        if (!action->isVisible()) {
            continue;
        }
        if (action->isSeparator()) {
            entries.append(QJsonObject{{QStringLiteral("separator"), true}});
            continue;
        }
        QJsonObject entry{
            {QStringLiteral("id"), action->objectName()},
            {QStringLiteral("text"), plainText(action)},
            {QStringLiteral("enabled"), action->isEnabled()},
        };
        if (action->isCheckable()) {
            entry.insert(QStringLiteral("checked"), action->isChecked());
        }
        if (qobject_cast<QWidgetAction *>(action)) {
            entry.insert(QStringLiteral("widget"), true); // e.g. the zoom slider
        }
        if (action->menu() && depth < MaxMenuDepth) {
            entry.insert(QStringLiteral("menu"), menuState(action->menu(), depth + 1));
        }
        entries.append(entry);
    }
    return entries;
}

QJsonArray actionsState(const KToolBar *toolBar, const WhaleViewCommandScope *scope)
{
    QJsonArray result;
    const auto actions = toolBar->actions();
    for (QAction *action : actions) {
        if (action->isSeparator() || isSpacer(action)) {
            continue;
        }
        QJsonArray shortcuts;
        const auto sequences = action->shortcuts();
        for (const QKeySequence &sequence : sequences) {
            shortcuts.append(sequence.toString(QKeySequence::PortableText));
        }
        QJsonObject state{
            {QStringLiteral("id"), action->objectName()},
            {QStringLiteral("text"), plainText(action)},
            {QStringLiteral("enabled"), action->isEnabled()},
            {QStringLiteral("checkable"), action->isCheckable()},
            {QStringLiteral("checked"), action->isChecked()},
            {QStringLiteral("shortcuts"), shortcuts},
            {QStringLiteral("actionKey"), actionKey(action)},
        };
        if (scope) {
            // What the toolbar tells on a click of the disabled button, and the view's gate.
            state.insert(QStringLiteral("disabledReason"), scope->disabledReason(action));
            state.insert(QStringLiteral("gateReason"), scope->viewActions() ? scope->viewActions()->gateReason(action->objectName()) : QString());
        }
        auto button = qobject_cast<QToolButton *>(toolBar->widgetForAction(action));
        // A menu as it is now (history menus are filled when they open); the button has the
        // menu of a popup action (Back, Forward).
        if (QMenu *menu = buttonMenu(button, action)) {
            state.insert(QStringLiteral("menu"), menuState(menu, 1));
        }
        if (button) {
            static const QHash<int, QString> modes = {{QToolButton::DelayedPopup, QStringLiteral("delayed")},
                                                      {QToolButton::MenuButtonPopup, QStringLiteral("button")},
                                                      {QToolButton::InstantPopup, QStringLiteral("instant")}};
            if (buttonMenu(button, action)) {
                state.insert(QStringLiteral("popup"), modes.value(button->popupMode()));
            }
        } else {
            state.insert(QStringLiteral("widget"), true);
        }
        result.append(state);
    }
    return result;
}

WhaleFileViewToolBar *fileViewToolBar(DolphinViewContainer *container)
{
    return container->findChild<WhaleFileViewToolBar *>(QString(), Qt::FindDirectChildrenOnly);
}

WhaleLocationBar *locationBarOf(DolphinViewContainer *container)
{
    WhaleFileViewToolBar *toolBar = fileViewToolBar(container);
    return toolBar ? toolBar->locationBar() : nullptr;
}

/// Placed in its toolbar (a layout entry), whether shown or not.
bool isPlaced(DolphinViewContainer *container, WhaleLocationBar *locationBar)
{
    WhaleFileViewToolBar *toolBar = fileViewToolBar(container);
    return toolBar && toolBar->actions().contains(locationBar->action());
}

QJsonObject locationBarSummary(DolphinViewContainer *container)
{
    WhaleLocationBar *locationBar = locationBarOf(container);
    if (!locationBar) {
        return {{QStringLiteral("exists"), false}};
    }
    return {
        {QStringLiteral("exists"), true},
        {QStringLiteral("connected"), container->urlNavigator() == locationBar->urlNavigator()},
        {QStringLiteral("present"), isPlaced(container, locationBar)},
        {QStringLiteral("widgetKey"), widgetKey(locationBar, QStringLiteral("location-bar"))},
    };
}

KEditToolBar *openDialog(DolphinMainWindow *window)
{
    WhaleToolbarConfigurator *configurator = WhaleToolbarConfigurator::of(window);
    return configurator ? configurator->dialog() : nullptr;
}

QString descriptorIdOfName(DolphinMainWindow *window, const QString &text)
{
    // The combo shows the descriptors' names (with KDE's " <gui name>" suffix if the cosmetics fail).
    WhaleToolbarManager *manager = WhaleToolbarManager::of(window);
    if (manager) {
        const auto descriptors = manager->descriptors();
        for (const WhaleToolbarDescriptor &descriptor : descriptors) {
            if (text == descriptor.displayName || text.startsWith(descriptor.displayName + QLatin1String(" <"))) {
                return descriptor.id;
            }
        }
    }
    return text;
}

struct DialogItem {
    QString id; // the action's name, "-" for a separator, "|" for a spacer
    QString text;
};

/// The item as KEditToolBar has it: its drag data carries the action's name.
DialogItem dialogItem(QListWidget *list, int row)
{
    DialogItem item{QString(), list->item(row)->text()};
    const QModelIndex index = list->model()->index(row, 0);
    const std::unique_ptr<QMimeData> data(list->model()->mimeData({index}));
    if (data) {
        QDataStream stream(data->data(QStringLiteral("application/x-kde-action-list")));
        QString tag;
        QString name;
        QString statusText;
        bool separator = false;
        bool spacer = false;
        stream >> tag >> name >> statusText >> separator >> spacer;
        item.id = separator ? QStringLiteral("-") : spacer ? QStringLiteral("|") : name;
    }
    return item;
}

QJsonArray dialogItems(QListWidget *list)
{
    QJsonArray result;
    for (int row = 0; list && row < list->count(); ++row) {
        const DialogItem item = dialogItem(list, row);
        result.append(QJsonObject{{QStringLiteral("id"), item.id}, {QStringLiteral("text"), item.text}});
    }
    return result;
}

int findItem(QListWidget *list, const QString &id)
{
    for (int row = 0; list && row < list->count(); ++row) {
        if (dialogItem(list, row).id == id) {
            return row;
        }
    }
    return -1;
}

// "Available actions" and "Current actions", in the order KEditToolBar creates them.
QListWidget *availableList(KEditToolBar *dialog)
{
    return dialog->findChildren<QListWidget *>().value(0);
}

QListWidget *currentList(KEditToolBar *dialog)
{
    return dialog->findChildren<QListWidget *>().value(1);
}

QPushButton *dialogButton(KEditToolBar *dialog, QDialogButtonBox::StandardButton which)
{
    auto box = dialog->findChild<QDialogButtonBox *>();
    return box ? box->button(which) : nullptr;
}

/// KEditToolBar's arrow buttons between the lists, by their icons.
QToolButton *arrowButton(KEditToolBar *dialog, bool insert)
{
    const bool rightToLeft = QApplication::isRightToLeft();
    const QString icon = insert != rightToLeft ? QStringLiteral("go-next") : QStringLiteral("go-previous");
    const auto buttons = dialog->findChildren<QToolButton *>();
    for (QToolButton *button : buttons) {
        if (button->icon().name() == icon) {
            return button;
        }
    }
    return nullptr;
}

QStringList idsOf(const QJsonValue &value)
{
    QStringList ids;
    if (value.isString()) {
        ids.append(value.toString());
    }
    const QJsonArray array = value.toArray();
    for (const QJsonValue &entry : array) {
        ids.append(entry.toString());
    }
    return ids;
}

QString selectInDialog(DolphinMainWindow *window, KEditToolBar *dialog, const QString &descriptorId)
{
    auto combo = dialog->findChild<QComboBox *>();
    if (!combo) {
        return QStringLiteral("Configure Toolbars has no toolbar list");
    }
    for (int i = 0; i < combo->count(); ++i) {
        if (descriptorIdOfName(window, combo->itemText(i)) == descriptorId) {
            combo->setCurrentIndex(i);
            Q_EMIT combo->activated(i); // what choosing an entry does
            return QString();
        }
    }
    return QStringLiteral("Configure Toolbars offers no toolbar \"%1\"").arg(descriptorId);
}

QString insertInDialog(KEditToolBar *dialog, const QString &id)
{
    QListWidget *available = availableList(dialog);
    QListWidget *current = currentList(dialog);
    const int row = findItem(available, id);
    if (row < 0) {
        return QStringLiteral("\"%1\" is not in the available actions of this toolbar").arg(id);
    }
    // Behind the last current action: KEditToolBar inserts behind the current item there.
    if (current->count() > 0) {
        current->setCurrentRow(current->count() - 1);
    }
    available->setCurrentRow(row);
    QToolButton *insert = arrowButton(dialog, true);
    if (!insert || !insert->isEnabled()) {
        return QStringLiteral("the insert button is not enabled for \"%1\"").arg(id);
    }
    insert->click();
    return QString();
}

QString removeInDialog(KEditToolBar *dialog, const QString &id)
{
    QListWidget *current = currentList(dialog);
    const int row = findItem(current, id);
    if (row < 0) {
        return QStringLiteral("\"%1\" is not in the current actions of this toolbar").arg(id);
    }
    current->setCurrentRow(row);
    QToolButton *remove = arrowButton(dialog, false);
    if (!remove || !remove->isEnabled()) {
        return QStringLiteral("the remove button is not enabled for \"%1\"").arg(id);
    }
    remove->click();
    return QString();
}

/// Confirms KDE's question of "Defaults" ("… reset all toolbars …") as soon as it is open.
void confirmDefaultsQuestion(KEditToolBar *dialog, std::shared_ptr<bool> answered)
{
    auto timer = new QTimer(dialog);
    auto elapsed = std::make_shared<QElapsedTimer>();
    elapsed->start();
    QObject::connect(timer, &QTimer::timeout, dialog, [timer, dialog, answered, elapsed]() {
        auto box = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        auto buttons = box && box != dialog ? box->findChild<QDialogButtonBox *>() : nullptr;
        if (QPushButton *yes = buttons ? buttons->button(QDialogButtonBox::Yes) : nullptr) {
            *answered = true;
            timer->deleteLater();
            yes->click();
        } else if (elapsed->elapsed() > MenuTimeoutMs) {
            timer->deleteLater();
        }
    });
    timer->start(20);
}

void sendMouse(QWidget *widget, QEvent::Type type, const QPoint &position)
{
    // Far apart in time: two clicks are never a double click.
    static int timestamp = 1000000;
    timestamp += 1000;
    QWidget *window = widget->window();
    const QPointF local = widget->mapTo(window, QPointF(position));
    const Qt::MouseButtons buttons = type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton;
    qt_handleMouseEvent(window->windowHandle(), local, window->mapToGlobal(local), buttons, Qt::LeftButton, type, Qt::NoModifier, timestamp);
}

/// Where a press opens the menu of @p button: its arrow for a button with a menu button.
QPoint menuPressPosition(QToolButton *button)
{
    if (button->popupMode() != QToolButton::MenuButtonPopup) {
        return button->rect().center();
    }
    QStyleOptionToolButton option;
    option.initFrom(button);
    option.rect = button->rect();
    option.subControls = QStyle::SC_ToolButton | QStyle::SC_ToolButtonMenu;
    option.features = QStyleOptionToolButton::MenuButtonPopup | QStyleOptionToolButton::HasMenu;
    option.toolButtonStyle = button->toolButtonStyle();
    option.iconSize = button->iconSize();
    const QRect arrow = button->style()->subControlRect(QStyle::CC_ToolButton, &option, QStyle::SC_ToolButtonMenu, button);
    return arrow.isValid() ? arrow.center() : QPoint(button->width() - 3, button->height() / 2);
}

QAction *menuEntry(QMenu *menu, const QString &name)
{
    const auto actions = menu->actions();
    for (QAction *action : actions) {
        if (!action->isSeparator() && action->isVisible() && (action->objectName() == name || plainText(action) == name)) {
            return action;
        }
    }
    return nullptr;
}

QStringList entryNames(QMenu *menu)
{
    QStringList names;
    const auto actions = menu->actions();
    for (QAction *action : actions) {
        if (!action->isSeparator() && action->isVisible()) {
            names.append(action->objectName().isEmpty() ? plainText(action) : action->objectName());
        }
    }
    return names;
}

void sendKey(QWidget *widget, int key)
{
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
    QCoreApplication::sendEvent(widget, &press);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
    QCoreApplication::sendEvent(widget, &release);
}

bool waitFor(const std::function<bool()> &condition)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition()) {
        if (timer.elapsed() > MenuTimeoutMs) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return true;
}

/**
 * Chooses the entry @p path ("Entry" or "Submenu > Entry") of the open @p menu with the
 * keyboard, as the user can: to the entry, Right into a submenu, Return.
 */
QJsonObject chooseEntry(QMenu *menu, const QString &path)
{
    // Not "/": entries of history menus are paths.
    const QStringList segments = path.split(QStringLiteral(" > "));
    QMenu *current = menu;
    for (int i = 0; i < segments.count(); ++i) {
        QAction *entry = menuEntry(current, segments.at(i));
        if (!entry) {
            const QJsonObject result =
                failure(QStringLiteral("the menu has no entry \"%1\"; it has: %2").arg(segments.at(i), entryNames(current).join(QStringLiteral(", "))));
            menu->hide();
            return result;
        }
        const bool last = i == segments.count() - 1;
        QString problem;
        if (!entry->isEnabled()) {
            problem = QStringLiteral("the entry \"%1\" is disabled").arg(segments.at(i));
        } else if (last && qobject_cast<QWidgetAction *>(entry)) {
            problem = QStringLiteral("the entry \"%1\" is a widget, not a command").arg(segments.at(i));
        } else if (last && entry->menu()) {
            problem = QStringLiteral("the entry \"%1\" is a submenu; name one of its entries (\"%1 > …\")").arg(segments.at(i));
        } else if (!last && !entry->menu()) {
            problem = QStringLiteral("the entry \"%1\" has no submenu").arg(segments.at(i));
        }
        if (!problem.isEmpty()) {
            menu->hide();
            return failure(problem);
        }
        current->setActiveAction(entry);
        if (last) {
            // Return activates the entry and closes all menus, as a click does.
            sendKey(current, Qt::Key_Return);
            return ok();
        }
        sendKey(current, Qt::Key_Right);
        QMenu *submenu = entry->menu();
        if (!waitFor([submenu]() {
                return submenu->isVisible();
            })) {
            menu->hide();
            return failure(QStringLiteral("the submenu \"%1\" did not open").arg(segments.at(i)));
        }
        current = submenu;
    }
    return ok();
}
}

QString WhaleDevToolbars::instanceId(DolphinViewContainer *container)
{
    static int count = 0;
    QString id = container->property(instanceIdProperty).toString();
    if (id.isEmpty()) {
        id = QStringLiteral("view-%1").arg(++count);
        container->setProperty(instanceIdProperty, id);
    }
    return id;
}

QList<WhaleDevToolbars::Instance> WhaleDevToolbars::instances(DolphinMainWindow *window)
{
    QList<Instance> result;
    auto tabs = window->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"));
    for (int tab = 0; tabs && tab < tabs->count(); ++tab) {
        DolphinTabPage *page = tabs->tabPageAt(tab);
        if (!page) {
            continue;
        }
        result.append({page->primaryViewContainer(), tab, 0});
        if (page->splitViewEnabled() && page->secondaryViewContainer()) {
            result.append({page->secondaryViewContainer(), tab, 1});
        }
    }
    return result;
}

QJsonObject WhaleDevToolbars::globalState(DolphinMainWindow *window)
{
    // Not KMainWindow::toolBar(): that creates a toolbar of the name if there is none.
    auto toolBar = window->findChild<KToolBar *>(QStringLiteral("mainToolBar"), Qt::FindDirectChildrenOnly);
    if (!toolBar) {
        return {{QStringLiteral("visible"), false}, {QStringLiteral("layout"), QJsonArray()}, {QStringLiteral("actions"), QJsonArray()}};
    }
    return {
        {QStringLiteral("visible"), !toolBar->isHidden()},
        {QStringLiteral("layout"), layoutOf(toolBar)},
        {QStringLiteral("actions"), actionsState(toolBar, nullptr)},
    };
}

QJsonObject WhaleDevToolbars::fileViewState(DolphinViewContainer *container, const QString &ownerView)
{
    WhaleFileViewToolBar *toolBar = fileViewToolBar(container);
    QJsonObject state{
        {QStringLiteral("instanceId"), instanceId(container)},
        {QStringLiteral("ownerView"), ownerView},
    };
    if (!toolBar) {
        state.insert(QStringLiteral("visible"), false);
        return state;
    }
    // visible: not hidden ("Show File View Toolbars"); shown: on screen now.
    state.insert(QStringLiteral("visible"), !toolBar->isHidden());
    state.insert(QStringLiteral("shown"), toolBar->isVisible());
    state.insert(QStringLiteral("locationBar"), locationBarState(container));
    // Its actions exist once it has been shown (lazy); the automation never creates them.
    WhaleViewCommandScope *scope = toolBar->scope();
    state.insert(QStringLiteral("initialized"), scope != nullptr);
    if (scope) {
        state.insert(QStringLiteral("layout"), layoutOf(toolBar));
        state.insert(QStringLiteral("actions"), actionsState(toolBar, scope));
    } else {
        WhaleToolbarManager *manager = WhaleToolbarManager::of(container);
        state.insert(QStringLiteral("layout"), manager ? QJsonArray::fromStringList(manager->fileViewLayout().toStringList()) : QJsonArray());
        state.insert(QStringLiteral("actions"), QJsonArray());
    }
    return state;
}

QJsonArray WhaleDevToolbars::instancesState(DolphinMainWindow *window)
{
    QJsonArray result;
    const auto all = instances(window);
    for (const Instance &instance : all) {
        WhaleFileViewToolBar *toolBar = fileViewToolBar(instance.container);
        result.append(QJsonObject{
            {QStringLiteral("instanceId"), instanceId(instance.container)},
            {QStringLiteral("tab"), instance.tab},
            {QStringLiteral("pane"), instance.pane},
            {QStringLiteral("visible"), toolBar && !toolBar->isHidden()},
            {QStringLiteral("initialized"), toolBar && toolBar->scope()},
            {QStringLiteral("locationBar"), locationBarSummary(instance.container)},
        });
    }
    return result;
}

QJsonObject WhaleDevToolbars::dialogState(DolphinMainWindow *window)
{
    KEditToolBar *dialog = openDialog(window);
    if (!dialog || !dialog->isVisible()) {
        return {{QStringLiteral("open"), false}};
    }
    auto combo = dialog->findChild<QComboBox *>();
    QJsonArray toolBars;
    for (int i = 0; combo && i < combo->count(); ++i) {
        toolBars.append(descriptorIdOfName(window, combo->itemText(i)));
    }
    QJsonObject buttons;
    for (const auto &[name, which] : {std::pair{QStringLiteral("ok"), QDialogButtonBox::Ok},
                                      std::pair{QStringLiteral("apply"), QDialogButtonBox::Apply},
                                      std::pair{QStringLiteral("cancel"), QDialogButtonBox::Cancel},
                                      std::pair{QStringLiteral("defaults"), QDialogButtonBox::RestoreDefaults}}) {
        QPushButton *button = dialogButton(dialog, which);
        buttons.insert(name, button && button->isEnabled());
    }
    return {
        {QStringLiteral("open"), true},
        {QStringLiteral("selected"), combo ? descriptorIdOfName(window, combo->currentText()) : QString()},
        {QStringLiteral("toolBars"), toolBars},
        {QStringLiteral("available"), dialogItems(availableList(dialog))},
        {QStringLiteral("current"), dialogItems(currentList(dialog))},
        // Apply is enabled while there are changes that are not applied.
        {QStringLiteral("modified"), buttons.value(QStringLiteral("apply")).toBool()},
        {QStringLiteral("buttons"), buttons},
    };
}

QJsonObject WhaleDevToolbars::click(KToolBar *toolBar, QAction *action, const QString &menuEntryPath, const QString &disabledReason)
{
    auto button = qobject_cast<QToolButton *>(toolBar->widgetForAction(action));
    if (!button) {
        return failure(QStringLiteral("\"%1\" is a widget in the toolbar, not a button").arg(action->objectName()));
    }
    if (!toolBar->isVisible()) {
        return failure(QStringLiteral("the toolbar of \"%1\" is not shown (hidden, or not in the current tab)").arg(action->objectName()));
    }
    if (!button->isVisible()) {
        return failure(QStringLiteral("the button \"%1\" is not shown: the toolbar has no room for it (its overflow menu)").arg(action->objectName()));
    }
    if (!action->isEnabled()) {
        return failure(QStringLiteral("the action \"%1\" is disabled").arg(action->objectName())
                       + (disabledReason.isEmpty() ? QString() : QStringLiteral(": ") + disabledReason));
    }

    if (menuEntryPath.isEmpty()) {
        if (buttonMenu(button, action) && button->popupMode() == QToolButton::InstantPopup) {
            return failure(QStringLiteral("\"%1\" only opens a menu; choose an entry (menuEntry)").arg(action->objectName()));
        }
        const QPoint center = button->rect().center();
        sendMouse(button, QEvent::MouseButtonPress, center);
        sendMouse(button, QEvent::MouseButtonRelease, center);
        return ok();
    }

    QMenu *menu = buttonMenu(button, action);
    if (!menu) {
        return failure(QStringLiteral("\"%1\" has no menu").arg(action->objectName()));
    }
    // The menu opens in an event loop of its own (QMenu::exec()); the entry is chosen in it,
    // once the menu is shown and filled (history menus fill themselves when they open).
    auto result = std::make_shared<QJsonObject>();
    auto chosen = std::make_shared<bool>(false);
    const QMetaObject::Connection connection = QObject::connect(menu, &QMenu::aboutToShow, menu, [menu, menuEntryPath, result, chosen]() {
        QTimer::singleShot(0, menu, [menu, menuEntryPath, result, chosen]() {
            if (!menu->isVisible()) {
                return;
            }
            *result = chooseEntry(menu, menuEntryPath);
            *chosen = true;
        });
    });
    const QPoint position = menuPressPosition(button);
    sendMouse(button, QEvent::MouseButtonPress, position);
    // An instant popup and the menu button have come back with the menu closed; a delayed
    // popup opens while the button is held.
    const bool done = waitFor([chosen]() {
        return *chosen;
    });
    sendMouse(button, QEvent::MouseButtonRelease, position);
    QObject::disconnect(connection);
    if (!done) {
        menu->hide();
        return failure(QStringLiteral("the menu of \"%1\" did not open").arg(action->objectName()));
    }
    return *result;
}

QJsonObject WhaleDevToolbars::configure(DolphinMainWindow *window, const QJsonObject &request)
{
    WhaleToolbarConfigurator *configurator = WhaleToolbarConfigurator::of(window);
    if (!configurator) {
        return failure(QStringLiteral("the window has no Configure Toolbars"));
    }
    const QString select = request.value(QStringLiteral("select")).toString();
    if (!select.isEmpty() && select != WhaleToolbarDescriptor::globalId() && select != WhaleToolbarDescriptor::fileViewId()) {
        return failure(QStringLiteral("unknown toolbar \"%1\" (global or file-view)").arg(select));
    }
    if (request.value(QStringLiteral("open")).toBool()) {
        // As the window's action and the File View Toolbar's context menu open it.
        configurator->configure(select.isEmpty() ? WhaleToolbarDescriptor::globalId() : select);
    }
    KEditToolBar *dialog = openDialog(window);
    if (!dialog || !dialog->isVisible()) {
        return failure(QStringLiteral("Configure Toolbars is not open (open: true)"));
    }
    QString error;
    if (!select.isEmpty()) {
        error = selectInDialog(window, dialog, select);
    }
    const QStringList removals = idsOf(request.value(QStringLiteral("remove")));
    for (int i = 0; error.isEmpty() && i < removals.count(); ++i) {
        error = removeInDialog(dialog, removals.at(i));
    }
    const QStringList insertions = idsOf(request.value(QStringLiteral("insert")));
    for (int i = 0; error.isEmpty() && i < insertions.count(); ++i) {
        error = insertInDialog(dialog, insertions.at(i));
    }
    if (error.isEmpty() && request.value(QStringLiteral("defaults")).toBool()) {
        // KDE's own Defaults: its question, answered with "Reset".
        auto answered = std::make_shared<bool>(false);
        confirmDefaultsQuestion(dialog, answered);
        dialogButton(dialog, QDialogButtonBox::RestoreDefaults)->click();
        if (!*answered) {
            error = QStringLiteral("KDE's question of Defaults did not show up");
        }
    }
    const auto press = [dialog](QDialogButtonBox::StandardButton which, const QString &name) {
        QPushButton *button = dialogButton(dialog, which);
        if (!button || !button->isEnabled()) {
            return QStringLiteral("the button %1 is not enabled (nothing to apply?)").arg(name);
        }
        button->click();
        return QString();
    };
    if (error.isEmpty() && request.value(QStringLiteral("apply")).toBool()) {
        error = press(QDialogButtonBox::Apply, QStringLiteral("Apply"));
    }
    if (error.isEmpty() && request.value(QStringLiteral("ok")).toBool()) {
        error = press(QDialogButtonBox::Ok, QStringLiteral("OK"));
    }
    if (error.isEmpty() && request.value(QStringLiteral("cancel")).toBool()) {
        error = press(QDialogButtonBox::Cancel, QStringLiteral("Cancel"));
    }
    if (!error.isEmpty()) {
        QJsonObject result = failure(error);
        result.insert(QStringLiteral("dialog"), dialogState(window));
        return result;
    }
    QJsonObject result = ok();
    result.insert(QStringLiteral("dialog"), dialogState(window));
    return result;
}

bool WhaleDevToolbars::changesFiles(const QString &name)
{
    // The actions behind the automation's file operation IDs, and those that create items.
    static const QStringList names = {QStringLiteral("edit_copy"),
                                      QStringLiteral("edit_cut"),
                                      QStringLiteral("edit_paste"),
                                      QStringLiteral("movetotrash"),
                                      QStringLiteral("deletefile"),
                                      QStringLiteral("duplicate"),
                                      QStringLiteral("copy_location"),
                                      QStringLiteral("copy_to_inactive_split_view"),
                                      QStringLiteral("move_to_inactive_split_view"),
                                      QStringLiteral("create_dir"),
                                      QStringLiteral("create_file"),
                                      QStringLiteral("new_menu")};
    return names.contains(name);
}

QJsonObject WhaleDevToolbars::locationBarState(DolphinViewContainer *container)
{
    QJsonObject state = locationBarSummary(container);
    WhaleLocationBar *locationBar = locationBarOf(container);
    if (!locationBar) {
        return state;
    }
    DolphinUrlNavigator *navigator = locationBar->urlNavigator();
    QLineEdit *editor = navigator->editor() ? navigator->editor()->lineEdit() : nullptr;
    const bool editable = navigator->isUrlEditable();
    // The window's focus widget, as focus.role: also while the window is not active.
    QWidget *focus = container->window()->focusWidget();
    state.insert(QStringLiteral("visible"), locationBar->isVisible());
    state.insert(QStringLiteral("editable"), editable);
    state.insert(QStringLiteral("url"), navigator->locationUrl().toString());
    state.insert(QStringLiteral("text"), editable && editor ? editor->text() : QString());
    state.insert(QStringLiteral("placeholder"), editor ? editor->placeholderText() : QString());
    state.insert(QStringLiteral("focused"), focus && (focus == navigator || navigator->isAncestorOf(focus)));
    state.insert(QStringLiteral("active"), container->isActive());
    return state;
}

QJsonObject WhaleDevToolbars::locationBarInput(DolphinViewContainer *container, const QString &text, bool submit)
{
    WhaleLocationBar *locationBar = locationBarOf(container);
    if (!locationBar) {
        return failure(QStringLiteral("the view has no location bar"));
    }
    DolphinUrlNavigator *navigator = locationBar->urlNavigator();
    if (container->urlNavigator() != navigator) {
        return failure(QStringLiteral("the location bar is not connected to its view"));
    }
    if (!isPlaced(container, locationBar)) {
        return failure(QStringLiteral("the location bar is not in the File View Toolbar (removed in Configure Toolbars, or the toolbar never shown)"));
    }
    if (!locationBar->isVisible()) {
        return failure(QStringLiteral("the location bar is not shown (File View Toolbars hidden, not in the current tab, or in the overflow menu)"));
    }
    if (!locationBar->isEnabled()) {
        return failure(QStringLiteral("the location bar is disabled"));
    }
    if (!navigator->isUrlEditable()) {
        // As the user: the toggle button right of the breadcrumb.
        QAbstractButton *toggle = nullptr;
        const auto buttons = navigator->findChildren<QAbstractButton *>();
        for (QAbstractButton *button : buttons) {
            if (button->inherits("KDEPrivate::KUrlNavigatorToggleButton") && button->isVisible()) {
                toggle = button;
            }
        }
        if (!toggle) {
            return failure(QStringLiteral("the location bar shows no button to edit the location"));
        }
        // A click into an inactive window activates it (the window manager does that; without
        // one, offscreen, the window stays inactive after a dialog has closed).
        if (!toggle->window()->isActiveWindow()) {
            toggle->window()->activateWindow();
            QCoreApplication::processEvents();
        }
        const QPoint center = toggle->rect().center();
        sendMouse(toggle, QEvent::MouseButtonPress, center);
        sendMouse(toggle, QEvent::MouseButtonRelease, center);
        if (!waitFor([navigator]() {
                return navigator->isUrlEditable();
            })) {
            return failure(QStringLiteral("the location bar did not become editable"));
        }
    }
    QLineEdit *editor = navigator->editor() ? navigator->editor()->lineEdit() : nullptr;
    if (!editor || !editor->isVisible()) {
        return failure(QStringLiteral("the location bar has no editor"));
    }
    // Typed over the whole text: Ctrl+A, then the text key by key (KDE's completion follows
    // it as for the user), then Return.
    auto key = [editor](int code, Qt::KeyboardModifiers modifiers, const QString &keyText) {
        QKeyEvent press(QEvent::KeyPress, code, modifiers, keyText);
        QCoreApplication::sendEvent(editor, &press);
        QKeyEvent release(QEvent::KeyRelease, code, modifiers, keyText);
        QCoreApplication::sendEvent(editor, &release);
    };
    editor->setFocus(Qt::OtherFocusReason);
    key(Qt::Key_A, Qt::ControlModifier, QString());
    if (text.isEmpty()) {
        key(Qt::Key_Delete, Qt::NoModifier, QString());
    } else {
        for (const QChar character : text) {
            key(Qt::Key_unknown, Qt::NoModifier, QString(character));
        }
    }
    // KDE's completion may have added to it (e.g. the "/" of a folder), as for the user.
    if (!editor->text().startsWith(text)) {
        return failure(QStringLiteral("the editor shows \"%1\" instead of the text").arg(editor->text()));
    }
    QJsonObject result = ok();
    result.insert(QStringLiteral("text"), editor->text());
    if (submit) {
        key(Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
    }
    return result;
}
