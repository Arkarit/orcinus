# Whale Multi-Toolbar – Architektur- und Umbauplan

Stand: 2026-10-08, Branch `feature/multi-toolbar-plan` (abgezweigt von `master` = 5fc824a41).
Nur Analyse und Plan, keine Produktimplementierung. **Nachtrag 2026-10-09: TB0-Ergebnisse in
Abschnitt 24**; sie bestätigen den Plan und präzisieren einige Punkte (dort jeweils mit Verweis).

Alle Aussagen zu Dolphin beziehen sich auf den Code in diesem Stand. Aussagen zu KXmlGui beziehen
sich auf den Quellcode von **KXmlGui 6.24.0** (installierte Version, Tag `v6.24.0`, im Scratchpad
gelesen) und auf einen Wegwerf-Spike (siehe 11.4). Zeilennummern sind Orientierung, kein Vertrag.

Legende: **[D]** = Dolphin-Datei würde geändert, **[W]** = rein Whale-seitig, **[K]** = Verhalten von
KDE Frameworks, das wir nutzen bzw. hinnehmen.

---

## 0. Kurzfassung

- **KDE kann mehr, als erwartet:** `KEditToolBar` hat bereits einen eingebauten Umschalter
  „Toolbar:“ (Combobox) und zeigt ihn, sobald die XMLGUI-Dokumente der Fabrik mehr als eine
  editierbare `<ToolBar>` enthalten. Die Liste „Available actions“ stammt aus der
  `KActionCollection` des XMLGUI-Clients, dem die Toolbar gehört. Mit einem **zweiten
  XMLGUI-Client für die File View Toolbar** bekommen wir Dropdown, typabhängige Available-Liste,
  Reihenfolge, Separator, Expanding Spacer, Change Icon/Text, Apply/OK/Cancel über beide Toolbars
  ohne eigenen Dialog. Im Spike verifiziert.
- **Migration ist fast geschenkt:** Die globale Toolbar bleibt die XMLGUI-Toolbar `mainToolBar`
  in `dolphinui.rc`. Die bestehende Benutzerkonfiguration
  (`~/.local/share/kxmlgui5/whale/dolphinui.rc`) gilt damit unverändert als Main/Global Toolbar.
  Die File View Toolbar liegt in einer eigenen RC-Datei, für die es noch keine lokale Kopie
  gibt, sie startet also mit Defaults. KXmlGuis Versionshandler bewahrt Benutzer-Toolbars sogar
  bei einem Versionssprung der installierten RC-Datei.
- **Das eigentliche Problem ist das Action-Targeting.** Fast alle Dolphin-Actions zielen
  implizit auf die aktive View: `DolphinMainWindow` über `m_activeViewContainer` und
  `DolphinViewActionHandler` über `m_currentView`, das bei jedem View-Wechsel umgehängt wird.
  Eine geteilte QAction kann daher weder pro View unterschiedlichen Zustand zeigen noch wissen,
  aus welcher Toolbar sie ausgelöst wurde.
- **Empfohlene QAction-Strategie: „gebundene Action-Sets“** (Mischung aus B und C). Jede File
  View bekommt eine eigene, schlanke `KActionCollection` mit eigenen QAction-Instanzen, **ohne
  Shortcuts**. Erzeugt werden sie größtenteils von den bestehenden, schon jetzt
  view-parametrisierten Klassen: eine zweite `DolphinViewActionHandler`-Instanz, fest an die
  eigene `DolphinView` gebunden (Dolphin macht genau das bereits in `DolphinPart`), und eine fest
  an den eigenen `WhaleViewHost` gebundene `WhaleViewActions`. Für die wenigen
  MainWindow-Actions (Navigation, Reload, Stop, Filter) gibt es kleine Whale-Kommandos mit
  explizitem `DolphinViewContainer*`. Die Business-Logik wird nicht dupliziert, Shortcuts bleiben
  einmalig auf den globalen Actions, und der Zustand stimmt pro View.
- **Ownership:** `DolphinViewContainer` (= File-View-Instanz) besitzt seine
  `WhaleFileViewToolBar`. Pro Hauptfenster gibt es einen `WhaleToolbarManager` mit den
  Descriptoren. Nirgends steht „links/rechts“; Split, Tabs und spätere Docks folgen allein aus
  dem Lebenszyklus der Container.
- **Dolphin-Diff:** voraussichtlich ca. 10–20 Zeilen in 2–3 Dolphin-Dateien
  (`dolphinviewcontainer.cpp`, `dolphinmainwindow.cpp`, optional `dolphinui.rc`).
- **Empfehlung: umsetzen**, beginnend mit einem kurzen Spike (TB0), der die zweite
  Handler-Instanz im echten Whale prüft. Ich sehe keinen technischen Blocker. Eine Abweichung
  vom Wunsch gibt es: „Defaults“ setzt in KDE **alle** Toolbars zurück, nicht nur die gewählte
  (siehe 8.4).

---

## 1. Ist-Zustand

### 1.1 Eine XMLGUI-Toolbar

- `src/dolphinui.rc` (Version 52) definiert genau eine Toolbar
  `<ToolBar noMerge="1" name="mainToolBar">` mit dem Text „Main Toolbar“ und den Defaults
  `go_back, go_forward, view_settings, url_navigators, split_view, split_stash, toggle_search,
  hamburger_menu`. Die Telefon-Variante `dolphinuiforphones.rc` hat ebenfalls nur `mainToolBar`.
- `DolphinMainWindow` ruft `setupGUI(Save | Create | ToolBar, "dolphinui.rc")` auf
  (`dolphinmainwindow.cpp:231`). Zum Bearbeiten dient der Standardweg von KXmlGui:
  `KXmlGuiWindow::configureToolbars()` öffnet `KEditToolBar(guiFactory())`, und
  `newToolBarConfig` führt zu `DolphinMainWindow::saveNewToolbarConfig()`
  (`dolphinmainwindow.cpp:3240`). Das baut die GUI neu auf, hängt die Navigatoren neu ein und
  ruft `plugViewModes` von Whale auf.
- Die Konfiguration des Benutzers liegt in `~/.local/share/kxmlgui5/whale/dolphinui.rc`, als
  vollständige Kopie des XML mit `<ToolBar name="mainToolBar">` und `<ActionProperties>` für
  Change Icon/Text und Shortcuts. Die Datei des aktuellen Benutzers enthält:
  `go_back, view_settings, go_up, url_navigators, split_view, split_stash, toggle_search,
  hamburger_menu`. Dev-Build und installiertes Whale teilen sich diese Datei, weil die
  Komponente in beiden „whale“ heißt.
- Sonderfälle im MainWindow, die `toolBar()` direkt abfragen:
  - `updateHamburgerMenu()` (`:1590`) blendet Einträge ein, die nicht in der Toolbar sind.
  - `updateAllowedToolbarAreas()` erlaubt nur oben/unten, wenn `url_navigators` in der Toolbar
    ist.
  - `updateSplitActions()` setzt den Popup-Modus am Button.
  - `addHamburgerMenuToToolbar()` (`:3210`) ist eine Migration von 2021.
  - Ein `MiddleClickActionEventFilter` auf der Toolbar sorgt dafür, dass ein Mittelklick auf
    Back/Forward/Up/Home in einem neuen Tab öffnet.

### 1.2 Location Bar als Toolbar-Widget

`url_navigators` ist eine `DolphinNavigatorsWidgetAction` (QWidgetAction), **genau ein Widget pro
Fenster**. Es enthält einen QSplitter mit **zwei** `DolphinUrlNavigator`s, die per Abstand an den
beiden Panes ausgerichtet werden (`followViewContainersGeometry`). Ist die Action in der Toolbar,
sitzt das Widget dort. Sonst hängt `DolphinTabPage::insertNavigatorsWidget()` es oben in die
Tab-Page (`dolphintabpage.cpp:246`). Jeder `DolphinViewContainer` hat zusätzlich einen
unsichtbaren `m_urlNavigator` mit der History, und der sichtbare Navigator wird nur „verbunden“
(`connectUrlNavigator`, `dolphinviewcontainer.cpp:293`).

**Präzedenzfall:** Eine Interaktion mit dem Navigator einer inaktiven Pane aktiviert diese Pane
(`DolphinUrlNavigator::activated → DolphinViewContainer::activate`,
`dolphinviewcontainer.cpp:316`). Dolphin kennt also schon „pane-lokale Bedienelemente aktivieren
ihre Pane“.

### 1.3 Wie Actions heute ihr Ziel finden

Der Mechanismus „aktive View“ läuft **synchron**:
`DolphinViewContainer::setActive(true) → DolphinView::setActive → Q_EMIT activated →
DolphinTabPage::slotViewActivated (:482) → activeViewChanged → DolphinTabWidget →
DolphinMainWindow::activeViewChanged (:1707)`. Dort werden umgehängt:

- `m_activeViewContainer`, auf das alle MainWindow-Slots zugreifen
  (goBack/goUp/reloadView/find/…),
- `m_actionHandler->setCurrentView(view)`, d. h. alle Handler-Actions,
- `m_whaleViewActions->setCurrentHost(host)`, d. h. Whale-View-Modi, Gates und
  „Show flat“,
- anschließend `updateHistory/updateFileAndEditActions/updatePasteAction/updateViewActions/
  updateGoActions/updateSearchAction`, die den **einen** globalen Action-Zustand aus der aktiven
  View neu berechnen.

Es gibt drei Gruppen:

| Gruppe | Wo | Ziel heute | Explizites Ziel möglich? |
|---|---|---|---|
| MainWindow-Actions | `DolphinMainWindow::setupActions()` | `m_activeViewContainer` (implizit) | Slots nein, aber die öffentliche API von `DolphinViewContainer`/`DolphinView` hat es (z. B. `urlNavigatorInternalWithHistory()->goBack()`, `reload()`, `setFilterBarVisible()`) |
| Handler-Actions | `DolphinViewActionHandler::createActions()` | `m_currentView`, umhängbar per `setCurrentView()` | **Ja, schon heute:** eine Instanz pro Ziel. `DolphinPart` erzeugt eine eigene Instanz mit eigener Collection (`dolphinpart.cpp:98`) |
| Whale-View-Actions | `WhaleViewActions` | `m_host`, umhängbar per `setCurrentHost()` | Konzeptionell ja. Praktisch besitzt die Klasse heute auch fensterweite Dienste (Registry, TreeCache) und wird von `WhaleViewHost` per `window()->findChild<WhaleViewActions*>()` gefunden (`whaleviewhost.cpp:137, 237, 624`). Bei zwei Instanzen pro Fenster würde das falsch auflösen. |

Weitere Feststellungen:

- Alle QActions sind **Singletons pro Fenster** in `actionCollection()`. Shortcuts hängen an
  genau diesen Objekten.
- Der Zustand (checked/enabled/Text/Icon) existiert **einmal** und spiegelt die aktive View.
  `view_settings` ist ein `KActionMenu`, dessen Untermenü **dieselben** QActions enthält
  (`view_mode`-Einträge, Zoom-Slider, Sort/Group/Rollen, `show_preview`, `show_hidden_files`,
  dazu von Whale die Plugin-View-Einträge und `whale_include_subfolders`).
- `DolphinViewActionHandler` setzt den Zustand eines Teils seiner Actions **nicht selbst**. So
  setzt etwa `DolphinMainWindow::updateViewActions()` (`:2803`) `restore_view_settings_default`
  und `toggle_filter`, und `updateFileAndEditActions()` ist für Cut/Rename/Trash/… zuständig.
- `basic_actions` (Handler, `:194`) holt Cut/Copy/Paste aus der Collection des Besitzers. In
  einer fremden Collection fehlen sie (auch in `DolphinPart`), dann hängt Qt Null-Actions an
  (Warnung). Für eigene Instanzen bedeutet das: `basic_actions` nicht anbieten oder die
  Prototypen vorher anlegen.
- Whale-Gates (M5, `WhaleViewActions::updateGates`) schalten Actions über **eigene
  QActionGroups pro Action** ab. Das funktioniert pro Action-Instanz und lässt sich deshalb pro
  View genauso verwenden.

### 1.4 View-lokale Leisten

Die Treemap-Leiste gehört zum Plugin-Widget (`plugins/treemap/treemapview.*`), die
Details-Leiste (Spaltenkopf) zu kitemviews. Beide liegen im `WhaleViewHost`-Stack und
**bleiben unverändert**. Dieser Plan berührt sie nicht.

---

## 2. Zielarchitektur

```text
DolphinMainWindow                          (KXmlGuiWindow, Shell-XMLGUI-Client)
├─ mainToolBar  = Global/Main Toolbar      (XMLGUI wie heute, Actions → aktive View)
├─ WhaleToolbarManager  [W]                (1 pro Fenster)
│   ├─ descriptors: global, file-view      (generisch, erweiterbar)
│   ├─ WhaleFileViewToolbarClient [W]      (KXMLGUIClient: Prototyp-Actions + whalefileviewtoolbarui.rc)
│   └─ WhaleToolbarConfigurator [W]        (öffnet KEditToolBar, Umschalter, Reload)
└─ DolphinTabWidget → DolphinTabPage → n × DolphinViewContainer   (= File-View-Instanz)
        ├─ WhaleFileViewToolBar [W]        (KToolBar, gehört dem Container)
        │   └─ WhaleViewCommandScope [W]   (gebundenes Action-Set, Ziel = dieser Container, keine Shortcuts)
        │        ├─ KActionCollection (privat, nie in der XMLGUI-Fabrik)
        │        ├─ DolphinViewActionHandler   (2. Instanz, setCurrentView(eigene View) einmalig)
        │        ├─ WhaleViewActions-Binding   (setCurrentHost(eigener Host) einmalig)
        │        └─ WhaleContainerCommands     (Back/Forward/Up/Home/Reload/Stop/Filter … mit explizitem Container)
        ├─ (search bar, admin bar, messages, selection-mode bars – Dolphin)
        ├─ WhaleViewHost (View-lokale Controls + Content, unverändert)
        └─ (filter bar, status bar – Dolphin)
```

Leitprinzip: **Der Ziel-Container wird beim Erzeugen des Action-Sets festgelegt und ändert sich
nie.** Das Action-Set fragt nirgends nach „aktiver View“. Die globale Toolbar bleibt, wie sie
ist: dieselben QActions, die über Shortcuts und Menüs auf die aktive View wirken.

---

## 3. Global vs. File View Toolbar

| | Global/Main Toolbar | File View Toolbar |
|---|---|---|
| Anzahl | 1 pro Hauptfenster | 1 pro File-View-Instanz (heute: 1 bzw. 2 pro sichtbarem Tab) |
| Technik | XMLGUI-Container `mainToolBar` (KToolBar im MainWindow) | eigene `KToolBar` im `DolphinViewContainer`, Layout aus Descriptor-Konfiguration |
| Actions | Fenster-Collection (Singletons) | gebundene Instanzen pro View |
| Ziel | aktive View (wie heute) | ausschließlich die eigene View |
| Shortcuts | ja (einmalig, wie heute) | **keine** (Shortcuts wirken global → aktive View) |
| Konfiguration | `dolphinui.rc` + lokale Kopie | `whalefileviewtoolbarui.rc` + lokale Kopie, **eine** für alle Instanzen |
| Konfigurierbar via | Configure Toolbars, Combo „Main Toolbar“ | Configure Toolbars, Combo „File View Toolbar“ |

---

## 4. Action-Targeting

### 4.1 Globale Toolbar

Unverändert: `global action → DolphinMainWindow/Handler/WhaleViewActions → aktive View`.
Es gibt keinen Codepfad, der dafür angefasst werden muss.

### 4.2 File View Toolbar

`toolbar instance (Container X) → WhaleViewCommandScope(X) → X`.

Umsetzung pro Action-Familie:

1. **Handler-Actions** (View-Modi, Zoom, Sortierung, Gruppierung, Rollen, Previews, Hidden,
   View-Properties, Rename/Trash/Delete/Duplicate/Properties/Copy Location, Create Folder/File):
   eine eigene `DolphinViewActionHandler`-Instanz mit privater Collection, einmalig
   `setCurrentView(X->view())`. Die Logik ist **dieselbe Klasse**, und der Handler verbindet sich
   selbst mit den View-Signalen (`modeChanged`, `previewsShownChanged`, `sortRoleChanged`,
   `zoomLevelChanged`, `hiddenFilesShownChanged`, `selectionChanged` …). Damit stimmt der
   Zustand pro View automatisch.
2. **Whale-View-Actions** (Plugin-View-Einträge, „Show flat (no folders)“, Gates nach
   Capability): ein `WhaleViewActions`, gebunden an den eigenen Handler und
   `setCurrentHost(X->viewHost())`. Voraussetzung ist ein Refactoring (siehe 14, TB1):
   fensterweite Dienste (Registry, TreeCache) kommen in ein eigenes Objekt
   `WhaleViewServices`, und der Host findet dieses statt `WhaleViewActions`.
