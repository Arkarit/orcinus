# Dolphin-Architektur – Landkarte für Whale

Stand: Upstream-Dolphin `e0daa45` (26.11.70-Entwicklungsstand) + Whale-Commit `97e69ec` (nur `CLAUDE.md`).
Erstellt: 2026-09-25. Rein lesende Analyse; alle wichtigen Aussagen wurden im Sourcecode nachgeprüft.
Pfade relativ zu `src/`, sofern nicht anders angegeben. Zeilennummern beziehen sich auf diesen Stand und
verschieben sich bei Upstream-Merges.

---

## 0. Das Wichtigste in 10 Punkten

1. Dolphin ist eine **Qt6/KF6-Widgets-Anwendung in C++20** mit CMake/ECM. Kein QML.
2. Die eigentliche Dateiansicht ist **kein QListView/QTreeView**, sondern ein eigenes, auf
   `QGraphicsView` basierendes Model/View/Controller-Framework: `kitemviews/` (`KFileItemModel`,
   `KItemListView`, `KItemListController`, `KItemListSelectionManager`).
3. Dateisystemzugriff läuft **vollständig über KIO** (`KDirLister`, `KIO::*Job`). Dolphin selbst
   kennt keine Dateisystem-Details – dadurch funktionieren `smb:`, `sftp:`, `trash:`, `zip:` usw. „gratis“.
4. Objekt-Hierarchie: `DolphinMainWindow` → `DolphinTabWidget` → `DolphinTabPage` (Split) →
   `DolphinViewContainer` (Leisten drumherum) → `DolphinView` (Dateiansicht, fensterunabhängig).
5. Das Hauptfenster verteilt den Zustand der **aktiven** View per Signal an Panels usw.:
   `selectionChanged`, `urlChanged`, `requestItemInfo`, `fileItemsChanged`.
6. Menüs/Toolbar sind **KXMLGUI** (`dolphinui.rc`); alle Actions leben in der `KActionCollection`.
7. Die Einstellungen kommen aus **KConfigXT**: Aus `*.kcfg`-Dateien werden Singleton-Klassen wie
   `GeneralSettings::self()` generiert.
8. Echte In-Process-Plugins gibt es nur für: **Versionskontrolle** (`KVersionControlPlugin`, Namespace
   `dolphin/vcs`), **Kontextmenü-Actions** (`KAbstractFileItemActionPlugin`, geladen von KIO) und
   **Overlay-Icons** (`KOverlayIconPlugin`). Panels, Spalten, View-Modi, Statusleiste, Info-Panel,
   Suche usw. sind **fest eingebaut**.
9. Information-Panel, Tooltips und Baloo-Metadaten existieren nur bei `HAVE_BALOO` (Baloo +
   baloo-widgets gefunden).
10. Der beste Einstieg für ein Whale-Plugin-System ist das Signal-Bündel in
    `DolphinMainWindow::connectViewSignals()`/`activeViewChanged()`. Hinzu kommen die Stellen
    `setupDockWidgets()`, `DolphinContextMenu::addAdditionalActions()` und
    `KFileItemModelRolesUpdater::rolesData()`.

---

## 1. Projektstruktur

### 1.1 Repository-Wurzel

| Pfad | Zweck | Art |
|---|---|---|
| `CMakeLists.txt` | Versionen, `find_package`, Feature-Flags (`HAVE_BALOO`, `HAVE_KUSERFEEDBACK`, `HAVE_PACKAGEKIT`, `HAVE_TERMINAL`, `HAVE_X11`) | Build |
| `CMakePresets.json` | In-Tree-Presets `dev`, `asan`, `unity`, … | Build |
| `src/` | **Der gesamte Dolphin-Code** | Dolphin |
| `doc/` | DocBook-Handbuch (KDocTools) | KDE-Infrastruktur |
| `po/` | Übersetzungen (~100 Sprachen), von KDE-l10n per `GIT_SILENT`-Commits gepflegt | KDE-Infrastruktur |
| `appiumtests/` | UI-Tests über Selenium/AT-SPI (`dolphintest.py`) | Test |
| `cmake/` | `DbusInterfaceMacros.cmake`, `FindGem.cmake` | Build |
| `completions/` | zsh-Completion | Infrastruktur |
| `LICENSES/`, `REUSE.toml` | SPDX-Lizenzen; CI prüft REUSE-Konformität | KDE-Infrastruktur |
| `.gitlab-ci.yml`, `.kde-ci.yml` | KDE-CI-Templates und Abhängigkeitsliste | KDE-Infrastruktur |
| `.flatpak-manifest.json` | Flatpak-Build | Infrastruktur |
| `DolphinVcsConfig.cmake.in` | CMake-Paket für die öffentliche VCS-Plugin-Bibliothek | Plugin-SDK |
| `plasma-dolphin.service.in` | systemd-User-Service für `dolphin --daemon` | Infrastruktur |

### 1.2 Build-Targets (`src/CMakeLists.txt`)

```
libdolphinvcs.so  (SHARED, installiert + Header)   ← öffentliche VCS-Plugin-API
        ▲
libdolphinprivate.so (SHARED)                      ← kitemviews/, views/, View-Settings
        ▲             ▲
        │        dolphinpart.so (MODULE, kf6/parts) ← DolphinView als KPart (Konqueror)
        │
libdolphinstatic.a (STATIC)                        ← App-Shell: Hauptfenster, Tabs, Panels,
        ▲                                             Suche, Selection-Mode, Settings-Dialog …
dolphin (Executable) = main.cpp + dbusinterface.cpp

Außerdem: kcm_dolphinviewmodes / kcm_dolphingeneral (KCMs), servicemenuinstaller,
kf6/kfileitemaction/{movetonewfolder,hidefile,setfoldericon}itemaction.so, kconf_update-Helfer, Tests
```

Merksatz: **Alles, was die Dateiansicht betrifft, liegt in `dolphinprivate`. Alles, was das Fenster
betrifft, liegt in `dolphinstatic`.** Die Unit-Tests linken gegen `dolphinprivate`.

### 1.3 Verzeichnisse unter `src/`

| Verzeichnis | Aufgabe | Wichtige Klassen |
|---|---|---|
| *(Wurzel)* | App-Shell | `DolphinMainWindow`, `DolphinTabWidget`/`TabBar`/`TabPage`, `DolphinViewContainer`, `DolphinContextMenu`, `DolphinNavigatorsWidgetAction`, `DolphinUrlNavigator`, `DolphinDockWidget`, `DolphinBookmarkHandler`, `DBusInterface`, `global.h` (Namespace `Dolphin`) |
| `kitemviews/` | Eigenes MVC-Framework der Dateiansicht | `KItemModelBase`, `KFileItemModel`, `KItemListView`, `KStandardItemListView`, `KFileItemListView`, `KItemListWidget`, `KStandardItemListWidget`, `KFileItemListWidget`, `KItemListController`, `KItemListSelectionManager`, `KItemListContainer`, `KFileItemModelRolesUpdater`, `KItemSet`/`KItemRange` |
| `kitemviews/private/` | Interna | Layouter, Rubberband, Role-Editor (Inline-Rename), Keyboard-Suche, Selection-Toggle, Filter, Sortieralgorithmus, `KDirectoryContentsCounter`, `KBalooRolesProvider`, `KPixmapModifier`, `KFileItemClipboard` |
| `kitemviews/accessibility/` | QAccessible-Implementierungen | |
| `views/` | Dolphin-spezifische Dateiansicht | `DolphinView`, `DolphinItemListView`, `DolphinFileItemListWidget`, `DolphinViewActionHandler`, `ViewProperties`, `DragAndDropHelper`, `ViewModeController`, `ZoomLevelInfo` |
| `views/versioncontrol/` | VCS-Plugin-API + Anbindung | `KVersionControlPlugin`, `VersionControlObserver`, `UpdateItemStatesThread` |
| `views/tooltips/` | Hover-Tooltips (nur Baloo) | `ToolTipManager`, `DolphinFileMetaDataWidget` |
| `panels/` | Dock-Panels | `Panel` (Basis), `places/PlacesPanel`, `folders/FoldersPanel`, `information/InformationPanel`, `terminal/TerminalPanel` |
| `search/` | Suchleiste, Query-Aufbau | `Search::Bar`, `Search::Popup`, `DolphinQuery`, `selectors/*` |
| `selectionmode/` | Touch-freundlicher Auswahlmodus | `SelectionMode::TopBar`, `BottomBar`, `BottomBarContentsContainer`, `SingleClickSelectionProxyStyle` |
| `settings/` | Einstellungsdialog, `*.kcfg`, Migrationen | `DolphinSettingsDialog`, `SettingsPageBase`, Seiten in `interface/`, `viewmodes/`, `contextmenu/`, `trash/`, `userfeedback/`, `kcm/` |
| `statusbar/` | Statusleiste, freier Speicher | `DolphinStatusBar`, `StatusBarSpaceInfo`, `MountPointObserver`, `DiskSpaceUsageMenu` |
| `filterbar/` | Namensfilter unter der View | `FilterBar` |
| `admin/` | „Als Administrator handeln“ (kio-admin) | `Admin::Bar`, `Admin::WorkerIntegration` |
| `itemactions/` | Mitgelieferte Kontextmenü-Plugins | `MoveToNewFolderItemAction`, `HideFileItemAction`, `SetFolderIconItemAction` |
| `trash/` | Papierkorb-Zustand | `Trash` |
| `userfeedback/` | Telemetrie (optional) | `DolphinFeedbackProvider` |
| `tests/` | QTest-Unit-Tests | |
| `icons/` | App-Icons | |

