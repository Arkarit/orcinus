/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whalestylemanager.h"

#include "dolphinmainwindow.h"
#include "whalestyleelement.h"
#include "whalestyleelements.h"
#include "whalestyles.h"

#include <KActionCategory>
#include <KActionCollection>
#include <KActionMenu>
#include <KConfigGroup>
#include <KLocalizedString>
#include <KMessageBox>
#include <KXMLGUIFactory>

#include <QDomDocument>
#include <QInputDialog>
#include <QMenu>

namespace
{
const QString ActionPrefix = QStringLiteral("whale_style_");
// The style a window applied or saved last; new windows start from it (E3).
const QString StateGroup = QStringLiteral("Styles");
const QString LastStyleKey = QStringLiteral("LastStyle");

// The ID of a style whose name is @p name, ignoring case and spaces around it.
QString idOfName(const QString &name)
{
    const QString wanted = name.trimmed();
    const QList<WhaleStyle> styles = WhaleStyles::self()->store().styles();
    for (const WhaleStyle &style : styles) {
        if (style.name.compare(wanted, Qt::CaseInsensitive) == 0) {
            return style.id;
        }
    }
    return QString();
}
}

WhaleStyleManager *WhaleStyleManager::ensure(DolphinMainWindow *window)
{
    if (auto manager = window->findChild<WhaleStyleManager *>(QString(), Qt::FindDirectChildrenOnly)) {
        return manager;
    }
    return new WhaleStyleManager(window);
}

QString WhaleStyleManager::actionName(const QString &id)
{
    return ActionPrefix + id;
}

QString WhaleStyleManager::menuActionName()
{
    return QStringLiteral("whale_styles");
}

QString WhaleStyleManager::saveActionName()
{
    return QStringLiteral("whale_save_style");
}

QString WhaleStyleManager::saveLastActionName()
{
    return QStringLiteral("whale_save_last_style");
}

WhaleStyleManager::WhaleStyleManager(DolphinMainWindow *window)
    : QObject(window)
    , m_window(window)
    , m_lastStyleId(KSharedConfig::openStateConfig()->group(StateGroup).readEntry(LastStyleKey, QString()))
{
}

WhaleStyleManager::~WhaleStyleManager() = default;

void WhaleStyleManager::setupActions()
{
    if (m_menu) {
        return;
    }
    KActionCollection *collection = m_window->actionCollection();

    m_menu = new KActionMenu(QIcon::fromTheme(QStringLiteral("preferences-desktop-theme-global")), i18nc("@action:inmenu View", "Styles"), m_window);
    m_menu->setPopupMode(QToolButton::InstantPopup);
    m_menu->setWhatsThis(i18nc("@info:whatsthis",
                               "A style switches the whole look of the window at once: one or two views, their view "
                               "modes, toolbars, panels and the status bar. Give a style a shortcut in "
                               "<interface>Configure Keyboard Shortcuts</interface>."));
    collection->addAction(menuActionName(), m_menu);
    // Another process may have changed the styles.
    connect(m_menu->menu(), &QMenu::aboutToShow, WhaleStyles::self(), &WhaleStyles::reload);

    m_saveAction = collection->addAction(saveActionName());
    m_saveAction->setText(i18nc("@action:inmenu Styles", "Save as Style…"));
    m_saveAction->setIcon(QIcon::fromTheme(QStringLiteral("document-save-as")));
    connect(m_saveAction, &QAction::triggered, this, &WhaleStyleManager::askAndSaveCurrentAsStyle);

    m_saveLastAction = collection->addAction(saveLastActionName());
    m_saveLastAction->setIcon(QIcon::fromTheme(QStringLiteral("document-save")));
    connect(m_saveLastAction, &QAction::triggered, this, &WhaleStyleManager::saveLastStyle);
    updateSaveLastAction();

    m_category = new KActionCategory(i18nc("@title:group of shortcuts", "Styles"), collection);
    m_menu->addAction(m_saveLastAction);
    m_menu->addAction(m_saveAction);
}