3. **MainWindow-Actions mit trivialer, öffentlicher Container-API**: kleine Whale-Kommandos
   (`WhaleContainerCommands`), z. B.
   - `go_back`: `X->urlNavigatorInternalWithHistory()->goBack()` und Redirect-Skip wie in
     `DolphinMainWindow::goBack` (`:1290`),
   - `go_forward/go_up/go_home`: dasselbe Muster,
   - `view_redisplay`: `X->reload()`,
   - `stop`: `X->view()->stopLoading()`,
   - `toggle_filter`: `X->setFilterBarVisible(!…)`,
   - History-Popups von Back/Forward aus der History von X, Mittelklick → neuer Tab mit URL
     aus X.

   Das sind wenige Zeilen pro Action, die heute als Einzeiler im MainWindow stehen. Die
   Duplizierung ist bewusst klein gehalten. Wo Dolphin mehr Logik hat, wird sie nicht kopiert,
   sondern es gilt 4.
4. **Alles andere** wird in der File View Toolbar zunächst **nicht angeboten** (Descriptor-Filter,
   siehe 9). Falls später nötig, gibt es eine dokumentierte Fallback-Delegation
   „aktivieren und weiterleiten“: `X->activate()` (synchron, siehe 1.3), danach
   `Q_ASSERT(mainWindow->activeViewContainer() == X)` und dann die globale Action auslösen. Das
   ist deterministisch, weil die Aktivierung synchron ist, aber nur für Actions ohne eigenen
   Zustand in der Toolbar gedacht.

### 4.3 Aktivierung bei Toolbar-Interaktion (Empfehlung)

Eine Interaktion mit der File View Toolbar von X **aktiviert X**, wie es der URL-Navigator
heute tut. Der Befehl selbst zielt trotzdem explizit auf X und ist nicht auf die Aktivierung
angewiesen. Die Aktivierung sorgt nur dafür, dass globale Toolbar, Statusanzeigen,
Selection-Mode-Leisten und Shortcuts danach dieselbe View meinen. Ohne sie würde z. B. „Back“
in B gedrückt, während A aktiv bleibt, und der nächste Shortcut ginge überraschend nach A.
Offene Entscheidung E4.

---

## 5. QAction-/Proxy-Strategie

### 5.1 Können beide File View Toolbars dieselben QActions benutzen?

**Nein.** Begründung aus dem Code:

- Eine QAction hat **einen** Zustand. Steckt dieselbe Action in zwei Toolbars, zeigen beide
  Buttons denselben checked/enabled-Wert. Anforderung 8 ist damit nicht erfüllbar.
- `QAction::triggered` sagt nicht, aus welchem Widget ausgelöst wurde. `QToolBar::actionTriggered`
  wüsste es zwar, aber die mit `triggered` verbundenen Dolphin-Slots laufen trotzdem auf
  `m_activeViewContainer` bzw. `m_currentView`. Toolbar A würde dann bei aktiver View B auf B
  wirken.
- `view_settings` ist ein Menü aus geteilten Unter-Actions. Pro View bräuchte es ohnehin eigene
  Untermenüs.

### 5.2 Bewertung der Alternativen

| Kriterium | A: Shared QAction, Ziel aus Toolbar-Kontext | B: Eigene QAction-Instanzen pro View | C: Leichte Proxies → gemeinsame Commands |
|---|---|---|---|
| Keine doppelte Business-Logik | ja | **ja, wenn** die Instanzen von denselben Klassen erzeugt werden (Handler/WhaleViewActions) | ja, aber die Commands mit explizitem Ziel müssten erst gebaut werden. Dolphins Slots haben keins. |
| Keine doppelten Shortcuts | ja | ja, wenn den Instanzen die Shortcuts entzogen werden | ja |
| Zustand pro View | **nein** | ja (der Handler verbindet die View-Signale selbst) | ja, aber jede Action braucht einen eigenen State-Provider |
| Toolbar A bedient nie B | **nein** (Slots nutzen die aktive View) | ja (festes Ziel) | ja (festes Ziel) |
| Menü-Actions (`view_settings`, Sort, Zoom-Slider) | nein | ja, kostenlos | aufwendig (jedes Untermenü nachbauen) |
| Dolphin-Diff | groß (alle Slots umbauen) | **minimal** | mittel bis groß |

### 5.3 Empfehlung: „gebundene Action-Sets“ = B, wo Dolphin-Klassen schon parametrisiert sind; C nur für die MainWindow-Actions

- Pro View eine `WhaleViewCommandScope` mit privater `KActionCollection`.
- Inhalt: wie in 4.2, Nr. 1–3. Nr. 3 sind echte kleine Proxies (C) mit explizitem Container und
  eigenem State-Provider.
- **Shortcuts entziehen:** Nach dem Erzeugen setzt der Scope für jede Action
  `setShortcuts({})`. Der Handler ruft in `createActions()` `setDefaultShortcut(s)` auf, und das
  setzt auch den aktiven Shortcut. Ohne diesen Schritt gäbe es bei mehreren sichtbaren Toolbars
  „Ambiguous shortcut overload“, weil QActions in sichtbaren Widgets per Default
  `Qt::WindowShortcut` sind. Das muss getestet werden.
- Die Collection kommt **nie** in die XMLGUI-Fabrik. Damit taucht sie weder im
  Shortcut-Dialog noch in „Find Action…“ (KCommandBar) auf, denn beide sammeln nur die
  Collections der Fabrik-Clients (`kxmlguiwindow.cpp:56–130`,
  `kxmlguifactory.cpp` `showConfigureShortcutsDialog`).
- Die Action-IDs sind **dieselben wie global** (`go_back`, `show_preview`, …). Das ist gut für
  Dev-MCP, Tests und ActionProperties.
- Lazy: Der Scope wird erst beim ersten `showEvent` der Toolbar erzeugt. Versteckte,
  nie angesehene Tabs zahlen nichts. Eine Handler-Instanz erzeugt etwa 100 QActions
  (Sort-, Rollen- und Gruppen-Gruppen). Bei vielen offenen und angesehenen Tabs ist das
  vertretbar; in TB0 messen.

Nicht gewählt: A (s. o.) und ein reines C für alles (müsste die Menüs von Handler und
WhaleViewActions nachbauen, also doppelte Logik).

---

## 6. Runtime-State

Der Zustand wird **pro Instanz** aus der eigenen View berechnet, nie aus globalen Actions
gespiegelt.

| Zustand | Quelle in der File View Toolbar |
|---|---|
| View Mode (Icons/Compact/Details/Plugin) checked | eigener Handler (`updateViewActions` bei `modeChanged`) + eigener `WhaleViewActions` (`setShownViewEntry`, `currentViewChanged`) |
| Show Previews / Show Hidden Files | eigener Handler (`previewsShownChanged`, `hiddenFilesShownChanged`) |
| Show flat (no folders) checked/enabled | eigener `WhaleViewActions::updateContentScopeAction` (Host-Signale) |
| Zoom / Sortierung / Gruppierung / Rollen | eigener Handler |
| Plugin-Capabilities (z. B. Treemap: Hidden/Preview/Zoom/Sort aus) | Gates des eigenen `WhaleViewActions` (`updateGates`, Host `capabilitiesChanged`) |
| Rename/Trash/… enabled nach Auswahl | eigener Handler + Gates (Auswahl der eigenen View, inkl. T2-Router `WhaleCommandRouter::selectedItems(eigeneView)`) |
| Back/Forward enabled | `WhaleContainerCommands`: History des eigenen `urlNavigatorInternalWithHistory()` (Logik wie `updateHistory`, `:455`) |
| Up enabled | wie `updateGoActions`, aber für X |
| Stop enabled | `DolphinView::directoryLoadingStarted/Completed` von X (wie `:2871`) |
| Filter checked | Filterbar-Sichtbarkeit von X |
| `restore_view_settings_default` enabled | `!X->view()->isViewSettingsDefaults()` beim Handler-Update von X (heute im MainWindow) |

Die Konfiguration („diese Action steht in der Toolbar“) und der Laufzeitzustand („in dieser View
gerade disabled“) bleiben getrennt. Nicht unterstützte Actions werden **ausgegraut, nicht
ausgeblendet** (Anforderung 17). Das entspricht den M5-Gates. Der Grund für das Ausgrauen kann
wie bisher über den `DisabledActionNotifier`-Hook angezeigt werden, dann aber in der eigenen
Pane statt in der aktiven (Detail für TB3).

---

## 7. Split-/Multi-View-Lifecycle

- **Ownership:** `WhaleFileViewToolBar` ist ein Kind seines `DolphinViewContainer`. Wenn der
  Container erzeugt wird (`DolphinTabPage::createViewContainer`, bei Split-Öffnen, Tab-Öffnen und
  Session-Restore), entsteht die Toolbar mit. Wird der Container zerstört (Split schließen, Tab
  schließen), verschwindet sie. Der Manager führt keine Liste, die gepflegt werden müsste. Er
  meldet Layoutänderungen per Signal, und jede Toolbar baut sich selbst neu.
- **Ohne Split:** 1 sichtbare File View Toolbar pro Tab. **Mit Split:** 2, jede in ihrer Hälfte.
  **Split öffnen:** Der neue Container bringt seine Toolbar mit. **Split schließen:** Die
  Toolbar verschwindet, die Konfiguration (RC-Datei) bleibt erhalten.
- **Verbindung zum Fenster:** wie beim `WhaleViewHost` lazy über `window()->findChild<…>()`,
  hier auf `WhaleToolbarManager`. Ein Hook im MainWindow ist dafür nicht nötig. Wegen
  `findChild` darf es pro Fenster nur **ein** Manager-Objekt geben, und genau dieser Fehler
  passiert heute beim Mehrfach-`WhaleViewActions` (siehe 1.3).
- **Platzierung (Empfehlung):** eine neue erste Zeile im `QGridLayout` des Containers über der
  Suchleiste. Dafür wird `LayoutStructure` in `dolphinviewcontainer.cpp:59` um
  `whaleToolBar = 0` erweitert und die übrigen Zeilen um 1 verschoben. Die Alternative ohne
  Dolphin-Diff wäre der obere Rand des `WhaleViewHost`. Dann läge die Toolbar unter
  Suche, Admin-Bar und Meldungen und würde beim Suchen „wandern“. Offene Entscheidung E5.
- **Sichtbarkeit:** eine fensterübergreifende Einstellung „File View Toolbars anzeigen“ (eigene
  Whale-Konfiguration, nicht `GeneralSettings`, um `dolphin_generalsettings.kcfg` nicht
  anzufassen). `KXmlGuiWindow`s Menü „Toolbars Shown“ kennt nur die direkten
  KToolBar-Kinder des Fensters (`KMainWindow::toolBars()`, `kmainwindow.cpp:974`). Unsere
  Toolbars tauchen dort nicht auf, deshalb braucht es eine eigene Toggle-Action (E7).
- **KToolBar außerhalb des MainWindows:** `KToolBar::mainWindow()` ist nur das direkte Elternteil
  (`ktoolbar.cpp:1106`). Ohne MainWindow fällt das Kontextmenü auf QToolBar zurück (`:947`).
  Es gibt dann auch keine Area-, Lock- oder Settings-Dirty-Logik und keinen Eintrag in
  `saveMainWindowSettings` (das nur `toolBars()` iteriert, `kmainwindow.cpp:631`). Das ist
  gewollt. Wir bauen ein eigenes kleines Kontextmenü mit „Configure File View Toolbar…“
  (öffnet den Dialog mit vorausgewählter File View Toolbar) und „Hide File View Toolbars“.
  `readConfig=false`. KDEs Icon- und Stil-Einstellungen (`loadKDESettings`) gelten trotzdem.

---

## 8. Settings-Dialog

### 8.1 Was KEditToolBar schon kann (Quellcode 6.24 + Spike)

- `findToolBars()` (`kedittoolbar.cpp:53`) sammelt **alle** `<ToolBar>` aller Fabrik-Clients.
  `loadToolBarCombo()` (`:1147`) füllt daraus die Combobox „Toolbar:“ und zeigt sie bei mehr
  als einer Toolbar.
- `loadActions()` (`:1187`) nimmt die Actions für „Available actions“ aus
  `m_currentXmlData->actionCollection()`, also der Collection **des Clients, dem die gewählte
  Toolbar gehört**. Mit eigenem Client ist die Liste damit typabhängig gefiltert.
- Separator und Expanding Spacer werden immer angeboten. Change Icon/Change Text schreiben
  `<ActionProperties>` in das Dokument des jeweiligen Clients.
- Änderungen liegen bis Apply/OK in einem In-Memory-DOM **pro Client**. Beim Umschalten in der
  Combo gehen sie nicht verloren. `save()` (`:888`) speichert jedes geänderte Dokument
  (`m_isModified`) in die lokale Datei seines Clients und baut alle Clients neu
  (`rebuildKXMLGUIClients`).
- `KEditToolBar::setDefaultToolBar(name)` wählt die Toolbar beim Öffnen vor.

**Spike** (Scratchpad, nicht im Repo): `KXmlGuiWindow` mit `mainToolBar` und ein zweiter
`KXMLGUIClient` mit eigener Collection, eigener RC und `<ToolBar name="whaleFileViewToolBar">`.
Ergebnis offscreen:

- Combo sichtbar mit „Main Toolbar <tbspike>“ und „File View Toolbar <tbspike_fileview>“,
  Vorauswahl per `setDefaultToolBar` funktioniert.
- Bei File View: Available = nur die Client-Prototypen (`go_up`, `show_preview`), Current =
  `go_back, separator, view_redisplay, spacer`.
- Nach dem Umschalten auf Main: Available = Shell-Collection, Current = Main-Layout.
- `KMainWindow::toolBars()` blieb bei 1. Der Template-Container eines eigenen
  `KXMLGUIBuilder` hängt nicht im Fenster.
- Beim Beenden gab es ein „double free“ aus der Spike-eigenen Lebensdauer (Builder als Member,
  Client auf dem Stack). Daraus folgt: Ownership von Client und Builder in TB2 sauber regeln und
  unter ASan testen.

### 8.2 Ziel-UX und kleine Kosmetik

```text
Configure Toolbars — Whale
Toolbar: [ Main Toolbar        v ]      ← KDE-eigene Combo
         [ File View Toolbar     ]
Available actions:      Current actions:
…                       …
[Defaults]                       [OK] [Apply] [Cancel]
```

- KEditToolBar hängt an jeden Combo-Eintrag „ <gui-name>“ an (`XmlData::toolBarText`, `:153`).
  Daraus würde „Main Toolbar <dolphin>“, weil `dolphinui.rc` `<gui name="dolphin">` heißt. Die
  Abhilfe ist eine **rein kosmetische** Nachbehandlung: nach dem Öffnen die Texte der Combo per
  `findChild<QComboBox*>()` auf die Descriptor-Namen setzen. Die Logik von KDE ist
  indexbasiert und unberührt. Falls ein KF-Update das bricht, bleibt nur der Suffix sichtbar,
  funktional ändert sich nichts.

### 8.3 Ablauf

Der `WhaleToolbarConfigurator` übernimmt den Slot `configureToolbars`. Vorbild ist der
About-Dialog, der im Konstruktor per disconnect/connect ersetzt wurde (`dolphinmainwindow.cpp:235`).
Alternativ geht ein einzeiliger Override in `DolphinMainWindow`.

1. `saveMainWindowSettings` wie bei KDE.
2. `WhaleFileViewToolbarClient` zur Fabrik hinzufügen, **nur für die Dauer des Dialogs**
   (Variante „temporärer Client“, E12).
3. `KEditToolBar(guiFactory())`, `setDefaultToolBar(gewählt)`, Combo-Texte setzen.
4. `newToolBarConfig` → `DolphinMainWindow::saveNewToolbarConfig()` (wie bisher) **und**
   Manager → Layout der File View Toolbar neu lesen → alle Instanzen bauen neu.
5. Dialog zerstört → Client aus der Fabrik entfernen.

Warum nur temporär in der Fabrik? Dauerhaft in der Fabrik stünden die Prototyp-Actions doppelt in
„Find Action…“ (KCommandBar nimmt alle Fabrik-Clients) und müssten im Shortcut-Dialog per
`setShortcutsConfigurable(false)` versteckt werden (`kshortcutseditor.cpp:355` respektiert das).
Der Preis: ActionProperties der File View Toolbar (Change Icon/Text) müssen wir zur Laufzeit
selbst aus dem Client-DOM lesen. Das sind etwa 40 Zeilen und gut testbar. Fallback, falls sich
das als fragil erweist: Client dauerhaft in der Fabrik, Prototypen leiten an die globale Action
weiter, und die Collection bekommt den Anzeigenamen „File View Toolbar“.

### 8.4 Defaults

KDE-Verhalten (`KEditToolBarPrivate::defaultClicked`, `:655`): Bestätigungsdialog „reset **all**
toolbars of this application“, dann werden **die lokalen XML-Dateien aller Fabrik-Clients**
gelöscht und sofort angewendet. Das setzt Main **und** File View zurück.

Nur die gewählte Toolbar zurückzusetzen geht ohne Eingriff in private KDE-Interna nicht. Man
müsste den Defaults-Button per `findChild` umverdrahten und das private Widget neu laden. Das ist
zu fragil.
**Empfehlung:** KDE-Semantik übernehmen, denn der Dialogtext sagt ehrlich „all toolbars“.
Optional käme ein eigener Eintrag „Reset File View Toolbar“ ins Kontextmenü der File View
Toolbar dazu, der nur deren lokale Datei verwirft (E3).

### 8.5 Apply / OK / Cancel

