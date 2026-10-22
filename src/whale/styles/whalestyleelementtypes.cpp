/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whalestyleelementtypes.h"

#include "dolphin_directoryviewpropertysettings.h"
#include "dolphin_generalsettings.h"
#include "dolphinmainwindow.h"
#include "dolphintabpage.h"
#include "dolphintabwidget.h"
#include "dolphinviewcontainer.h"
#include "filterbar/filterbar.h"
#include "settings/viewmodes/viewmodesettings.h"
#include "views/dolphinview.h"
#include "views/viewproperties.h"
#include "whalesplitlock.h"
#include "whalestyleelement.h"
#include "whalestyleelements.h"
#include "whaletoolbarsettings.h"
#include "whaleviewhost.h"
#include "whaleviewregistry.h"
#include "whaleviewservices.h"

#include <KActionCollection>
#include <KLocalizedString>
#include <KPluginMetaData>
#include <KStandardAction>
#include <KToolBar>

#include <QApplication>
#include <QBuffer>
#include <QDockWidget>
#include <QMenuBar>
#include <QPointer>

#include <array>

namespace
{
// ---- helpers ----

QString boolText(bool value)
{
    return value ? QStringLiteral("true") : QStringLiteral("false");
}

std::optional<bool> boolFrom(const QString &text)
{
    if (text == QLatin1String("true")) {
        return true;
    }
    if (text == QLatin1String("false")) {
        return false;
    }
    return std::nullopt;
}

// "name=true,name=false", in the order given.
QString switchesText(const QList<std::pair<QString, bool>> &switches)
{
    QStringList parts;
    for (const auto &[name, on] : switches) {
        parts.append(name + u'=' + boolText(on));
    }
    return parts.join(u',');
}

QList<std::pair<QString, bool>> switchesFrom(const QString &text)
{
    QList<std::pair<QString, bool>> result;
    const QStringList parts = text.split(u',', Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString name = part.section(u'=', 0, 0).trimmed();
        const std::optional<bool> on = boolFrom(part.section(u'=', 1).trimmed());
        if (!name.isEmpty() && on) {
            result.append({name, *on});
        }
    }
    return result;
}

DolphinTabPage *currentTabPage(const DolphinMainWindow *window)
{
    // The tab widget is not public; tests reach it the same way.
    const auto tabWidget = window->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"));
    return tabWidget ? tabWidget->currentTabPage() : nullptr;
}

// Lets the views and panels of @p window read the settings again, as the settings dialog does.
void refreshViews(DolphinMainWindow *window)
{
    const bool invoked = QMetaObject::invokeMethod(window, "refreshViews", Qt::DirectConnection);
    Q_ASSERT(invoked);
    Q_UNUSED(invoked)
}

QString modeName(DolphinView::Mode mode)
{
    switch (mode) {
    case DolphinView::IconsView:
        return i18nc("@item:intext view mode", "Icons");
    case DolphinView::CompactView:
        return i18nc("@item:intext view mode", "Compact");
    case DolphinView::DetailsView:
        return i18nc("@item:intext view mode", "Details");
    }
    return QString();
}

// ---- split ----

class SplitElement : public WhaleStyleElement
{
public:
    QString key() const override
    {
        return QStringLiteral("split");
    }
    QString title() const override
    {
        return i18nc("@item:inlistbox style element", "Split view");
    }
    QString capture(const DolphinMainWindow *window) const override
    {
        const DolphinTabPage *page = currentTabPage(window);
        return boolText(page && page->splitViewEnabled());
    }
    void apply(DolphinMainWindow *window, const QString &value) const override
    {
        DolphinTabPage *page = currentTabPage(window);
        const std::optional<bool> split = boolFrom(value);
        if (!page || !split || page->splitViewEnabled() == *split) {
            return;
        }
        if (*split) {
            // Activates the new right view, so the window updates its actions.
            page->setSplitViewEnabled(true, WithoutAnimation);
        } else {
            // The left view stays, whichever view is active.
            window->closeRightSplitView();
        }
        // Either way DolphinTabPage remembered the split view for new windows and tabs (E3).
    }
    QString describe(const QString &value) const override
    {
        const std::optional<bool> split = boolFrom(value);
        if (!split) {
            return QString();
        }
        return *split ? i18nc("@item:intext style element value", "Two views") : i18nc("@item:intext style element value", "One view");
    }
};

// ---- viewStyle.left, viewStyle.right ----

class ViewStyleElement : public WhaleStyleElement
{
public:
    explicit ViewStyleElement(DolphinView::PanelRole role)
        : m_role(role)
    {
    }
    QString key() const override
    {
        return m_role == DolphinView::PanelRole::Left ? QStringLiteral("viewStyle.left") : QStringLiteral("viewStyle.right");
    }
    QString title() const override
    {
        return m_role == DolphinView::PanelRole::Left ? i18nc("@item:inlistbox style element", "Style of the left view")
                                                      : i18nc("@item:inlistbox style element", "Style of the right view");
    }
    // With a style for each folder, the style belongs to the folder, not to the view (F1).
    bool isAvailable(const DolphinMainWindow *window) const override
    {
        Q_UNUSED(window)
        return GeneralSettings::globalViewProps();
    }
    QString capture(const DolphinMainWindow *window) const override
    {
        if (!isAvailable(window)) {
            return QString();
        }
        QByteArray data;
        if (const DolphinView *view = shownView(window)) {
            data = view->ownStyleData();
        }
        if (data.isEmpty()) {
            // Without a right view: what the right view starts from.
            data = ViewProperties(QUrl(), m_role).toData();
        }
        return withoutTimestamp(QString::fromUtf8(data));
    }
    void apply(DolphinMainWindow *window, const QString &value) const override
    {
        if (!isAvailable(window) || value.isEmpty()) {
            return;
        }
        const QByteArray data = value.toUtf8();
        // Written down for the position, so new windows, tabs and splits start from it (E3),
        ViewProperties stored(QUrl(), m_role);
        stored.setDirProperties(data);
        stored.save();
        // and shown by the view of the position in the current tab.
        if (DolphinView *view = shownView(window)) {
            view->setOwnStyleData(data);
        }
    }
    QString describe(const QString &value) const override
    {
        if (value.isEmpty()) {
            return QString();
        }
        auto buffer = std::make_shared<QBuffer>();
        buffer->setData(value.toUtf8());
        if (!buffer->open(QIODevice::ReadOnly)) {
            return QString();
        }
        ViewPropertySettings settings(std::make_unique<KConfig>(buffer, KConfig::SimpleConfig));
        QStringList parts{modeName(static_cast<DolphinView::Mode>(settings.viewMode()))};
        if (settings.previewsShown()) {
            parts.append(i18nc("@item:intext style element value", "previews"));
        }
        if (settings.hiddenFilesShown()) {
            parts.append(i18nc("@item:intext style element value", "hidden files"));
        }
        return parts.join(i18nc("@item:intext separator of a list", ", "));
    }

private:
    // The time of the last change is no part of a style; it would differ each time.
    static QString withoutTimestamp(const QString &data)
    {
        QStringList lines = data.split(u'\n');
        lines.removeIf([](const QString &line) {
            return line.startsWith(QLatin1String("Timestamp="));
        });
        return lines.join(u'\n');
    }