void WhaleStyleManager::plugStyles()
{
    if (m_plugged) {
        return;
    }
    setupActions();
    m_plugged = true;
    connect(WhaleStyles::self(), &WhaleStyles::changed, this, &WhaleStyleManager::rebuildStyleActions);
    rebuildStyleActions();
}

bool WhaleStyleManager::applyStyle(const QString &id)
{
    const WhaleStyle *style = WhaleStyles::self()->store().style(id);
    if (!style) {
        return false;
    }
    // A copy: applying may let the window reload the styles.
    applyStyle(WhaleStyle(*style));
    return true;
}

void WhaleStyleManager::applyStyle(const WhaleStyle &style)
{
    const QList<const WhaleStyleElement *> elements = WhaleStyleElements::self().elements();
    for (const WhaleStyleElement *element : elements) {
        const auto it = style.elements.constFind(element->key());
        WhaleStyleElementState state;
        if (it != style.elements.cend()) {
            state = *it;
        } else {
            state.value = element->valueIfMissing();
            state.enabled = !state.value.isEmpty();
        }
        if (state.enabled && element->isAvailable(m_window)) {
            element->apply(m_window, state.value);
        }
    }
    setLastStyle(style.id);
}

QString WhaleStyleManager::lastStyleId() const
{
    return m_lastStyleId;
}

bool WhaleStyleManager::saveLastStyle()
{
    WhaleStyles *styles = WhaleStyles::self();
    styles->reload();
    const WhaleStyle *style = styles->store().style(m_lastStyleId);
    if (!style) {
        return false;
    }
    styles->editableStore().updateStyle(recaptureStyle(*style));
    styles->save();
    return true;
}

void WhaleStyleManager::setLastStyle(const QString &id)
{
    if (id.isEmpty()) {
        return;
    }
    m_lastStyleId = id;
    KConfigGroup state = KSharedConfig::openStateConfig()->group(StateGroup);
    state.writeEntry(LastStyleKey, id);
    state.sync();
    updateSaveLastAction();
}

void WhaleStyleManager::updateSaveLastAction()
{
    if (!m_saveLastAction) {
        return;
    }
    const WhaleStyle *style = WhaleStyles::self()->store().style(m_lastStyleId);
    if (style) {
        m_saveLastAction->setText(
            i18nc("@action:inmenu Styles, %1 the style applied last", "Save Style \"%1\"", QString(style->name).replace(u'&', QLatin1String("&&"))));
        m_saveLastAction->setToolTip(i18nc("@info:tooltip", "Save the current state of the window as the style \"%1\"", style->name));
    } else {
        m_saveLastAction->setText(i18nc("@action:inmenu Styles", "Save Style"));
        m_saveLastAction->setToolTip(i18nc("@info:tooltip", "Apply a style first; this saves the current state of the window as that style"));
    }
    m_saveLastAction->setEnabled(style);
}

WhaleStyle WhaleStyleManager::captureStyle() const
{
    return recaptureStyle(WhaleStyle());
}

WhaleStyle WhaleStyleManager::recaptureStyle(const WhaleStyle &style) const
{
    WhaleStyle result = style;
    const QList<const WhaleStyleElement *> elements = WhaleStyleElements::self().elements();
    for (const WhaleStyleElement *element : elements) {
        const bool available = element->isAvailable(m_window);
        auto it = result.elements.find(element->key());
        if (it == result.elements.end()) {
            if (available) {
                result.elements.insert(element->key(), {true, element->capture(m_window)});
            }
        } else if (available) {
            // Not available (e.g. the style of a view with a style for each folder): left as it was.
            it->value = element->capture(m_window);
        }
    }
    return result;
}

QString WhaleStyleManager::saveCurrentAsStyle(const QString &name)
{
    if (name.trimmed().isEmpty()) {
        return QString();
    }
    WhaleStyles *styles = WhaleStyles::self();
    styles->reload();
    WhaleStyleStore &store = styles->editableStore();

    QString id = idOfName(name);
    if (id.isEmpty()) {
        WhaleStyle style = captureStyle();
        style.name = name;
        id = store.addStyle(style);
    } else {
        store.updateStyle(recaptureStyle(*store.style(id)));
    }
    styles->save();
    setLastStyle(id);
    return id;
}