Hilfsdateien in der Wurzel: `smallhash.h` (kompakte Rollen-Map pro Item, Upstream-Optimierung vom
Juli 2026), `animatedheightwidget.*` (Basis aller ein-/ausfahrenden Leisten),
`middleclickactioneventfilter.*`, `disabledactionnotifier.*`, `dolphinplacesmodelsingleton.*`,
`dolphinpackageinstaller.*` (PackageKit), `servicemenushortcutmanager.*`, `dolphinremoveaction.*`.

### 1.4 Schichten

| Schicht | Wo |
|---|---|
| Anwendung / Shell | `main.cpp`, `dbusinterface.*`, `global.*`, `dolphinmainwindow.*`, `dolphintab*`, `dolphinviewcontainer.*`, `*.rc` |
| UI-Widgets | `filterbar/`, `search/`, `selectionmode/`, `statusbar/`, `admin/bar`, `panels/*`, `views/tooltips/`, `dolphinurlnavigator` |
| Views | `kitemviews/k*listview*`, `k*listwidget*`, `kitemlistcontainer`, `views/dolphinview`, `views/dolphinitemlistview`, `views/dolphinfileitemlistwidget` |
| Models | `kitemviews/kitemmodelbase`, `kfileitemmodel` (+ `private/kfileitemmodelfilter`), `DolphinPlacesModel`, `settings/servicemodel` |
| Controller | `kitemviews/kitemlistcontroller`, `kitemlistselectionmanager`, `views/dolphinviewactionhandler`, `views/viewmodecontroller`, `views/versioncontrol/versioncontrolobserver` |
| Services/Helfer | `kfileitemmodelrolesupdater`, `private/kdirectorycontentscounter*`, `private/kbaloorolesprovider`, `draganddrophelper`, `trash/dolphintrash`, `statusbar/mountpointobserver*`, `dolphinpackageinstaller`, `search/dolphinquery` |
| Settings | `settings/**`, `views/viewproperties`, `panels/*/*.kcfg`, `search/*.kcfg` |

### 1.5 Dolphin-eigen vs. KDE-Infrastruktur

- **Dolphin-eigen:** alles unter `src/`, insbesondere das komplette `kitemviews`-Framework.
- **Nur genutzt (KDE Frameworks):**
  - **KIO:** `KDirLister`, `KFileItem`, Jobs, `KIO::PreviewJob`, `KFileItemActions`, `KNewFileMenu`,
    `KFilePlacesModel`/`View`, `KUrlNavigator`, `KOverlayIconPlugin`.
  - **KXMLGUI:** `KXmlGuiWindow`, `KActionCollection`.
  - **KConfig/KConfigXT.**
  - **KParts:** Dolphin als Part; Konsole-Part im Terminal-Panel.
  - **Baloo** und **baloo-widgets** (`Baloo::FileMetaDataWidget`), **KFileMetaData**.
  - **Solid, KNewStuff, KI18n, KCrash, KDBusAddons, KBookmarks, KCMUtils, KUserFeedback, PackageKit-Qt.**
  - **Qt Multimedia:** nur für den Mediaplayer im Info-Panel.
- **Zunächst ignorieren:** `po/`, `doc/`, `LICENSES/`, CI-Dateien, `cmake/`, `completions/`,
  `userfeedback/`, kconf_update-Migrationen (`settings/*.upd`, `*.py`), `accessibility/`.

---

## 2. Programmstart

### 2.1 Ablauf von `main()` (`main.cpp:53–293`)

```
main()
 ├─ root+sudo?  → Hinweis „dolphin --sudo“ und Abbruch                    (55–68)
 ├─ QApplication, Icon-Theme, Style, KLocalizedString-Domain "dolphin"     (73–93)
 ├─ KAboutData + KCrash::initialize()                                      (95–131)
 ├─ QCommandLineParser: --select --split --new-window --sudo/--admin
 │                      --daemon [--self-test] +[Url]                      (133–156)
 ├─ urls = Dolphin::validateUris(args)                   (global.cpp:22)
 ├─ --daemon → KDBusService + DBusInterface, KEIN Fenster → app.exec()     (172–192)
 ├─ ohne --new-window: Dolphin::attachToExistingInstance(...)              (194–208)
 │      → fragt per D-Bus laufende Instanzen (org.kde.dolphin-<pid>,
 │        /dolphin/Dolphin_1), übergibt URLs, return 0
 ├─ keine URLs → Dolphin::homeUrl()                                        (210–218)
 ├─ mainWindow = new DolphinMainWindow()                                   (220)
 │   openDirectories(urls, split) | openFiles(urls, split); show()
 ├─ KDBusService(Multiple) + DBusInterface (org.freedesktop.FileManager1)  (231–236)
 ├─ Session-Restore / „Tabs merken“: mainWindow->restore(1)                (238–273)
 └─ app.exec()
```

Session-Restore läuft manuell über `KXmlGuiWindow::classNameOfToplevel(1)` + `restore(1)` und nicht
über das `kRestoreMainWindows`-Makro.

**Für die Entwicklung wichtig:** Ohne `--new-window` gibt ein frisch gebautes Dolphin/Whale seine
URLs an eine bereits laufende System-Dolphin-Instanz ab und beendet sich. Zum Testen also immer
`--new-window` verwenden. Solange Whale noch Dolphins D-Bus-Namen trägt, gilt das auch
umgekehrt.

### 2.2 Konstruktor `DolphinMainWindow::DolphinMainWindow()` (`dolphinmainwindow.cpp:117–273`)

In dieser Reihenfolge:
1. `new MainWindowAdaptor(this)` (142): D-Bus-Interface `org.kde.dolphin.MainWindow`
2. `KIO::FileUndoManager` mit eigenem UI-Interface (158–164)
3. `DolphinNavigatorsWidgetAction` (Adressleiste(n) in der Toolbar, 173)
4. `DolphinTabWidget` als Central Widget; `activeViewChanged` → `DolphinMainWindow::activeViewChanged` (175–180)
5. `setupActions()` (183): alle Fenster-Actions
6. `DolphinViewActionHandler` (185): alle View-Actions
7. `setupDockWidgets()` (206): Panels
8. `ServiceMenuShortcutManager` (209), `setupFileItemActions()` (211): `KFileItemActions`
9. `setupGUI(..., "dolphinui.rc")` (213–214): KXMLGUI baut Menüs und Toolbar
10. Hamburger-Menü, Event-Filter, Config-Watcher (217–272)

### 2.3 Zentrale Objekte und Singletons

`GeneralSettings::self()` und weitere KConfigXT-Singletons, `DolphinPlacesModelSingleton::instance()`,
`DolphinNewFileMenuObserver::instance()`, `KIO::FileUndoManager::self()`, `Trash::instance()`,
`KFileItemClipboard::instance()`, `Admin::WorkerIntegration`, `DolphinUrlNavigatorsController`
(nur statisch).