    DolphinView *shownView(const DolphinMainWindow *window) const
    {
        const DolphinTabPage *page = currentTabPage(window);
        if (!page) {
            return nullptr;
        }
        DolphinViewContainer *container = m_role == DolphinView::PanelRole::Left ? page->primaryViewContainer()
            : page->splitViewEnabled()                                           ? page->secondaryViewContainer()
                                                                                 : nullptr;
        return container ? container->view() : nullptr;
    }

    DolphinView::PanelRole m_role;
};

// ---- view.left, view.right ----

// LK7 (E9): the view shown on a side, the file list or a view plugin (e.g. the treemap). For the
// current tab only.
class ShownViewElement : public WhaleStyleElement
{
public:
    explicit ShownViewElement(DolphinView::PanelRole role)
        : m_role(role)
    {
    }
    QString key() const override
    {
        return m_role == DolphinView::PanelRole::Left ? QStringLiteral("view.left") : QStringLiteral("view.right");
    }
    QString title() const override
    {
        return m_role == DolphinView::PanelRole::Left ? i18nc("@item:inlistbox style element", "View on the left")
                                                      : i18nc("@item:inlistbox style element", "View on the right");
    }
    bool isAvailable(const DolphinMainWindow *window) const override
    {
        return host(window);
    }
    QString capture(const DolphinMainWindow *window) const override
    {
        const WhaleViewHost *shown = host(window);
        return shown ? shown->currentViewId() : QString();
    }
    void apply(DolphinMainWindow *window, const QString &value) const override
    {
        WhaleViewHost *shown = host(window);
        if (!shown || value.isEmpty() || shown->currentViewId() == value) {
            return;
        }
        // Not as the user's choice (selectView()): applying a style leaves the preferred views of
        // the folders alone.
        if (value != WhaleViewHost::dolphinViewId()) {
            const WhaleViewServices *services = WhaleViewServices::of(window);
            WhaleViewRegistry *registry = services ? services->registry() : nullptr;
            WhaleViewPlugin *plugin = registry && registry->pluginMetaData(value).isValid() ? registry->plugin(value) : nullptr;
            if (plugin && shown->showPluginView(value, plugin)) {
                return;
            }
        }
        // A missing plugin, or one that cannot show the location: the file list, quietly.
        shown->showDolphinView();
    }
    QString describe(const QString &value) const override
    {
        if (value.isEmpty()) {
            return QString();
        }
        if (value == WhaleViewHost::dolphinViewId()) {
            return i18nc("@item:intext style element value", "File list");
        }
        const KPluginMetaData metaData = KPluginMetaData::findPluginById(WhaleViewRegistry::pluginNamespace(), value);
        return metaData.isValid() ? metaData.name() : value;
    }

private:
    WhaleViewHost *host(const DolphinMainWindow *window) const
    {
        const DolphinTabPage *page = currentTabPage(window);
        if (!page) {
            return nullptr;
        }
        DolphinViewContainer *container = m_role == DolphinView::PanelRole::Left ? page->primaryViewContainer()
            : page->splitViewEnabled()                                           ? page->secondaryViewContainer()
                                                                                 : nullptr;
        return container ? container->viewHost() : nullptr;
    }