Funktioniert nativ wie gewünscht (8.1): Beide Toolbars bearbeiten, Apply bzw. OK übernimmt
beide, Cancel verwirft alles noch nicht Angewendete. Wie in KDE üblich macht Cancel ein bereits
angewendetes Apply nicht rückgängig.

---

## 9. Konfigurationsmodell

### 9.1 Descriptor (generisch)

```cpp
struct WhaleToolbarDescriptor {
    QString id;               // stable: "global", "file-view"
    QString displayName;      // i18n: "Main Toolbar", "File View Toolbar"
    QString xmlGuiToolBar;    // "mainToolBar" | "whaleFileViewToolBar"
    QString rcFile;           // "dolphinui.rc" (Shell) | "whalefileviewtoolbarui.rc" (eigener Client)
    enum class Target { ActiveView, OwnFileView } target;
    QStringList allowedActions;   // nur für OwnFileView: Prototypen des Clients
    // Instanzen: global = mainToolBar; file-view = alle WhaleFileViewToolBar mit diesem Descriptor
};
```

- Heute gibt es zwei Descriptoren. Weitere (Preview, Git, plugin-definiert, eine zweite globale
  Toolbar) sind entweder **weitere `<ToolBar>` in einem Client** (global) oder **weitere Clients
  mit eigener RC** (instanzbasiert). Die Combo im Dialog wächst automatisch mit, weil KDE alle
  Toolbars der Fabrik listet. Nichts ist auf zwei Einträge festgelegt.
- Ein plugin-definierter Toolbar-Typ wäre dasselbe Muster: Das Plugin liefert die erlaubten
  Action-IDs und eine Fabrik für gebundene Actions, und Whale erzeugt den Client.

### 9.2 Persistenz

| Set | Datei (installiert) | Lokale Benutzerdatei | Wer schreibt |
|---|---|---|---|
| Global/Main | `dolphinui.rc` (Qt-Ressource `:/kxmlgui5/whale/…`) | `~/.local/share/kxmlgui5/whale/dolphinui.rc` | KEditToolBar (wie heute) |
| File View | `whalefileviewtoolbarui.rc` (neu, Qt-Ressource) | `~/.local/share/kxmlgui5/whale/whalefileviewtoolbarui.rc` | KEditToolBar (Part-Client) |

- Es gibt **ein** Set pro Typ, nicht „Left/Right“ und nicht „Details/Treemap“. Alle Instanzen
  lesen beim Neuaufbau das geparste Layout des Managers.
- Das Toolbar-Aussehen der File View Toolbars (Icongröße, Textstil) wird, falls überhaupt
  konfigurierbar (E7), gemeinsam in einer Whale-Gruppe gespeichert, nicht über
  `KMainWindow::saveMainWindowSettings`.
- `KConfig`/`GeneralSettings` werden für das Layout nicht gebraucht. XMLGUI ist die
  Persistenz. Das ist die Lösung mit der wenigsten Fork-Sonderlogik.

---

## 10. Migration

1. **Bestehende Toolbar → Global/Main:** Das ergibt sich automatisch, weil der Name `mainToolBar`
   und die Datei `dolphinui.rc` bleiben. Auch wenn später die Version von `dolphinui.rc`
   steigt, übernimmt `KXmlGuiVersionHandler` die Benutzer-Toolbars **namentlich** in das neue
   Dokument (`kxmlguiversionhandler.cpp`: `extractToolBars` / `removeToolBars` /
   `insertToolBars`). Eine Benutzerkonfiguration geht dabei nicht verloren.
2. **File View Toolbar → Defaults:** Eine lokale `whalefileviewtoolbarui.rc` existiert noch nicht.
3. **Keine automatische Aufteilung.** Wer heute Back/Forward/View Settings global hat, sieht sie
   danach global **und** pro View. Das ist redundant, aber nichts geht kaputt, und der Benutzer
   kann entfernen. Optional ein einmaliger Hinweis (E11). Die Einmaligkeit hängt dann an einem
   Whale-Konfig-Flag, z. B. `MultiToolbarIntroduced=1`.
4. **Defaults für neue Benutzer** (ohne lokale Datei) ändern wir erst in einem späteren
   Milestone über `mainToolBar` in `dolphinui.rc` (E6). Für Toolbars ist **kein Versionssprung
   nötig**, denn bestehende lokale Dateien gewinnen ohnehin. Einen Sprung braucht es nur, wenn
   sich Menüs ändern.
5. **Reversibel:** Lokale `whalefileviewtoolbarui.rc` löschen und File View Toolbars
   ausblenden, dann ist der alte Zustand exakt wiederhergestellt. `dolphinui.rc` des Benutzers
   wird nie von Whale-Code angefasst.
6. **Testbar:** Tests mit temporärem `XDG_DATA_HOME` und vorbereiteter lokaler
   `dolphinui.rc` (Version 52, eigene Toolbar) prüfen: Main = Benutzerlayout, File View =
   Default. Ein zweiter Fall prüft dasselbe mit simuliertem Versionssprung der installierten
   Datei.

---

## 11. XMLGUI-/KDE-Integration

### 11.1 Was wir von KDE nutzen

`KToolBar` (Darstellung, KDE-Stil), `KActionCollection` (Prototypen und private Instanzen),
`KXMLGUIClient` mit eigener RC und eigenem `KXMLGUIBuilder` (verhindert ein sichtbares
Container-Widget im Fenster), `KEditToolBar` unverändert, `KXmlGuiVersionHandler` für die
Migration.

### 11.2 Minimale Whale-Erweiterungen

1. Eigener Client für die File View Toolbar mit Prototyp-Actions (Text, Icon, Tooltip,
   Priority; ohne Funktion).
2. Layout-Parser für `<ToolBar name="whaleFileViewToolBar">` (Action/Separator/Spacer) und
   `<ActionProperties>` aus dem Client-DOM.
3. Combo-Kosmetik (8.2), Vorauswahl, Neuladen nach `newToolBarConfig`.
4. Eigenes Kontextmenü der File View Toolbar.

### 11.3 Grenzen von KDE (dokumentiert)

- Defaults setzt immer alle Toolbars zurück (8.4).
- Die Combo-Labels bekommen ein „<gui-name>“-Suffix (8.2).
- Ein Client dauerhaft in der Fabrik würde in KCommandBar sichtbar (8.3).
- Shortcuts in Tooltips: Die Instanz-Actions haben keine Shortcuts, also zeigt der Tooltip
  keine. Gewollt, denn ein Shortcut wirkt auf die aktive View, nicht zwingend auf diese.

### 11.4 Spike-Artefakte

`$SCRATCHPAD/spike/` (CMake + `main.cpp` + zwei RC-Dateien) und der KXmlGui-Quellcode in
`$SCRATCHPAD/src/kxmlgui`. Nicht im Repo, nicht committet.

---

## 12. Plugin-Views

- Die File View Toolbar gehört zur File View, nicht zum View-Typ. Bei einem Wechsel
  File List ↔ Cushion Treemap in Pane X bleibt die Toolbar und **nur der Zustand** ändert sich.
  Die Gates des eigenen `WhaleViewActions` reagieren auf `WhaleViewHost::currentViewChanged`/
  `capabilitiesChanged` von X.
- Treemap in X (Capabilities u. a. ohne HiddenFiles/Previews/HostOrder/Zoom): `show_hidden_files`,
  `show_preview`, Sort/Group, Zoom und `whale_include_subfolders` sind in **X's** Toolbar
  disabled. Gleichzeitig kann die File List in Y alles aktiv haben. Back/Up/Reload bleiben in
  beiden aktiv.
- Keine Treemap-Toolbar, die View-lokale Treemap-Leiste bleibt im Plugin.
- Plugin-View-Einträge im `view_settings`-Menü der Instanz kommen aus dem eigenen
  `WhaleViewActions` mit derselben Registry (aus `WhaleViewServices`, fensterweit).

---

## 13. Zukunft: frei dockbare Views

- Die Architektur hängt an **`DolphinViewContainer` als Instanz**, nicht an „primary/secondary“.
  Ein Container in einem Dock, ein dritter Container oder ein dynamisch erzeugter Container
  bekommt seine Toolbar beim Erzeugen. Der Manager wird über `window()->findChild` gefunden. Für
  Docks in eigenen Top-Level-Fenstern bräuchte das später einen expliziten Zeiger, das ist eine
  kleine Anpassung.
- Was heute auf zwei Panes festgelegt ist, liegt **in Dolphin** (`DolphinTabPage` mit
  `m_primaryViewContainer/m_secondaryViewContainer`, `DolphinNavigatorsWidgetAction` mit
  `Primary/Secondary`), nicht in diesem Plan.
- Die globale Semantik „aktive View“ skaliert ebenfalls, weil sie schon heute nur einen
  Container-Zeiger kennt.
- Location Bar: Langfristig gehört pro View ein eigener Navigator in die File View Toolbar.
  `DolphinViewContainer::connectUrlNavigator(DolphinUrlNavigator*)` erlaubt das bereits pro
  Instanz. Das ist ein eigener späterer Milestone (TB7). Bis dahin bleibt `url_navigators`
  global (E9).
- Dev-MCP und Tests adressieren Views künftig über eine **stabile Instanz-ID** (z. B. ein Whale-
  Property am Container) statt über einen Pane-Index, behalten den Index aber als Komfort.

---

## 14. Dolphin-Diff-Risiko

| Datei | Änderung | Warum | Umfang |
|---|---|---|---|
| `src/dolphinviewcontainer.cpp` **[D]** | `LayoutStructure` + 1 Zeile `whaleToolBar` (Zeilen verschieben), Toolbar erzeugen und in Zeile 0 einhängen, Include | Platzierung über der Suchleiste (E5) | ~10 Zeilen |
| `src/dolphinmainwindow.cpp` **[D]** | `WhaleToolbarManager` im Konstruktor erzeugen; `configureToolbars` übernehmen (disconnect/connect wie About) | Dialog mit zweitem Client | ~3–5 Zeilen |
| `src/dolphinui.rc` **[D]** | später neue Global-Defaults (E6) | nur neue Benutzer | ~3 Zeilen, ohne Versionssprung |
| `src/CMakeLists.txt` **[D]** | Whale-Toolbar-Lib verlinken (analog `whaleviewhost`) | Build | 1–2 Zeilen |
| `src/views/dolphinviewactionhandler.*` | **keine** geplant | Zweitinstanz funktioniert wie in `DolphinPart`, Shortcuts entzieht Whale nachträglich | 0 (TB0 bestätigt) |
| `src/dolphinmainwindow.cpp` `updateHamburgerMenu` | optional später: Actions, die in einer File View Toolbar stehen, nicht zusätzlich im Hamburger zeigen | Kosmetik | E-Kandidat, nicht geplant |

Vollständig in Whale (`src/whale/toolbars/` als neue statische Lib `whaletoolbars`, analog
`whaleviewhost`): `WhaleToolbarDescriptor`, `WhaleToolbarManager`,
`WhaleFileViewToolbarClient` + `whalefileviewtoolbarui.rc`, `WhaleToolbarLayout` (Parser),
`WhaleToolbarConfigurator`, `WhaleFileViewToolBar`, `WhaleViewCommandScope`,
`WhaleContainerCommands`. Dazu ein Refactoring in `src/whale/host/`: `WhaleViewServices`
herauslösen, `WhaleViewActions` bindbar machen, Lookups in `WhaleViewHost` umstellen.

Merge-Risiko mit upstream: gering. `LayoutStructure` und der MainWindow-Konstruktor ändern sich
selten. `dolphinui.rc` ändert sich upstream gelegentlich, Konflikte dort sind trivial.

---

## 15. Teststrategie

Alle Tests in `src/whale/tests/` als QTest (`ecm_add_test`), offscreen lauffähig, mit eigenem
`XDG_DATA_HOME`/`XDG_CONFIG_HOME` (siehe bisherige Whale-Tests: globale View-Properties in
`init()` zurücksetzen).

**Toolbar-Instanzen** (`whalefileviewtoolbartest`)
- ohne Split: genau 1 sichtbare `WhaleFileViewToolBar` im aktiven Tab
- Split an: 2, jede Nachfahre ihres Containers; Split aus: 1, die zweite ist zerstört
  (QPointer)
- zweiter Tab: eigene Toolbars; nie angezeigter Tab hat noch keinen Scope (lazy)
- beide Instanzen haben dieselbe Action-Reihenfolge (Konfiguration), aber verschiedene
  QAction-Objekte

**Targeting**
- Back in Toolbar A navigiert A, B bleibt unverändert, auch wenn B vorher aktiv war
- symmetrisch für B
- globale Toolbar/Shortcut wirkt auf die aktive View
- nach Klick in Toolbar B ist B aktiv (falls E4 = ja)
- keine Shortcuts an Instanz-Actions, keine Warnung „Ambiguous shortcut“ (Message-Handler
  fängt Warnungen)

**State**
- A = Details/Previews an/Hidden aus, B = Treemap/Previews aus/Hidden an → checked/enabled
  pro Toolbar korrekt
- Auswahl nur in A → Rename/Trash in A enabled, in B disabled
- Flat in A an → `whale_include_subfolders` checked nur in A
- Back/Forward/Up/Stop enabled je nach eigener History/Ladezustand

**Settings** (`whaletoolbarconfigtest`)
- Dialog öffnen: Combo hat zwei Einträge mit Descriptor-Namen, die Vorauswahl stimmt
- Available-Liste der File View = Prototypen; Main = Shell-Collection
- beide editieren, Apply → beide lokalen Dateien geschrieben, Instanzen neu aufgebaut
- Cancel ohne Apply → keine Datei geändert
- Defaults → beide Dateien entfernt, Defaults aktiv (KDE-Semantik)
- Neustart: neues Fenster liest das gespeicherte Layout
- Change Text an einem File-View-Prototyp → Instanzen zeigen den Text, die globale Action nicht

**Migration** (`whaletoolbarmigrationtest`)
- vorbereitete lokale `dolphinui.rc` (Benutzerlayout) → Main = Benutzerlayout, File View =
  Default
- dasselbe mit installierter Datei in höherer Version (Version-Handler)
- keine lokale Datei → beide Defaults

**Plugins**
- File List vs. Cushion Treemap in zwei Panes: Gates nur in der Treemap-Pane
- Plugin entfernt (`setPluginAvailable(false)`) → Toolbar bleibt, Zustand File List

**Future sanity**
- Unit-Test erzeugt einen dritten `DolphinViewContainer` außerhalb einer `DolphinTabPage`
  (Dock-Simulation) → er bekommt eine funktionierende Toolbar, Targeting stimmt
- Grep-Wächter (CMake-Test, wie `WhaleIdentityTest`): `src/whale/toolbars` enthält kein
  `primaryViewContainer`/`secondaryViewContainer`

**Bestehende Suites** müssen grün bleiben: `whaleactionstest`, `whaleviewhosttest`,
`dolphinmainwindowtest` (bekannte Altfehler siehe Memory: `testAccessibilityTree`).

---

## 16. Dev-MCP-Strategie (minimal, später)

Erweiterungen in `src/whale/devmcp/` (Regel: klein halten, ein neues Tool nur, wenn Shell oder
Tests es nicht zuverlässig können):

- `get_ui_state` erweitern:
  - `toolbars.global` mit `{visible, actions:[{id, enabled, checked}]}`
  - `panes.N.toolbar` mit `{instanceId, ownerView, visible, actions:[{id, enabled, checked,
    menu:[…]}]}`
  - `toolbarDialog` mit `{open, selected, available:[…], current:[…]}`, falls offen
- neues Tool `trigger_toolbar_action {toolbar: "global" | pane-index | instanceId, id,
  menuEntry?}`: klickt den echten `QToolButton` (bzw. wählt den Menüeintrag) der
  angegebenen Instanz, damit Aktivierungs- und Targeting-Pfad mitgetestet werden.
- neues Tool `configure_toolbars {open, select, insert?, remove?, apply?, cancel?, defaults?}`
  über die Dialog-Widgets (`findChild`). Für Abnahme-Szenarien reicht open/select/apply/cancel.
- Akzeptanzskript `test_tb_acceptance.py` (privater Bus, Neustart wie O3).

---

## 17. Milestones

