# Dolphin architecture – a map for Orcinus

State: upstream Dolphin `e0daa45` (26.11.70 development state) plus the fork's commit `a5f3759`
(only `CLAUDE.md`). Written 2026-09-25 (under the working title Whale), translated to English on
2026-10-26. A read-only analysis; all important statements were checked in the source code. Paths
are relative to `src/` unless stated otherwise. Line numbers refer to that state and shift with
upstream merges. The fork's own changes since then are described in `design-notes.md`.

---

## 0. The essentials in 10 points

1. Dolphin is a **Qt6/KF6 widgets application in C++20** built with CMake/ECM. No QML.
2. The actual file view is **not a QListView/QTreeView**, but its own model/view/controller
   framework based on `QGraphicsView`: `kitemviews/` (`KFileItemModel`, `KItemListView`,
   `KItemListController`, `KItemListSelectionManager`).
3. File system access goes **entirely through KIO** (`KDirLister`, `KIO::*Job`). Dolphin itself knows
   no file system details, which is why `smb:`, `sftp:`, `trash:`, `zip:` and so on work "for free".
4. Object hierarchy: `DolphinMainWindow` → `DolphinTabWidget` → `DolphinTabPage` (split) →
   `DolphinViewContainer` (bars around the view) → `DolphinView` (file view, independent of the
   window).
5. The main window distributes the state of the **active** view by signal to panels and others:
   `selectionChanged`, `urlChanged`, `requestItemInfo`, `fileItemsChanged`.
6. Menus and toolbars are **KXMLGUI** (`dolphinui.rc`); all actions live in the
   `KActionCollection`.
7. Settings come from **KConfigXT**: singleton classes such as `GeneralSettings::self()` are
   generated from `*.kcfg` files.
8. Real in-process plugins exist only for **version control** (`KVersionControlPlugin`, namespace
   `dolphin/vcs`), **context menu actions** (`KAbstractFileItemActionPlugin`, loaded by KIO) and
   **overlay icons** (`KOverlayIconPlugin`). Panels, columns, view modes, status bar, Information
   panel, search and so on are **built in**.
9. The Information panel, tooltips and Baloo metadata exist only with `HAVE_BALOO` (Baloo and
   baloo-widgets found).
10. The best entry point for a plugin system is the bundle of signals in
    `DolphinMainWindow::connectViewSignals()`/`activeViewChanged()`, plus the places
    `setupDockWidgets()`, `DolphinContextMenu::addAdditionalActions()` and
    `KFileItemModelRolesUpdater::rolesData()`.

---

## 1. Project structure

### 1.1 Repository root

| Path | Purpose | Kind |
|---|---|---|
| `CMakeLists.txt` | Versions, `find_package`, feature flags (`HAVE_BALOO`, `HAVE_KUSERFEEDBACK`, `HAVE_PACKAGEKIT`, `HAVE_TERMINAL`, `HAVE_X11`) | Build |
| `CMakePresets.json` | In-tree presets `dev`, `asan`, `unity`, … | Build |
| `src/` | **All of Dolphin's code** | Dolphin |
| `doc/` | DocBook handbook (KDocTools) | KDE infrastructure |
| `po/` | Translations (~100 languages), maintained by KDE l10n through `GIT_SILENT` commits | KDE infrastructure |
| `appiumtests/` | UI tests through Selenium/AT-SPI (`dolphintest.py`) | Test |
| `cmake/` | `DbusInterfaceMacros.cmake`, `FindGem.cmake` | Build |
| `completions/` | zsh completion | Infrastructure |
| `LICENSES/`, `REUSE.toml` | SPDX licenses; CI checks REUSE compliance | KDE infrastructure |
| `.gitlab-ci.yml`, `.kde-ci.yml` | KDE CI templates and dependency list | KDE infrastructure |
| `.flatpak-manifest.json` | Flatpak build | Infrastructure |
| `DolphinVcsConfig.cmake.in` | CMake package for the public VCS plugin library | Plugin SDK |
| `plasma-dolphin.service.in` | systemd user service for `dolphin --daemon` | Infrastructure |

### 1.2 Build targets (`src/CMakeLists.txt`)

```
libdolphinvcs.so  (SHARED, installed + headers)    ← public VCS plugin API
        ▲
libdolphinprivate.so (SHARED)                      ← kitemviews/, views/, view settings
        ▲             ▲
        │        dolphinpart.so (MODULE, kf6/parts) ← DolphinView as a KPart (Konqueror)
        │
libdolphinstatic.a (STATIC)                        ← application shell: main window, tabs, panels,
        ▲                                             search, selection mode, settings dialog …
dolphin (executable) = main.cpp + dbusinterface.cpp

Also: kcm_dolphinviewmodes / kcm_dolphingeneral (KCMs), servicemenuinstaller,
kf6/kfileitemaction/{movetonewfolder,hidefile,setfoldericon}itemaction.so, kconf_update helpers, tests
```

Rule of thumb: **everything about the file view is in `dolphinprivate`; everything about the window
is in `dolphinstatic`.** The unit tests link against `dolphinprivate`.

### 1.3 Directories under `src/`

| Directory | Purpose | Important classes |
|---|---|---|
| *(root)* | Application shell | `DolphinMainWindow`, `DolphinTabWidget`/`TabBar`/`TabPage`, `DolphinViewContainer`, `DolphinContextMenu`, `DolphinNavigatorsWidgetAction`, `DolphinUrlNavigator`, `DolphinDockWidget`, `DolphinBookmarkHandler`, `DBusInterface`, `global.h` (namespace `Dolphin`) |
| `kitemviews/` | Own MVC framework of the file view | `KItemModelBase`, `KFileItemModel`, `KItemListView`, `KStandardItemListView`, `KFileItemListView`, `KItemListWidget`, `KStandardItemListWidget`, `KFileItemListWidget`, `KItemListController`, `KItemListSelectionManager`, `KItemListContainer`, `KFileItemModelRolesUpdater`, `KItemSet`/`KItemRange` |
| `kitemviews/private/` | Internals | Layouter, rubber band, role editor (inline rename), keyboard search, selection toggle, filter, sort algorithm, `KDirectoryContentsCounter`, `KBalooRolesProvider`, `KPixmapModifier`, `KFileItemClipboard` |
| `kitemviews/accessibility/` | QAccessible implementations | |
| `views/` | Dolphin-specific file view | `DolphinView`, `DolphinItemListView`, `DolphinFileItemListWidget`, `DolphinViewActionHandler`, `ViewProperties`, `DragAndDropHelper`, `ViewModeController`, `ZoomLevelInfo` |
| `views/versioncontrol/` | VCS plugin API and integration | `KVersionControlPlugin`, `VersionControlObserver`, `UpdateItemStatesThread` |
| `views/tooltips/` | Hover tooltips (Baloo only) | `ToolTipManager`, `DolphinFileMetaDataWidget` |
| `panels/` | Dock panels | `Panel` (base), `places/PlacesPanel`, `folders/FoldersPanel`, `information/InformationPanel`, `terminal/TerminalPanel` |
| `search/` | Search bar, query building | `Search::Bar`, `Search::Popup`, `DolphinQuery`, `selectors/*` |
| `selectionmode/` | Touch-friendly selection mode | `SelectionMode::TopBar`, `BottomBar`, `BottomBarContentsContainer`, `SingleClickSelectionProxyStyle` |
| `settings/` | Settings dialog, `*.kcfg`, migrations | `DolphinSettingsDialog`, `SettingsPageBase`, pages in `interface/`, `viewmodes/`, `contextmenu/`, `trash/`, `userfeedback/`, `kcm/` |
| `statusbar/` | Status bar, free space | `DolphinStatusBar`, `StatusBarSpaceInfo`, `MountPointObserver`, `DiskSpaceUsageMenu` |
| `filterbar/` | Name filter below the view | `FilterBar` |
| `admin/` | "Act as Administrator" (kio-admin) | `Admin::Bar`, `Admin::WorkerIntegration` |
| `itemactions/` | Bundled context menu plugins | `MoveToNewFolderItemAction`, `HideFileItemAction`, `SetFolderIconItemAction` |
| `trash/` | Trash state | `Trash` |
| `userfeedback/` | Telemetry (optional) | `DolphinFeedbackProvider` |
| `tests/` | QTest unit tests | |
| `icons/` | Application icons | |