    DolphinView::PanelRole m_role;
};

// ---- splitLock ----

// LK7 (E8): the linked views of the split view, for the current tab only. Where they cannot be
// linked, they stay unlinked.
class SplitLockElement : public WhaleStyleElement
{
public:
    QString key() const override
    {
        return QStringLiteral("splitLock");
    }
    QString title() const override
    {
        return i18nc("@item:inlistbox style element", "Linked views");
    }
    QString capture(const DolphinMainWindow *window) const override
    {
        const WhaleSplitLock *lock = WhaleSplitLock::of(currentTabPage(window));
        return boolText(lock && lock->isLocked());
    }
    void apply(DolphinMainWindow *window, const QString &value) const override
    {
        WhaleSplitLock *lock = WhaleSplitLock::of(currentTabPage(window));
        const std::optional<bool> locked = boolFrom(value);
        if (lock && locked) {
            lock->setLocked(*locked);
        }
    }
    QString describe(const QString &value) const override
    {
        const std::optional<bool> locked = boolFrom(value);
        if (!locked) {
            return QString();
        }
        return *locked ? i18nc("@item:intext style element value", "Linked") : i18nc("@item:intext style element value", "Not linked");
    }
};

// ---- filterBar.left, filterBar.right ----

struct FilterModeName {
    KFileItemModelFilter::FilterMode mode;
    QLatin1String name;
};

constexpr std::array<FilterModeName, 3> filterModeNames{{
    {KFileItemModelFilter::PlainText, QLatin1String("PlainText")},
    {KFileItemModelFilter::Glob, QLatin1String("Glob")},
    {KFileItemModelFilter::Regex, QLatin1String("Regex")},
}};

struct FilterScopeName {
    KFileItemModelFilter::Scope scope;
    QLatin1String name;
};

constexpr std::array<FilterScopeName, 3> filterScopeNames{{
    {KFileItemModelFilter::Scope::Files, QLatin1String("Files")},
    {KFileItemModelFilter::Scope::Folders, QLatin1String("Folders")},
    {KFileItemModelFilter::Scope::FilesAndFolders, QLatin1String("FilesAndFolders")},
}};

// The state of a filter bar. As text: "visible=true,lock=true,case=false,mode=Glob,scope=Files,text=…";
// the text comes last and may contain anything.
struct FilterState {
    bool visible = false;
    bool locked = true;
    bool caseSensitive = false;
    KFileItemModelFilter::FilterMode mode = KFileItemModelFilter::Glob;
    KFileItemModelFilter::Scope scope = KFileItemModelFilter::Scope::Files;
    QString text;