| MS | Inhalt | Ergebnis / Abnahme | Dolphin-Diff |
|---|---|---|---|
| **TB0** Spike (Wegwerf-Branch, nie mergen) | im echten Whale: Scope mit 2. Handler + `WhaleViewActions`-Bindung, drei Actions (`go_back`, `show_preview`, `view_settings`) in einer Container-Toolbar; temporärer Client im echten Configure-Toolbars-Pfad | Fragen beantwortet: Shortcut-Konflikte, `basic_actions`-Nulls, findChild-Problem, Speicher pro Scope, `createGUI`-Pfade (`addHamburgerMenuToToolbar`), Aktivierung | – |
| **TB1** Vorarbeiten | `WhaleViewServices` herauslösen; `WhaleViewActions` mit fester Bindung möglich; Host-Lookups umstellen | keine sichtbare Änderung, alle Tests grün | 0 |
| **TB2** File View Toolbar zur Laufzeit | `WhaleFileViewToolBar`, Manager, Descriptor, Client + RC mit Defaults, Parser; Scope mit Navigation/Reload/Stop + `view_settings` | Toolbar pro View, Targeting/State/Split-Tests | `dolphinviewcontainer.cpp`, CMake |
| **TB3** Erlaubtes Action-Set vervollständigen | View-Modi, Zoom, Sort, Preview/Hidden/Flat, Filter, Auswahl-Actions (Rename/Trash/…); Gates; disabled reason in eigener Pane | State- und Plugin-Tests | 0 |
| **TB4** Settings-Dialog | Configurator, temporärer Client, Combo-Labels, Vorauswahl, Kontextmenü, ActionProperties | Settings-Tests inkl. Restart | `dolphinmainwindow.cpp` (3–5 Zeilen) |
| **TB5** Migration & Defaults | Migrationstests; neue Global-Defaults für neue Benutzer (E6); Sichtbarkeits-Toggle (E7); optional Hinweis (E11) | Migrationstests, UX-Diary-Eintrag | `dolphinui.rc` (optional) |
| **TB6** Dev-MCP & Abnahme | State/Tools aus 16, `test_tb_acceptance.py` offscreen + Wayland | Abnahme via Dev-MCP | 0 |
| **TB7** (später, eigener Plan) | Location Bar pro View in der File View Toolbar; Abbau des geteilten Navigator-Splitters | – | voraussichtlich `dolphintabpage.cpp`, `dolphinnavigatorswidgetaction.*` |

Gegenüber dem Vorschlag ist das Refactoring als eigener, verhaltensneutraler Schritt (TB1)
herausgezogen. Die Laufzeit-Toolbar (TB2) kommt **vor** dem Dialog (TB4). So lässt sich
Targeting/State testen, bevor UI-Konfiguration hinzukommt, und TB2 ist mit einem festen
Default-Layout schon nutzbar.

---

## 18. Aktionszuordnung (erste Version)

Spalten: **heute** = Ziel/Herkunft; **Vorschlag** = G (nur global), V (in der File View Toolbar
erlaubt; global bleibt sie trotzdem verfügbar), ? (Entscheidung nötig); **Weg** = wie die
Instanz entsteht (H = Handler-Zweitinstanz, W = WhaleViewActions-Bindung, C = Whale-Command mit
explizitem Container, – = nicht in V).

### Aktuelle Toolbar-Inhalte (Default + Benutzer)

| ID | heute | Vorschlag | Weg | Begründung |
|---|---|---|---|---|
| `go_back` | MainWindow, aktive View (`:1290`), Popup-History, Mittelklick | **V** (Default V) | C | Die History ist pro Container. „Zurück“ ist die typische pane-lokale Aktion. |
| `go_forward` | wie oben | **V** (Default V) | C | dito |
| `go_up` | MainWindow, aktive View | **V** (Default V) | C | dito |
| `view_settings` | Handler-Menü (+ Whale-Einträge) | **V** (Default V), global weiter erlaubt | H+W | Der Zustand (Modus, Previews, Hidden, Flat, Sort) ist pro View, und das Menü bekommt eigene Unter-Actions. Siehe 19 (O3). |
| `url_navigators` | eine QWidgetAction mit zwei Navigatoren | **G** | – | Ein Widget pro Fenster, das sich bereits an den Panes ausrichtet. Pro View erst in TB7. |
| `split_view` | Tab-Ebene, Text/Icon nach aktiver Seite | **G** | – | Betrifft Tab-Layout. Eine pane-eigene Action „Close this view“ wäre später eine **neue** Action (E-Kandidat). |
| `split_stash` | Tab-Ebene | **G** | – | dito |
| `toggle_search` | aktive View (`find()`) | **G** | – | Vorgabe 20; die Suchleiste ist zwar pro Container, aber Ctrl+F/Toggle wirken auf die aktive View. Ein Duplikat pro View ist erst sinnvoll, wenn Suche pro View gewollt ist (E8). |
| `hamburger_menu` | Fenster | **G** | – | Fensterweit |

### Weitere plausible Toolbar-Actions

| ID | heute | Vorschlag | Weg | Begründung |
|---|---|---|---|---|
| `go_home` | aktive View | V | C | Navigation einer View |
| `view_redisplay` (Reload) | aktive View; MainWindow-Signal `urlRefreshed` | V (Default V) | C | Pane-lokal. Das Signal `urlRefreshed` nur für die aktive View emittieren (Verhalten wie heute) |
| `stop` | aktive View; enabled über Ladesignale | V | C | Pane-lokal |
| `replace_location`, `editable_location` | aktive View, Navigator | G | – | Hängt am Navigator-Widget (TB7) |
| `icons`/`compact`/`details`/`view_mode` | Handler | V | H+W | Pro View (19) |
| `view_zoom_in/out/reset`, `zoom` | Handler | V | H | Pro View; Gate bei Plugin ohne Zoom |
| `show_preview` | Handler | V | H | Pro View; Gate |
| `show_hidden_files` | Handler | V | H | Pro View; Gate (Treemap) |
| `whale_include_subfolders` | Whale | V | W | Pro View (Content Scope) |
| `sort`, `folders_first`, `hidden_last`, `group_by`, `additional_info` | Handler | V | H | Pro View; Gate HostOrder |
| `view_properties`, `restore_view_settings_default` | Handler (+ MainWindow-State) | V | H (+ State aus C) | Pro View |
| `toggle_filter`, `show_filter_bar` | aktive View | V | C | Die Filterleiste ist pro Container |
| `renamefile`, `movetotrash`, `deletefile`, `duplicate`, `properties`, `copy_location` | Handler + T2-Router | V (nicht Default) | H | Auswahl der eigenen View; Router hat bereits einen `DolphinView*`-Parameter |
| `edit_cut`, `edit_copy`, `edit_paste` | MainWindow, aktive View + `routeAction(view,…)` | ? | C | Mit explizitem View-Parameter machbar (`routeAction` nimmt die View); Paste-Zustand/-Gründe pro View → TB3 oder später |
| `create_dir`, `create_file`, `new_menu` | Handler-Signal → MainWindow `createDirectory(url)`; `KNewFileMenu` ist **ein** Objekt pro Fenster | ? | C / Aktivieren+Weiterleiten | `createDirectory(url)` nimmt die URL schon explizit; `new_menu` hat ein gemeinsames Menüobjekt → vorerst nicht in V |
| `toggle_selection_mode(_tool_bar)` | aktive View; Bottom-Bar nutzt die Fenster-Collection | ? | Aktivieren+Weiterleiten | Die Leiste zeigt globale Actions → nur sinnvoll, wenn die Pane dabei aktiv wird (E4) |
| `edit_select_all`, `invert_selection` | aktive View | V | C | `X->view()->selectAll()` (URL-Editor-Sonderfall bleibt global) |
| `copy_to_inactive_split_view`, `move_to_inactive_split_view`, `focus_inactive_split_view` | Tab-Ebene, „andere“ Pane | G | – | Semantik „die andere“ setzt genau zwei Panes voraus |
| `compare_files` | Auswahl beider Panes | G | – | Fenster-/Tab-weite Auswahl |
| `open_terminal`, `open_terminal_here`, `open_preferred_search_tool`, `manage_disk_space` | URL der aktiven View | ? (vorerst G) | später C | Wären pro View sinnvoll, aber nicht Teil des ersten Schnitts |
| `edit_undo`, `edit_redo` | globaler `FileUndoManager` | G | – | Global |
| `file_new`, `new_tab`, `file_close`, `undo_close_tab`, `closed_tabs`, `activate_*_tab`, `file_quit` | Fenster/Tabs | G | – | Fensterweit |
| `add_to_places`, `bookmarks` | aktive View/Fenster | G | – | Fensterweit (Places) |
| `show_*_panel`, `panels`, `lock_panels`, `focus_*_panel`, `switch_terminal_url_sync` | Docks | G | – | Fensterweit |
| `options_*`, `options_configure*`, `window_color_sheme`, `help_*` | Fenster | G | – | Fensterweit |
| `basic_actions` | Handler-Menü mit Cut/Copy/Paste | – | – | Braucht MainWindow-Actions in derselben Collection; nicht anbieten |
| `split_view_menu`, `popout_split_view` | Tab-Ebene | G | – | wie `split_view` |

**Vorgeschlagene Defaults**

- File View Toolbar (neu): `go_back, go_forward, go_up, view_redisplay, <Spacer/>, view_settings`.
- Global/Main für **neue** Benutzer (E6): `url_navigators, split_view, split_stash, toggle_search,
  hamburger_menu`. Back/Forward/View Settings sind dann pro View. Bestehende Benutzer behalten
  ihr Layout (10).

---

## 19. O3 / View-Mode

- Heute: `view_settings` (Toolbar) und `view_mode` wirken über Handler und WhaleViewActions auf
  die aktive View. Seit O3/View-Menü-Cleanup enthält die Liste auch Plugin-Views; die Wahl
  schreibt über `WhaleViewHost::selectView()` die Präferenz pro Ort.
- **Empfehlung B: auch in der File View Toolbar erlauben, dort als Default.** Der View-Modus ist
  ein Zustand **der View**, und die Pane mit der Treemap ist dann direkt erkennbar und
  umschaltbar. Genau diese Mehrdeutigkeit („welche Pane schalte ich gerade um?“) motiviert den
  Umbau. In der File View Toolbar wirkt die Wahl auf genau diese View (eigener Handler +
  eigener `WhaleViewActions` → `selectView()` auf dem eigenen Host); die O3-Präferenz wird wie
  bisher pro Ort geschrieben.
- Global bleibt `view_settings` konfigurierbar (wirkt auf aktive View), ist aber für neue
  Benutzer nicht mehr Default (E6).

---

## 20. Search

- Heute: `edit_find` (Ctrl+F) und `toggle_search` öffnen/schließen die **Suchleiste des aktiven
  Containers** (`DolphinMainWindow::find`, `:969`). Jede Pane hat ihre eigene Suchleiste und ihren
  eigenen Suchmodus; eine Suche über beide Panes gibt es nicht. `toggle_search` zeigt den Zustand
  der aktiven Pane (`updateSearchAction`).
- **Bleibt global** (Vorgabe), kein Duplikat in der File View Toolbar. Weil die Aktivierung bei
  Klick in eine Pane (4.3) die globale Toolbar mitzieht, sieht der Benutzer immer den Suchzustand
  der Pane, mit der er gerade arbeitet. Pro-View-Suche wäre technisch einfach (C:
  `X->setSearchBarVisible`) und ist offene Entscheidung E8.

---

## 21. Benennung im UI

- Intern: `global` / `file-view`.
- Combo im Dialog und Kontextmenüs: **„Main Toolbar“** für die globale. Das ist der KDE-Standard,
  bereits so in `dolphinui.rc` übersetzt, und die Bezeichnung in „Toolbars Shown“. „Global“ ist
  Entwicklersprache; „Window Toolbar“ ist ungewohnt.
- Für die pro-View-Toolbar: **„File View Toolbar“**. „Pane Toolbar“ wäre kürzer, aber
  „Pane“ verliert mit frei dockbaren Views seinen Sinn; „View Toolbar“ kollidiert mit dem Menü
  „View“. Offene Entscheidung E2.

---

## 22. Offene Entscheidungen

| # | Frage | Empfehlung |
|---|---|---|
| E1 | O3: View-Mode in der File View Toolbar? | B: ja, dort Default |
| E2 | UI-Namen | „Main Toolbar“ / „File View Toolbar“ |
| E3 | Defaults nur für gewählte Toolbar? | KDE-Semantik (alle) hinnehmen; optional „Reset File View Toolbar“ im Kontextmenü |
| E4 | Aktiviert ein Klick in die File View Toolbar ihre Pane? | Ja (Präzedenz URL-Navigator) |
| E5 | Platzierung | Neue erste Zeile im Container (kleiner Dolphin-Diff) statt im Host |
| E6 | Neue Global-Defaults für neue Benutzer | Ja, in TB5: `url_navigators, split_view, split_stash, toggle_search, hamburger_menu` |
| E7 | Sichtbarkeit/Aussehen der File View Toolbars | Ein Toggle für alle (Kontextmenü + View-Menü); Icon-only, kleine Icons; kein eigenes Aussehen pro Instanz |
| E8 | Suche auch pro View? | Vorerst nein |
| E9 | Location Bar pro View | Später (TB7), bis dahin global |
| E10 | Auswahl-Actions/Cut/Copy/Paste/New in der File View Toolbar | Rename/Trash/… in TB3 erlaubt (nicht Default); Cut/Copy/Paste/New später |
| E11 | Hinweis an Bestandsbenutzer wegen doppelter Buttons | Optional einmaliger KMessageWidget-Hinweis, keine automatische Änderung |
| E12 | Prototyp-Client temporär oder dauerhaft in der Fabrik | Temporär (nur während des Dialogs); Fallback dauerhaft |

---

## 23. Klare Empfehlung

**Umsetzen**, in der Reihenfolge TB0 → TB6. Die Architektur muss vorher nicht geändert werden;
das einzige vorbereitende Refactoring (TB1) betrifft Whale-Code. Es gibt keinen technischen Blocker:

- Der Settings-Teil ist mit KDE-Bordmitteln lösbar, besser als erwartet (Combo, typabhängige
  Available-Liste, Apply/Cancel über alle Toolbars). Einzige Abweichung ist „Defaults setzt alles
  zurück“.
- Die Migration ist strukturell sicher (Name `mainToolBar` bleibt, neue Datei für File View).
- Die größte Hürde ist das Action-Targeting. Sie wird durch die vorhandene View-Parametrisierung
  von `DolphinViewActionHandler` (Präzedenz `DolphinPart`) und `WhaleViewActions` stark
  entschärft; das Risiko (Seiteneffekte einer Zweitinstanz, Shortcut-Entzug, Speicher) klärt TB0
  mit wenig Aufwand, bevor Produktcode entsteht.

---

## 24. TB0-Ergebnisse (2026-10-09)

Spike auf dem lokalen Wegwerf-Branch `feature/multi-toolbar-tb0-spike` (Commit 72def271c, **nie
pushen, nie mergen**): `src/whale/spike/whaletb0spike.{h,cpp}`, `whaletb0spikeui.rc`, Hooks in
`dolphinviewcontainer.cpp` (LayoutStructure + 1 Zeile) und `dolphinmainwindow.cpp`
(Configure Toolbars umgehängt), Messtest `src/whale/tests/whaletb0spiketest.cpp` (12 Tests, alle
grün, offscreen). Bestehende Suites mit Spike: `whaleactionstest`, `whaleviewhosttest`,
`whaleviewmenutest`, `whalecommandroutingtest`, `whalepersistencetest`, `whaleselectiontest`,
`dolphinviewbridgetest` grün; `dolphinmainwindowtest` nur mit dem bekannten Altfehler
`testAccessibilityTree` (nicht gegen master neu verglichen). Screenshot (offscreen, Split): jede
Pane zeigt ihre eigene Leiste `Back | Previews <Spacer> View Settings`; Back links ausgegraut
(keine History), rechts aktiv.

### 24.1 Antworten auf die TB0-Fragen

| Frage | Ergebnis | Folge für den Plan |
|---|---|---|
| Funktioniert eine zweite `DolphinViewActionHandler`-Instanz pro View? | **Ja.** Zustand pro View korrekt (Previews, Hidden, View-Modus, Auswahl), Trigger wirkt nur auf die eigene View, `view_settings`-Menü besteht aus den Actions der Instanz. | 5.3 bestätigt. |
| Initialzustand | `setCurrentView()` setzt den Action-Zustand **nicht**; der Besitzer muss danach `updateViewActions()` aufrufen (macht heute `DolphinMainWindow::updateViewActions`). | Scope ruft es nach dem Binden auf (TB2). |
| Shortcuts nicht entzogen | 17 Instanz-Actions haben Default-Shortcuts. Bei sichtbaren Instanzen öffnet KXmlGuis Konfliktprüfung beim Shortcut einen **modalen Dialog** „The key sequence 'Alt+.' is ambiguous … No action will be triggered.“ – auch die globale Action wirkt dann nicht. | Entzug ist **Pflicht** (5.3), Test bleibt als Regressionstest. |
| Shortcuts entzogen | Alt+. wirkt nur auf die aktive View, keine Mehrdeutigkeit. | bestätigt |
| `basic_actions`-Nulls | 3 × „QWidget::insertAction: Attempt to insert null action“ pro Scope (Cut/Copy/Paste fehlen in der Instanz-Collection); das Menü der Instanz enthält nur Rename/Trash/Properties. | Im Scope Cut/Copy/Paste **vor** dem Handler als C-Kommandos anlegen oder `basic_actions` nicht anbieten und die Warnung in Tests verbieten (TB3). |
| findChild-Problem | Mit zwei Scopes gibt es **3 `WhaleViewActions` und 3 `WhaleViewRegistry`** (und 3 TreeCaches) pro Fenster. `findChild` liefert zufällig die richtige (direktes Kind des Fensters wird zuerst gefunden), die Registry und der Cache werden aber pro Instanz neu angelegt. | TB1 (`WhaleViewServices` herauslösen) ist notwendig, nicht nur Kosmetik. |
| Kosten pro Scope | 66 Actions, 255 QObjects, ~1,6 ms (RelWithDebInfo, offscreen); globale Collection: 153 Actions. | Lazy-Erzeugung beim ersten Anzeigen reicht; kein weiterer Optimierungsbedarf. |
| Plugin-Views | Treemap rechts: `show_hidden_files`/`show_preview` nur rechts disabled, links und global (links aktiv) enabled; Plugin-Eintrag nur in der rechten Instanz checked; „Details“ in der rechten Instanz schaltet nur rechts zurück. | 12 bestätigt. |
| Split-Lebenszyklus | Ohne Split 1, mit Split 2, Split schließen → zweite Toolbar zerstört; neuer Tab bekommt seine eigene. Gleiches Layout, verschiedene QAction-Objekte. | 7 bestätigt. |
| Configure Toolbars mit temporärem Client | Combo „Main Toolbar“ / „File View Toolbar“ (Suffix-Kosmetik greift), Available der File View = nur Prototypen, Insert + Apply schreibt `~/.local/share/kxmlgui5/whale/<rc>` und baut beide Instanzen neu, Hauptleiste unverändert (8 Actions), nach Schließen ist der Client wieder aus der Fabrik, `KMainWindow::toolBars()` = 1. | 8.1–8.3 bestätigt. |
| Cancel ohne Apply | schreibt nichts | 8.5 bestätigt |
| Defaults | Frage „Do you really want to reset all toolbars of this application to their default? The changes will be applied immediately.“; danach sind **beide** lokalen Dateien gelöscht, auch wenn File View gewählt war. | 8.4 bestätigt (E3 bleibt). |

