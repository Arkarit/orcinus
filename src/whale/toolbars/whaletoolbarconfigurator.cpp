/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaletoolbarconfigurator.h"

#include "whalefileviewtoolbarclient.h"
#include "whalelocationbar.h"
#include "whaletoolbarmanager.h"
#include "whaleviewactions.h"
#include "whaleviewcommandscope.h"

#include <KActionCollection>
#include <KConfigGroup>
#include <KEditToolBar>
#include <KLocalizedString>
#include <KSharedConfig>
#include <KStandardAction>
#include <KToolBar>
#include <KXMLGUIBuilder>
#include <KXMLGUIFactory>
#include <KXmlGuiWindow>

#include <QComboBox>
#include <QDomDocument>

namespace
{
/**
 * Builds the <ToolBar> of the File View Toolbar's file as a hidden toolbar outside of the
 * window: the factory wants a container for it, the file views have toolbars of their own.
 */
class TemplateBuilder : public KXMLGUIBuilder
{
public:
    TemplateBuilder()
        : KXMLGUIBuilder(nullptr)
    {
    }

    ~TemplateBuilder() override
    {
        // Only if the factory went away without removing the client (the window is destroyed).
        delete m_toolBar;
    }

    QStringList containerTags() const override
    {
        return {QStringLiteral("toolbar")};
    }

    QWidget *createContainer(QWidget *, int, const QDomElement &element, QAction *&containerAction) override
    {
        containerAction = nullptr;
        delete m_toolBar;
        m_toolBar = new KToolBar(nullptr, false, false);
        m_toolBar->setObjectName(element.attribute(QStringLiteral("name")));
        m_toolBar->hide();
        return m_toolBar;
    }

    void removeContainer(QWidget *container, QWidget *, QDomElement &, QAction *) override
    {
        delete container;
    }

private:
    QPointer<KToolBar> m_toolBar;
};
}

/**
 * The File View Toolbar's file as a client of the window's XML GUI factory, with the
 * prototype actions. Exists while Configure Toolbars is open.
 */
class WhaleFileViewToolbarEditClient : public WhaleFileViewToolbarClient
{
public:
    explicit WhaleFileViewToolbarEditClient(const KXmlGuiWindow *window)
        : m_window(window)
    {
        setClientBuilder(&m_builder);
        resetPrototypes();
    }

    ~WhaleFileViewToolbarEditClient() override
    {
        setClientBuilder(nullptr);
    }

    /**
     * The actions a File View Toolbar can show, as the window's actions of the same names
     * describe them. Again after the factory changed them (the user's texts and icons of
     * the file, which it applies when the client is added).
     */
    void resetPrototypes()
    {
        const QStringList names = WhaleViewCommandScope::allowedActions();
        for (const QString &name : names) {
            const QAction *source = m_window->actionCollection()->action(name);
            if (!source) {
                continue;
            }
            QAction *prototype = actionCollection()->action(name);
            if (!prototype) {
                prototype = actionCollection()->addAction(name);
                KActionCollection::setShortcutsConfigurable(prototype, false);
            }
            prototype->setText(source->text());
            prototype->setIconText(source->iconText());
            prototype->setToolTip(source->toolTip());
            prototype->setIcon(source->icon());
            prototype->setPriority(source->priority());
            prototype->setCheckable(source->isCheckable());
        }
        // The location bar of the view: no action of the window describes it.
        QAction *locationBar = actionCollection()->action(WhaleLocationBar::actionName());
        if (!locationBar) {
            locationBar = actionCollection()->addAction(WhaleLocationBar::actionName());
            KActionCollection::setShortcutsConfigurable(locationBar, false);
        }
        locationBar->setText(i18nc("@action:intoolbar", "Location Bar"));
        locationBar->setIconText(QString());
        locationBar->setToolTip(QString());
        locationBar->setIcon(QIcon::fromTheme(QStringLiteral("edit-select-text")));
    }

private:
    const KXmlGuiWindow *m_window;
    TemplateBuilder m_builder;
};

WhaleToolbarConfigurator::WhaleToolbarConfigurator(KXmlGuiWindow *window, const std::function<void()> &saveNewToolbarConfig)
    : QObject(window)
    , m_window(window)
    , m_saveNewToolbarConfig(saveNewToolbarConfig)
{
    connect(window->guiFactory(), &KXMLGUIFactory::clientAdded, this, &WhaleToolbarConfigurator::slotClientAdded);
    connect(window->guiFactory(), &KXMLGUIFactory::clientRemoved, this, &WhaleToolbarConfigurator::slotClientRemoved);
}

WhaleToolbarConfigurator::~WhaleToolbarConfigurator()
{
    // With the dialog open the window is being destroyed: its factory is gone and has let go
    // of the client. Nothing to remove; the client and its hidden toolbar go with this.
}

WhaleToolbarConfigurator *WhaleToolbarConfigurator::install(KXmlGuiWindow *window, const std::function<void()> &saveNewToolbarConfig)
{
    Q_ASSERT(window);
    if (WhaleToolbarConfigurator *configurator = of(window)) {
        return configurator;
    }
    auto configurator = new WhaleToolbarConfigurator(window, saveNewToolbarConfig);
    // The window's action opens this dialog instead of KXmlGuiWindow::configureToolbars().
    // The context menu of the main toolbar uses the same action.
    if (QAction *action = window->actionCollection()->action(KStandardAction::name(KStandardAction::ConfigureToolbars))) {
        disconnect(action, &QAction::triggered, window, nullptr);
        connect(action, &QAction::triggered, configurator, [configurator]() {
            configurator->configure(WhaleToolbarDescriptor::globalId());
        });
    }
    return configurator;
}