### 2.4 Klassen, die man zuerst verstehen sollte

`DolphinMainWindow` → `DolphinViewContainer` → `DolphinView` → `KFileItemModel` →
`KItemListController`. Die Details stehen in der Lesereihenfolge (Kapitel 11).

---

## 3. Hauptfenster und UI

### 3.1 Aufbau

```
DolphinMainWindow (KXmlGuiWindow, dolphinui.rc)
 ├─ actionCollection  ◄─ setupActions(), DolphinViewActionHandler, DolphinBookmarkHandler,
 │                       DolphinRecentTabsMenu, DolphinNewFileMenu, Admin::WorkerIntegration
 ├─ KHamburgerMenu ── aboutToShowMenu ─► updateHamburgerMenu()   (dynamisch neu aufgebaut)
 ├─ Toolbar: DolphinNavigatorsWidgetAction "url_navigators" (1–2 DolphinUrlNavigator)
 ├─ centralWidget: DolphinTabWidget (+ DolphinTabBar)
 │    └─ DolphinTabPage  (QSplitter → 1 oder 2 Container = Split-View)
 │         └─ DolphinViewContainer   (QGridLayout, Zeilen von oben nach unten:)
 │              Search::Bar │ Admin::Bar │ KMessageWidget │ SelectionMode::TopBar
 │              DolphinView ─ KItemListContainer ─ KItemListController ─ DolphinItemListView ─ KFileItemModel
 │              SelectionMode::BottomBar │ FilterBar │ DolphinStatusBar
 └─ DolphinDockWidgets: Places, Folders, Information, Terminal
```

- `DolphinTabWidget` (`dolphintabwidget.cpp`): `openNewTab` (229), `openDirectories` (264),
  `openFiles` (297), `currentTabChanged` (519), Session-Format `saveProperties`/`readProperties`.
- `DolphinTabPage` (`dolphintabpage.cpp`): `setSplitViewEnabled` (81), `slotViewActivated` (482),
  `createViewContainer` (535).
- `DolphinViewContainer` (`dolphinviewcontainer.cpp`): `setUrl` (627), `setSelectionModeEnabled`
  (401), `showMessage` (481), `slotItemActivated` (753), Fehlerbehandlung in `slotErrorMessageFromView`
  (1059). Jeder Container hat einen **internen** `DolphinUrlNavigator` mit der History; der sichtbare
  Navigator in der Toolbar wird nur „angeschlossen“ (`connectUrlNavigator`, 279).

### 3.2 Das Konzept „aktive View“ – der zentrale Verteiler

```
DolphinView::setActive → activated
  → DolphinTabPage::slotViewActivated → activeViewChanged(container)
  → DolphinTabWidget::activeViewChanged
  → DolphinMainWindow::activeViewChanged (1665–1718)
       ├ trennt alle Verbindungen zur alten View
       ├ connectViewSignals(container)            (2797–2848)
       ├ m_actionHandler->setCurrentView(view)
       ├ update*Actions()
       └ Q_EMIT urlChanged(url), selectionChanged(selection)   → Panels
```

`connectViewSignals()` verbindet unter anderem:
- `DolphinView::selectionChanged` → `slotSelectionChanged` (407) → `Q_EMIT selectionChanged`
- `requestContextMenu` → `openContextMenu` (1524)
- `urlActivated` → `handleUrl` (1483)
- `tabRequested`/`windowRequested`, `goBack`/`goForward`/`goUp`
- den internen Navigator: `urlChanged` → `changeUrl` (367) → `Q_EMIT urlChanged`

**Das ist die wichtigste Stelle für ein späteres Plugin-System:** Hier laufen die URL, die Auswahl
und die Hover-Information der aktiven View zusammen.

### 3.3 Menüs, Toolbars, Actions

- **`DolphinMainWindow::setupActions()`** (1798–2339): Datei, Bearbeiten, Ansicht (Split, Reload,
  Stop, editierbare Adresse), Gehe zu (Back/Forward mit History-Popup, Recent Tabs), Extras
  (Terminal, Vergleichen, Festplattenbelegung), Lesezeichen, Einstellungen, Tab-Wechsel.
- **`DolphinViewActionHandler::createActions()`** (`views/dolphinviewactionhandler.cpp:81–436`):
  Ordner/Datei anlegen, Umbenennen, Papierkorb, Löschen, Duplizieren, Eigenschaften, View-Modus
  (Symbole/Kompakt/Details), Zoom, Sortierung, Gruppierung, Zusatzinfos (Spalten), Vorschau,
  versteckte Dateien. `setCurrentView()` (45–74) verdrahtet ca. 15 View-Signale neu.
- **KXMLGUI:** `dolphinui.rc` (Resource `/kxmlgui5/dolphin`, über `dolphin.qrc`) definiert Menüs, die
  `mainToolBar` und die States `has_selection`/`has_no_selection`. Letztere schaltet
  `updateFileAndEditActions()` (2630) um. `dolphinuiforphones.rc` ist die Mobile-Variante (ein Test
  prüft, dass beide synchron bleiben).
- **Hamburger-Menü:** `updateHamburgerMenu()` (1553–1639) wird bei jedem Öffnen neu gebaut.

### 3.4 Die zentrale Dateiansicht

`DolphinView` (`views/dolphinview.cpp`, Konstruktor ab 89) erzeugt `KFileItemModel` (134),
`DolphinItemListView` (135), `KItemListController(model, view)` (140) und `KItemListContainer` (168).
`DolphinView` kennt das Hauptfenster **nicht**; es kommuniziert nur über Signale. Genau deshalb kann
`dolphinpart` es in Konqueror einbetten. Details zum MVC-Framework stehen in Kapitel 5.

### 3.5 Kontextmenüs (`dolphincontextmenu.cpp`)

Aufrufkette: `KItemListController::itemContextMenuRequested`/`viewContextMenuRequested` →
`DolphinView::slotItemContextMenuRequested` (1318) / `slotViewContextMenuRequested` (1333) →
`requestContextMenu(pos, item, selectedItems, url)` → `DolphinMainWindow::openContextMenu`
(1524–1541) → `new DolphinContextMenu(this, item, selection, url, m_fileItemActions)` → `exec()`.

`DolphinContextMenu::addAllActions()` (69–115) wählt anhand von Klick-Ziel und URL-Schema:

| Fall | Methode | Inhalt |
|---|---|---|
| Papierkorb + Item | `addTrashItemContextMenu` (176) | Wiederherstellen, Ausschneiden, Kopieren, Löschen, Eigenschaften |
| Papierkorb leer/Hintergrund | `addTrashContextMenu` (136) | Papierkorb leeren usw. |
| Item(s) | `addItemContextMenu` (280–352) | Öffnen (in Tab/Fenster/Split), „Öffnen mit“, Neu, Standard-Actions (`insertDefaultItemActions` 399), **Plugins** (`addAdditionalActions`), Kopieren/Verschieben nach, Eigenschaften |
| Hintergrund | `addViewportContextMenu` (354–397) | Neu, Einfügen, Sortieren/Ansicht, **Plugins**, Eigenschaften |

**Plugin-Einhängepunkt:** `addAdditionalActions()` (545–562)
- `m_fileItemActions->addActionsTo(this, MenuActionSource::All, …)`: Service-Menüs und
  KFileItemAction-Plugins
- danach `view->versionControlActions(selectedItems)`: VCS-Plugins

Das Menü ist fest strukturiert (Enum `ContextType`, Schema-Abfragen). Es gibt keinen
Hook/kein Signal „Menü wird gebaut“.

### 3.6 Panels / Docks

- Basisklasse `Panel : QWidget` (`panels/panel.h`): `setUrl()` (46) ruft das rein virtuelle
  `urlChanged()` auf; außerdem `refreshUrl()`, `readSettings()`, `setCustomContextMenuActions()`.
  Die Inhalte werden lazy in `showEvent` erzeugt.
- **Ausnahme:** `PlacesPanel` erbt direkt von `KFilePlacesView` (KIO), **nicht** von `Panel`.
- Erzeugt werden alle Panels fest in `DolphinMainWindow::setupDockWidgets()` (2341–2606). Jedes
  steckt in einem `DolphinDockWidget`, der Toggle kommt aus `createPanelAction` (2979).