### 24.2 Neue Befunde

1. **Navigation aktiviert die Pane – immer.** Auch ein programmgesteuertes Back in der inaktiven
   Pane (ohne Klick) macht sie aktiv: `DolphinView::setUrl → urlChanged → sichtbarer
   DolphinUrlNavigator → activated → DolphinViewContainer::activate`. Dolphin unterdrückt das
   nur in `DolphinViewContainer::redirect()` (`blockActivation`). Folge für E4: Bei allen
   Navigations-Actions (Back/Forward/Up/Home, Ordner öffnen) ist die Aktivierung durch Dolphin
   schon festgelegt; „Toolbar-Klick aktiviert“ (Empfehlung E4) ist damit nur konsequent. Würde
   man E4 = nein wählen, müsste man Dolphins Verhalten ändern. Empfehlung bleibt: **ja**.
2. **Dolphin selbst nutzt „aktivieren und weiterleiten“.** `DolphinViewContainer` baut für
   „Authorization required“ eine eigene Action, weil `actAsAdminAction()` „always refers to the
   active view instead of this->m_view“, und löst dann `setActive(true); actAsAdminAction->trigger();`
   aus (`dolphinviewcontainer.cpp`, um Zeile 1090). Das ist genau der Fallback aus 4.2 Nr. 4 –
   ein Präzedenzfall in Dolphin.
3. **Lebensdauer des temporären Clients an `QDialog::finished` koppeln, nicht an `destroyed`.**
   `destroyed` kommt auch, wenn der Dialog als Kind des Fensters in dessen Destruktor zerstört wird;
   dann ist `KXmlGuiWindow` samt Fabrik schon weg → SIGSEGV (im Spike reproduziert und behoben).
   Für TB4: zusätzlich im Destruktor des Configurators aufräumen, ohne die Fabrik anzufassen,
   wenn das Fenster schon zerstört wird.
4. **Gemeinsame View-Properties.** Mit Whales Default „gleiche Ansicht für alle Ordner“
   (`GlobalViewProps`) teilen sich beide Panes Previews/Hidden/Modus persistent: Lädt eine View
   ihre Properties neu, übernimmt sie die zuletzt von der anderen geschriebenen Werte. Die
   Toolbars zeigen trotzdem immer den **tatsächlichen** Zustand ihrer View; die Tests für
   unabhängigen Zustand brauchen aber `GlobalViewProps=false` und verschiedene Ordner. Kein
   Toolbar-Problem, aber für die UX-Erwartung wichtig (UX-Diary-Kandidat beim Umbau).
5. **Rename ohne Auswahl ist in der File List enabled** (startet Dolphins Selection Mode). Das
   ist Dolphin-Semantik, keine Toolbar-Abweichung; die Plan-Formulierung in 6 („Rename/Trash
   enabled nach Auswahl“) gilt für Plugin-Views (Gates), nicht für die File List.

### 24.3 Bewertung

TB0 bestätigt die Architektur ohne Änderungen am Grundansatz. Kein Blocker. Präzisierungen für
TB1–TB4: `updateViewActions()` nach dem Binden (TB2), Shortcut-Entzug als Pflicht mit
Regressionstest (TB2), `basic_actions`-Behandlung (TB3), `WhaleViewServices` notwendig (TB1),
Client-Lebensdauer über `finished` (TB4). **Nächster Schritt: TB1** (verhaltensneutrales
Refactoring von `WhaleViewActions`), auf dem Go des Benutzers. Den Spike-Branch nach TB4 löschen.

---

## 25. TB1-Ergebnisse (2026-10-09)

Branch `feature/multi-toolbar` (von `master` = 5fc824a41, mit den beiden Plan-Commits als
86969c4a0 / 54d1a5fb8 vorneweg). Commits: 9a66c9819 (Refactoring), 5ab89feef (Tests + Lint).
Nicht gepusht, nicht gemergt. **Keine Dolphin-Datei geändert, Plugin-API bleibt Version 9.**

### 25.1 Ownership

```text
DolphinMainWindow
├─ WhaleViewServices        (genau 1, direktes Kind des Fensters, kein Singleton)
│   ├─ WhaleViewRegistry    (Plugin-Discovery einmal pro Fenster)
│   └─ WhaleTreeCache       (M8b-Semantik unverändert, pro Fenster)
└─ WhaleViewActions         (die des Fensters, aktive View; benutzt die Services, besitzt sie nicht)
```

- Erzeugt werden die Services von `WhaleViewServices::ensure(window)`, aufgerufen vom
  bisherigen Konstruktor `WhaleViewActions(collection, handler, window)`. Den ruft
  `DolphinMainWindow` unverändert auf (`this` passt jetzt auf die `QWidget*`-Überladung). So
  bleibt `dolphinmainwindow.cpp` unberührt, und Owner ist trotzdem das Fenster.
- Lebensdauer: Die Services werden nach `WhaleViewActions` als Kind des Fensters angelegt und
  daher nach ihnen zerstört (Qt löscht Kinder in Anlegereihenfolge). Tab-Widget und View-Bereiche
  sind noch früher angelegt und sterben zuerst. Es gibt keine manuellen Deletes. Tests prüfen das
  Aufräumen per QPointer.
- Jedes Fenster hat eigene Services. Registry und Cache werden nicht fensterübergreifend geteilt
  (Test).

### 25.2 Was verschoben wurde

| vorher | nachher | Grund |
|---|---|---|
| `WhaleViewActions::m_registry` | `WhaleViewServices::registry()` | fensterweit |
| `WhaleViewActions::m_treeCache` | `WhaleViewServices::treeCache()` | fensterweit |
| `WhaleViewActions::notifyViewChanged()` (vom Host per findChild aufgerufen) | `WhaleViewServices::notifyViewChanged()` → Signal `viewChanged()`; `WhaleViewActions` leitet es als bisheriges `sessionChanged()` weiter | Die Session ist fensterweit; das Signal bleibt, damit `DolphinMainWindow` unverändert bleibt. |

Nicht verschoben, weil pro Action-Set: Plugin-Einträge der View-Modi, „View Modes“-Untermenü,
„Show flat“, Gates, Disabled-Reason-Hook, Rename-Weiterleitung. `WhaleViewActions::registry()`
und `treeCache()` bleiben als delegierende Bequemlichkeit (Tests, Dev-MCP-Altcode).

### 25.3 Bindung von WhaleViewActions

- Neu ist der Konstruktor `WhaleViewActions(WhaleViewServices *, KActionCollection *,
  DolphinViewActionHandler *, QObject *parent)` für Sets, deren Eltern-Objekt nicht das Fenster
  ist, z. B. eine spätere File View Toolbar.
- `setCurrentHost()` ist die einzige Bindung. Die Klasse folgt der aktiven View nicht selbst.
  Das tut das Fenster, indem es `setCurrentHost` bei jedem Wechsel aufruft. Ein an View X
  gebundenes Set ruft es einmal auf (dokumentiert, Test `testBoundActionSetStaysOnItsHost`).
- `WhaleViewActions::clearShortcuts(KActionCollection *)` ist der vorbereitete Helper für TB2.
- TB0-Befund `updateViewActions()` nach dem Binden ist im Test-Helper umgesetzt und
  geprüft. Im Produkt gehört er in den Scope von TB2.

### 25.4 Entfernte findChild-Abhängigkeiten

- `WhaleViewHost`: 3 × `window()->findChild<WhaleViewActions *>()` (TreeCache für Plugin-Kontext,
  TreeCache für F1-Inhalt, Session-Benachrichtigung) und 1 × `window()->findChild<WhaleViewRegistry
  *>()` sind ersetzt durch `WhaleViewServices::of(this)`. Dahinter steckt eine Suche nur unter
  den **direkten** Kindern des Fensters nach dem dort eindeutigen Service-Objekt.
- Dev-MCP: Registry, Cache und Plugin-Availability laufen über die Services. Die UI-Abfragen
  (View-Menü, Gate-Gründe, `set_view remember=true`) nutzen `windowViewActions()`, also das Set
  als direktes Kind des Fensters. Gebundene Sets liegen später im View-Bereich.
- Neuer Lint `whale_view_host_no_action_lookup` (ctest): `src/whale/host` darf
  `findChild(ren)<WhaleViewActions` nicht enthalten. Er hätte die 3 alten Stellen in master
  gefunden.
- Tests suchen weiter per `findChild<WhaleViewActions*>()`. Das ist unkritisch, weil Qt die
  direkten Kinder zuerst prüft und es im Produkt bisher nur ein Set gibt. In TB2 auf die
  direkte Suche umstellen, falls ein Test dann ein gebundenes Set im Fenster hat.

### 25.5 Tests

Neu: `whaleviewservicestest` (12 Testfunktionen, offscreen und 3 × Wayland grün) deckt die
geforderten Punkte 1–16 ab:

- ein Service pro Fenster; zwei Fenster haben zwei verschiedene Services;
- Registry und Cache sind Kinder der Services;
- das Fenster-Set benutzt die Services und besitzt nichts davon;
- ein zweites, gebundenes Set (eigene Collection, eigener Handler) teilt die Services; danach
  gibt es pro Fenster 1 Registry, 1 Cache und 2 Action-Sets;
- Plugin-Availability ist für beide Sets identisch;
- das zweite Set löschen lässt Services und Fenster-Set funktionsfähig;
- das Fenster schließen räumt alles auf;
- ein Host in einem Fenster mit Services, aber **ohne** Action-Set, findet die Plugins und meldet
  View-Wechsel; ohne Services gibt es wie bisher keine Plugins;
- zwei View-Bereiche teilen den Cache (Treemap links = Miss, rechts = Hit/Join);
- ein gebundenes Set bleibt bei Aktivierungswechseln auf Host B, während das Fenster-Set
  wechselt, und seine Einträge wirken nur auf B;
- ohne Shortcuts: Alt+. wirkt auf die aktive View, kein „ambiguous“-Dialog, die globalen
  Shortcuts sind unverändert;
- `updateViewActions()` nach dem Binden ergibt den Zustand der eigenen View.

Regression:
- **Offscreen:** alle 34 Whale-Tests grün, inklusive Dev-MCP-Acceptance M3, M4, M5, M6, M7, M9,
  T1, T2, O3, F1, der Lints `whale_view_api_boundary`, `whale_view_plugin_dependencies`,
  `whale_identity_regressions`, `whale_dev_mcp_default_off` und des neuen Lints.