WhaleToolbarConfigurator *WhaleToolbarConfigurator::of(const QWidget *widget)
{
    if (!widget) {
        return nullptr;
    }
    return widget->window()->findChild<WhaleToolbarConfigurator *>(QString(), Qt::FindDirectChildrenOnly);
}

KEditToolBar *WhaleToolbarConfigurator::dialog() const
{
    return m_dialog;
}

void WhaleToolbarConfigurator::configure(const QString &descriptorId)
{
    if (m_dialog) {
        m_dialog->raise();
        m_dialog->activateWindow();
        return;
    }

    // As KXmlGuiWindow::configureToolbars().
    KConfigGroup group(KSharedConfig::openConfig(), QString());
    m_window->saveMainWindowSettings(group);

    m_editClient = std::make_unique<WhaleFileViewToolbarEditClient>(m_window);
    m_window->guiFactory()->addClient(m_editClient.get());

    // Find Action lists the actions of all clients of the factory: not the prototypes.
    m_findAction = m_window->actionCollection()->action(QStringLiteral("open_kcommand_bar"));
    if (m_findAction) {
        m_findActionWasEnabled = m_findAction->isEnabled();
        m_findAction->setEnabled(false);
    }

    m_dialog = new KEditToolBar(m_window->guiFactory(), m_window);
    m_dialog->setAttribute(Qt::WA_DeleteOnClose);
    m_dialog->setDefaultToolBar(WhaleToolbarManager::ensure(m_window)->descriptor(descriptorId).xmlGuiToolBar);
    connect(m_dialog, &KEditToolBar::newToolBarConfig, this, &WhaleToolbarConfigurator::slotNewToolBarConfig);
    // Not destroyed(): that also comes when the window is destroyed, after its factory.
    connect(m_dialog, &QDialog::finished, this, &WhaleToolbarConfigurator::slotFinished);
    m_dialog->show();
    polishToolBarNames();
}

void WhaleToolbarConfigurator::slotNewToolBarConfig()
{
    // The dialog has saved the changed files and built all clients again; the client of the
    // File View Toolbar has read its file.
    m_saveNewToolbarConfig();

    // The window has just removed and added itself, which puts it behind the other clients
    // of its factory. KEditToolBar takes the first client for the window (the one that gets
    // ui_standards.rc merged) the next time it saves or resets: the window first again.
    KXMLGUIFactory *factory = m_window->guiFactory();
    const auto clients = factory->clients();
    for (KXMLGUIClient *client : clients) {
        if (client == m_window) {
            break;
        }
        factory->removeClient(client);
        factory->addClient(client);
    }

    WhaleToolbarManager::reloadAllFileViewLayouts();
    // "Defaults" has created the widgets of the dialog again.
    polishToolBarNames();
}

void WhaleToolbarConfigurator::slotFinished()
{
    if (m_editClient) {
        m_window->guiFactory()->removeClient(m_editClient.get());
        m_editClient.reset();
    }
    if (m_findAction) {
        m_findAction->setEnabled(m_findActionWasEnabled);
    }
    if (m_dialog) {
        m_dialog->deleteLater();
        m_dialog = nullptr;
    }
}

void WhaleToolbarConfigurator::slotClientAdded(KXMLGUIClient *client)
{
    if (m_editClient && client == m_editClient.get()) {
        // Whatever the factory found for actions of these names (a shortcut scheme, the
        // file): a shortcut acts on the active view through the window's action.
        WhaleViewActions::clearShortcuts(m_editClient->actionCollection());
    }
}

void WhaleToolbarConfigurator::slotClientRemoved(KXMLGUIClient *client)
{
    if (m_editClient && client == m_editClient.get()) {
        // The factory never takes back the texts and icons it applied ("Defaults").
        m_editClient->resetPrototypes();
    }
}

void WhaleToolbarConfigurator::polishToolBarNames()
{
    if (!m_dialog) {
        return;
    }
    // Cosmetics: KEditToolBar appends " <name of the GUI>" to the toolbars' names, here
    // "Main Toolbar <dolphin>". It works with the indexes of the list, not with its texts; if
    // this stops matching, the suffixes are shown and nothing else changes.
    QStringList suffixes;
    const auto clients = m_window->guiFactory()->clients();
    for (KXMLGUIClient *client : clients) {
        suffixes.append(QLatin1String(" <") + client->domDocument().documentElement().attribute(QStringLiteral("name")) + QLatin1Char('>'));
    }
    const auto combos = m_dialog->findChildren<QComboBox *>();
    for (QComboBox *combo : combos) {
        for (int i = 0; i < combo->count(); ++i) {
            const QString text = combo->itemText(i);
            for (const QString &suffix : std::as_const(suffixes)) {
                if (text.endsWith(suffix) && text.size() > suffix.size()) {
                    combo->setItemText(i, text.chopped(suffix.size()));
                    break;
                }
            }
        }
    }
}

#include "moc_whaletoolbarconfigurator.cpp"