    QString toText() const
    {
        QString modeName;
        for (const FilterModeName &entry : filterModeNames) {
            if (entry.mode == mode) {
                modeName = entry.name;
            }
        }
        QString scopeName;
        for (const FilterScopeName &entry : filterScopeNames) {
            if (entry.scope == scope) {
                scopeName = entry.name;
            }
        }
        return switchesText({{QStringLiteral("visible"), visible}, {QStringLiteral("lock"), locked}, {QStringLiteral("case"), caseSensitive}})
            + QLatin1String(",mode=") + modeName + QLatin1String(",scope=") + scopeName + QLatin1String(",text=") + text;
    }

    static std::optional<FilterState> fromText(const QString &value)
    {
        const qsizetype textAt = value.indexOf(QLatin1String(",text="));
        if (textAt < 0) {
            return std::nullopt;
        }
        FilterState state;
        state.text = value.mid(textAt + 6);
        const QStringList parts = value.left(textAt).split(u',');
        for (const QString &part : parts) {
            const QString key = part.section(u'=', 0, 0);
            const QString entry = part.section(u'=', 1);
            if (key == QLatin1String("visible") || key == QLatin1String("lock") || key == QLatin1String("case")) {
                const std::optional<bool> on = boolFrom(entry);
                if (!on) {
                    return std::nullopt;
                }
                (key == QLatin1String("visible") ? state.visible : key == QLatin1String("lock") ? state.locked : state.caseSensitive) = *on;
            } else if (key == QLatin1String("mode")) {
                for (const FilterModeName &name : filterModeNames) {
                    if (entry == name.name) {
                        state.mode = name.mode;
                    }
                }
            } else if (key == QLatin1String("scope")) {
                for (const FilterScopeName &name : filterScopeNames) {
                    if (entry == name.name) {
                        state.scope = name.scope;
                    }
                }
            }
        }
        // A hidden filter bar filters nothing.
        if (!state.visible) {
            state.text.clear();
        }
        return state;
    }
};

class FilterBarElement : public WhaleStyleElement
{
public:
    explicit FilterBarElement(DolphinView::PanelRole role)
        : m_role(role)
    {
    }
    QString key() const override
    {
        return m_role == DolphinView::PanelRole::Left ? QStringLiteral("filterBar.left") : QStringLiteral("filterBar.right");
    }
    QString title() const override
    {
        return m_role == DolphinView::PanelRole::Left ? i18nc("@item:inlistbox style element", "Filter of the left view")
                                                      : i18nc("@item:inlistbox style element", "Filter of the right view");
    }
    // The filter bar belongs to the view; there is none to set without a right view.
    bool isAvailable(const DolphinMainWindow *window) const override
    {
        return container(window);
    }
    QString capture(const DolphinMainWindow *window) const override
    {
        DolphinViewContainer *shown = container(window);
        const FilterBar *bar = shown ? shown->findChild<FilterBar *>() : nullptr;
        if (!bar) {
            return QString();
        }
        FilterState state;
        state.visible = shown->isFilterBarVisible();
        state.locked = bar->isLocked();
        state.caseSensitive = bar->isCaseSensitive();
        state.mode = bar->filterMode();
        state.scope = bar->filterScope();
        state.text = state.visible ? bar->text() : QString();
        return state.toText();
    }
    void apply(DolphinMainWindow *window, const QString &value) const override
    {
        DolphinViewContainer *shown = container(window);
        FilterBar *bar = shown ? shown->findChild<FilterBar *>() : nullptr;
        const std::optional<FilterState> state = FilterState::fromText(value);
        if (!bar || !state) {
            return;
        }
        bar->setFilterScope(state->scope);
        bar->setFilterMode(state->mode);
        bar->setCaseSensitive(state->caseSensitive);
        bar->setLocked(state->locked);
        if (shown->isFilterBarVisible() != state->visible) {
            // Showing the filter bar gives it the focus; a style leaves the focus where it was.
            QPointer<QWidget> focus = QApplication::focusWidget();
            shown->setFilterBarVisible(state->visible);
            if (focus) {
                focus->setFocus();
            }
        }
        if (state->visible) {
            bar->setText(state->text);
        }
    }
    QString describe(const QString &value) const override
    {
        const std::optional<FilterState> state = FilterState::fromText(value);
        if (!state) {
            return QString();
        }
        if (!state->visible) {
            return i18nc("@item:intext style element value", "Hidden");
        }
        QString scope;
        switch (state->scope) {
        case KFileItemModelFilter::Scope::Files:
            scope = i18nc("@item:intext the filter applies to", "files");
            break;
        case KFileItemModelFilter::Scope::Folders:
            scope = i18nc("@item:intext the filter applies to", "folders");
            break;
        case KFileItemModelFilter::Scope::FilesAndFolders:
            scope = i18nc("@item:intext the filter applies to", "files and folders");
            break;
        }
        return state->text.isEmpty() ? i18nc("@item:intext style element value, %1 what the filter applies to", "Shown, empty (%1)", scope)
                                     : i18nc("@item:intext style element value, %1 the filter, %2 what it applies to", "\"%1\" (%2)", state->text, scope);
    }

private:
    DolphinViewContainer *container(const DolphinMainWindow *window) const
    {
        const DolphinTabPage *page = currentTabPage(window);
        if (!page) {
            return nullptr;
        }
        return m_role == DolphinView::PanelRole::Left ? page->primaryViewContainer() : page->splitViewEnabled() ? page->secondaryViewContainer() : nullptr;
    }