| Panel | Taste | Eingänge (vom Hauptfenster) | Ausgänge (zum Hauptfenster) |
|---|---|---|---|
| Information (`HAVE_BALOO`) | F11 | `urlChanged`, `selectionChanged`, `requestItemInfo`, `fileItemsChanged`, `settingsChanged` | `urlActivated` → `handleUrl` |
| Ordner (Baum, auf kitemviews gebaut) | F7 | `urlChanged` | `folderActivated` → `changeUrl`, Tab-Requests |
| Terminal (Konsole-KPart) | F4 | `urlChanged`, `urlRefreshed` | `changeUrl` → `slotTerminalDirectoryChanged` |
| Orte (`KFilePlacesView`) | F9 | `urlChanged` | `placeActivated`, Tab/Fenster/Split-Requests, Aushängen |

Panels sprechen **nie direkt** mit der View, sondern nur über Signale/Slots des Hauptfensters.
Dieses Muster kann man für Plugin-Panels direkt übernehmen.

---

## 4. Dateisystem und Navigation

### 4.1 Repräsentation

- Eine Datei entspricht einem **`KFileItem`** (KIO). Es enthält URL, `UDSEntry`, MIME-Typ, Icon,
  Rechte usw.
- **`KFileItemModel`** (`kitemviews/kfileitemmodel.h/.cpp`) ist eine flache, sortierte Liste:
  - `ItemData { KFileItem item; SmallHash values; ItemData *parent; }` (h:385–389); `parent` ist nur
    im Baum-Modus gesetzt.
  - Items werden über den **Index (int)** angesprochen, Werte über **Rollen** (`QByteArray`-Keys wie
    `"text"`, `"size"`, `"iconPixmap"`, `"version"`). `data(int)` (382) liefert billige Rollen lazy
    (`retrieveData` 2231). Teure Rollen füllt `KFileItemModelRolesUpdater` asynchron nach.
  - Die Rollen-Tabelle ist statisch: `rolesInfoMap()` (3157).
  - Lookup: `fileItem(int)` 717, `fileItem(QUrl)` 726, `index(KFileItem)` 735, `index(QUrl)` 740.
- `KItemModelBase` (`kitemviews/kitemmodelbase.h`) ist die abstrakte Basis (kein `QAbstractItemModel`!)
  mit den Signalen `itemsInserted`/`itemsRemoved`/`itemsMoved`/`itemsChanged(ranges, roles)`.
- Die Datenquelle ist ein normaler **`KDirLister`** (angelegt in `KFileItemModel`, cpp:288). Er
  liefert `itemsAdded` → `slotItemsAdded` (1451) → gepuffert → `insertItems` (1827). Weitere Slots:
  `itemsDeleted` → `slotItemsDeleted` (1558), `refreshItems` → `slotRefreshItems` (1629),
  `listingDirCompleted` → `slotCompleted`.
- Baum-Ansicht (aufklappbare Ordner in Details): `setExpanded` (874) öffnet zusätzliche Ordner im
  selben `KDirLister` (`KDirLister::Keep`).
- Filter: `private/kfileitemmodelfilter.*` (Text/Glob/Regex + MIME), `applyFilters` (1116).
- Sortierung: `lessThan` (2398), `sort` (2471) mit parallelem Mergesort
  (`private/kfileitemmodelsortalgorithm.h`).
- Gruppierung: `groups()` (664) mit `nameRoleGroups`, `sizeRoleGroups`, `timeRoleGroups` usw.

### 4.2 Navigation

```
Adressleiste / Panels / Doppelklick / Back-Forward-Up
      │
      ▼
DolphinViewContainer::setUrl (627) → interner KUrlNavigator (History!)
      │ urlChanged
      ├──► DolphinViewContainer::slotUrlNavigatorLocationChanged (934)
      │       Protokoll listbar? → m_view->setUrl(url)
      │       sonst → QDesktopServices::openUrl / Fehler
      │           ▼
      │    DolphinView::setUrl (819) → loadDirectory (2460)
      │           ▼
      │    KFileItemModel::loadDirectory (348) → KDirLister::openUrl  (asynchron, KIO-Worker)
      │           ▼
      │    slotItemsAdded → insertItems → itemsInserted → View zeichnet
      │    slotCompleted → DolphinView::updateViewState (2064): Auswahl/Scrollposition wiederherstellen
      │
      └──► DolphinMainWindow::changeUrl (367) → Q_EMIT urlChanged → alle Panels
```

- Die History liegt pro Container im `KUrlNavigator`. Für jeden Ort werden Auswahl, Scrollposition
  und aufgeklappte Ordner über `DolphinView::saveState`/`restoreState` (1810/1785) mitgespeichert.
- Aktivieren eines Items: `KItemListController::itemActivated` → `DolphinView::slotItemActivated`
  (1229) → `DolphinViewContainer::slotItemActivated` (753). Ordner (und Archive über
  `openItemAsFolderUrl` 1852 → `zip:`/`tar:`) werden navigiert, Dateien per `KIO::OpenUrlJob`
  geöffnet.
- Redirects (z. B. `desktop:/`) laufen über `KDirLister::redirection` →
  `DolphinView::slotDirectoryRedirection` (2016) → `DolphinViewContainer::redirect` (994).

### 4.3 Rolle von KIO, URLs, Frameworks

Alles ist `QUrl`-basiert. Jedes Protokoll mit einem KIO-Worker funktioniert automatisch.
Sonderbehandlungen sind hart codiert über ca. 53 `url.scheme() == "…"`-Vergleiche (u. a. `trash`,
`admin`, `*search*`, `timeline`, `recentlyused`, `smb`, `network`, `kdeconnect`). Eine Registry gibt
es nicht.

### 4.4 Dateioperationen (alle in `views/dolphinview.cpp`, sofern nicht anders angegeben)

| Operation | Funktion | KIO |
|---|---|---|
| Kopieren / Ausschneiden | `copySelectedItemsToClipboard` 969 / `cutSelectedItemsToClipboard` 961 | `KFileItemModel::createMimeData`, `KIO::setClipboardDataCut` |
| Einfügen | `paste` 1020, `pasteIntoFolder` 1025 → `pasteToUrl` 2711 | `KIO::paste` → `PasteJob` |
| Kopieren/Verschieben in andere Split-Hälfte | `copySelectedItems` 976, `moveSelectedItems` 998 | `KIO::copy`/`KIO::move` |
| Duplizieren | `duplicateSelectedItems` 1033 | `KIO::copyAs` |
| Papierkorb / Löschen | `trashSelectedItems` 935 / `deleteSelectedItems` 948 | `KIO::DeleteOrTrashJob` |
| Umbenennen | `renameSelectedItems` 879 → inline über `KItemListRoleEditor` → `slotRoleEditingFinished` 2309, oder `KIO::RenameFileDialog` | `KIO::moveAs` |
| Neu anlegen | `DolphinNewFileMenu : KNewFileMenu` (`dolphinnewfilemenu.*`), `DolphinMainWindow::createDirectory` 834 | KNewFileMenu |
| Drag & Drop | `DolphinView::dropUrls` 1547 → `DragAndDropHelper::dropUrls` | `KIO::drop` → `DropJob` |
| Rückgängig | `DolphinMainWindow::undo` 881 | `KIO::FileUndoManager` |

Die Actions dazu werden in `DolphinViewActionHandler` (Rename 527, Trash 538, Delete 549, Duplicate
896) und `DolphinMainWindow` (Cut 909, Copy 919, Paste 929) verdrahtet.

---

## 5. Selection

### 5.1 MVC der Dateiansicht

```
               DolphinView  (besitzt Model, View, Container; verbindet Controller-Signale 210–227)
                    │
KItemListContainer (QAbstractScrollArea + QGraphicsView)
                    │ Events: KItemListView::event() → controller->processEvent()
                    ▼
            KItemListController  ── besitzt ──► KItemListSelectionManager
            (Maus, Tastatur, DnD,               KItemListKeyboardSearchManager
             Rubberband, Touch)
               │ liest/ändert                   ▲ itemsInserted/Removed/Moved (von der View weitergereicht)
               ▼                                │
         KFileItemModel ── Signale ──► KItemListView → KStandardItemListView → KFileItemListView
                                         → DolphinItemListView
                                         ├ recycelte KItemListWidgets (… → DolphinFileItemListWidget)
                                         ├ KItemListRubberBand, Autoscroll
                                         └ KFileItemModelRolesUpdater (Icons/Previews/Größen)
```