Helper files in the root: `smallhash.h` (compact role map per item, an upstream optimization from
July 2026), `animatedheightwidget.*` (base of all sliding bars), `middleclickactioneventfilter.*`,
`disabledactionnotifier.*`, `dolphinplacesmodelsingleton.*`, `dolphinpackageinstaller.*`
(PackageKit), `servicemenushortcutmanager.*`, `dolphinremoveaction.*`.

### 1.4 Layers

| Layer | Where |
|---|---|
| Application / shell | `main.cpp`, `dbusinterface.*`, `global.*`, `dolphinmainwindow.*`, `dolphintab*`, `dolphinviewcontainer.*`, `*.rc` |
| UI widgets | `filterbar/`, `search/`, `selectionmode/`, `statusbar/`, `admin/bar`, `panels/*`, `views/tooltips/`, `dolphinurlnavigator` |
| Views | `kitemviews/k*listview*`, `k*listwidget*`, `kitemlistcontainer`, `views/dolphinview`, `views/dolphinitemlistview`, `views/dolphinfileitemlistwidget` |
| Models | `kitemviews/kitemmodelbase`, `kfileitemmodel` (+ `private/kfileitemmodelfilter`), `DolphinPlacesModel`, `settings/servicemodel` |
| Controllers | `kitemviews/kitemlistcontroller`, `kitemlistselectionmanager`, `views/dolphinviewactionhandler`, `views/viewmodecontroller`, `views/versioncontrol/versioncontrolobserver` |
| Services/helpers | `kfileitemmodelrolesupdater`, `private/kdirectorycontentscounter*`, `private/kbaloorolesprovider`, `draganddrophelper`, `trash/dolphintrash`, `statusbar/mountpointobserver*`, `dolphinpackageinstaller`, `search/dolphinquery` |
| Settings | `settings/**`, `views/viewproperties`, `panels/*/*.kcfg`, `search/*.kcfg` |

### 1.5 Dolphin's own code vs. KDE infrastructure

- **Dolphin's own:** everything under `src/`, in particular the whole `kitemviews` framework.
- **Only used (KDE Frameworks):**
  - **KIO:** `KDirLister`, `KFileItem`, jobs, `KIO::PreviewJob`, `KFileItemActions`, `KNewFileMenu`,
    `KFilePlacesModel`/`View`, `KUrlNavigator`, `KOverlayIconPlugin`.
  - **KXMLGUI:** `KXmlGuiWindow`, `KActionCollection`.
  - **KConfig/KConfigXT.**
  - **KParts:** Dolphin as a part; the Konsole part in the terminal panel.
  - **Baloo** and **baloo-widgets** (`Baloo::FileMetaDataWidget`), **KFileMetaData**.
  - **Solid, KNewStuff, KI18n, KCrash, KDBusAddons, KBookmarks, KCMUtils, KUserFeedback, PackageKit-Qt.**
  - **Qt Multimedia:** only for the media player in the Information panel.
- **Ignore at first:** `po/`, `doc/`, `LICENSES/`, CI files, `cmake/`, `completions/`,
  `userfeedback/`, kconf_update migrations (`settings/*.upd`, `*.py`), `accessibility/`.

---

## 2. Start-up

### 2.1 Flow of `main()` (`main.cpp:53–293`)

```
main()
 ├─ root+sudo?  → hint "dolphin --sudo" and abort                          (55–68)
 ├─ QApplication, icon theme, style, KLocalizedString domain "dolphin"      (73–93)
 ├─ KAboutData + KCrash::initialize()                                       (95–131)
 ├─ QCommandLineParser: --select --split --new-window --sudo/--admin
 │                      --daemon [--self-test] +[Url]                       (133–156)
 ├─ urls = Dolphin::validateUris(args)                   (global.cpp:22)
 ├─ --daemon → KDBusService + DBusInterface, NO window → app.exec()         (172–192)
 ├─ without --new-window: Dolphin::attachToExistingInstance(...)            (194–208)
 │      → asks running instances over D-Bus (org.kde.dolphin-<pid>,
 │        /dolphin/Dolphin_1), hands over the URLs, return 0
 ├─ no URLs → Dolphin::homeUrl()                                            (210–218)
 ├─ mainWindow = new DolphinMainWindow()                                    (220)
 │   openDirectories(urls, split) | openFiles(urls, split); show()
 ├─ KDBusService(Multiple) + DBusInterface (org.freedesktop.FileManager1)   (231–236)
 ├─ session restore / "remember tabs": mainWindow->restore(1)               (238–273)
 └─ app.exec()
```

Session restore is done by hand through `KXmlGuiWindow::classNameOfToplevel(1)` + `restore(1)`, not
through the `kRestoreMainWindows` macro.