void WhaleStyleManager::takeShortcuts()
{
    WhaleStyles *styles = WhaleStyles::self();
    WhaleStyleStore &store = styles->editableStore();
    bool changed = false;
    const QList<WhaleStyle> all = store.styles();
    for (WhaleStyle style : all) {
        const QAction *action = m_window->actionCollection()->action(actionName(style.id));
        if (action && action->shortcut() != style.shortcut) {
            style.shortcut = action->shortcut();
            store.updateStyle(style);
            changed = true;
        }
    }
    removeSavedActionProperties();
    if (changed) {
        styles->save();
    }
}

void WhaleStyleManager::rebuildStyleActions()
{
    KActionCollection *collection = m_window->actionCollection();
    for (const QString &name : std::as_const(m_styleActionNames)) {
        // Deletes the action, which also leaves the menu.
        collection->removeAction(collection->action(name));
    }
    m_styleActionNames.clear();
    m_menu->menu()->clear();

    const QList<WhaleStyle> styles = WhaleStyles::self()->store().styles();
    for (const WhaleStyle &style : styles) {
        QAction *action = m_category->addAction(actionName(style.id));
        action->setText(QString(style.name).replace(u'&', QLatin1String("&&")));
        action->setToolTip(i18nc("@info:tooltip", "Apply the style \"%1\" to the window", style.name));
        // No default shortcut: the style's one is the user's choice (orcinusstylesrc).
        action->setShortcut(style.shortcut);
        const QString id = style.id;
        connect(action, &QAction::triggered, this, [this, id]() {
            applyStyle(id);
        });
        m_menu->addAction(action);
        m_styleActionNames.append(action->objectName());
    }
    if (!styles.isEmpty()) {
        m_menu->menu()->addSeparator();
    }
    // Renamed or deleted meanwhile.
    updateSaveLastAction();
    m_menu->addAction(m_saveLastAction);
    m_menu->addAction(m_saveAction);
}

void WhaleStyleManager::askAndSaveCurrentAsStyle()
{
    bool accepted = false;
    const QString name = QInputDialog::getText(m_window,
                                               i18nc("@title:window", "Save as Style"),
                                               i18nc("@label:textbox", "Name of the style:"),
                                               QLineEdit::Normal,
                                               QString(),
                                               &accepted)
                             .trimmed();
    if (!accepted || name.isEmpty()) {
        return;
    }
    const QString existing = idOfName(name);
    if (!existing.isEmpty()) {
        const QString shownName = WhaleStyles::self()->store().style(existing)->name;
        const auto answer = KMessageBox::questionTwoActions(
            m_window,
            i18nc("@info", "There is already a style named \"%1\". Replace it with the current state of the window?", shownName),
            i18nc("@title:window", "Replace Style"),
            KGuiItem(i18nc("@action:button", "Replace"), QStringLiteral("document-save-as")),
            KStandardGuiItem::cancel());
        if (answer != KMessageBox::PrimaryAction) {
            return;
        }
    }
    saveCurrentAsStyle(name);
}

void WhaleStyleManager::removeSavedActionProperties()
{
    // As ServiceMenuShortcutManager::cleanupStaleShortcuts(): XML GUI saves the shortcuts of all
    // actions of the collection, also of these, but would never give them back to an action
    // created after setupGUI(). Kept, they would only go stale.
    const QString file = m_window->localXMLFile();
    if (file.isEmpty()) {
        return;
    }
    const QString xml = KXMLGUIFactory::readConfigFile(file, m_window->componentName());
    if (xml.isEmpty()) {
        return;
    }
    QDomDocument document;
    document.setContent(xml);
    QDomElement properties = KXMLGUIFactory::actionPropertiesElement(document);
    bool modified = false;
    for (QDomElement element = properties.firstChildElement(); !element.isNull();) {
        const QDomElement next = element.nextSiblingElement();
        if (element.attribute(QStringLiteral("name")).startsWith(ActionPrefix)) {
            properties.removeChild(element);
            modified = true;
        }
        element = next;
    }
    if (modified) {
        KXMLGUIFactory::saveConfigFile(document, file, m_window->componentName());
    }
}

#include "moc_whalestylemanager.cpp"