### 5.2 Wo die Auswahl lebt

`KItemListSelectionManager` (`kitemviews/kitemlistselectionmanager.cpp`):
- **Zustand:** `m_currentItem`, `m_anchorItem`, `KItemSet m_selectedItems`, „anchored selection“ für
  Shift-Bereiche.
- **API:** `setCurrentItem` 24, `setSelectedItems` 52, `setSelected(index, count, Select|Deselect|Toggle)`
  104, `clearSelection` 150, `beginAnchoredSelection` 170, `endAnchoredSelection` 178.
- **Anpassung bei Modelländerungen:** `itemsInserted` 212, `itemsRemoved` 275, `itemsMoved` 323.
- `KItemSet` (`kitemset.h`) ist eine bereichskomprimierte Indexmenge.

### 5.3 Maus, Rubberband, Tastatur, Drag & Drop (`kitemviews/kitemlistcontroller.cpp`)

| Thema | Stelle |
|---|---|
| Klick-Logik (Ctrl/Shift, Toggle, Rechtsklick, Klick auf bereits Ausgewähltes) | `mousePressEvent` 604 → **`onPress` 1649–1837** |
| Loslassen, Einfach-/Doppelklick-Aktivierung, Expansion | `mouseReleaseEvent` 695 → **`onRelease` 1839–1930**, `mouseDoubleClickEvent` 722 |
| Rubberband | `startRubberBand` 1932, `mouseMoveEvent`-Zweig 667–690, **`slotRubberBandChanged` 1372**, Zeichnen in `KItemListView::slotRubberBandActivationChanged` 1716 |
| Tastatur | **`keyPressEvent` 238–524** (Auswahl-Übernahme 489–518) |
| Type-ahead | `private/kitemlistkeyboardsearchmanager.cpp`, `slotChangeCurrentItem` 526 |
| Drag-Start | `startDragging` 1451 |
| Drop | `dragMoveEvent` 855, `dropEvent` 930 → `itemDropEvent` → `DolphinView::slotItemDropEvent` 1527 |
| Kontextmenü | `contextMenuEvent` 775 |
| Touch | `tapTriggered` 1148, `tapAndHoldTriggered` 1184 (startet den Selection-Mode) |
| Hit-Zonen | `KItemListView::itemAt` 442, `isAboveSelectionToggle` 462, `isAboveExpansionToggle` 479, `isAboveText` 492 |
| Selection-Toggle (Häkchen beim Hover) | `private/kitemlistselectiontoggle.*`, `DolphinItemListView::setEnabledSelectionToggles` |

**Selection-Mode** (`selectionmode/`): `DolphinViewContainer::setSelectionModeEnabled` (401) →
`DolphinView::setSelectionModeEnabled` (345) → `KItemListController::setSelectionModeEnabled` (223).
In diesem Modus wirken Klicks wie mit gedrückter Ctrl-Taste, und die BottomBar zeigt passende
Actions.

### 5.4 Wie die Auswahl in die App gelangt

`KItemListSelectionManager::selectionChanged` → `DolphinView::slotSelectionChanged` (1679),
**entprellt** (0 ms beim Wechsel zwischen leer und nicht leer, sonst 300 ms) →
`emitSelectionChangedSignal` (1693) → `DolphinView::selectionChanged(KFileItemList)`. Empfänger:
- Statusleiste
- BottomBar
- `DolphinViewActionHandler`
- `DolphinMainWindow::slotSelectionChanged` (407): aktualisiert `KFileItemActions` und sendet
  `DolphinMainWindow::selectionChanged` an das Info-Panel weiter

Zugriffsfunktionen: `DolphinView::selectedItems()` 460, `selectedItemsCount()` 482, `selectAll` 844,
`invertSelection` 850, `clearSelection` 856, `markUrlsAsSelected` 488.

### 5.5 Stellen für Änderungen am Selection-Verhalten

1. `KItemListController::onPress`/`onRelease`: Klick-Semantik, Drag vs. Klick, Einfach-/Doppelklick
2. `KItemListController::keyPressEvent`: Tastatur-Selektion
3. `slotRubberBandChanged`, `startRubberBand`, `mouseMoveEvent`: Rubberband
4. `KItemListSelectionManager`: Anker-Semantik, Verhalten bei Modelländerungen
5. `KItemListView::isAbove*` und `selectionRect*` der Widgets: Trefferzonen
6. `DolphinView::slotSelectionChanged` (Entprellung), `updateSelectionState`/`updateViewState`
   (Wiederherstellung nach dem Laden oder nach Jobs)
7. Tests: `tests/kitemlistcontrollertest.cpp`, `kitemlistselectionmanagertest.cpp`

---

## 6. Plugins und Erweiterbarkeit – Überblick

| # | Mechanismus | Richtung | In-Process-Code von Dritten? | Beeinflusst |
|---|---|---|---|---|
| 1 | **VCS-Plugins** `KVersionControlPlugin` | in Dolphin | ja (C++) | Item-Status-Overlays, Textfarbe, Kontextmenü-Actions, Statusmeldungen |
| 2 | **KFileItemAction-Plugins** `KAbstractFileItemActionPlugin` | in KIO (Dolphin nutzt es) | ja (C++) | Kontextmenü-Einträge |
| 3 | **Service-Menüs** (`.desktop` in `kio/servicemenus`) | in KIO | nein (Kommandos) | Kontextmenü-Einträge (+ Shortcuts ab KIO 6.24) |
| 4 | **Overlay-Icon-Plugins** `KOverlayIconPlugin` | in KIO/Dolphin | ja (C++) | Emblem-Icons auf Items |
| 5 | **Thumbnailer** (KIO `ThumbnailCreator`) | in KIO | ja, aber im KIO-Worker | Vorschaubilder |
| 6 | **KIO-Worker** (Protokolle) | in KIO | eigener Prozess | neue „Dateisysteme“ |
| 7 | **KParts** | Dolphin → andere (`dolphinpart`); Konsole → Dolphin | – | Einbettung |
| 8 | **KCMs** `kcm_dolphin*` | Dolphin → Systemeinstellungen/Konqueror | – | Einstellungsseiten |
| 9 | **D-Bus** `org.freedesktop.FileManager1`, `org.kde.dolphin.MainWindow` | extern → Dolphin | nein | Fenster öffnen/aktivieren; **keine** Auswahl- oder Action-API |
| 10 | **KNewStuff** (`servicemenu.knsrc` + `servicemenuinstaller`) | Installation von Service-Menüs | – | – |
| 11 | **KXMLGUI** | Benutzer | – | Toolbar/Shortcuts für jede Action in der ActionCollection |

**Bereits erweiterbar:** Kontextmenü (Plugins, Service-Menüs), VCS-Anzeige und -Actions,
Overlay-Icons, Vorschaubilder, Protokolle, Toolbar-Belegung und Shortcuts.

**Fest eingebaut:**
- **Panels:** `setupDockWidgets`
- **View-Modi:** Enum `DolphinView::Mode`; das Item-Widget ist fest `DolphinFileItemListWidget`
  (`dolphinitemlistview.cpp:131`), obwohl `KItemListView::setWidgetCreator` existiert
- **Spalten/Rollen:** statische `rolesInfoMap`
- **Kontextmenü-Struktur**
- **Statusleiste**, Leisten im ViewContainer
- **Suche:** Enum `SearchTool`
- **Inhalt des Info-Panels und der Tooltips**
- **Einstellungsseiten:** feste Liste in `dolphinsettingsdialog.cpp:45–105`
- **URL-Schema-Sonderfälle**

---

## 7. Preview / Information-Panel

### 7.1 Vorschaubilder in der Dateiansicht