**Important for development:** without `--new-window`, a freshly built Dolphin hands its URLs to an
already running instance and quits. Always test with `--new-window`. (The fork has its own D-Bus
identity since the rename, so it no longer hands URLs to Dolphin; `--new-window` still bypasses the
fork's own running instance.)

### 2.2 Constructor `DolphinMainWindow::DolphinMainWindow()` (`dolphinmainwindow.cpp:117–273`)

In this order:
1. `new MainWindowAdaptor(this)` (142): D-Bus interface `org.kde.dolphin.MainWindow`
2. `KIO::FileUndoManager` with its own UI interface (158–164)
3. `DolphinNavigatorsWidgetAction` (location bar(s) in the toolbar, 173)
4. `DolphinTabWidget` as central widget; `activeViewChanged` → `DolphinMainWindow::activeViewChanged` (175–180)
5. `setupActions()` (183): all window actions
6. `DolphinViewActionHandler` (185): all view actions
7. `setupDockWidgets()` (206): panels
8. `ServiceMenuShortcutManager` (209), `setupFileItemActions()` (211): `KFileItemActions`
9. `setupGUI(..., "dolphinui.rc")` (213–214): KXMLGUI builds menus and toolbar
10. Hamburger menu, event filter, config watcher (217–272)

### 2.3 Central objects and singletons

`GeneralSettings::self()` and further KConfigXT singletons, `DolphinPlacesModelSingleton::instance()`,
`DolphinNewFileMenuObserver::instance()`, `KIO::FileUndoManager::self()`, `Trash::instance()`,
`KFileItemClipboard::instance()`, `Admin::WorkerIntegration`, `DolphinUrlNavigatorsController`
(static only).

### 2.4 Classes to understand first

`DolphinMainWindow` → `DolphinViewContainer` → `DolphinView` → `KFileItemModel` →
`KItemListController`. Details in the reading order (chapter 11).

---

## 3. Main window and UI

### 3.1 Structure

```
DolphinMainWindow (KXmlGuiWindow, dolphinui.rc)
 ├─ actionCollection  ◄─ setupActions(), DolphinViewActionHandler, DolphinBookmarkHandler,
 │                       DolphinRecentTabsMenu, DolphinNewFileMenu, Admin::WorkerIntegration
 ├─ KHamburgerMenu ── aboutToShowMenu ─► updateHamburgerMenu()   (rebuilt dynamically)
 ├─ Toolbar: DolphinNavigatorsWidgetAction "url_navigators" (1–2 DolphinUrlNavigator)
 ├─ centralWidget: DolphinTabWidget (+ DolphinTabBar)
 │    └─ DolphinTabPage  (QSplitter → 1 or 2 containers = split view)
 │         └─ DolphinViewContainer   (QGridLayout, rows from top to bottom:)
 │              Search::Bar │ Admin::Bar │ KMessageWidget │ SelectionMode::TopBar
 │              DolphinView ─ KItemListContainer ─ KItemListController ─ DolphinItemListView ─ KFileItemModel
 │              SelectionMode::BottomBar │ FilterBar │ DolphinStatusBar
 └─ DolphinDockWidgets: Places, Folders, Information, Terminal
```

- `DolphinTabWidget` (`dolphintabwidget.cpp`): `openNewTab` (229), `openDirectories` (264),
  `openFiles` (297), `currentTabChanged` (519), session format `saveProperties`/`readProperties`.
- `DolphinTabPage` (`dolphintabpage.cpp`): `setSplitViewEnabled` (81), `slotViewActivated` (482),
  `createViewContainer` (535).
- `DolphinViewContainer` (`dolphinviewcontainer.cpp`): `setUrl` (627), `setSelectionModeEnabled`
  (401), `showMessage` (481), `slotItemActivated` (753), error handling in `slotErrorMessageFromView`
  (1059). Each container has an **internal** `DolphinUrlNavigator` holding the history; the visible
  navigator in the toolbar is only "connected" (`connectUrlNavigator`, 279).

### 3.2 The "active view" concept – the central distributor

```
DolphinView::setActive → activated
  → DolphinTabPage::slotViewActivated → activeViewChanged(container)
  → DolphinTabWidget::activeViewChanged
  → DolphinMainWindow::activeViewChanged (1665–1718)
       ├ disconnects everything from the old view
       ├ connectViewSignals(container)            (2797–2848)
       ├ m_actionHandler->setCurrentView(view)
       ├ update*Actions()
       └ Q_EMIT urlChanged(url), selectionChanged(selection)   → panels
```

`connectViewSignals()` connects, among others:
- `DolphinView::selectionChanged` → `slotSelectionChanged` (407) → `Q_EMIT selectionChanged`
- `requestContextMenu` → `openContextMenu` (1524)
- `urlActivated` → `handleUrl` (1483)
- `tabRequested`/`windowRequested`, `goBack`/`goForward`/`goUp`
- the internal navigator: `urlChanged` → `changeUrl` (367) → `Q_EMIT urlChanged`

**This is the most important place for a plugin system:** the URL, the selection and the hover
information of the active view come together here.

### 3.3 Menus, toolbars, actions

- **`DolphinMainWindow::setupActions()`** (1798–2339): File, Edit, View (split, reload, stop,
  editable location), Go (back/forward with history popup, recent tabs), Tools (terminal, compare,
  disk usage), Bookmarks, Settings, tab switching.
- **`DolphinViewActionHandler::createActions()`** (`views/dolphinviewactionhandler.cpp:81–436`):
  create folder/file, rename, trash, delete, duplicate, properties, view mode
  (Icons/Compact/Details), zoom, sorting, grouping, additional information (columns), previews,
  hidden files. `setCurrentView()` (45–74) reconnects about 15 view signals.
- **KXMLGUI:** `dolphinui.rc` (resource `/kxmlgui5/dolphin`, through `dolphin.qrc`) defines the
  menus, `mainToolBar` and the states `has_selection`/`has_no_selection`, switched by
  `updateFileAndEditActions()` (2630). `dolphinuiforphones.rc` is the mobile variant (a test checks
  that both stay in sync).
- **Hamburger menu:** `updateHamburgerMenu()` (1553–1639) is rebuilt every time it opens.

### 3.4 The central file view

`DolphinView` (`views/dolphinview.cpp`, constructor from 89) creates `KFileItemModel` (134),
`DolphinItemListView` (135), `KItemListController(model, view)` (140) and `KItemListContainer` (168).
`DolphinView` does **not** know the main window; it communicates only by signals. That is exactly
why `dolphinpart` can embed it in Konqueror. Details of the MVC framework in chapter 5.

### 3.5 Context menus (`dolphincontextmenu.cpp`)

Call chain: `KItemListController::itemContextMenuRequested`/`viewContextMenuRequested` →
`DolphinView::slotItemContextMenuRequested` (1318) / `slotViewContextMenuRequested` (1333) →
`requestContextMenu(pos, item, selectedItems, url)` → `DolphinMainWindow::openContextMenu`
(1524–1541) → `new DolphinContextMenu(this, item, selection, url, m_fileItemActions)` → `exec()`.

`DolphinContextMenu::addAllActions()` (69–115) chooses by click target and URL scheme:

| Case | Method | Content |
|---|---|---|
| Trash + item | `addTrashItemContextMenu` (176) | Restore, cut, copy, delete, properties |
| Trash empty/background | `addTrashContextMenu` (136) | Empty trash and so on |
| Item(s) | `addItemContextMenu` (280–352) | Open (in tab/window/split), "Open With", New, standard actions (`insertDefaultItemActions` 399), **plugins** (`addAdditionalActions`), copy/move to, properties |
| Background | `addViewportContextMenu` (354–397) | New, paste, sort/view, **plugins**, properties |

**Plugin hook:** `addAdditionalActions()` (545–562)
- `m_fileItemActions->addActionsTo(this, MenuActionSource::All, …)`: service menus and
  KFileItemAction plugins
- then `view->versionControlActions(selectedItems)`: VCS plugins

The menu has a fixed structure (enum `ContextType`, scheme checks). There is no hook or signal
"menu is being built".

### 3.6 Panels / docks

- Base class `Panel : QWidget` (`panels/panel.h`): `setUrl()` (46) calls the pure virtual
  `urlChanged()`; also `refreshUrl()`, `readSettings()`, `setCustomContextMenuActions()`. Contents are
  created lazily in `showEvent`.
- **Exception:** `PlacesPanel` inherits `KFilePlacesView` (KIO) directly, **not** `Panel`.
- All panels are created in `DolphinMainWindow::setupDockWidgets()` (2341–2606). Each sits in a
  `DolphinDockWidget`; the toggle comes from `createPanelAction` (2979).

| Panel | Key | Inputs (from the main window) | Outputs (to the main window) |
|---|---|---|---|
| Information (`HAVE_BALOO`) | F11 | `urlChanged`, `selectionChanged`, `requestItemInfo`, `fileItemsChanged`, `settingsChanged` | `urlActivated` → `handleUrl` |
| Folders (tree, built on kitemviews) | F7 | `urlChanged` | `folderActivated` → `changeUrl`, tab requests |
| Terminal (Konsole KPart) | F4 | `urlChanged`, `urlRefreshed` | `changeUrl` → `slotTerminalDirectoryChanged` |
| Places (`KFilePlacesView`) | F9 | `urlChanged` | `placeActivated`, tab/window/split requests, unmount |

Panels **never** talk to the view directly, only through the main window's signals and slots. This
pattern can be taken over directly for plugin panels.

---

## 4. File system and navigation

### 4.1 Representation

- A file is a **`KFileItem`** (KIO). It contains URL, `UDSEntry`, MIME type, icon, permissions and
  so on.
- **`KFileItemModel`** (`kitemviews/kfileitemmodel.h/.cpp`) is a flat, sorted list:
  - `ItemData { KFileItem item; SmallHash values; ItemData *parent; }` (h:385–389); `parent` is only
    set in tree mode.
  - Items are addressed by **index (int)**, values by **roles** (`QByteArray` keys such as `"text"`,
    `"size"`, `"iconPixmap"`, `"version"`). `data(int)` (382) provides cheap roles lazily
    (`retrieveData` 2231). Expensive roles are filled asynchronously by `KFileItemModelRolesUpdater`.
  - The role table is static: `rolesInfoMap()` (3157).
  - Lookup: `fileItem(int)` 717, `fileItem(QUrl)` 726, `index(KFileItem)` 735, `index(QUrl)` 740.
- `KItemModelBase` (`kitemviews/kitemmodelbase.h`) is the abstract base (not a
  `QAbstractItemModel`!) with the signals `itemsInserted`/`itemsRemoved`/`itemsMoved`/
  `itemsChanged(ranges, roles)`.
- The data source is a plain **`KDirLister`** (created in `KFileItemModel`, cpp:288). It delivers
  `itemsAdded` → `slotItemsAdded` (1451) → buffered → `insertItems` (1827). Further slots:
  `itemsDeleted` → `slotItemsDeleted` (1558), `refreshItems` → `slotRefreshItems` (1629),
  `listingDirCompleted` → `slotCompleted`.
- Tree view (expandable folders in Details): `setExpanded` (874) opens additional folders in the same
  `KDirLister` (`KDirLister::Keep`).
- Filter: `private/kfileitemmodelfilter.*` (text/glob/regex + MIME), `applyFilters` (1116).
- Sorting: `lessThan` (2398), `sort` (2471) with a parallel merge sort
  (`private/kfileitemmodelsortalgorithm.h`).
- Grouping: `groups()` (664) with `nameRoleGroups`, `sizeRoleGroups`, `timeRoleGroups` and so on.

### 4.2 Navigation

```
Location bar / panels / double click / back-forward-up
      │
      ▼
DolphinViewContainer::setUrl (627) → internal KUrlNavigator (history!)
      │ urlChanged
      ├──► DolphinViewContainer::slotUrlNavigatorLocationChanged (934)
      │       protocol listable? → m_view->setUrl(url)
      │       otherwise → QDesktopServices::openUrl / error
      │           ▼
      │    DolphinView::setUrl (819) → loadDirectory (2460)
      │           ▼
      │    KFileItemModel::loadDirectory (348) → KDirLister::openUrl  (asynchronous, KIO worker)
      │           ▼
      │    slotItemsAdded → insertItems → itemsInserted → view paints
      │    slotCompleted → DolphinView::updateViewState (2064): restore selection/scroll position
      │
      └──► DolphinMainWindow::changeUrl (367) → Q_EMIT urlChanged → all panels
```

- The history lives per container in the `KUrlNavigator`. For each location, selection, scroll
  position and expanded folders are saved along through `DolphinView::saveState`/`restoreState`
  (1810/1785).
- Activating an item: `KItemListController::itemActivated` → `DolphinView::slotItemActivated` (1229)
  → `DolphinViewContainer::slotItemActivated` (753). Folders (and archives through
  `openItemAsFolderUrl` 1852 → `zip:`/`tar:`) are navigated, files are opened with `KIO::OpenUrlJob`.
- Redirects (e.g. `desktop:/`) go through `KDirLister::redirection` →
  `DolphinView::slotDirectoryRedirection` (2016) → `DolphinViewContainer::redirect` (994).

### 4.3 Role of KIO, URLs, frameworks

Everything is `QUrl` based. Every protocol with a KIO worker works automatically. Special cases are
hard-coded in about 53 `url.scheme() == "…"` comparisons (among them `trash`, `admin`, `*search*`,
`timeline`, `recentlyused`, `smb`, `network`, `kdeconnect`). There is no registry.

### 4.4 File operations (all in `views/dolphinview.cpp` unless stated otherwise)

| Operation | Function | KIO |
|---|---|---|
| Copy / cut | `copySelectedItemsToClipboard` 969 / `cutSelectedItemsToClipboard` 961 | `KFileItemModel::createMimeData`, `KIO::setClipboardDataCut` |
| Paste | `paste` 1020, `pasteIntoFolder` 1025 → `pasteToUrl` 2711 | `KIO::paste` → `PasteJob` |
| Copy/move to the other split half | `copySelectedItems` 976, `moveSelectedItems` 998 | `KIO::copy`/`KIO::move` |
| Duplicate | `duplicateSelectedItems` 1033 | `KIO::copyAs` |
| Trash / delete | `trashSelectedItems` 935 / `deleteSelectedItems` 948 | `KIO::DeleteOrTrashJob` |
| Rename | `renameSelectedItems` 879 → inline through `KItemListRoleEditor` → `slotRoleEditingFinished` 2309, or `KIO::RenameFileDialog` | `KIO::moveAs` |
| Create new | `DolphinNewFileMenu : KNewFileMenu` (`dolphinnewfilemenu.*`), `DolphinMainWindow::createDirectory` 834 | KNewFileMenu |
| Drag and drop | `DolphinView::dropUrls` 1547 → `DragAndDropHelper::dropUrls` | `KIO::drop` → `DropJob` |
| Undo | `DolphinMainWindow::undo` 881 | `KIO::FileUndoManager` |

The actions are wired in `DolphinViewActionHandler` (rename 527, trash 538, delete 549, duplicate
896) and `DolphinMainWindow` (cut 909, copy 919, paste 929).

---

## 5. Selection

### 5.1 MVC of the file view

```
               DolphinView  (owns model, view, container; connects controller signals 210–227)
                    │
KItemListContainer (QAbstractScrollArea + QGraphicsView)
                    │ events: KItemListView::event() → controller->processEvent()
                    ▼
            KItemListController  ── owns ──► KItemListSelectionManager
            (mouse, keyboard, DnD,           KItemListKeyboardSearchManager
             rubber band, touch)
               │ reads/changes                 ▲ itemsInserted/Removed/Moved (forwarded by the view)
               ▼                               │
         KFileItemModel ── signals ──► KItemListView → KStandardItemListView → KFileItemListView
                                         → DolphinItemListView
                                         ├ recycled KItemListWidgets (… → DolphinFileItemListWidget)
                                         ├ KItemListRubberBand, autoscroll
                                         └ KFileItemModelRolesUpdater (icons/previews/sizes)
```

### 5.2 Where the selection lives

`KItemListSelectionManager` (`kitemviews/kitemlistselectionmanager.cpp`):
- **State:** `m_currentItem`, `m_anchorItem`, `KItemSet m_selectedItems`, "anchored selection" for
  Shift ranges.
- **API:** `setCurrentItem` 24, `setSelectedItems` 52, `setSelected(index, count, Select|Deselect|Toggle)`
  104, `clearSelection` 150, `beginAnchoredSelection` 170, `endAnchoredSelection` 178.
- **Adjustment on model changes:** `itemsInserted` 212, `itemsRemoved` 275, `itemsMoved` 323.
- `KItemSet` (`kitemset.h`) is a range-compressed index set.

### 5.3 Mouse, rubber band, keyboard, drag and drop (`kitemviews/kitemlistcontroller.cpp`)

| Topic | Place |
|---|---|
| Click logic (Ctrl/Shift, toggle, right click, click on something already selected) | `mousePressEvent` 604 → **`onPress` 1649–1837** |
| Release, single/double click activation, expansion | `mouseReleaseEvent` 695 → **`onRelease` 1839–1930**, `mouseDoubleClickEvent` 722 |
| Rubber band | `startRubberBand` 1932, `mouseMoveEvent` branch 667–690, **`slotRubberBandChanged` 1372**, painting in `KItemListView::slotRubberBandActivationChanged` 1716 |
| Keyboard | **`keyPressEvent` 238–524** (selection handling 489–518) |
| Type-ahead | `private/kitemlistkeyboardsearchmanager.cpp`, `slotChangeCurrentItem` 526 |
| Drag start | `startDragging` 1451 |
| Drop | `dragMoveEvent` 855, `dropEvent` 930 → `itemDropEvent` → `DolphinView::slotItemDropEvent` 1527 |
| Context menu | `contextMenuEvent` 775 |
| Touch | `tapTriggered` 1148, `tapAndHoldTriggered` 1184 (starts selection mode) |
| Hit zones | `KItemListView::itemAt` 442, `isAboveSelectionToggle` 462, `isAboveExpansionToggle` 479, `isAboveText` 492 |
| Selection toggle (check mark on hover) | `private/kitemlistselectiontoggle.*`, `DolphinItemListView::setEnabledSelectionToggles` |

**Selection mode** (`selectionmode/`): `DolphinViewContainer::setSelectionModeEnabled` (401) →
`DolphinView::setSelectionModeEnabled` (345) → `KItemListController::setSelectionModeEnabled` (223).
In this mode clicks act as with Ctrl held, and the bottom bar shows matching actions.

### 5.4 How the selection reaches the application

`KItemListSelectionManager::selectionChanged` → `DolphinView::slotSelectionChanged` (1679),
**debounced** (0 ms when switching between empty and non-empty, otherwise 300 ms) →
`emitSelectionChangedSignal` (1693) → `DolphinView::selectionChanged(KFileItemList)`. Receivers:
- status bar
- bottom bar
- `DolphinViewActionHandler`
- `DolphinMainWindow::slotSelectionChanged` (407): updates `KFileItemActions` and forwards
  `DolphinMainWindow::selectionChanged` to the Information panel

Accessors: `DolphinView::selectedItems()` 460, `selectedItemsCount()` 482, `selectAll` 844,
`invertSelection` 850, `clearSelection` 856, `markUrlsAsSelected` 488.

### 5.5 Places for changing selection behavior

1. `KItemListController::onPress`/`onRelease`: click semantics, drag vs. click, single/double click
2. `KItemListController::keyPressEvent`: keyboard selection
3. `slotRubberBandChanged`, `startRubberBand`, `mouseMoveEvent`: rubber band
4. `KItemListSelectionManager`: anchor semantics, behavior on model changes
5. `KItemListView::isAbove*` and the widgets' `selectionRect*`: hit zones
6. `DolphinView::slotSelectionChanged` (debouncing), `updateSelectionState`/`updateViewState`
   (restoring after loading or after jobs)
7. Tests: `tests/kitemlistcontrollertest.cpp`, `kitemlistselectionmanagertest.cpp`

---

## 6. Plugins and extensibility – overview

| # | Mechanism | Direction | Third-party in-process code? | Affects |
|---|---|---|---|---|
| 1 | **VCS plugins** `KVersionControlPlugin` | into Dolphin | yes (C++) | item status overlays, text color, context menu actions, status messages |
| 2 | **KFileItemAction plugins** `KAbstractFileItemActionPlugin` | into KIO (used by Dolphin) | yes (C++) | context menu entries |
| 3 | **Service menus** (`.desktop` in `kio/servicemenus`) | into KIO | no (commands) | context menu entries (+ shortcuts from KIO 6.24) |
| 4 | **Overlay icon plugins** `KOverlayIconPlugin` | into KIO/Dolphin | yes (C++) | emblem icons on items |
| 5 | **Thumbnailers** (KIO `ThumbnailCreator`) | into KIO | yes, but in the KIO worker | previews |
| 6 | **KIO workers** (protocols) | into KIO | own process | new "file systems" |
| 7 | **KParts** | Dolphin → others (`dolphinpart`); Konsole → Dolphin | – | embedding |
| 8 | **KCMs** `kcm_dolphin*` | Dolphin → System Settings/Konqueror | – | settings pages |
| 9 | **D-Bus** `org.freedesktop.FileManager1`, `org.kde.dolphin.MainWindow` | external → Dolphin | no | open/activate windows; **no** selection or action API |
| 10 | **KNewStuff** (`servicemenu.knsrc` + `servicemenuinstaller`) | installing service menus | – | – |
| 11 | **KXMLGUI** | user | – | toolbar/shortcuts for every action in the action collection |

**Already extensible:** context menu (plugins, service menus), VCS display and actions, overlay
icons, previews, protocols, toolbar contents and shortcuts.

**Built in:**
- **Panels:** `setupDockWidgets`
- **View modes:** enum `DolphinView::Mode`; the item widget is fixed to `DolphinFileItemListWidget`
  (`dolphinitemlistview.cpp:131`), although `KItemListView::setWidgetCreator` exists
- **Columns/roles:** static `rolesInfoMap`
- **Context menu structure**
- **Status bar**, the bars in the view container
- **Search:** enum `SearchTool`
- **Contents of the Information panel and the tooltips**
- **Settings pages:** fixed list in `dolphinsettingsdialog.cpp:45–105`
- **URL scheme special cases**

---

## 7. Previews / Information panel

### 7.1 Previews in the file view

Responsible is `KFileItemModelRolesUpdater` (`kitemviews/kfileitemmodelrolesupdater.cpp`), created by
`KFileItemListView::onModelChanged` (`kfileitemlistview.cpp:243`).

```
KFileItemListView ─(visible range, debounced 50 ms)─► RolesUpdater::setVisibleIndexRange
  startUpdating() (907):
    updateVisibleIcons()     visible items synchronously (≤200 ms): MIME type, icon
    indexesToResolve()       priority: visible > ±5 pages > first/last page > up to 500 items
    startPreviewJob() (984)  ONE bundled KIO::PreviewJob(items, 128|256 px,
                             [PreviewSettings] plugins)
      generated → slotGotPreview (581) → "iconPixmap" → KFileItemModel::setData
      rolesData() (1384): type, extension, iconOverlays (KOverlayIconPlugin),
                          folder size (KDirectoryContentsCounter, worker thread),
                          Baloo roles (KBalooRolesProvider)
  → KFileItemModel::itemsChanged → KItemListView::slotItemsChanged
  → KStandardItemListWidget::updatePixmapCache() → paint
```

On hover Dolphin shows animated preview sequences, e.g. video frames:
`loadNextHoverSequencePreview` (1066) produces the role `"hoverSequencePixmaps"`.

### 7.2 Information panel (`panels/information/`, only `HAVE_BALOO`)

```
DolphinMainWindow: selectionChanged / requestItemInfo (hover) / urlChanged / fileItemsChanged
        ▼
InformationPanel (informationpanel.cpp)
   setSelection 49 │ requestDelayedItemInfo 75 (300 ms) │ urlChanged 98 (200 ms)
        └──────────► showItemInfo() 218   ← THE decision point
              >1 selected → content->showItems(list)
              1 item      → content->showItem(item)
              nothing     → KIO::stat(folder) → showItem(...)
        ▼
InformationPanelContent (informationpanelcontent.cpp)
   showItem 153
    ├─ refreshMetaData 301 → Baloo::FileMetaDataWidget::setItems   (external: baloo-widgets)
    └─ refreshPreview 223   ← MIME switch
         ├ search URL     → icon
         ├ refreshPixmapView 165 → own KIO::PreviewJob → PixmapViewer
         ├ animated image → PixmapViewer::setAnimatedImageFileName (QMovie)
         └ audio/video    → MediaWidget (QtMultimedia: QMediaPlayer/QVideoWidget)
```

- Settings: `dolphin_informationpanelsettings.kcfg`. Which metadata fields are shown is stored by
  baloo-widgets itself (`baloofileinformationrc`), not by Dolphin.
- **Tooltips** (`views/tooltips/`, Baloo only): `ToolTipManager::startContentRetrieval` (123) with an
  own `KIO::PreviewJob` (512 px) + `DolphinFileMetaDataWidget` → `KToolTipWidget`.

### 7.3 Building blocks for a universal preview/info panel

- **Reusable:** `InformationPanel::showItemInfo()` (selection/hover/folder logic),
  `InformationPanelContent::showItem`/`showItems`, `PixmapViewer`, `MediaWidget`, the preview job
  handling (`refreshPixmapView`, `refreshPixmapViewForItsSize`).
- **Natural extension point:** the MIME switch in `InformationPanelContent::refreshPreview()` would be
  the place for a "preview provider" registry (text, PDF, Markdown, code, archives, …).
- **Limitations:**
  - Without Baloo there is no Information panel at all (`#if HAVE_BALOO` in
    `dolphinmainwindow.cpp:2367` and `src/CMakeLists.txt`).
  - Three independent preview job pipelines run (view, panel, tooltip).
  - The metadata display is an external black box (`Baloo::FileMetaDataWidget`). An own widget based
    on `KFileMetaData` would be conceivable.
  - `InformationPanelContent` creates its children with the panel as parent, not with `this`.

---

## 8. Git-related extensions

### 8.1 What exists

The VCS plugin API (`views/versioncontrol/kversioncontrolplugin.h`) is made exactly for
context-dependent Git functions. The **Git plugin itself is not in this repository** but in
`dolphin-plugins` (installed under `<plugindir>/dolphin/vcs/fileviewgitplugin.so`, together with
svn, hg, bazaar and dropbox). It is off until enabled in Settings → Context Menu
(`[VersionControl] enabledPlugins` in the application's configuration).

Flow per `DolphinView` (observer created in `dolphinview.cpp:269`):

```
KFileItemModel: itemsInserted / itemsChanged / directoryLoadingCompleted
   → VersionControlObserver::verifyDirectory (165)   (local files only, debounced 100–500 ms)
   → initPlugins (291): KPluginMetaData::findPlugins("dolphin/vcs") ∩ enabledPlugins
   → searchPlugin (320): plugin->fileName() (e.g. ".git") exists? / localRepositoryRoot()
   → UpdateItemStatesThread (worker thread, global mutex):
        beginRetrieval(dir) → itemVersion(item)* → endRetrieval()
   → slotThreadFinished (196): model->setData(index, {"version": ItemVersion})
   → DolphinFileItemListWidget::refreshCache (21–91): text color + emblem "vcs-*"
Context menu: DolphinContextMenu::addAdditionalActions → DolphinView::versionControlActions (803)
   → VersionControlObserver::actions (105) → plugin->versionControlActions(items)
                                             or outOfVersionControlActions(items)
```

### 8.2 Where a Git plugin would get information and could provide UI

| Need | Existing source |
|---|---|
| Current selection | `DolphinView::selectionChanged(KFileItemList)`; in the window: `DolphinMainWindow::selectionChanged` / `slotSelectionChanged` (407) |
| Current folder | `DolphinMainWindow::urlChanged(QUrl)`; `DolphinView::url()` |
| Status per file | role `"version"` in `KFileItemModel` (filled by `VersionControlObserver`) |
| Context menu actions | `KVersionControlPlugin::versionControlActions()` – already there |
| Toolbar/shortcut actions | `actionCollection()`; template: `ServiceMenuShortcutManager` (stable names + `KActionCategory`) |
| Own panel (e.g. Git log/diff) | pattern from `setupDockWidgets()` + `Panel` (today only compiled in) |
| Messages | `KVersionControlPlugin::infoMessage`/`errorMessage` → status bar or `DolphinViewContainer::showMessage` |

**Limits of the VCS API:**
- A plugin only gets a path, `KFileItem`s and `KFileItemList`s, no access to window, view or panels.
- Local files only; only one plugin is active per folder.
- The status enum and the presentation are fixed.
- Plugin instances exist per view; settings are read only once per view.
- Enabling uses the **translated** name (`p.name()`, `versioncontrolobserver.cpp:301`) instead of the
  plugin ID, which can break when the UI language changes.

---

## 9. The current plugin architecture in detail

### 9.1 VCS plugins

- **Technique:** C++/Qt; KCoreAddons `KPluginMetaData::findPlugins("dolphin/vcs")` +
  `KPluginFactory::instantiatePlugin<KVersionControlPlugin>()` (`versioncontrolobserver.cpp:298–302`).
  The plugin itself uses `K_PLUGIN_CLASS_WITH_JSON`.
- **SDK:** `libdolphinvcs` + installed header `<Dolphin/KVersionControlPlugin>` + CMake package
  `DolphinVcs`.
- **Interface** (pure virtual, `kversioncontrolplugin.h:141–185`): `fileName()`,
  `beginRetrieval(dir)`, `endRetrieval()`, `itemVersion(item)`, `versionControlActions(items)`,
  `outOfVersionControlActions(items)`. Optional: `localRepositoryRoot(dir)`. Signals:
  `itemVersionsChanged`, `infoMessage`, `errorMessage`, `operationCompletedMessage`.
- **Threading:** `beginRetrieval`/`itemVersion`/`endRetrieval` run in the worker thread, the actions
  in the GUI thread.
- **Registration:** installed in `<plugindir>/dolphin/vcs/`, enabled through
  `[VersionControl] enabledPlugins`.
- **Loading:** lazily at the first folder check. **Unloading:** never explicitly; the instances die
  with the view.

### 9.2 KFileItemAction plugins

- **Technique:** C++/Qt, `KAbstractFileItemActionPlugin` (KIO), JSON metadata,
  `kcoreaddons_add_plugin(... INSTALL_NAMESPACE "kf6/kfileitemaction")`.
- **Loading:** not Dolphin but **KIO's `KFileItemActions`** loads these plugins. Dolphin creates the
  object in `DolphinMainWindow::setupFileItemActions()` (2608) and sets `setItemListProperties` on
  every selection change. For the context menu, `addActionsTo()` calls the plugins; KIO filters by
  `MimeTypes`, `X-KDE-Require` and `kservicemenurc [Show]`.
- **What the plugin gets:** `actions(const KFileItemListProperties &items, QWidget *parentWidget)`.
  `parentWidget` is the `DolphinMainWindow`; there is no other Dolphin API.
- **What it can provide:** `QAction`s, `QWidgetAction`s, submenus, but only in the context menu
  (item and background). Not in the Folders or Places panel, not in the toolbar (exception: service
  menus from KIO 6.24 through `ServiceMenuShortcutManager`).
- **Enabling/disabling:** Settings → Context Menu (`contextmenusettingspage.cpp:295–306`) writes
  `kservicemenurc [Show] <pluginId>`; Dolphin then recreates `KFileItemActions` (config watcher,
  `dolphinmainwindow.cpp:265–270`).
- **Side effect:** these plugins appear in **all** KIO-based programs, not only in Dolphin.

### 9.3 Walkthrough: the smallest plugin in the repository – `itemactions/movetonewfolderitemaction`

**1. Header** (`movetonewfolderitemaction.h`):
```cpp
class MoveToNewFolderItemAction : public KAbstractFileItemActionPlugin {
    Q_OBJECT
public:
    MoveToNewFolderItemAction(QObject *parent);
    QList<QAction *> actions(const KFileItemListProperties &fileItemInfos, QWidget *parentWidget) override;
};
```

**2. Implementation** (`movetonewfolderitemaction.cpp`, 61 lines):
```cpp
K_PLUGIN_CLASS_WITH_JSON(MoveToNewFolderItemAction, "movetonewfolderitemaction.json")

QList<QAction *> MoveToNewFolderItemAction::actions(const KFileItemListProperties &fileItemInfos, QWidget *parentWidget)
{
    const KFileItemList &selectedItems = fileItemInfos.items();
    if (selectedItems.isEmpty()) return {};
    if (selectedItems.size() == 1 && selectedItems[0].isDir()) return {};

    QAction *a = new QAction(i18nc("@action:inmenu", "Move to New Folder…"), parentWidget);
    connect(a, &QAction::triggered, this, [=, this]() {
        auto newFileMenu = new KNewFileMenu(parentWidget);
        newFileMenu->setWorkingDirectory(/* folder of the first item */);
        newFileMenu->createDirectory();
        connect(newFileMenu, &KNewFileMenu::directoryCreated, this, [=](const QUrl &createdUrl) {
            KIO::CopyJob *job = KIO::move(selectedItems.urlList(), createdUrl);
            KIO::FileUndoManager::self()->recordCopyJob(job);
        });
    });
    return {a};
}
#include "movetonewfolderitemaction.moc"
```

**3. Metadata** (`movetonewfolderitemaction.json`):
```json
{ "KPlugin": { "Icon": "folder-new", "Name": "Move to New Folder",
               "MimeTypes": ["application/octet-stream", "inode/directory"] },
  "X-KDE-Require": "Write",
  "X-KDE-Show-In-Submenu": "true" }
```

**4. CMake** (`itemactions/CMakeLists.txt`, included through `add_subdirectory(itemactions)` in
`src/CMakeLists.txt:681`):
```cmake
kcoreaddons_add_plugin(movetonewfolderitemaction
    SOURCES movetonewfolderitemaction.cpp movetonewfolderitemaction.h
    INSTALL_NAMESPACE "kf6/kfileitemaction")
target_link_libraries(movetonewfolderitemaction
    KF6::I18n KF6::KIOCore KF6::KIOWidgets KF6::KIOFileWidgets)
```

**5. Path into the menu:** right click → `DolphinView::requestContextMenu` →
`DolphinMainWindow::openContextMenu` → `DolphinContextMenu::addItemContextMenu` →
`addAdditionalActions` → `KFileItemActions::addActionsTo` → KIO finds and filters the plugin and calls
`actions()`.

Testing hint: the plugin has to be under `kf6/kfileitemaction` in the plugin path (install into a
private prefix and source its `prefix.sh`, or point `QT_PLUGIN_PATH` to the build's `bin`).

**A VCS plugin** works the same way: derive from `KVersionControlPlugin`, implement the six pure
virtual methods, `K_PLUGIN_CLASS_WITH_JSON`, link against `DolphinVcs::DolphinVcs`,
`INSTALL_NAMESPACE "dolphin/vcs"`, then enable it in the settings.

### 9.4 Can plugins add their own UI?

| Contribution | VCS plugin | FileItemAction plugin | Service menu | Overlay plugin |
|---|---|---|---|---|
| Context menu entries | ✅ | ✅ | ✅ | – |
| Toolbar / shortcuts | – | – | ✅ (KIO ≥ 6.24) | – |
| Panels / docks | – | – | – | – |
| Item decoration | ✅ (fixed status enum) | – | – | ✅ (emblem icons) |
| Columns / roles | – | – | – | – |
| Status messages | ✅ | – | – | – |
| Access to window/view | – | only `parentWidget` | – | – |

### 9.5 Limitations of the existing architecture

- There is no general plugin API of Dolphin's own. The two in-process types are narrowly tailored.
- Plugins get no context: no access to main window, view, navigation or panels, and no events (URL
  change, selection change).
- There is no lifecycle (no loading/unloading at runtime), only lazy loading, and per view.
- The plugin list of the settings pages is fixed.
- D-Bus only allows window operations.
- Many features depend on compile-time flags (`HAVE_BALOO`).
- Context menus are built in three separate places: `DolphinContextMenu`, the selection mode's
  bottom bar (`bottombarcontentscontainer.cpp:518`) and `DolphinPart::slotOpenContextMenu`.

### 9.6 What can be reused for the fork

1. **Loading idiom:** `KPluginMetaData::findPlugins(ns)` + `KPluginFactory::instantiatePlugin<T>()` +
   JSON metadata + an enable list in KConfig (template `versioncontrolobserver.cpp:291–318`). A
   JavaScript host (e.g. `QJSEngine`) could itself be such a C++ plugin that loads scripts from
   `QStandardPaths`, like the search for service menus (`contextmenusettingspage.cpp:273`).
2. **Event bus:** the existing `DolphinMainWindow` signals (`selectionChanged`, `urlChanged`,
   `requestItemInfo`, `fileItemsChanged`, `settingsChanged`), which already follow the active view.
3. **Panel pattern:** `Panel` + `DolphinDockWidget` + `createPanelAction`; the construction in
   `setupDockWidgets()` would only need a registry loop.
4. **Action registration:** the `ServiceMenuShortcutManager` pattern for script-defined actions
   (stable names, shortcuts, usable in toolbars).
5. **Asynchronous item decoration:** the `VersionControlObserver` pattern (debounced, worker thread,
   `model->setData`) and the overlay plugin loop in `KFileItemModelRolesUpdater` (124–138).
6. **Message API:** `DolphinViewContainer::showMessage(text, type, actions)`.
7. **Settings pages:** `SettingsPageBase` is already a clean page interface.

---

## 10. Natural extension points for a plugin/script system

| Extension point | Place | What a plugin could do there |
|---|---|---|
| **Event bus** | `DolphinMainWindow::activeViewChanged` (1665), `connectViewSignals` (2797), `slotSelectionChanged` (407), `changeUrl` (367) | React to URL, selection and hover changes |
| **Panels** | `DolphinMainWindow::setupDockWidgets` (2341), `panels` KActionMenu (2585) | Own docks (Git, preview, notes, …) |
| **Context menu** | `DolphinContextMenu::addAdditionalActions` (545), `DolphinMainWindow::openContextMenu` (1524) | A third provider list with full context (window, selection, URL, context type) |
| **Actions/toolbar** | `DolphinMainWindow::setupActions` (1798), `DolphinViewActionHandler::createActions` (81), `actionCollection()` | Own actions, shortcuts, toolbar buttons |
| **Columns/roles** | `KFileItemModel::rolesInfoMap` (3157, static today), `KFileItemModelRolesUpdater::rolesData` (1384) | Own columns (e.g. Git status, tags) |
| **Item presentation** | `DolphinFileItemListWidget::refreshCache` (21), `DolphinItemListView::defaultWidgetCreator` (131) | Own decorations, badges |
| **Asynchronous annotation** | `VersionControlObserver` as a pattern | Script-computed badges per folder |
| **Preview/info** | `InformationPanel::showItemInfo` (218), `InformationPanelContent::refreshPreview` (223), `ToolTipManager::startContentRetrieval` (123) | Preview providers per MIME type, additional info sections |
| **Bars/messages** | `DolphinViewContainer` grid slots (57–68), `showMessage` (481) | Own bars above or below the view, notifications |
| **URL schemes** | about 53 scattered `scheme()` comparisons | A scheme registry with capabilities |
| **Settings** | `DolphinSettingsDialog` (45–105), `SettingsPageBase` | Plugin settings pages |
| **Automation** | `org.kde.dolphin.MainWindow` (`dolphinmainwindow.h:75`) | A D-Bus API for selection, URL, triggering actions |

---

## 11. Recommended reading order

### Must understand (in this order)

| # | File | What to look for |
|---|---|---|
| 1 | `main.cpp` | Start-up, single-instance logic |
| 2 | `dolphinmainwindow.h` | Signals, members – the overview of the window |
| 3 | `dolphinmainwindow.cpp` | Constructor (117), `activeViewChanged` (1665), `connectViewSignals` (2797), `openContextMenu` (1524), `setupDockWidgets` (2341) – only skim `setupActions` |
| 4 | `dolphintabpage.cpp` | Split view, `slotViewActivated` |
| 5 | `dolphinviewcontainer.cpp` | Bar layout, `setUrl`, `slotUrlNavigatorLocationChanged`, `slotItemActivated` |
| 6 | `views/dolphinview.h` + `.cpp` | Constructor, `setUrl`, selection signals, file operations, `requestContextMenu` |
| 7 | `kitemviews/kitemmodelbase.h` | Index + role concept, model signals |
| 8 | `kitemviews/kfileitemmodel.h` + `.cpp` | `ItemData`, `KDirLister` integration, `slotItemsAdded`/`insertItems`, `rolesInfoMap` |
| 9 | `kitemviews/kitemlistcontroller.cpp` | `onPress`, `onRelease`, `keyPressEvent`, rubber band, drag |
| 10 | `kitemviews/kitemlistselectionmanager.cpp` | Selection state |
| 11 | `dolphincontextmenu.cpp` | Context menu construction, `addAdditionalActions` |
| 12 | `views/versioncontrol/kversioncontrolplugin.h` + `versioncontrolobserver.cpp` | Dolphin's only own plugin system |
| 13 | `itemactions/movetonewfolderitemaction.*` + `itemactions/CMakeLists.txt` | Minimal plugin |

### Look at when needed

| File | When |
|---|---|
| `kitemviews/kitemlistview.cpp` | Layout, animations, widget recycling, drop indicator |
| `kitemviews/kfileitemmodelrolesupdater.cpp` | Previews, icons, folder sizes, overlays |
| `kitemviews/kstandarditemlistwidget.cpp` | Painting an item |
| `views/dolphinfileitemlistwidget.cpp` | VCS presentation |
| `views/dolphinviewactionhandler.cpp` | View actions |
| `panels/panel.h`, `panels/information/*` | Panels, preview panel |
| `views/viewproperties.cpp` | View properties per folder |
| `views/draganddrophelper.cpp` | Drop behavior |
| `settings/dolphinsettingsdialog.cpp`, `settings/contextmenu/contextmenusettingspage.cpp` | Settings, plugin activation |
| `dolphinui.rc` | Menu/toolbar structure |
| `global.cpp`, `dbusinterface.cpp` | D-Bus, instance reuse |
| `selectionmode/*`, `search/*` | Selection mode, search |

### KDE infrastructure, ignore at first

`po/`, `doc/`, `LICENSES/`, `REUSE.toml`, `.gitlab-ci.yml`, `.kde-ci.yml`, `.flatpak-manifest.json`,
`cmake/`, `completions/`, `appiumtests/`, `userfeedback/`, `settings/kcm/`, `settings/*.upd` + `*.py` +
update helpers (kconf_update), `kitemviews/accessibility/`, `dolphinpart*` (only relevant if the fork
should be embeddable in Konqueror), `Messages.sh`, `admin/` (until needed).

---

## 12. Observations in passing

- **`settings/kcm/kcmdolphinnavigation.*`** is dead code: the file is in no CMakeLists, the referenced
  JSON file and `NavigationSettingsPage` are missing.
- **`DolphinGeneralConfigModule::defaults()`** (`settings/kcm/kcmdolphingeneral.cpp:67–72`) calls
  `applySettings()` instead of `restoreDefaults()`. Probably an upstream bug.
- VCS enabling compares with the translated plugin name instead of the ID
  (`versioncontrolobserver.cpp:301`).
- In `hidefileitemaction.json`, `X-KDE-Show-In-Submenu` is a boolean, in the other two plugins the
  string `"true"`.
- `smallhash.h` is not a fork change but an upstream optimization (commits `f9488f6`, `6a520e6`,
  July 2026).