- **Gesamtsuite offscreen:** 65 Tests, davon 2 rot:
  - `folderspaneltest`: Timeout unter `-j4`, allein grün.
  - `dolphinmainwindowtest::testAccessibilityTree`: bekannter Altfehler. Gegen master (detached
    gebaut) mit **identischer** Meldung verglichen, sowohl im Gesamtlauf („computed value …
    baseline“) als auch einzeln („QToolButton's accessibleInterface …“).
- **Wayland (Whale-Unit-Tests, -j1):** rot sind nur die bekannten Altfehler
  `whaleactionstest::testItemInfo` und `whaletreemaptest::testHover`.
- Neuer Test-Gotcha: Ein nie angezeigtes `DolphinMainWindow` stürzt unter Wayland bei einer
  Clipboard-Änderung in `updatePasteAction()` ab (bekannt). Der Fenster-lose Host-Test verwirft
  deshalb vorher das Hauptfenster aus `init()`.

### 25.6 Abweichungen vom Plan

Keine inhaltlichen Abweichungen. Präzisierungen:
- Die Session-Benachrichtigung wanderte mit in die Services. Sie war der dritte
  findChild-Lookup des Hosts und ist fensterweit.
- Die Services werden über `ensure()` aus dem Konstruktor von `WhaleViewActions` angelegt, statt
  im MainWindow. Das spart die Dolphin-Änderung, Owner bleibt das Fenster.
- `of()` ist bewusst eine Suche unter den direkten Kindern, kein expliziter Zeiger. Grund: Der
  Host entsteht in `DolphinViewContainer` (Dolphin) vor seiner Einbettung ins Fenster. Ein
  expliziter Zeiger bräuchte einen Dolphin-Hook. Eindeutig ist die Suche trotzdem, weil es genau
  ein Service-Objekt pro Fenster gibt.

### 25.7 Bewertung

TB1 ist abgeschlossen. Der Benutzer sieht keine Änderung (keine Toolbar-, Menü-, Shortcut- oder
Settings-Änderung). **TB2 ist ohne weiteres Architektur-Refactoring möglich** und wartet auf das
Go des Benutzers.

---

## 26. TB2-Ergebnisse (2026-10-10)

Branch `feature/multi-toolbar`. TB1 wurde am 2026-10-10 gepusht (bis 3adef5d70). TB2 ist
**nicht** gepusht. Commits: b9fb1fe90 (Toolbar-Grundlage und Laufzeit), 38cb0c6c6 (Tests und
Lint). Plugin-API bleibt Version 9.

### 26.1 Klassen (`src/whale/toolbars/`, in `dolphinstatic` gebaut)

| Klasse / Datei | Aufgabe |
|---|---|
| `WhaleToolbarDescriptor` | Art einer Toolbar: id (`global`/`file-view`), Anzeigename, `<ToolBar>`, RC-Datei, Ziel (`ActiveView`/`OwnFileView`), erlaubte Actions |
| `WhaleToolbarLayout` | liest `<Action>`, `<Separator/>`, `<Spacer/>` aus einer `<ToolBar>`. Die Semantik ist dieselbe, die KEditToolBar schreibt (Attribute wie `name="separator_0"` werden ignoriert, getestet). |
| `WhaleFileViewToolbarClient` | `KXMLGUIClient` für `whalefileviewtoolbarui.rc` (Ressource `:/kxmlgui5/whale/`, Benutzerkopie in `kxmlgui5/whale/`) |
| `WhaleToolbarManager` | genau einer pro Fenster, direktes Kind (`ensure()`/`of()` wie die Services). Kennt die Descriptoren und das gemeinsame Layout, Signal `fileViewLayoutChanged()` |
| `WhaleFileViewToolBar` | `KToolBar` pro `DolphinViewContainer`, baut sich aus dem Layout |
| `WhaleViewCommandScope` | gebundenes Action-Set einer View (private Collection) |
| `WhaleContainerCommands` | Back/Forward (mit History-Menü), Up, Home, Reload, Stop mit festem Container |

Nicht gebaut, weil für TB2 unnötig: Configurator, Prototyp-Actions des Clients und ein
`KXMLGUIBuilder`. Die kommen in TB4, wenn der Client in die Fabrik geht.

### 26.2 Ownership und Lebenszyklus

```text
DolphinMainWindow ─┬─ WhaleViewServices (TB1)
                   └─ WhaleToolbarManager ── unique_ptr<WhaleFileViewToolbarClient>
DolphinViewContainer ── WhaleFileViewToolBar (Zeile 0 des Layouts)
                          └─ WhaleViewCommandScope (lazy, beim ersten showEvent)
                               ├─ KActionCollection "whale-file-view" (nie in der Fabrik)
                               ├─ WhaleContainerCommands
                               ├─ DolphinViewActionHandler (setCurrentView einmalig + updateViewActions)
                               └─ WhaleViewActions (Services des Fensters, setCurrentHost einmalig)
```

- Der Client ist kein QObject und wird vom Manager per `unique_ptr` gehalten. Es gibt keinen
  Client auf dem Stack und keinen in der Fabrik. Damit kann es den Double-free aus dem TB0-Spike
  nicht geben. TB4 muss ihn bei `QDialog::finished` wieder aus der Fabrik nehmen.
- Manager und Services entstehen lazy beim ersten Anzeigen einer Toolbar bzw. mit den
  Fenster-Actions. Eine Pane-Liste gibt es nirgends.
- Split öffnen, schließen, Tab öffnen und schließen: Die Toolbar entsteht und stirbt mit ihrem
  Container. Getestet mit 5 Split-Zyklen und 3 Tab-Zyklen, mit QPointer auf Toolbar, Scope und
  Actions.
- Ein dritter Container in einem `QDockWidget` des Fensters funktioniert (Test): Er bekommt eine
  Toolbar, dieselben Services und denselben Manager, und Up/Back zielen richtig.
- Session-Restore (Tab-Widget `saveProperties`/`readProperties`) erzeugt beide Toolbars
  korrekt.
- Lint `whale_toolbars_no_pane_sides`: `src/whale/toolbars` enthält weder
  primary/secondary noch left/right noch `inactiveViewContainer`.

### 26.3 Default-Layout und Darstellung

`whalefileviewtoolbarui.rc` (Version 1): `go_back, go_forward, go_up, view_redisplay,
<Spacer/>, view_settings`. Die Benutzerkopie gilt nach XML-GUI-Regeln (Test). Nicht erlaubte
IDs (z. B. `basic_actions`) werden übergangen. Erlaubt sind in TB2: die sechs genannten
plus `go_home` und `stop`.

Darstellung: kleine Icons (16 px), Icon-only, für alle Instanzen gleich. Gesetzt über
`KToolBar::applySettings()` auf Benutzer-Ebene, daher bleibt es auch bei KDE-Stilwechseln.
Nicht verschiebbar. Kein Kontextmenü (`Qt::PreventContextMenu`), weil KToolBars Menü ein
MainWindow als Parent braucht und QToolBars Menü ans Fenster weitergereicht würde.

### 26.4 Targeting, Shortcuts, Aktivierung

- Jede Action zielt auf den eigenen Container. Getestet:
  - Back in B bei aktivem A navigiert nur B;
  - Back in A navigiert nur A;
  - `trigger()` ohne Klick (also ohne Aktivierung) wirkt trotzdem auf die eigene View;
  - Reload lädt nur die eigene View neu;
  - Alt+Links wirkt auf die aktive View.
- Keine Shortcuts. Der Scope leert am Ende seines Konstruktors die ganze Collection. Der Test
  prüft alle erreichbaren Actions rekursiv inklusive Menüs (über 50 pro Instanz) und schlägt bei
  jedem Shortcut fehl. Die globalen Shortcuts bleiben. Alt+. und F5 lösen bei zwei sichtbaren
  Toolbars keinen „ambiguous“-Dialog aus.
- Ein Maus-Druck auf einen Button aktiviert die Pane (Event-Filter auf den Buttons). Der Befehl
  zielt unabhängig davon auf die eigene View. Navigation aktiviert ohnehin (TB0).
- Nicht übernommen: Mittelklick für „in neuem Tab“ (Back/Forward/Up) und das
  `urlRefreshed`-Signal beim Reload (Terminal-Panel). Beides sind Signale bzw. Slots des
  MainWindow und kommen ggf. in TB3. Die History-Menüs zeigen den Pfad statt Dolphins
  Places-Namen, weil `DolphinMainWindow::urlNavigatorHistoryAction` privat ist.

### 26.5 Zustand und Cushion Treemap

- Back/Forward folgen `historyChanged` der eigenen History, Up der eigenen URL, Stop
  `directoryLoadingStarted/Completed` der eigenen View, und `restore_view_settings_default`
  folgt `viewSettingsChanged` (das setzt sonst das MainWindow).
- `view_settings` besteht aus den Actions der Instanz (Test: enthält nicht die globalen).
  Beispiel A = Details, B = Cushion Treemap: A hat Details checked, B Cushion Treemap. In B sind
  Hidden, Previews, Flat, Sort und Zoom-in disabled, in A enabled. Ein Wechsel in B zurück zur
  File List behält dieselbe Toolbar-Instanz und übernimmt den neuen Zustand.
- **Gefundener und behobener Fehler (Wayland-Abnahme, auch vom Benutzer gesehen: Menü am
  Fensterrand abgeschnitten):** `DolphinViewActionHandler` hängt das `view_settings`-Menü an
  `QApplication::activeWindow()`
  (`viewSettings->menu()->setParent(QApplication::activeWindow())`, Upstream-Commit 1b292eb01).
  `QWidget::setParent()` entfernt dabei den Popup-Typ. Entsteht der Scope in einem aktiven
  Fenster, z. B. beim Split-Öffnen, wurde das Menü zu einem Kind-Widget im Fenster und dort
  abgeschnitten gezeichnet. Fix im Scope: Das Menü wird ein Popup mit dem eigenen Fenster als
  Parent. Der Accessibility-Zweck des Hacks bleibt erhalten. Test
  `testViewSettingsMenuIsPopup`; das tatsächliche Öffnen wird nur offscreen geprüft, auf
  Wayland mit echter Eingabe abgenommen.
- **Upstream-Befund dabei:** Dasselbe betrifft das **globale** Menü eines zweiten
  `DolphinMainWindow`, das erzeugt wird, während ein anderes aktiv ist. Dessen Menü wird dann
  Kind des **anderen** Fensters (offscreen nachgewiesen). Das ist nicht Teil von TB2 und ein
  Ticket-Kandidat.
- Geteilte ViewProperties (`GlobalViewProps`) bleiben unverändert und sind Future Work. Die
  Toolbar zeigt immer den tatsächlichen Zustand ihrer View.

### 26.6 Performance und Lifetime

- Kosten wie in TB0 gemessen: etwa 66 Actions und 1,6 ms pro angezeigter View. Nie angezeigte
  Tabs kosten nichts.
- Pro Scope gibt es 3 Qt-Warnungen „Attempt to insert null action“ aus `basic_actions` des
  Handlers (wie in `DolphinPart`). `basic_actions` wird nicht angeboten, Dummy-Actions gibt es
  bewusst nicht.
- **ASan/UBSan:** separater Build `../build-whale-asan` (RelWithDebInfo,
  `ECM_ENABLE_SANITIZERS=address;undefined`, ohne Dev-MCP).
  - `whalefileviewtoolbartest` und `whaleviewservicestest`: 0 Befunde.
  - `whaleactionstest`, `whaletreemaptest`, `whalerecursivecontenttest`, `whaleviewhosttest`:
    ASan sauber. UBSan meldet „downcast“ bei Objekten in der Zerstörung (Test-Plugin
    `TestListWidget`, `WhaleViewHost`-Lambdas), auf TB1 in **identischer** Anzahl (6/4/2/4),
    also vorbestehend.
  - Mit `CMAKE_BUILD_TYPE=Debug` bricht jeder Test mit mehreren Fenstern pro Prozess in Dolphins
    `Admin::WorkerIntegration::createActAsAdminAction` ab. Grund: Ein `Q_ASSERT` liest den
    statischen `instance`-Zeiger eines schon zerstörten Fensters (heap-use-after-free,
    upstream). Deshalb läuft ASan auf RelWithDebInfo. Ebenfalls ein Ticket-Kandidat.

### 26.7 Tests und Abnahme

- Neu: `whalefileviewtoolbartest` mit 15 Testfunktionen: Layout-Parser, Default- und
  Benutzerlayout, eine Toolbar pro View inklusive Platzierung und Accessibility, Split- und
  Tab-Lebenszyklus, Action-Identität und Shortcuts, Targeting, unabhängiger Navigationszustand,
  View Settings mit Treemap, Menü als Popup, kein Shortcut-Dialog, dritter Container,
  Session-Restore, Fenster schließen.
- Der TB1-Test zählt jetzt die Action-Sets der Toolbars mit.
- Regression offscreen: alle 36 Whale-Tests grün, inklusive aller Dev-MCP-Acceptances (M3–M9,
  T1, T2, O3, F1) und aller Lints.
- Gesamtsuite offscreen: nur der bekannte Altfehler
  `dolphinmainwindowtest::testAccessibilityTree`, mit identischen Meldungen wie auf master. Da
  er früh abbricht, prüft der neue Test die Accessibility-Kette der Toolbar-Buttons selbst (Name
  plus Hauptfenster als Vorfahre).
- Wayland (Unit-Tests):
  - `whalefileviewtoolbartest` 5 × grün;
  - rot nur die bekannten `whaleactionstest::testItemInfo` und `whaletreemaptest::testHover`;
  - `whaleselectiontest::testRenameSelected` schlug einmal fehl, danach 6/6 grün auf TB2 und
    6/6 auf TB1, also nicht zuzuordnen (Wayland-Flake);
  - sporadisch schlägt `qWaitForWindowActive` fehl (Wayland-Aktivierung). Die neuen Tests
    brechen dann sauber ab, statt mit Nullzeigern weiterzulaufen, und räumen eine Test-RC-Datei
    immer in `cleanup()` weg.
- Echte Plasma/Wayland-Abnahme (Dev-MCP-Session, echte Klicks über ydotool):
  - eine Pane und Split, Details links, Cushion Treemap rechts (Screenshots);
  - Back links bei aktiver rechter Pane navigiert links und aktiviert links (3 ×);
  - View-Settings-Menüs links und rechts öffnen mit dem richtigen Zustand. Rechts sind in der
    Treemap Sort, Group, Additional Info, Flat, Previews, Hidden und Restore disabled.
  - neuer Tab, Split schließen: genau eine Toolbar bleibt;
  - im Log nur die erwarteten Null-Action-Warnungen.
  - Ein früher Klick ohne Navigation ließ sich nicht reproduzieren. Wahrscheinlichste Ursache
    war das damals noch fehlerhafte, im Fenster liegende Menü.

### 26.8 Dolphin-Diff

Nur `src/dolphinviewcontainer.cpp`: `LayoutStructure` um `whaleFileViewToolBar = 0` erweitert
(übrige Zeilen +1), eine Zeile zum Erzeugen/Einhängen, ein Include (+11/−8). Die
Build-Verkabelung liegt in `src/whale/CMakeLists.txt` (`target_sources(dolphinstatic …)`).
`src/CMakeLists.txt` und `dolphinmainwindow.cpp` sind unverändert.

### 26.9 Abweichungen vom Plan

- Prototyp-Actions, Builder und Configurator kommen erst in TB4 (keine leeren Hüllen).
- `allowedActions` ist in TB2 auf Navigation plus `view_settings` begrenzt. TB3 erweitert.
- Neu gegenüber dem Plan: Korrektur des Popup-Typs beim `view_settings`-Menü (26.5).
- Kontextmenü vorerst keins (Plan 7: eigenes Menü kommt mit TB4/TB5).

### 26.10 Bewertung

TB2 ist abgeschlossen. Die Laufzeit-Toolbar existiert stabil, zielt korrekt und braucht keinen
globalen Active-View-Zustand. **TB3 ist möglich**, auf das Go des Benutzers.

---

## 27. TB3-Ergebnisse (2026-10-11)

Branch `feature/multi-toolbar`, **nicht** gepusht. Commits: 1f8a95d45 (Action-Satz, Routing,
Zustand), 5b82ccf7c (Tests). Plugin-API bleibt Version 9. TB4 ist nicht begonnen.

### 27.1 Erlaubter Action-Satz

Eine Definition: `WhaleViewCommandScope::allowedActions()` (35 IDs, alle identisch mit den
globalen IDs, per Test geprüft). Der Descriptor der File View Toolbar, die Toolbars und die Tests
lesen nur diese Liste. `WhaleViewCommandScope::action(id)` liefert ausschließlich erlaubte
Actions; die private Collection enthält mehr, weil der Handler alle Dolphin-View-Actions erzeugt.

| Gruppe | IDs | Weg |
|---|---|---|
| Darstellung | `icons`, `compact`, `details`, `view_mode`, `view_settings`, `view_zoom_in`, `view_zoom_out`, `view_zoom_reset`, `zoom`, `show_preview`, `show_hidden_files`, `sort`, `folders_first`, `hidden_last`, `group_by`, `additional_info`, `view_properties`, `restore_view_settings_default` | H (Handler-Zweitinstanz) |
| Darstellung | `whale_include_subfolders` | W (`WhaleViewActions`) |
| Navigation/Zustand | `go_back`, `go_forward`, `go_up`, `go_home`, `view_redisplay`, `stop`, `toggle_filter`, `show_filter_bar` | C (`WhaleContainerCommands`) |
| Auswahl | `renamefile`, `movetotrash`, `deletefile`, `duplicate`, `properties`, `copy_location` | H + T2-Router |
| Auswahl | `edit_select_all`, `invert_selection` | C |

Text, Icon, Tooltip und Menü-Charakter kommen aus den echten lokalen Actions (dieselben Klassen
bzw. dieselben i18n-Texte wie global). TB4 kann die Prototypen daraus ableiten, z. B. aus einem
Scope oder aus den globalen Actions gleicher ID.

Die Einträge der Plugin-Views (z. B. Cushion Treemap) haben keine Action-ID und sind deshalb
nicht einzeln in eine Toolbar legbar. Sie sind über `view_settings` und `view_mode` erreichbar.
Ob sie eigene IDs bekommen, ist eine Frage für TB4.

**Bewusst ausgeschlossen** (unverändert global): `url_navigators`, `replace_location`,
`editable_location`, `split_view`, `split_stash`, `split_view_menu`, `popout_split_view`,
`toggle_search`, `hamburger_menu`, `compare_files`, `copy_to_inactive_split_view`,
`move_to_inactive_split_view`, `focus_inactive_split_view`, `edit_undo`, `edit_redo`,
Tab-/Fenster-/Panel-/Einstellungs-/Hilfe-Actions, außerdem `edit_cut`, `edit_copy`,
`edit_paste`, `create_dir`, `create_file`, `new_menu`, `toggle_selection_mode(_tool_bar)`,
`open_terminal(_here)`, `open_preferred_search_tool`, `manage_disk_space`, `basic_actions`,
`delete_shortcut`. Keine davon musste mitgelöst werden.

Das Default-Layout (`whalefileviewtoolbarui.rc`) ist unverändert.

### 27.2 Zustand pro View

- Handler-Actions und Gates: wie in TB2, jetzt für den ganzen Satz getestet. Die Gates des
  eigenen `WhaleViewActions` erfassen auch die neuen lokalen `edit_select_all` und
  `invert_selection`, weil die Container-Kommandos vor dem ersten `updateGates()` in der
  Collection stehen.
- **Auswahl-Actions:** neuer kleiner State-Provider `WhaleViewCommandScope::updateSelectionActions()`.
  Er spiegelt `DolphinMainWindow::updateFileAndEditActions()` für die eigene View, mit
  `WhaleCommandRouter::selectedItems(eigeneView)` (also inklusive Deep Selection). Auslöser:
  `DolphinView::selectionChanged` und `WhaleViewActions::commandSelectionChanged` des eigenen
  Sets. Der Handler selbst setzt diese Zustände nicht, das macht global das MainWindow.
- Filter: `toggle_filter` folgt `showFilterBarChanged` des eigenen Containers.
- Restore Defaults und Stop: wie in TB2 (`viewSettingsChanged`, Ladesignale der eigenen View).
- Disabled bleibt sichtbar: Keine Action wird aus Toolbar oder Menü entfernt (Test).

### 27.3 Auswahl, Deep Selection, „kein eindeutiges Ziel“

- Rename/Trash/Delete/Duplicate/Copy Location wirken nur auf die Auswahl der eigenen View. Bei
  einer Plugin-View mit eigener Auswahl (Treemap) gehen sie über den T2-Router an deren
  `WhaleFileOperations`. Getestet: Datei links ausgewählt, Deep Selection rechts, Papierkorb
  rechts entfernt nur das tiefe Item; die Rückfrage der Treemap bleibt.
- Die Schutzregeln sind unverändert: In Flat und Treemap bleibt `duplicate` durch das Gate
  „no destination folder“ disabled. Paste, Create New und Drop sind nicht im lokalen Satz.
- Ein Auslösen der Selection-Mode-Leiste gibt es lokal nicht. Der Handler bittet um das Verlassen
  des Selection Mode nach einer Aktion; das führt der Scope für den eigenen Container aus.

### 27.4 Disabled-Reasons

Dolphins `DisabledActionNotifier` hängt an Shortcuts. Lokale Actions haben keine, also gibt es
dort keinen Auslöser. Lösung ohne neue Infrastruktur: Ein **Klick auf einen disabled Button**
der File View Toolbar zeigt die Erklärung in der Message-Leiste **des eigenen Containers**
(`DolphinViewContainer::showMessage`, dieselbe Leiste wie beim Notifier). Der Event-Filter der
Toolbar bekommt den Klick auch bei disabled Buttons.

Erklärt wird nur, wofür es global schon eine Erklärung gibt (`WhaleViewCommandScope::disabledReason()`):
- kein Ziel (Flat/Treemap) für `duplicate`: Text aus `WhaleViewHost::refusal()`, geliefert über
  den vorhandenen `DisabledReasonHook` des eigenen `WhaleViewActions`;
- fehlende Rechte für `renamefile`, `deletefile`, `duplicate`: Dolphins Texte.

Capability-Gates (Hidden/Preview/Zoom/Sort in der Treemap) und „nichts ausgewählt“ haben wie
global keine Erklärung.

### 27.5 Navigation und Reload

- **Mittelklick** auf Back, Forward, Up, Home und auf Einträge der History-Menüs öffnet das Ziel
  als Hintergrund-Tab (`DolphinMainWindow::openNewTab`), mit History und URL des eigenen
  Containers. Dolphins `MiddleClickActionEventFilter` wird wiederverwendet. Ohne History-Ziel
  passiert nichts.
- **Reload:** `X->reload()`, danach `urlRefreshed(url)` des Fensters, aber nur, wenn X die aktive
  View ist. Das Signal ist fensterweit und für die aktive View gedacht (einziger Empfänger:
  Terminal-Panel). Ein echter Klick aktiviert die Pane vorher, erfüllt die Bedingung also immer.
  Ein programmatisches Auslösen für eine inaktive View lädt neu, meldet aber nichts.

### 27.6 Shortcuts, Menüs, mehrere Fenster

- Alle lokalen Actions und Unter-Actions sind shortcut-frei; der Test läuft jetzt über den
  ganzen Satz. Globale Shortcuts bleiben, kein „ambiguous“-Dialog.
- Alle Menüs der lokalen Actions (view_settings, view_mode, Sort, Group, Additional Info,
  Back/Forward-History) sind Popups; ein Parent, falls vorhanden, liegt im eigenen Fenster. Test
  mit drei Containern in zwei Fenstern, das zweite Fenster entsteht bei aktivem ersten.
- Jedes Fenster nutzt eigene Services und einen eigenen Manager (Test).

### 27.7 Tests und Abnahme

- `whalefileviewtoolbartest`: 26 Testfunktionen (11 neu): erlaubte und ausgeschlossene Actions,
  unabhängiger Zustand (View Mode, Preview, Hidden, Flat, Sort, Group, Zoom, Restore),
  Plugin-Gates über drei View-Wechsel, Auswahl pro View, Deep Selection, Disabled-Reasons,
  Filter, Mittelklick, Reload, Stop, Menüs/zweites Fenster.
- Offscreen: alle 36 Whale-Tests grün (Unit, Lints, Dev-MCP-Acceptances). `whale_dev_mcp_m7_e2e`
  schlug einmal bei parallelem Lauf (`-j4`) fehl, einzeln und mit `-j2` grün.
- Wayland: `whalefileviewtoolbartest` 3 × grün.
- ASan/UBSan (`../build-whale-asan`): `whalefileviewtoolbartest` und `whaleviewservicestest`
  0 Befunde. `whaleactionstest`, `whaleviewhosttest`, `whalecommandroutingtest`: ASan sauber,
  UBSan nur die bekannten Meldungen aus Test-Plugin und `WhaleViewHost`-Lambdas (6/4/6).
- **Altfehler, getrennt:** `dolphinmainwindowtest` schlägt in `testAccessibilityTree` und
  `testThumbnailAfterRename` fehl und hängt danach in `testViewModeAfterDynamicView`. Auf dem
  TB2-Stand (4ce9094d1) ist das Bild identisch; einzeln laufen die beiden letzten grün. Der
  Hänger war in 26.7 noch nicht notiert.
- Echte Plasma/Wayland-Abnahme (Dev-Whale mit eigenem Profil, erweitertes Benutzerlayout,
  echte Klicks über ydotool): Dateiliste links, Cushion Treemap rechts;
  - `details`, Hidden und Flat per Klick links ändern nur links;
  - Auswahl links aktiviert nur links die Auswahl-Buttons;
  - Klick auf disabled `duplicate` zeigt den Grund in der jeweils eigenen Pane (Flat links,
    Treemap rechts), die andere bleibt ohne Meldung;
  - Treemap-Deep-Selection: Papierkorb rechts fragt nach und entfernt nur das tiefe Item;
  - Mittelklick Back/Up öffnet das Ziel der eigenen Pane als Hintergrund-Tab;
  - Reload, View-Settings-Menüs beider Panes (echte Popups, richtiger Zustand), Split-Zyklen,
    Tab-Wechsel; Log ohne Warnungen außer den bekannten.
  - Nicht auf Wayland geprüft: zweites Fenster (nur offscreen im Test).

### 27.8 Dolphin-Diff und Abweichungen

Dolphin-Diff: **0 Dateien.** Geändert sind nur `src/whale/toolbars/` (6 Dateien) und der Test.

Abweichungen vom Auftrag:
- Lokale Auswahl-Actions sind ohne Auswahl **disabled**, global bleiben sie enabled und starten
  den Selection Mode. `properties` bleibt lokal enabled und zeigt ohne Auswahl den Ordner.
- Disabled-Reasons erscheinen auf Klick statt auf Shortcut (27.4).
- `view_redisplay` meldet `urlRefreshed` nur für die aktive View (27.5).
- Plugin-View-Einträge sind nicht einzeln platzierbar (27.1).

### 27.9 Bewertung

TB3 ist abgeschlossen. Der lokale Action-Satz ist vollständig, zielt explizit auf die eigene
View und braucht keinen Active-View-Zustand. **TB4-ready: ja**, auf das Go des Benutzers.

---

## 28. TB4-Ergebnisse (2026-10-13)

Branch `feature/multi-toolbar`, **nicht** gepusht. Commits: 0e72ca5d7 (Settings-Integration),
d8e6b854c (Tests). Plugin-API bleibt Version 9. TB5 ist nicht begonnen. Die interaktive
Wayland-Abnahme ist nach Freigabe erfolgt (28.12).

### 28.1 Configurator

`WhaleToolbarConfigurator` (`src/whale/toolbars/whaletoolbarconfigurator.{h,cpp}`), genau einer
pro Hauptfenster, direktes Kind (`install()`/`of()` wie Manager und Services).

- `install(window, saveNewToolbarConfig)` hängt die vorhandene Standard-Action „Configure
  Toolbars…“ um (disconnect/connect wie beim About-Dialog). Die Action wird im eigenen
  `actionCollection()` des Fensters per `KStandardAction::name()` gefunden, keine globale Suche.
  Das Kontextmenü der Hauptleiste benutzt dieselbe Action und landet damit im selben Dialog.
- `configure(descriptorId)` öffnet KDEs `KEditToolBar(guiFactory())` unverändert, mit
  `setDefaultToolBar()` für die Vorauswahl (`global` → `mainToolBar`, `file-view` →
  `whaleFileViewToolBar`). Ist der Dialog schon offen, kommt er nach vorn, die Auswahl bleibt.
- Kein eigener Dialog, kein eigener Umschalter. Der Dialog bleibt wie bei KDE nicht-modal.

### 28.2 Temporärer Client und Factory-Lifetime

Abweichung von der Skizze in 8.3: Der Client des Managers (liest das Layout zur Laufzeit) geht
**nie** in die Fabrik. Für den Dialog gibt es einen **eigenen** Client derselben Datei,
`WhaleFileViewToolbarEditClient` (privat in der `.cpp`, abgeleitet von
`WhaleFileViewToolbarClient`), mit den Prototyp-Actions und einem eigenen Builder. Damit
existieren die Prototypen überhaupt nur, solange der Dialog offen ist.

```text
configure()      → Client erzeugen (unique_ptr im Configurator) → guiFactory()->addClient()
                 → KEditToolBar anlegen, show()
QDialog::finished → guiFactory()->removeClient() → Client löschen → Dialog deleteLater()
```

- Aufgeräumt wird bei `finished` (OK, Cancel, Schließen, Esc), nicht bei `destroyed`.
- Wird das Fenster mit offenem Dialog zerstört, ist die Fabrik schon weg und hat den Client
  losgelassen (`~KXMLGUIFactory` setzt `factory` der Clients auf null). Der Destruktor des
  Configurators fasst die Fabrik nicht an; Client und Template-Leiste gehen mit ihm. Keine
  KXmlGui-Warnung, kein Befund unter ASan.
- Der Builder (`TemplateBuilder`) baut `<ToolBar>` als versteckte `KToolBar` **ohne Parent**.
  Sie hängt nicht im Fenster (`KMainWindow::toolBars()` bleibt 1) und wird beim Entfernen des
  Clients gelöscht. Nebenwirkung: Solange der Dialog offen ist, steht sie in
  `QApplication::topLevelWidgets()` (nie sichtbar, kein natives Fenster).

### 28.3 Neuer Befund: Reihenfolge der Fabrik-Clients

Nicht im Plan und nicht im TB0-Spike gesehen (dort nur ein Apply):

- `KEditToolBarWidget::rebuildKXMLGUIClients()` nimmt den **ersten** Client der Fabrik als
  Fenster und mischt nur bei ihm `ui_standards.rc` ein.
- `KXmlGuiWindow::saveNewToolbarConfig()` entfernt das Fenster und fügt es wieder an, also
  **hinten**. Nach dem ersten Apply steht ein anderer Client vorn (schon ohne Whale:
  `KDEPrivate::ToolBarHandler`, mit Terminal-Panel die KonsolePart).
- In Dolphin fällt das nicht auf, weil ein Apply immer die Hauptleiste geändert und damit das
  gemischte Dokument lokal gespeichert hat. Mit einer zweiten Toolbar gibt es Applys, die nur
  die File View Toolbar ändern: Das Fenster wurde dann beim **zweiten** Apply ohne
  `ui_standards.rc` neu gebaut, zwei Menüs fehlten (im Test reproduziert: 6 statt 8).

Abhilfe im Configurator, nach `saveNewToolbarConfig()`: Alle Clients, die vor dem Fenster
stehen, werden entfernt und wieder angefügt (dasselbe, was KEditToolBar beim Speichern mit allen
Clients tut). Danach steht das Fenster wieder vorn. Test `testApplyTwice` mit einem
zusätzlichen Fremd-Client. Upstream-Ticket-Kandidat, hier nicht gemeldet.

### 28.4 Combo und Labels

- Die Combo ist KDEs eigene und erscheint automatisch: „Main Toolbar“, „File View Toolbar“.
- Kosmetik: `polishToolBarNames()` entfernt nur Suffixe, die exakt „ <gui-name>“ eines
  Fabrik-Clients sind (`<dolphin>`, `<whale_fileview>`). Nur Texte, die Zuordnung bleibt
  indexbasiert bei KDE. Greift es nicht mehr, sieht man die Suffixe, sonst ändert sich nichts.
- Nach **Defaults** baut KEditToolBar sein Widget neu; die Kosmetik läuft deshalb auch nach
  jedem `newToolBarConfig` (Test).

### 28.5 Available Actions und Prototypen

- Main Toolbar: unverändert die Collection des Fensters, keine Filterung.
- File View Toolbar: genau `WhaleViewCommandScope::allowedActions()` (35), keine zweite Liste.
  Die Prototypen entstehen in einer Schleife über diese Liste und übernehmen Text, Icon-Text,
  Tooltip, Icon, Priority und Checkable von der **Fenster-Action gleicher ID**. Keine
  Einzeldefinitionen.
- Prototypen sind eigene `QAction`-Objekte ohne Funktion, weder die Fenster-Actions noch die
  Actions einer Pane (Test). Keine Toolbar zeigt je einen Prototyp.
- Die Fabrik wendet beim Hinzufügen die `ActionProperties` der Datei auf die Prototypen an,
  nimmt sie aber nie zurück. Deshalb werden die Prototypen bei jedem `clientRemoved` neu aus
  den Fenster-Actions gefüllt; nach Defaults zeigt der Dialog wieder die Standardtexte.
- Grenze: Hat der Benutzer Text oder Icon derselben Action in der **Hauptleiste** geändert,
  zeigt der Dialog diesen Text auch als Ausgangswert der File-View-Liste. Die Toolbars der
  Panes zeigen den echten Standard. Rein kosmetisch im Dialog.

### 28.6 Apply / OK / Cancel / Defaults

Alles KDE-nativ, keine eigene Undo-Schicht. Bei `newToolBarConfig`:

1. `DolphinMainWindow::saveNewToolbarConfig()` wie bisher (Navigator-Widget, View-Modi im
   View-Menü, Hamburger, erlaubte Toolbar-Bereiche).
2. Client-Reihenfolge herstellen (28.3).
3. `WhaleToolbarManager::reloadAllFileViewLayouts()`: alle Manager des Prozesses lesen die Datei
   neu, alle File View Toolbars bauen sich neu auf. Kein Neustart.
4. Combo-Kosmetik.

Getestet: Apply nur File View (Datei geschrieben, beide Panes sofort, Hauptleiste und
`dolphinui.rc` unberührt), beide Toolbars in einem Apply, Cancel ohne Apply (keine Datei),
Apply + Änderung + Cancel (Angewendetes bleibt), OK, dreimal Apply hintereinander.

**Defaults:** KDE-Semantik bewusst übernommen. Rückfrage „… reset all toolbars of this
application …“; Abbrechen ändert nichts; nach Bestätigung sind beide lokalen Dateien weg und
Hauptleiste und alle File View Toolbars zeigen sofort die Defaults, auch wenn File View gewählt
war. Der Dialog bleibt offen und arbeitet weiter. E3 (eigener „Reset File View Toolbar“) bleibt
offen für später.

### 28.7 ActionProperties

- `WhaleToolbarLayout` liest zusätzlich `<ActionProperties>` der Datei: `text`, `iconText`,
  `toolTip`, `icon`, `priority`. Andere Attribute werden nicht übernommen, **Shortcuts nie**.
  Die Properties gehören zum Layout-Vergleich, eine reine Text-Änderung löst also den Neuaufbau
  aus.
- `WhaleViewCommandScope::applyActionProperties()` setzt sie auf die Actions der eigenen View
  und merkt sich die Originalwerte. Beim nächsten Aufbau werden zuerst alle Originale
  wiederhergestellt (anders als bei KXmlGui zeigt die Toolbar nach Defaults sofort wieder das
  Original). Icon-Text und Tooltip, die vorher aus dem Text abgeleitet waren, sind danach
  wieder abgeleitet.
- Getestet: „Change Text…“ über den echten KDE-Unterdialog → beide Panes, auch nach „Neustart“
  (neues Fenster); die Fenster-Action gleichen Namens und die Hauptleiste bleiben unverändert.
- „Change Icon…“: KDEs Icon-Auswahl (`KIconDialog`) wird im Test nicht bedient. Getestet ist der
  Weg ab dem Attribut `icon="…"` in der Datei: beide Panes, Dialog behält es beim Speichern,
  Defaults stellt das Icon wieder her, Fenster-Action unverändert.
- Grenze wie bei KDE: Actions, deren Icon oder Text der Code später selbst ändert (z. B. das
  Icon von `view_mode`), überschreiben eine Benutzeränderung beim nächsten Zustandswechsel.
- Die File View Toolbar ist Icon-only; ein geänderter Text ist dort nur im Dialog sichtbar und
  dann, wenn die Darstellung später Text zeigt (E7).

### 28.8 Kontextmenü

Die File View Toolbar hat ein eigenes Menü mit genau einem Eintrag „Configure File View
Toolbar…“. Er öffnet denselben Dialog mit „File View Toolbar“ vorausgewählt, auch per
Rechtsklick auf einen Button. Das Menü der Fenster-Toolbars/Panels öffnet sich dort nicht mehr
zusätzlich. Kein „Configure Toolbars…“ daneben (wäre derselbe Dialog), kein Hide, kein Reset.

### 28.9 Shortcuts, Find Action, Shortcut-Dialog

- Prototypen haben nie Shortcuts: Sie entstehen ohne, sind als nicht konfigurierbar markiert,
  und nach jedem `clientAdded` der Fabrik wird ihre Collection geleert (Schutz gegen
  Shortcut-Schemata und gegen `shortcut="…"` in der Datei; getestet mit genau so einer Datei
  und nach Apply).
- Kein „ambiguous shortcut“ bei offenem Dialog: Alt+. und F5 wirken auf die aktive View (Test
  mit aktivem Hauptfenster).
- **Find Action** (KCommandBar) listet alle Fabrik-Clients. Solange der Dialog offen ist, ist
  die Action `open_kcommand_bar` dieses Fensters disabled und bekommt danach ihren vorherigen
  Zustand zurück. Andere Fenster sind nicht betroffen.
- **Configure Keyboard Shortcuts:** Dolphin übergibt dem Dialog nur die eigene Collection (und
  die der KonsolePart), nicht die Fabrik. Die Prototypen erscheinen dort nie.
- Nach `finished` ist der Client aus der Fabrik und gelöscht, die Prototypen existieren nicht
  mehr (Test mit QPointer).

### 28.10 Mehrere Fenster

Jedes Fenster hat eigenen Configurator, eigenen Dialog (Parent = eigenes Fenster) und eigenen
temporären Client in der eigenen Fabrik. Zwei Dialoge gleichzeitig funktionieren. Apply in
Fenster A aktualisiert auch die File View Toolbars von Fenster B (eine Konfiguration); die
**Hauptleiste** von B bleibt wie bei KDE bis zum Neustart alt. Fenster B schließen, während A
konfiguriert: kein Problem. Andere Prozesse lesen erst beim nächsten Start.

### 28.11 Tests

- Neu: `whaletoolbarconfigtest`, 21 Testfunktionen, bedient den echten `KEditToolBar` im
  Testprozess (Combo, Listen, Buttons, Unterdialoge): Combo/Labels/Vorauswahl, Available
  Actions beider Toolbars, Apply/OK/Cancel-Kombinationen, zweimal Apply, Separator/Spacer-
  Roundtrip, neues Fenster, zwei Fenster, Change Text, Icon, Defaults, 12 × Öffnen/Schließen,
  Fenster zerstören mit offenem Dialog (4 Varianten), Shortcuts, Find Action, Kontextmenü,
  Hauptleiste nach Apply.
- Offscreen: alle 36 Whale-Tests grün (Unit, Lints, alle Dev-MCP-Acceptances M3–M9, T1, T2, O3,
  F1), `clang-format` sauber.
- Gesamtsuite offscreen: nur `dolphinmainwindowtest` rot, Bild wie in 27.7
  (`testAccessibilityTree`, `testThumbnailAfterRename`, danach Hänger); alle Funktionen ab
  `testViewModeAfterDynamicView` laufen einzeln grün.
- **ASan/UBSan** (`../build-whale-asan`, offscreen): `whaletoolbarconfigtest`,
  `whalefileviewtoolbartest`, `whaleviewservicestest` mit 0 Befunden. Ein Lauf mit
  Leak-Erkennung zeigt keine Leaks aus dem neuen Code.
- Während TB4 gab es kein ydotool, keine Wayland-Fenster und keine Eingabe auf dem Desktop.

### 28.12 Wayland-Abnahme (2026-10-13, nach Freigabe)

Plasma/Wayland, Dev-Whale mit eigenem Profil, echte Eingabe über ydotool, nur in den Fenstern
der Test-Whale. Geprüft wurden nur die offscreen nicht abdeckbaren Punkte. Split: Dateiliste
in einer Pane, Cushion Treemap in der anderen.

| Prüfung | Ergebnis |
|---|---|
| Dialog öffnen (Fenster-Action), Combo mit der Maus umschalten | Dialog liegt über dem eigenen Fenster; Combo-Popup ist ein echtes Popup mit „Main Toolbar“ / „File View Toolbar“ ohne Suffix; Wechsel lädt die Listen |
| Action per Pfeil (`compact`) und per Drag & Drop (`details`) einfügen, Apply | beide Wege funktionieren; Datei geschrieben; beide Panes (auch die Treemap-Pane) zeigen sofort das neue Layout, Hauptleiste unverändert |
| Rechtsklick auf die File View Toolbar (beide Panes) | echtes Popup exakt an der Mausposition, ein Eintrag; öffnet den Dialog mit „File View Toolbar“ vorausgewählt |
| „Change Text…“ (getippt) und „Change Icon…“ mit KDEs Icon-Auswahl, Apply | `<Action icon="acrobat" iconText="Parent" name="go_up"/>` in der Datei; beide Panes zeigen das neue Icon |
| Änderung + Cancel | Datei bytegleich, Toolbars unverändert |
| Defaults | Rückfrage „… reset all toolbars of this application …“; nach „Reset“ lokale Datei weg, beide Panes zeigen sofort Default-Layout und Original-Icon, Dialog bleibt offen mit „File View Toolbar“ gewählt |

Log der Test-Whale: nur die bekannten Null-Action-Warnungen und QtDBus-Hinweise der
Abfragen, keine KXmlGui-Warnung.

Nicht auf Wayland geprüft: **zweites Fenster**. „New Window“ startet einen eigenen Prozess; zwei
Fenster in einem Prozess sind nur offscreen im Test abgedeckt (28.10). Sichtbar wurde dabei
nichts Neues. Nach der Abnahme ist die Test-Whale beendet, es läuft keine GUI-Automation mehr.

### 28.13 Dolphin-Diff und Abweichungen

Dolphin-Diff: nur `src/dolphinmainwindow.cpp`, **4 Zeilen** (ein Include, ein Aufruf von
`WhaleToolbarConfigurator::install()` über 3 Zeilen). Keine weitere Dolphin-Datei, kein Header.

Abweichungen vom Plan bzw. Auftrag:
- Eigener temporärer Client statt des Manager-Clients in der Fabrik (28.2).
- Client-Reihenfolge nach jedem Apply herstellen (28.3), neu.
- Find Action ist bei offenem Dialog disabled (28.9); sichtbare Kleinigkeit.
- Vorauswahl wird nicht nachgezogen, wenn der Dialog schon offen ist (28.1).
- „Change Icon…“ ist im automatischen Test nur ab dem geschriebenen Attribut abgedeckt (28.7),
  mit KDEs Icon-Auswahl auf Wayland abgenommen (28.12).
- Nicht gemacht, wie verlangt: Migration, neue Defaults, Sichtbarkeits-Setting, Reset-Eintrag,
  Location Bar, Änderungen am Action-Satz oder Routing.

Der Spike-Branch `feature/multi-toolbar-tb0-spike` existiert noch lokal (laut 24.3 nach TB4
löschen); nicht angefasst.

### 28.14 Bewertung

TB4 ist abgeschlossen, offscreen und auf Wayland abgenommen. **TB5-ready: ja**, auf das Go des
Benutzers.

---

## 29. TB5-Ergebnisse (2026-10-16)

Branch `feature/multi-toolbar`, **nicht** gepusht, nicht gemergt. Commits: 9befbe871
(Migration/Defaults/Sichtbarkeit), d97be71cc (Tests), Doku-Commit danach. Plugin-API bleibt
Version 9. TB6 und TB7 sind nicht begonnen. Die interaktive Wayland-Abnahme steht aus (29.12).

### 29.1 Neue Main-Toolbar-Defaults (E6)

`mainToolBar` in `src/dolphinui.rc` für Benutzer **ohne** eigene Kopie:
`url_navigators, split_view, split_stash, toggle_search, hamburger_menu`. Back, Forward und View
Settings fehlen dort, sie stehen in jeder File View Toolbar. `dolphinuiforphones.rc` ist
unverändert (eigene Leiste, kein Ziel von TB5).

**Kein Versionssprung** (bleibt 52), geprüft im Quellcode von KXmlGui 6.24
(`KXmlGuiVersionHandler`): Bei gleicher Version gewinnt die lokale Datei als Ganzes (nur `version >
bestVersion` wechselt die Wahl), die neuen Defaults erreichen Bestandsnutzer also nicht. Ein Sprung
wäre nur für neue **Menü**einträge in der RC nötig; den einzigen neuen Menüeintrag hängt Whale
deshalb per Code ein (29.4). Ein Sprung hätte außerdem KXmlGui veranlasst, die Benutzerdatei neu
zu schreiben (Toolbars und ActionProperties semantisch erhalten, aber nicht bytegleich).

### 29.2 Bestandsnutzer und Migration

- Whale-Code schreibt `~/.local/share/kxmlgui5/whale/dolphinui.rc` nie. Die Main Toolbar bleibt
  exakt (Reihenfolge, Buttons, ActionProperties). Test: Datei nach Start, Split, Schließen und
  zweitem Start **bytegleich**, keine `.backup`.
- Die File View Toolbar erscheint mit ihrem Default. Back/Forward/View Settings sind dann doppelt
  sichtbar; keine Bereinigung, kein Hinweis.
- Reversibel: File View Toolbars ausblenden (und ggf. `whalefileviewtoolbarui.rc` löschen) ergibt
  das Fenster von vorher (Test-Zeile „hidden“).
- Ältere Benutzerdatei (niedrigere Version): Das ist KDEs Versionshandler, unverändert seit vor
  TB5. Er nimmt die installierte Datei, setzt Benutzer-Toolbars namentlich und ActionProperties
  ein und **schreibt** die lokale Datei neu (Version 52). Test: Toolbar und Texte erhalten, Menüs
  installiert.
- Upstream-Pfad, nicht von Whale: `DolphinMainWindow::addHamburgerMenuToToolbar()` schreibt die
  Datei, wenn `GeneralSettings::version() < 201` und kein Hamburger in der Toolbar ist (Dolphins
  eigene Altmigration). Whale-Benutzer haben Version 202.

### 29.3 Sichtbarkeit (E7)

`WhaleToolbarSettings` (`src/whale/toolbars/whaletoolbarsettings.{h,cpp}`), eine Instanz pro
Prozess (`self()`), nicht `GeneralSettings`/kcfg:

```ini
# whalerc (KSharedConfig::openConfig(), XDG_CONFIG_HOME)
[WhaleToolbars]
ShowFileViewToolBars=false   # ohne Eintrag: true
```

- `setFileViewToolBarsVisible()` schreibt und `sync()`t sofort, dann Signal
  `fileViewToolBarsVisibleChanged`.
- Jede `WhaleFileViewToolBar` liest den Wert im Konstruktor (versteckt sich ggf.) und verbindet
  sich mit dem Signal. Damit reagieren alle Instanzen sofort: Split beide, andere Tabs, alle
  Fenster des Prozesses; neue Container übernehmen den Zustand.
- Eine verborgene Leiste, die nie sichtbar war, erzeugt keine Actions (Scope entsteht beim ersten
  `showEvent`, wie seit TB2).
- Aussehen: Icon-only, kleine Icons seit TB2 per `applySettings()` auf Benutzer-Ebene der
  `KToolBar`; KDE-Stil- und Icon-Änderungen ändern nur die KDE-Default-Ebene. Geprüft im
  KToolBar-Quellcode und im Test (auch nach `iconLoaderSettingsChanged`). Nichts geändert.
- KDEs „Shown Toolbars“/„Show Toolbar“ (`ToolBarHandler`) erfasst nur Leisten mit dem Fenster als
  Parent, die File View Toolbars also nicht. Keine Wechselwirkung.

### 29.4 View-Menü

- `KToggleAction` `whale_show_file_view_toolbars`, „Show File View Toolbars“, in der
  Action-Collection des Fensters (also auch in Find Action, Configure Shortcuts und als Button für
  die Main Toolbar platzierbar). Angelegt von `WhaleToolbarManager::setupActions()` **vor**
  `setupGUI`, damit eine in der Main Toolbar platzierte Action beim Start existiert.
- Nicht in `dolphinui.rc`. Der Manager hängt sie nach jedem `KXMLGUIFactory::clientAdded` des
  Fensters (setupGUI, createGUI, Apply in Configure Toolbars) in das View-Menü, direkt nach
  „Location Bar“ (also nach „Show Panels“, bei den ein-/ausblendbaren Fensterteilen); Fallback
  ans Ende. Präzedenz: `WhaleViewActions::plugViewModes()`.
- Bestandsnutzer mit eigener RC sehen den Eintrag damit ohne Versionssprung. Der Benutzerdatei,
  die KEditToolBar schreibt, wird er nicht hinzugefügt (Test).
- Hamburger-Menü bei verborgener Menüleiste: über „More“ → View (KHamburgerMenu zeigt die Menüs der
  Menüleiste); Dolphins `updateHamburgerMenu()` ist unverändert.

### 29.5 Kontextmenü

Zweiter Eintrag „Hide File View Toolbars“ (`whale_hide_file_view_toolbars`) nach „Configure File
View Toolbar…“. Blendet alle aus; der Weg zurück ist der View-Menü-Eintrag. Ohne Configurator
(kein Hauptfenster) erscheint nur „Hide“. Kein „Reset File View Toolbar“ (E3 bleibt offen).

### 29.6 Configure Toolbars und Defaults

- Funktioniert unabhängig von der Sichtbarkeit: Der temporäre Edit-Client (28.2) hängt nicht an den
  Leisten. Apply bei verborgenen Leisten: Datei geschrieben, Leisten bleiben verborgen, beim
  Einblenden zeigen sie das neue Layout (getestet für „nie gezeigt“ und „gezeigt, dann
  verborgen“).
- Defaults-Button unverändert: setzt beide Toolbars zurück; danach zeigt die Main Toolbar die neuen
  TB5-Defaults, auch für einen Bestandsnutzer (lokale Datei gelöscht), die File View Toolbars ihre
  Defaults. Die Sichtbarkeit ist kein Layout und bleibt, wie sie ist.

### 29.7 Persistenz und Multi-Window/Prozess-Semantik

- Neustart: Zustand bleibt (Test mit sauberem `HOME`/`XDG_CONFIG_HOME`/`XDG_DATA_HOME`).
- Mehrere Hauptfenster **im selben Prozess**: ein Schalter, alle Fenster live, alle Toggle-Actions
  synchron (Test).
- „New Window“ startet einen **neuen Prozess**; der liest den gespeicherten Wert beim Start. Kein
  Live-Sync zwischen laufenden Prozessen (KConfig liefert ihn ohne `KConfigWatcher` nicht; nicht
  erfunden). Ein laufender Prozess behält seinen Zustand bis zum eigenen Umschalten.

### 29.8 E11

Bewusst **nicht** umgesetzt (deferred): kein KMessageWidget, kein Banner, kein Flag
`MultiToolbarIntroduced`. Der Benutzer hat den Stand global benutzt und keinen Bedarf gesehen.
Neubewertung nur bei Rückmeldungen über verwirrende doppelte Buttons.

### 29.9 Tests

- Neu: `whaletoolbarmigrationtest`, 15 Testfunktionen/-zeilen (+ init/cleanup = 17):
  Bestandsnutzer (unverändert/geändert ohne Hamburger/ausgeblendet; bytegleich, ActionProperties),
  neuer Benutzer, ältere Benutzerdatei (Versionshandler), Defaults für Bestandsnutzer, Aussehen,
  View-Menü (auch nach Apply und neuem Fenster), Umschalten für Split und Tabs, neue Container,
  Kontextmenü, zwei Fenster, Persistenz (Datei, Neustart, Wert eines anderen Prozesses beim
  Start), Configure Toolbars bei verborgenen Leisten (2 Varianten).
- Angepasst: `whaletoolbarconfigtest` (neue Main-Defaults; Kontextmenü hat 2 Einträge; statt
  `view_settings` wird `split_view` in der Hauptleiste geprüft; Back steht dort nur noch in
  „Available“), `whalefileviewtoolbartest`. Grund für beide: Der Manager entsteht jetzt im
  Fensterkonstruktor und liest das Layout dort; Tests schreiben ihre Benutzerdatei vorher bzw.
  laden neu. Im echten Start existiert die Datei vorher, kein Verhaltensunterschied.
- Offscreen: alle 37 Whale-Tests grün (inkl. Lints, Dev-MCP-Acceptances), migrationtest 3× stabil,
  `clang-format` sauber.
- Gesamtsuite offscreen: `folderspaneltest` unter `-j4` rot, allein und wiederholt grün (bekannt,
  25.x); `dolphinmainwindowtest` wie bisher (`testThumbnailAfterRename` rot, Watchdog in
  `testViewModeAfterDynamicView`), die Funktionen danach einzeln grün; `testAccessibilityTree`
  diesmal grün.
- **ASan/UBSan** (`../build-whale-asan`, offscreen): `whaletoolbarmigrationtest`,
  `whaletoolbarconfigtest`, `whalefileviewtoolbartest`, `whaleviewservicestest` mit 0 Befunden.
  Leak-Lauf des Migrationstests: keine Leaks aus Whale-Toolbar-Code.
- Test-Gotcha: `GeneralSettings::version() >= 200` ohne gespeicherten Fensterzustand zeigt das
  Terminal-Panel (Upstream); Konsole stürzt offscreen mit zwei Fenstern ab. Der Test setzt die
  Version nur im Bestandsnutzer-Test und räumt Version und `whalestaterc` im `cleanup()` auf.
- Während TB5 kein ydotool, keine sichtbaren Fenster, keine Eingabe auf dem Desktop.

### 29.10 Dolphin-Diff

- `src/dolphinui.rc`: 3 Zeilen entfernt (Main-Defaults), Version unverändert.
- `src/dolphinmainwindow.cpp`: 2 Zeilen (Include, `WhaleToolbarManager::ensure(this)->
  setupActions(this)` vor `setupGUI`).
- Sonst nur Whale-Code (`src/whale/toolbars/`, CMake, Tests).

### 29.11 Abweichungen

- View-Menü-Eintrag per Code statt in `dolphinui.rc` (29.4), um den Versionssprung zu vermeiden.
- Ein zusätzlicher Dolphin-Aufruf vor `setupGUI` (Action muss vor dem Aufbau existieren).
- Manager entsteht früher (Fensterkonstruktor statt erstes Zeigen einer Leiste).
- Sichtbare Nebenwirkung der neuen Defaults (nur neue Benutzer): Dolphins Hamburger-Menü zeigt
  ohne `view_settings` in der Main Toolbar zusätzlich View-Modus, Flat, Hidden, Sortierung,
  Zusatzinfos und Zoom. Upstream-Logik, nicht geändert.
- UX-Diary-Eintrag UX-006.
- Nicht gemacht, wie verlangt: Hinweis (E11), Reset-Eintrag, Aussehen-Optionen, Dev-MCP (TB6),
  Location Bar pro View (TB7), Lokalisierung.

### 29.12 Wayland-Abnahme (ausstehend, nur nach Freigabe)

Offscreen nicht abdeckbar, vorgeschlagen:

1. View-Menü (Menüleiste und Hamburger → More → View): Eintrag sichtbar, Position, Haken.
2. Rechtsklick auf eine File View Toolbar → „Hide File View Toolbars“: echtes Popup, beide Panes
   verschwinden sofort, Layout springt sauber hoch.
3. Wieder einblenden: Split beide und ein zweiter Tab sofort richtig.
4. Neustart der Test-Whale: Zustand verborgen bleibt.
5. Configure Toolbars bei verborgenen Leisten: File View Toolbar ändern, Apply, Einblenden.
6. Optional: neues Profil ohne lokale `dolphinui.rc` → neue Main Toolbar sichtbar.

### 29.13 Bewertung

TB5 ist offscreen abgeschlossen. **TB6-ready: ja**, nach der Wayland-Abnahme (29.12) auf das Go
des Benutzers.