Zuständig ist `KFileItemModelRolesUpdater` (`kitemviews/kfileitemmodelrolesupdater.cpp`). Er wird von
`KFileItemListView::onModelChanged` erzeugt (`kfileitemlistview.cpp:243`).

```
KFileItemListView ─(sichtbarer Bereich, 50 ms entprellt)─► RolesUpdater::setVisibleIndexRange
  startUpdating() (907):
    updateVisibleIcons()     sichtbare Items synchron (≤200 ms): MIME-Typ, Icon
    indexesToResolve()       Priorität: sichtbar > ±5 Seiten > erste/letzte Seite > bis 500 Items
    startPreviewJob() (984)  EIN gebündelter KIO::PreviewJob(items, 128|256 px,
                             [PreviewSettings] Plugins)
      generated → slotGotPreview (581) → "iconPixmap" → KFileItemModel::setData
      rolesData() (1384): type, extension, iconOverlays (KOverlayIconPlugin),
                          Ordnergröße (KDirectoryContentsCounter, Worker-Thread),
                          Baloo-Rollen (KBalooRolesProvider)
  → KFileItemModel::itemsChanged → KItemListView::slotItemsChanged
  → KStandardItemListWidget::updatePixmapCache() → paint
```

Beim Überfahren (Hover) zeigt Dolphin animierte Vorschau-Sequenzen, etwa Video-Frames:
`loadNextHoverSequencePreview` (1066) erzeugt die Rolle `"hoverSequencePixmaps"`.

### 7.2 Information-Panel (`panels/information/`, nur `HAVE_BALOO`)

```
DolphinMainWindow: selectionChanged / requestItemInfo (Hover) / urlChanged / fileItemsChanged
        ▼
InformationPanel (informationpanel.cpp)
   setSelection 49 │ requestDelayedItemInfo 75 (300 ms) │ urlChanged 98 (200 ms)
        └──────────► showItemInfo() 218   ← DIE Entscheidungsstelle
              >1 ausgewählt → content->showItems(list)
              1 Item        → content->showItem(item)
              nichts        → KIO::stat(Ordner) → showItem(...)
        ▼
InformationPanelContent (informationpanelcontent.cpp)
   showItem 153
    ├─ refreshMetaData 301 → Baloo::FileMetaDataWidget::setItems   (extern: baloo-widgets)
    └─ refreshPreview 223   ← MIME-Weiche
         ├ Such-URL      → Icon
         ├ refreshPixmapView 165 → eigener KIO::PreviewJob → PixmapViewer
         ├ animiertes Bild → PixmapViewer::setAnimatedImageFileName (QMovie)
         └ audio/video   → MediaWidget (QtMultimedia: QMediaPlayer/QVideoWidget)
```

- Einstellungen: `dolphin_informationpanelsettings.kcfg`. Welche Metadaten-Felder angezeigt werden,
  speichert baloo-widgets selbst (`baloofileinformationrc`), nicht Dolphin.
- **Tooltips** (`views/tooltips/`, nur Baloo): `ToolTipManager::startContentRetrieval` (123) mit
  eigenem `KIO::PreviewJob` (512 px) + `DolphinFileMetaDataWidget` → `KToolTipWidget`.

### 7.3 Bausteine für ein universelles Preview-/Info-Panel

- **Wiederverwendbar:** `InformationPanel::showItemInfo()` (Logik Auswahl/Hover/Ordner),
  `InformationPanelContent::showItem`/`showItems`, `PixmapViewer`, `MediaWidget`, die
  Preview-Job-Verwaltung (`refreshPixmapView`, `refreshPixmapViewForItsSize`).
- **Natürlicher Erweiterungspunkt:** Die MIME-Weiche in `InformationPanelContent::refreshPreview()`
  wäre die Stelle für eine „Preview-Provider“-Registry (Text, PDF, Markdown, Code, Archive, …).
- **Einschränkungen:**
  - Ohne Baloo gibt es gar kein Info-Panel (`#if HAVE_BALOO` in `dolphinmainwindow.cpp:2367` und
    `src/CMakeLists.txt`).
  - Es laufen drei unabhängige PreviewJob-Pipelines (View, Panel, Tooltip).
  - Die Metadaten-Anzeige ist eine externe Blackbox (`Baloo::FileMetaDataWidget`). Für Whale wäre ein
    eigenes Widget auf Basis von `KFileMetaData` denkbar.
  - `InformationPanelContent` erzeugt seine Kinder mit dem Panel als Parent, nicht mit `this`.

---

## 8. Git-relevante Erweiterungen

### 8.1 Was es schon gibt

Die VCS-Plugin-API (`views/versioncontrol/kversioncontrolplugin.h`) ist genau für kontextabhängige
Git-Funktionen gebaut. Das **Git-Plugin selbst ist nicht in diesem Repo**, sondern in
`dolphin-plugins`. Auf diesem Rechner ist es installiert
(`/usr/lib/x86_64-linux-gnu/qt6/plugins/dolphin/vcs/fileviewgitplugin.so`, dazu svn, hg, bazaar,
dropbox), in `~/.config/dolphinrc` aber **nicht aktiviert**
(`[VersionControl] enabledPlugins` fehlt; aktivierbar unter Einstellungen → Kontextmenü).

Ablauf pro `DolphinView` (Observer angelegt in `dolphinview.cpp:269`):

```
KFileItemModel: itemsInserted / itemsChanged / directoryLoadingCompleted
   → VersionControlObserver::verifyDirectory (165)   (nur lokale Dateien, 100–500 ms entprellt)
   → initPlugins (291): KPluginMetaData::findPlugins("dolphin/vcs") ∩ enabledPlugins
   → searchPlugin (320): plugin->fileName() (z. B. ".git") existiert? / localRepositoryRoot()
   → UpdateItemStatesThread (Worker-Thread, globaler Mutex):
        beginRetrieval(dir) → itemVersion(item)* → endRetrieval()
   → slotThreadFinished (196): model->setData(index, {"version": ItemVersion})
   → DolphinFileItemListWidget::refreshCache (21–91): Textfarbe + Emblem "vcs-*"
Kontextmenü: DolphinContextMenu::addAdditionalActions → DolphinView::versionControlActions (803)
   → VersionControlObserver::actions (105) → plugin->versionControlActions(items)
                                             bzw. outOfVersionControlActions(items)
```

### 8.2 Wo ein Whale-Git-Plugin Informationen bekäme und UI bereitstellen könnte

| Bedarf | Vorhandene Quelle |
|---|---|
| Aktuelle Auswahl | `DolphinView::selectionChanged(KFileItemList)`; im Fenster: `DolphinMainWindow::selectionChanged` / `slotSelectionChanged` (407) |
| Aktuelles Verzeichnis | `DolphinMainWindow::urlChanged(QUrl)`; `DolphinView::url()` |
| Status pro Datei | Rolle `"version"` im `KFileItemModel` (von `VersionControlObserver` gefüllt) |
| Kontextmenü-Actions | `KVersionControlPlugin::versionControlActions()` – bereits vorhanden |
| Toolbar-/Shortcut-Actions | `actionCollection()`; Vorlage: `ServiceMenuShortcutManager` (stabile Namen + `KActionCategory`) |
| Eigenes Panel (z. B. Git-Log/Diff) | Muster aus `setupDockWidgets()` + `Panel` (heute nur fest einkompiliert) |
| Meldungen | `KVersionControlPlugin::infoMessage`/`errorMessage` → Statusleiste bzw. `DolphinViewContainer::showMessage` |

**Grenzen der VCS-API:**
- Ein Plugin bekommt nur Pfad, `KFileItem`s und `KFileItemList`, keinen Zugriff auf Fenster, View
  oder Panels.
- Nur lokale Dateien; pro Verzeichnis ist nur ein Plugin aktiv.
- Die Status-Enum und die Darstellung sind fest.
- Die Plugin-Instanzen existieren pro View, die Einstellungen werden pro View nur einmal gelesen.
- Das Aktivieren passiert über den **übersetzten** Namen (`p.name()`, `versioncontrolobserver.cpp:301`)
  statt über die Plugin-ID. Das kann beim Wechsel der UI-Sprache brechen.

---

## 9. Aktuelle Plugin-Architektur im Detail

### 9.1 VCS-Plugins

