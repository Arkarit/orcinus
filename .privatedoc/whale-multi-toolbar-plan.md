# Whale Multi-Toolbar – Architektur- und Umbauplan

Stand: 2026-10-08, Branch `feature/multi-toolbar-plan` (abgezweigt von `master` = 5fc824a41).
Nur Analyse und Plan, keine Produktimplementierung.

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