    DolphinView::PanelRole m_role;
};

// ---- zoom ----

struct ZoomMode {
    DolphinView::Mode mode;
    QLatin1String name;
};

constexpr std::array<ZoomMode, 3> zoomModes{{
    {DolphinView::IconsView, QLatin1String("Icons")},
    {DolphinView::CompactView, QLatin1String("Compact")},
    {DolphinView::DetailsView, QLatin1String("Details")},
}};

// The icon and preview sizes of the modes in @p value; sizes that cannot be read are 0.
QList<std::pair<ZoomMode, std::pair<int, int>>> zoomSizesFrom(const QString &value)
{
    QList<std::pair<ZoomMode, std::pair<int, int>>> result;
    const QStringList parts = value.split(u';', Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString name = part.section(u'=', 0, 0).trimmed();
        const QStringList sizes = part.section(u'=', 1).split(u',');
        for (const ZoomMode &mode : zoomModes) {
            if (name == mode.name && sizes.size() == 2) {
                result.append({mode, {sizes.at(0).toInt(), sizes.at(1).toInt()}});
            }
        }
    }
    return result;
}

class ZoomElement : public WhaleStyleElement
{
public:
    QString key() const override
    {
        return QStringLiteral("zoom");
    }
    QString title() const override
    {
        return i18nc("@item:inlistbox style element", "Zoom");
    }
    QString capture(const DolphinMainWindow *window) const override
    {
        Q_UNUSED(window)
        QStringList parts;
        for (const ZoomMode &mode : zoomModes) {
            const ViewModeSettings settings(mode.mode);
            parts.append(mode.name + u'=' + QString::number(settings.iconSize()) + u',' + QString::number(settings.previewSize()));
        }
        return parts.join(u';');
    }
    // One zoom for each view mode, for all views (F6).
    void apply(DolphinMainWindow *window, const QString &value) const override
    {
        bool changed = false;
        const auto sizes = zoomSizesFrom(value);
        for (const auto &[mode, size] : sizes) {
            ViewModeSettings settings(mode.mode);
            bool modeChanged = false;
            if (size.first > 0 && settings.iconSize() != size.first) {
                settings.setIconSize(size.first);
                modeChanged = true;
            }
            if (size.second > 0 && settings.previewSize() != size.second) {
                settings.setPreviewSize(size.second);
                modeChanged = true;
            }
            if (modeChanged) {
                // The views read the settings from the file again.
                settings.save();
                changed = true;
            }
        }
        if (changed) {
            refreshViews(window);
        }
    }
    QString describe(const QString &value) const override
    {
        QStringList parts;
        const auto sizes = zoomSizesFrom(value);
        for (const auto &[mode, size] : sizes) {
            parts.append(i18nc("@item:intext view mode, icon size / preview size in pixels", "%1 %2/%3", modeName(mode.mode), size.first, size.second));
        }
        return parts.join(i18nc("@item:intext separator of a list", ", "));
    }
};

// ---- menuBar ----

class MenuBarElement : public WhaleStyleElement
{
public:
    QString key() const override
    {
        return QStringLiteral("menuBar");
    }
    QString title() const override
    {
        return i18nc("@item:inlistbox style element", "Menu bar");
    }
    QString capture(const DolphinMainWindow *window) const override
    {
        // The action rather than the menu bar, which follows it a moment later (queued).
        const QAction *action = window->actionCollection()->action(KStandardAction::name(KStandardAction::ShowMenubar));
        return boolText(action ? action->isChecked() : !window->menuBar()->isHidden());
    }
    void apply(DolphinMainWindow *window, const QString &value) const override
    {
        const std::optional<bool> shown = boolFrom(value);
        QAction *action = window->actionCollection()->action(KStandardAction::name(KStandardAction::ShowMenubar));
        if (!shown || !action) {
            return;
        }
        if (action->isChecked() != *shown) {
            // Through the action, which also remembers it (DolphinMainWindow::toggleShowMenuBar()).
            action->trigger();
        }
    }
    QString describe(const QString &value) const override
    {
        const std::optional<bool> shown = boolFrom(value);
        if (!shown) {
            return QString();
        }
        return *shown ? i18nc("@item:intext style element value", "Shown") : i18nc("@item:intext style element value", "Hidden");
    }
};

// ---- toolBars ----

class ToolBarsElement : public WhaleStyleElement
{
public:
    QString key() const override
    {
        return QStringLiteral("toolBars");
    }
    QString title() const override
    {
        return i18nc("@item:inlistbox style element", "Toolbars");
    }
    QString capture(const DolphinMainWindow *window) const override
    {
        QList<std::pair<QString, bool>> switches;
        const QList<KToolBar *> toolBars = window->toolBars();
        for (const KToolBar *toolBar : toolBars) {
            switches.append({toolBar->objectName(), !toolBar->isHidden()});
        }
        return switchesText(switches);
    }
    void apply(DolphinMainWindow *window, const QString &value) const override
    {
        const auto switches = switchesFrom(value);
        for (const auto &[name, shown] : switches) {
            // Not KMainWindow::toolBar(), which would create a missing one.
            const QList<KToolBar *> toolBars = window->toolBars();
            for (KToolBar *toolBar : toolBars) {
                // KMainWindow keeps the toolbars' visibility in the window's state.
                if (toolBar->objectName() == name && toolBar->isHidden() == shown) {
                    toolBar->setVisible(shown);
                }
            }
        }
    }
    QString describe(const QString &value) const override
    {
        QStringList shown;
        const auto switches = switchesFrom(value);
        for (const auto &[name, on] : switches) {
            if (on) {
                // The other toolbars by their object names, as Configure Toolbars lists them.
                shown.append(name == QLatin1String("mainToolBar") ? i18nc("@item:intext toolbar", "Main Toolbar") : name);
            }
        }
        return shown.isEmpty() ? i18nc("@item:intext style element value", "None shown")
                               : i18nc("@item:intext style element value, list of toolbars", "Shown: %1", shown.join(QStringLiteral(", ")));
    }
};

// ---- fileViewToolBars ----

class FileViewToolBarsElement : public WhaleStyleElement
{
public:
    QString key() const override
    {
        return QStringLiteral("fileViewToolBars");
    }
    QString title() const override
    {
        return i18nc("@item:inlistbox style element", "File View Toolbars");
    }
    QString capture(const DolphinMainWindow *window) const override
    {
        Q_UNUSED(window)
        return boolText(WhaleToolbarSettings::self()->fileViewToolBarsVisible());
    }
    void apply(DolphinMainWindow *window, const QString &value) const override
    {
        Q_UNUSED(window)
        if (const std::optional<bool> shown = boolFrom(value)) {
            WhaleToolbarSettings::self()->setFileViewToolBarsVisible(*shown);
        }
    }
    QString describe(const QString &value) const override
    {
        const std::optional<bool> shown = boolFrom(value);
        if (!shown) {
            return QString();
        }
        return *shown ? i18nc("@item:intext style element value", "Shown") : i18nc("@item:intext style element value", "Hidden");
    }
};

// ---- statusBar ----

struct StatusBarChoice {
    GeneralSettings::EnumShowStatusBar::type value;
    QLatin1String name;
};

constexpr std::array<StatusBarChoice, 3> statusBarChoices{{
    {GeneralSettings::EnumShowStatusBar::Small, QLatin1String("Small")},
    {GeneralSettings::EnumShowStatusBar::FullWidth, QLatin1String("FullWidth")},
    {GeneralSettings::EnumShowStatusBar::Disabled, QLatin1String("Disabled")},
}};

class StatusBarElement : public WhaleStyleElement
{
public:
    QString key() const override
    {
        return QStringLiteral("statusBar");
    }
    QString title() const override
    {
        return i18nc("@item:inlistbox style element", "Status bar");
    }
    QString capture(const DolphinMainWindow *window) const override
    {
        Q_UNUSED(window)
        for (const StatusBarChoice &choice : statusBarChoices) {
            if (GeneralSettings::showStatusBar() == choice.value) {
                return choice.name;
            }
        }
        return QString();
    }
    void apply(DolphinMainWindow *window, const QString &value) const override
    {
        for (const StatusBarChoice &choice : statusBarChoices) {
            if (value == choice.name && GeneralSettings::showStatusBar() != choice.value) {
                GeneralSettings::setShowStatusBar(choice.value);
                // The views read the settings from the file again.
                GeneralSettings::self()->save();
                refreshViews(window);
            }
        }
    }
    QString describe(const QString &value) const override
    {
        if (value == QLatin1String("Small")) {
            return i18nc("@item:intext status bar", "Small");
        }
        if (value == QLatin1String("FullWidth")) {
            return i18nc("@item:intext status bar", "Full width");
        }
        if (value == QLatin1String("Disabled")) {
            return i18nc("@item:intext status bar", "Hidden");
        }
        return QString();
    }
};

// ---- panels ----

struct Panel {
    QLatin1String name;
    QLatin1String dock;
};

constexpr std::array<Panel, 4> panels{{
    {QLatin1String("places"), QLatin1String("placesDock")},
    {QLatin1String("folders"), QLatin1String("foldersDock")},
    {QLatin1String("information"), QLatin1String("infoDock")},
    {QLatin1String("terminal"), QLatin1String("terminalDock")},
}};

class PanelsElement : public WhaleStyleElement
{
public:
    QString key() const override
    {
        return QStringLiteral("panels");
    }
    QString title() const override
    {
        return i18nc("@item:inlistbox style element", "Panels");
    }
    QString capture(const DolphinMainWindow *window) const override
    {
        QList<std::pair<QString, bool>> switches;
        for (const Panel &panel : panels) {
            // The terminal panel exists only where a terminal may be used.
            if (const QDockWidget *dock = window->findChild<QDockWidget *>(panel.dock)) {
                switches.append({panel.name, !dock->isHidden()});
            }
        }
        return switchesText(switches);
    }
    void apply(DolphinMainWindow *window, const QString &value) const override
    {
        const auto switches = switchesFrom(value);
        for (const auto &[name, shown] : switches) {
            for (const Panel &panel : panels) {
                QDockWidget *dock = name == panel.name ? window->findChild<QDockWidget *>(panel.dock) : nullptr;
                if (dock && dock->isHidden() == shown) {
                    // As the panel's action in the menu does; the window's state keeps it.
                    dock->setVisible(shown);
                }
            }
        }
    }
    QString describe(const QString &value) const override
    {
        QStringList shown;
        const auto switches = switchesFrom(value);
        for (const auto &[name, on] : switches) {
            if (!on) {
                continue;
            }
            if (name == QLatin1String("places")) {
                shown.append(i18nc("@item:intext panel", "Places"));
            } else if (name == QLatin1String("folders")) {
                shown.append(i18nc("@item:intext panel", "Folders"));
            } else if (name == QLatin1String("information")) {
                shown.append(i18nc("@item:intext panel", "Information"));
            } else if (name == QLatin1String("terminal")) {
                shown.append(i18nc("@item:intext panel", "Terminal"));
            }
        }
        return shown.isEmpty() ? i18nc("@item:intext style element value", "None shown")
                               : i18nc("@item:intext style element value, list of panels", "Shown: %1", shown.join(QStringLiteral(", ")));
    }
};
}

void WhaleStyleElementTypes::addTo(WhaleStyleElements &elements)
{
    // The layout first, then the views, then the bars and panels around them.
    elements.add(std::make_unique<SplitElement>());
    elements.add(std::make_unique<ViewStyleElement>(DolphinView::PanelRole::Left));
    elements.add(std::make_unique<ViewStyleElement>(DolphinView::PanelRole::Right));
    elements.add(std::make_unique<ShownViewElement>(DolphinView::PanelRole::Left));
    elements.add(std::make_unique<ShownViewElement>(DolphinView::PanelRole::Right));
    elements.add(std::make_unique<SplitLockElement>()); // after the views it depends on
    elements.add(std::make_unique<FilterBarElement>(DolphinView::PanelRole::Left));
    elements.add(std::make_unique<FilterBarElement>(DolphinView::PanelRole::Right));
    elements.add(std::make_unique<ZoomElement>());
    elements.add(std::make_unique<MenuBarElement>());
    elements.add(std::make_unique<ToolBarsElement>());
    elements.add(std::make_unique<FileViewToolBarsElement>());
    elements.add(std::make_unique<StatusBarElement>());
    elements.add(std::make_unique<PanelsElement>());
}