- **Technik:** C++/Qt; KCoreAddons `KPluginMetaData::findPlugins("dolphin/vcs")` +
  `KPluginFactory::instantiatePlugin<KVersionControlPlugin>()` (`versioncontrolobserver.cpp:298–302`).
  Das Plugin selbst nutzt `K_PLUGIN_CLASS_WITH_JSON`.
- **SDK:** `libdolphinvcs` + installierter Header `<Dolphin/KVersionControlPlugin>` + CMake-Paket
  `DolphinVcs`.
- **Interface** (rein virtuell, `kversioncontrolplugin.h:141–185`):
  `fileName()`, `beginRetrieval(dir)`, `endRetrieval()`, `itemVersion(item)`,
  `versionControlActions(items)`, `outOfVersionControlActions(items)`. Optional:
  `localRepositoryRoot(dir)`. Signale: `itemVersionsChanged`, `infoMessage`, `errorMessage`,
  `operationCompletedMessage`.
- **Threading:** `beginRetrieval`/`itemVersion`/`endRetrieval` laufen im Worker-Thread, die
  Actions im GUI-Thread.
- **Registrierung:** Installation in `<plugindir>/dolphin/vcs/`. Aktivierung über
  `dolphinrc [VersionControl] enabledPlugins`.
- **Laden:** lazy beim ersten Verzeichnis-Check. **Entladen:** nie explizit; die Instanzen sterben
  mit der View.

### 9.2 KFileItemAction-Plugins

- **Technik:** C++/Qt, `KAbstractFileItemActionPlugin` (KIO), JSON-Metadaten,
  `kcoreaddons_add_plugin(... INSTALL_NAMESPACE "kf6/kfileitemaction")`.
- **Laden:** Nicht Dolphin lädt diese Plugins, sondern **KIOs `KFileItemActions`**. Dolphin erzeugt
  das Objekt in `DolphinMainWindow::setupFileItemActions()` (2608) und setzt bei jeder
  Auswahländerung `setItemListProperties`. Beim Kontextmenü ruft `addActionsTo()` die Plugins auf.
  KIO filtert dabei nach `MimeTypes`, `X-KDE-Require` und `kservicemenurc [Show]`.
- **Was das Plugin bekommt:** `actions(const KFileItemListProperties &items, QWidget *parentWidget)`.
  `parentWidget` ist das `DolphinMainWindow`, sonst gibt es keine Dolphin-API.
- **Was es liefern kann:** `QAction`s, `QWidgetAction`s, Untermenüs, aber nur im Kontextmenü (Item
  und Hintergrund). Nicht im Ordner- oder Orte-Panel, nicht in der Toolbar (Ausnahme:
  Service-Menüs ab KIO 6.24 über `ServiceMenuShortcutManager`).
- **Aktivieren/Deaktivieren:** Einstellungen → Kontextmenü (`contextmenusettingspage.cpp:295–306`)
  schreibt `kservicemenurc [Show] <pluginId>`. Danach erzeugt Dolphin `KFileItemActions` neu
  (Config-Watcher, `dolphinmainwindow.cpp:265–270`).
- **Nebeneffekt:** Diese Plugins erscheinen in **allen** KIO-basierten Programmen, nicht nur in Dolphin.

### 9.3 Anleitung: kleinstes Plugin im Repo – `itemactions/movetonewfolderitemaction`

**1. Header** (`movetonewfolderitemaction.h`):
```cpp
class MoveToNewFolderItemAction : public KAbstractFileItemActionPlugin {
    Q_OBJECT
public:
    MoveToNewFolderItemAction(QObject *parent);
    QList<QAction *> actions(const KFileItemListProperties &fileItemInfos, QWidget *parentWidget) override;
};
```

**2. Implementierung** (`movetonewfolderitemaction.cpp`, 61 Zeilen):
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
        newFileMenu->setWorkingDirectory(/* Ordner des ersten Items */);
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

**3. Metadaten** (`movetonewfolderitemaction.json`):
```json
{ "KPlugin": { "Icon": "folder-new", "Name": "Move to New Folder",
               "MimeTypes": ["application/octet-stream", "inode/directory"] },
  "X-KDE-Require": "Write",
  "X-KDE-Show-In-Submenu": "true" }
```

**4. CMake** (`itemactions/CMakeLists.txt`, eingebunden über `add_subdirectory(itemactions)` in
`src/CMakeLists.txt:681`):
```cmake
kcoreaddons_add_plugin(movetonewfolderitemaction
    SOURCES movetonewfolderitemaction.cpp movetonewfolderitemaction.h
    INSTALL_NAMESPACE "kf6/kfileitemaction")
target_link_libraries(movetonewfolderitemaction
    KF6::I18n KF6::KIOCore KF6::KIOWidgets KF6::KIOFileWidgets)
```

**5. Weg ins Menü:** Rechtsklick → `DolphinView::requestContextMenu` →
`DolphinMainWindow::openContextMenu` → `DolphinContextMenu::addItemContextMenu` →
`addAdditionalActions` → `KFileItemActions::addActionsTo` → KIO findet und filtert das Plugin und
ruft `actions()` auf.

Hinweis zum Testen: Das Plugin muss unter `kf6/kfileitemaction` im Plugin-Pfad liegen (installieren
nach `install-dolphin` + `source prefix.sh`, oder `QT_PLUGIN_PATH` auf `build-dolphin/bin` setzen).

**Ein VCS-Plugin** entsteht analog: Klasse von `KVersionControlPlugin` ableiten, die sechs rein
virtuellen Methoden implementieren, `K_PLUGIN_CLASS_WITH_JSON`, gegen `DolphinVcs::DolphinVcs`
linken, `INSTALL_NAMESPACE "dolphin/vcs"`, dann in den Einstellungen aktivieren.

### 9.4 Können Plugins eigene UI hinzufügen?

| Beitrag | VCS-Plugin | FileItemAction-Plugin | Service-Menü | Overlay-Plugin |
|---|---|---|---|---|
| Kontextmenü-Einträge | ✅ | ✅ | ✅ | – |
| Toolbar / Shortcuts | – | – | ✅ (KIO ≥ 6.24) | – |
| Panels / Docks | – | – | – | – |
| Item-Dekoration | ✅ (feste Status-Enum) | – | – | ✅ (Emblem-Icons) |
| Spalten / Rollen | – | – | – | – |
| Statusmeldungen | ✅ | – | – | – |
| Zugriff auf Fenster/View | – | nur `parentWidget` | – | – |

### 9.5 Einschränkungen der bestehenden Architektur

- Es gibt keine Dolphin-eigene, allgemeine Plugin-API. Die beiden In-Process-Typen sind eng
  zugeschnitten.
- Plugins bekommen keinen Kontext: kein Hauptfenster-, View-, Navigations- oder Panel-Zugriff und
  keine Events (URL-Wechsel, Auswahländerung).
- Es gibt keinen Lifecycle (kein Laden/Entladen zur Laufzeit), nur lazy Laden, und das pro View.
- Die Plugin-Liste der Einstellungsseiten ist fest.
- D-Bus erlaubt nur Fenster-Operationen.
- Viele Features hängen an Compile-Time-Flags (`HAVE_BALOO`).
- Kontextmenüs werden an drei Stellen separat aufgebaut: `DolphinContextMenu`, die BottomBar des
  Selection-Mode (`bottombarcontentscontainer.cpp:518`) und `DolphinPart::slotOpenContextMenu`.

### 9.6 Was sich für Whale wiederverwenden lässt

1. **Lade-Idiom:** `KPluginMetaData::findPlugins(ns)` + `KPluginFactory::instantiatePlugin<T>()` +
   JSON-Metadaten + Aktivierungsliste in KConfig (Vorlage `versioncontrolobserver.cpp:291–318`).
   Ein JavaScript-Host (z. B. `QJSEngine`) könnte selbst so ein C++-Plugin sein, das Skripte aus
   `QStandardPaths` lädt, analog zur Suche nach Service-Menüs (`contextmenusettingspage.cpp:273`).
2. **Event-Bus:** die bestehenden `DolphinMainWindow`-Signale (`selectionChanged`, `urlChanged`,
   `requestItemInfo`, `fileItemsChanged`, `settingsChanged`), die bereits der aktiven View folgen.
3. **Panel-Muster:** `Panel` + `DolphinDockWidget` + `createPanelAction`; der Aufbau in
   `setupDockWidgets()` müsste nur eine Registry-Schleife bekommen.
4. **Action-Registrierung:** das `ServiceMenuShortcutManager`-Muster für skriptdefinierte Actions
   (stabile Namen, Shortcuts, Toolbar-fähig).
5. **Asynchrone Item-Dekoration:** das `VersionControlObserver`-Muster (entprellt, Worker-Thread,
   `model->setData`) und die Overlay-Plugin-Schleife in `KFileItemModelRolesUpdater` (124–138).
6. **Meldungs-API:** `DolphinViewContainer::showMessage(text, type, actions)`.
7. **Einstellungsseiten:** `SettingsPageBase` ist bereits ein sauberes Seiten-Interface.

---

## 10. Natürliche Erweiterungspunkte für Whales Plugin-/Skript-System

| Erweiterungspunkt | Stelle | Was ein Plugin dort tun könnte |
|---|---|---|
| **Event-Bus** | `DolphinMainWindow::activeViewChanged` (1665), `connectViewSignals` (2797), `slotSelectionChanged` (407), `changeUrl` (367) | Auf URL-, Auswahl- und Hover-Änderungen reagieren |
| **Panels** | `DolphinMainWindow::setupDockWidgets` (2341), `panels` KActionMenu (2585) | Eigene Docks (Git, Preview, Notizen, …) |
| **Kontextmenü** | `DolphinContextMenu::addAdditionalActions` (545), `DolphinMainWindow::openContextMenu` (1524) | Dritte Provider-Liste mit vollem Kontext (Fenster, Auswahl, URL, Kontexttyp) |
| **Actions/Toolbar** | `DolphinMainWindow::setupActions` (1798), `DolphinViewActionHandler::createActions` (81), `actionCollection()` | Eigene Actions, Shortcuts, Toolbar-Buttons |
| **Spalten/Rollen** | `KFileItemModel::rolesInfoMap` (3157, heute statisch), `KFileItemModelRolesUpdater::rolesData` (1384) | Eigene Spalten (z. B. Git-Status, Tags) |
| **Item-Darstellung** | `DolphinFileItemListWidget::refreshCache` (21), `DolphinItemListView::defaultWidgetCreator` (131) | Eigene Dekorationen, Badges |
| **Asynchrone Annotation** | `VersionControlObserver` als Muster | Skriptberechnete Badges pro Verzeichnis |
| **Preview/Info** | `InformationPanel::showItemInfo` (218), `InformationPanelContent::refreshPreview` (223), `ToolTipManager::startContentRetrieval` (123) | Preview-Provider pro MIME-Typ, zusätzliche Info-Sektionen |
| **Leisten/Meldungen** | `DolphinViewContainer` Grid-Slots (57–68), `showMessage` (481) | Eigene Leisten über oder unter der View, Benachrichtigungen |
| **URL-Schemata** | ca. 53 verstreute `scheme()`-Vergleiche | Schema-Registry mit Fähigkeiten |
| **Einstellungen** | `DolphinSettingsDialog` (45–105), `SettingsPageBase` | Plugin-Einstellungsseiten |
| **Automatisierung** | `org.kde.dolphin.MainWindow` (`dolphinmainwindow.h:75`) | D-Bus-API für Auswahl, URL, Action-Trigger |

---

## 11. Empfohlene Lesereihenfolge

### Muss man verstehen (in dieser Reihenfolge)

| # | Datei | Worauf achten |
|---|---|---|
| 1 | `main.cpp` | Startablauf, Single-Instance-Logik |
| 2 | `dolphinmainwindow.h` | Signale, Member – der Überblick über das Fenster |
| 3 | `dolphinmainwindow.cpp` | Konstruktor (117), `activeViewChanged` (1665), `connectViewSignals` (2797), `openContextMenu` (1524), `setupDockWidgets` (2341) – `setupActions` nur überfliegen |
| 4 | `dolphintabpage.cpp` | Split-View, `slotViewActivated` |
| 5 | `dolphinviewcontainer.cpp` | Leisten-Layout, `setUrl`, `slotUrlNavigatorLocationChanged`, `slotItemActivated` |
| 6 | `views/dolphinview.h` + `.cpp` | Konstruktor, `setUrl`, Auswahl-Signale, Dateioperationen, `requestContextMenu` |
| 7 | `kitemviews/kitemmodelbase.h` | Index + Rollen-Konzept, Model-Signale |
| 8 | `kitemviews/kfileitemmodel.h` + `.cpp` | `ItemData`, `KDirLister`-Anbindung, `slotItemsAdded`/`insertItems`, `rolesInfoMap` |
| 9 | `kitemviews/kitemlistcontroller.cpp` | `onPress`, `onRelease`, `keyPressEvent`, Rubberband, Drag |
| 10 | `kitemviews/kitemlistselectionmanager.cpp` | Auswahl-Zustand |
| 11 | `dolphincontextmenu.cpp` | Kontextmenü-Aufbau, `addAdditionalActions` |
| 12 | `views/versioncontrol/kversioncontrolplugin.h` + `versioncontrolobserver.cpp` | Das einzige Dolphin-eigene Plugin-System |
| 13 | `itemactions/movetonewfolderitemaction.*` + `itemactions/CMakeLists.txt` | Minimal-Plugin |

### Bei Bedarf ansehen

| Datei | Wann |
|---|---|
| `kitemviews/kitemlistview.cpp` | Layout, Animationen, Widget-Recycling, Drop-Indikator |
| `kitemviews/kfileitemmodelrolesupdater.cpp` | Vorschaubilder, Icons, Ordnergrößen, Overlays |
| `kitemviews/kstandarditemlistwidget.cpp` | Zeichnen eines Items |
| `views/dolphinfileitemlistwidget.cpp` | VCS-Darstellung |
| `views/dolphinviewactionhandler.cpp` | View-Actions |
| `panels/panel.h`, `panels/information/*` | Panels, Preview-Panel |
| `views/viewproperties.cpp` | Ansichtseinstellungen pro Ordner |
| `views/draganddrophelper.cpp` | Drop-Verhalten |
| `settings/dolphinsettingsdialog.cpp`, `settings/contextmenu/contextmenusettingspage.cpp` | Einstellungen, Plugin-Aktivierung |
| `dolphinui.rc` | Menü-/Toolbar-Struktur |
| `global.cpp`, `dbusinterface.cpp` | D-Bus, Instanz-Wiederverwendung |
| `selectionmode/*`, `search/*` | Selection-Mode, Suche |

### KDE-Infrastruktur, zunächst ignorieren

`po/`, `doc/`, `LICENSES/`, `REUSE.toml`, `.gitlab-ci.yml`, `.kde-ci.yml`, `.flatpak-manifest.json`,
`cmake/`, `completions/`, `appiumtests/`, `userfeedback/`, `settings/kcm/`, `settings/*.upd` + `*.py` +
Update-Helfer (kconf_update), `kitemviews/accessibility/`, `dolphinpart*` (nur relevant, falls Whale
in Konqueror einbettbar sein soll), `Messages.sh`, `admin/` (bis man es braucht).

---

## 12. Beobachtungen am Rande

- **`settings/kcm/kcmdolphinnavigation.*`** ist toter Code: Die Datei steht in keiner CMakeLists,
  die referenzierte JSON-Datei und `NavigationSettingsPage` fehlen.
- **`DolphinGeneralConfigModule::defaults()`** (`settings/kcm/kcmdolphingeneral.cpp:67–72`) ruft
  `applySettings()` statt `restoreDefaults()` auf. Das ist vermutlich ein Upstream-Bug.
- Die VCS-Aktivierung vergleicht mit dem übersetzten Plugin-Namen statt mit der ID
  (`versioncontrolobserver.cpp:301`).
- In `hidefileitemaction.json` ist `X-KDE-Show-In-Submenu` ein Boolean, in den beiden anderen
  Plugins der String `"true"`.
- `smallhash.h` ist keine Whale-Änderung, sondern eine Upstream-Optimierung (Commits `f9488f6`,
  `6a520e6`, Juli 2026).
