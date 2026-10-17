# Whale: Location Bar pro View (TB7) – Analyse und Plan

Stand: 2026-10-17. Branch `feature/multi-toolbar` (TB0–TB6 abgeschlossen, bis 32f69bb03
gepusht). **Reine Analyse, kein Code.** Quellen: Dolphin-Code dieses Branches, KXmlGui 6.24.0
und KIO 6.24.0 (`src/filewidgets/kurlnavigator.cpp`, im Scratchpad geklont). Zeilenangaben
beziehen sich auf den aktuellen Branch.

Begriffe: **Container** = `DolphinViewContainer`; **interner Navigator** = sein
`m_urlNavigator`; **sichtbarer Navigator** = ein `DolphinUrlNavigator` im UI;
**Location Bar** = sichtbarer Navigator plus die kontextabhängigen Buttons daneben.

---

## 0. Kurzfassung

- **Die Grundannahme trägt, mit einer Präzisierung.** „Location Bar pro View in der File View
  Toolbar“ ist architektonisch richtig und günstig, weil Dolphin die nötige Trennung schon hat:
  Die History liegt ausschließlich im **internen** Navigator jedes Containers; der sichtbare
  Navigator ist ein austauschbarer UI-Proxy, angebunden über
  `DolphinViewContainer::connectUrlNavigator()`. Diese API reicht für TB7 **ohne Änderung**.
- **Präzisierung:** Die Location Bar muss **immer existieren und verbunden sein**, auch wenn sie
  nicht in der Leiste steht oder die Leiste verborgen ist. Dolphin ruft an sechs Stellen
  `activeViewContainer()->urlNavigator()` ohne Null-Prüfung auf (Ctrl+L, F6, Select All, neuer
  Tab, Up im neuen Tab). „Existiert und verbunden“ (eager, pro Container) und „in der Leiste
  platziert“ (lazy, mit dem Scope) sind deshalb zu trennen.
- **Der globale Doppel-Navigator kann nicht neben per-View-Navigatoren bestehen**:
  `connectUrlNavigator()` erlaubt genau einen verbundenen Navigator pro Container (Assert). Ein
  Legacy-Modus „globaler `url_navigators` weiter erlaubt“ würde einen zweiten, eigenen
  Synchronisationsweg verlangen. Empfehlung: `DolphinNavigatorsWidgetAction` wird in Whale
  **nicht mehr instanziert** (Datei bleibt unverändert), `url_navigators` verschwindet aus der
  Collection; vorhandene Einträge in Benutzer-RCs werden von KXmlGui still ignoriert und
  bleiben in der Datei (Downgrade-sicher).
- **Zielarchitektur (Variante C):** neue Whale-Klasse `WhaleLocationBar` (Widget: ein eigener
  `DolphinUrlNavigator` plus die drei Kontext-Buttons Papierkorb/Netzwerkordner/KDE Connect plus
  die `remote:`-Sonderlogik), genau eine pro Container, erzeugt mit der File View Toolbar und
  sofort per `connectUrlNavigator()` fest gebunden. In der File View Toolbar ist sie das neue
  Layout-Element `whale_location_bar` (höchstens einmal).
- **Dolphin-Diff:** ca. 50–70 Zeilen in `dolphinmainwindow.cpp`, `dolphintabpage.cpp`,
  `dolphintabwidget.cpp` (Null-Guards für den fehlenden globalen Navigator; vier Stellen, die
  heute per `primaryViewActive()` den globalen Navigator wählen, nehmen stattdessen
  `container->urlNavigator()`), dazu 1–2 Zeilen `dolphinui.rc`.
  `dolphinviewcontainer.*`, `dolphinurlnavigator.*`, `dolphinnavigatorswidgetaction.*`:
  **0**.
- **Migration** ist der heikelste Teil: Bestandsnutzer mit eigener `whalefileviewtoolbarui.rc`
  bekommen den neuen Default (mit Location Bar) nicht automatisch, und Nutzer mit verborgenen
  File View Toolbars (TB5) hätten nach TB7 gar keine Location Bar mehr. Beides braucht eine
  Entscheidung (L10, L15).
- **Spike nötig: ja, klein** (LB0): Kosten eines eager Navigators pro Container, Sizing in
  `QToolBar` (Expanding, Spacer, Overflow, Editiermodus), Höhe/Optik. Nicht durchgeführt;
  Fragen in 20.1.

---

## 1. Ist-Zustand

### 1.1 Objekte

| Objekt | Datei | Anzahl |
|---|---|---|
| `DolphinNavigatorsWidgetAction` (`url_navigators`) | `dolphinnavigatorswidgetaction.*` | 1 pro Fenster |
| sichtbare `DolphinUrlNavigator` darin | ebd. (`createNavigatorWidget`) | 1, bei Split 2 (der zweite bleibt nach dem ersten Split bestehen, nur verborgen) |
| interner `DolphinUrlNavigator m_urlNavigator` | `dolphinviewcontainer.h:507` | 1 pro Container |
| `DolphinUrlNavigatorsController` | `dolphinurlnavigatorscontroller.*` | statisch; Liste aller Navigatoren für Einstellungen |

### 1.2 Ablauf heute

- `DolphinMainWindow` erzeugt die Action (`dolphinmainwindow.cpp:182–184`) und übergibt sie
  dem `DolphinTabWidget` (Konstruktor, `dolphintabwidget.cpp:39`).
- **Tab-Wechsel** (`DolphinTabWidget::currentTabChanged`, `:572–589`): alter Tab
  `disconnectNavigators()`, ggf. `createSecondaryUrlNavigator()`, neuer Tab
  `connectNavigators(m_navigatorsWidget)`, `setSecondaryNavigatorVisible(split)`.
- `DolphinTabPage::connectNavigators()` (`dolphintabpage.cpp:223`): `insertNavigatorsWidget()`,
  primärer Navigator → primärer Container, sekundärer → sekundärer, Geometrie folgen.
- `insertNavigatorsWidget()` (`:246`): Steckt die Action in der Hauptleiste, nichts weiter;
  sonst `requestWidget(this)` in Zeile 0 des Grid-Layouts der Tab-Page plus Trennlinie.
- **Split an** (`setSplitViewEnabled`, `:99–125`): sekundären Container erzeugen, ggf.
  sekundären Navigator erzeugen, verbinden, sichtbar, Geometrie.
- **Split aus** (`:126–175`): sekundären Navigator verbergen und trennen; bleibt der rechte
  Container übrig, wird **der primäre sichtbare Navigator an ihn umgehängt** (`swapActiveView`,
  `:132–140`), dann Zeiger getauscht. Der schließende Container wird sofort oder nach der
  Animation (als „Zombie“, Signale blockiert) gelöscht.
- **Session-Restore** (`DolphinTabWidget::restoreState`, `:140–161`): Jeder Tab wird
  nacheinander `setCurrentIndex(i)` gesetzt, also jeweils verbunden und wieder getrennt.
- `DolphinMainWindow::saveNewToolbarConfig()` (`:3246`) ruft nach jedem Toolbar-Apply erneut
  `insertNavigatorsWidget()`, `updateAllowedToolbarAreas()` (`:3004`: oben/unten, solange
  `url_navigators` in der Hauptleiste ist) und `updateNavigatorsBackground()` (`:3017`).

### 1.3 Was `DolphinNavigatorsWidgetAction` zusätzlich tut

Nicht offensichtlich und für TB7 wichtig (`dolphinnavigatorswidgetaction.cpp:196–320`):

- pro Seite drei Buttons neben dem Navigator, nur bei passendem Schema sichtbar:
  **Empty Trash** (`trash:`), **Add Network Folder** (`remote:`, wenn `knetattach` existiert),
  **Open KDE Connect** (`kdeconnect:`);
- bei `remote:` schaltet der Navigator automatisch in den Editiermodus, leert den Text und
  zeigt den Platzhalter „Enter server URL (e.g. smb://[ip address])“ (Bug 414670);
- Hintergrund-Kosmetik für Breeze (`setBackgroundEnabled`), Text „Location Bar(s)“,
  `setHighlightFocusIndicator(true)` nur bei zwei sichtbaren Navigatoren;
- Ausrichtung an den Panes (`ViewGeometriesHelper`, `adjustSpacing`, Timer).

TB7 muss die ersten beiden Punkte pro View **mitnehmen**, die Ausrichtung wird überflüssig.

### 1.4 Wer den globalen Navigator direkt anspricht

| Stelle | Zweck | Auswahl heute |
|---|---|---|
| `dolphinmainwindow.cpp:808` `slotClickViewBackground` | Klick in den Hintergrund beendet den Editiermodus | `primaryViewActive() ? primary : secondary` |
| `:1726–1730` `activeViewChanged` | alte Verbindungen zum Fenster trennen | beide globalen Navigatoren |
| `:2890–2899` `connectViewSignals` | F6-Zustand, `tabRequested` (Mittelklick auf Breadcrumb), `activeTabRequested`, `newWindowRequested` | `primaryViewActive() ? …` |
| `:3004–3020` | erlaubte Toolbar-Bereiche, Hintergrund | die Action |
| `:3254` | nach Apply neu einhängen | die Action |
| `dolphintabwidget.cpp:209–226` `openNewActivatedTab` | Editiermodus in den neuen Tab übernehmen | `primaryViewActive() ? …`, dann `newActive->urlNavigator()` |

Über `container->urlNavigator()` (= verbundener sichtbarer Navigator, **ohne Null-Prüfung**):
`selectAll` (`:1114`), `toggleEditLocation` (`:1241`), `replaceLocation` (`:1247`),
`goUpInNewTab` (`:1339`), `openNewActivatedTab` (`:226`), Dev-MCP `focusRole`
(`whaledevautomation.cpp:827`, mit Null-Prüfung).

Über `urlNavigatorInternalWithHistory()` (History): Back/Forward/Up/Home und ihre Menüs
(`:1056–1100`, `:1298–1334`), `updateHistory` (`:463`), Fenstertitel/`changeUrl` (`:2887`),
Session (`dolphintabpage.cpp:305–365`), Whale `WhaleContainerCommands` (TB2).

`updateHamburgerMenu()` (`:1596`) hängt **nicht** an `url_navigators` (geprüft).

---

## 2. Ownership/Lifetime

| Objekt | Besitzer | Lebensdauer |
|---|---|---|
| `DolphinNavigatorsWidgetAction` | `DolphinMainWindow` (QObject-Parent) und dessen `actionCollection()` | Fenster |
| `QSplitter` der Action (`m_splitter`, `unique_ptr`) | die Action; Qt-Parent wechselt per `createWidget()` zwischen Hauptleiste und Tab-Page; `deleteWidget()` setzt Parent null | Fenster |
| sichtbare Navigatoren | Kinder der Seiten-Widgets im Splitter | Fenster; der sekundäre ab dem ersten Split |
| interner Navigator | Container (`std::unique_ptr`, ohne Qt-Parent) | Container |
| `m_urlNavigatorConnected` | `QPointer` im Container | bis `disconnectUrlNavigator()` |
| `m_urlNavigatorVisualState` | Container, gesetzt beim Trennen, verbraucht beim Verbinden | – |

Folgen für TB7: Ein per-View-Navigator als Kind der File View Toolbar (Kind des Containers)
lebt exakt so lange wie der Container, auch für den Zombie der Split-Animation. Der `QPointer`
im Container macht die Reihenfolge der Zerstörung unkritisch.

---

## 3. Navigator-/History-Modell

- **Kanonische History = interner Navigator.** Jeder `KUrlNavigator` enthält einen
  `KCoreUrlNavigator` mit History (`kurlnavigator.cpp:185, 213`), also technisch auch der
  sichtbare. Dessen History ist aber ein Nebenprodukt: Alle Back/Forward/History-Menüs, Session
  und Whale nutzen den internen; der sichtbare bekommt seine URL nur gespiegelt.
- **Datenfluss** (`connectUrlNavigator`, `dolphinviewcontainer.cpp:296–327`):
  - sichtbar → intern: `urlChanged → m_urlNavigator->setLocationUrl` (Benutzer klickt
    Breadcrumb, tippt, fügt per Mittelklick ein);
  - View → sichtbar: `DolphinView::urlChanged → setLocationUrl` (Back/Forward, Redirect, alles);
  - sichtbar → Container: `activated → activate`, `requestToLoseFocus → view->setFocus`,
    `urlsDropped → view->dropUrls` (eigene View);
  - einmalig: URL, Hidden-Ordner, Sortierung, Visual State, `setActive`, Read-only-Badge.
- **Disconnect** trennt genau diese fünf Verbindungen und merkt sich den Visual State
  (Editiermodus, Text, Cursor, Auswahl, Fokus).
- **Redirect** (`:1017–1047`) setzt den internen Navigator mit blockierten Signalen und trennt
  kurz `activated`, weil `KUrlNavigator::setLocationUrl()` **selbst `requestActivation()` ruft**
  (`kurlnavigator.cpp:1191–1198`) und sonst eine inaktive Pane aktiviert würde.
- **Austauschbarkeit:** Ein sichtbarer Navigator kann zerstört und neu verbunden werden, ohne
  History zu verlieren; nur der Visual State wandert über den Container.
- **Pro View genau ein interner Navigator, bleibt so.** TB7 baut kein zweites History-System;
  die neue Location Bar ist wie heute ein Proxy, nur fest einem Container zugeordnet.
- Upstream-Eigenheit, nicht TB7-Ziel: Die Session speichert `isUrlEditable()` des **internen**
  Navigators (`dolphintabpage.cpp:305`), den der Benutzer nie umschaltet; der Editiermodus des
  sichtbaren wird also nicht wirklich wiederhergestellt.

### 3.1 Bewertung `connectUrlNavigator`

| Frage | Antwort |
|---|---|
| dauerhaft genau einen sichtbaren Navigator? | Ja. Einmal verbinden, nie trennen; nichts in Dolphin trennt von selbst. |
| schon beim Erzeugen verbinden? | Ja. Voraussetzung nur `m_view` (Assert); die File View Toolbar entsteht im Container-Konstruktor nach dem View-Host (`dolphinviewcontainer.cpp:190`). |
| URL-Wechsel, Back/Forward | über `DolphinView::urlChanged` gespiegelt |
| Redirect | sauber, inkl. Aktivierungsschutz |
| aktive Pane | `setActive()` spiegelt auf den verbundenen Navigator (`:241–247`) |
| Lifetime | `QPointer`, keine Annahme über den Besitzer |
| Konflikt | `Q_ASSERT(!m_urlNavigatorConnected)`: Ein zweiter Verbindungsversuch (Dolphins Tab-/Split-Code mit dem globalen Navigator) ist ein Fehler. Deshalb muss Dolphins Code das Verbinden lassen (15). |

**Fazit:** Die API ermöglicht TB7 praktisch schon heute; sie muss nicht erweitert werden.

---

## 4. DolphinNavigatorsWidgetAction

Verantwortungen heute: QSplitter, Primary/Secondary, Geometrie-Folgen, Erzeugen der
Navigatoren, Umschalten Single/Split, Tab-Wechsel (über TabWidget/TabPage), Toolbar-Embedding
(Reparent zwischen Hauptleiste und Tab-Page), Kontext-Buttons, `remote:`-Logik,
Hintergrund/Fokusindikator, Action-Text.

| Variante | Bewertung |
|---|---|
| A komplett obsolet (nicht mehr erzeugen) | **Empfohlen.** Datei bleibt unverändert und kompiliert; kein Merge-Risiko in ihr. Kosten: Null-Guards an den Stellen aus 1.4. |
| B reduzieren (nur noch Kontext-Buttons liefern) | Refaktorierung einer Upstream-Datei; hohes Merge-Risiko bei jeder Upstream-Änderung |
| C nur für Main-Toolbar-Legacy behalten | Kollidiert mit `connectUrlNavigator` (ein Navigator pro Container); bräuchte einen eigenen Spiegel-Mechanismus. Nein. |
| D intern wiederverwenden (z. B. `createNavigatorWidget`) | privat und an Seiten (`Side`) gebunden; wäre B |
| E durch Whale-Widgets pro View ersetzen | **Empfohlen, zusammen mit A.** Die Kontext-Buttons und die `remote:`-Logik (~120 Zeilen) werden in `WhaleLocationBar` nachgebaut. Lizenz: Upstream-Datei ist LGPL-2.1-only/LGPL-3.0-only/KDE-accepted; bei Übernahme von Code die Herkunft im SPDX-Kopf vermerken (REUSE), sonst neu schreiben. |

---

## 5. Zielarchitektur

```text
DolphinMainWindow                (kein url_navigators mehr)
 └─ DolphinTabWidget             (m_navigatorsWidget == nullptr)
     └─ DolphinTabPage           (verbindet keine Navigatoren mehr)
         └─ DolphinViewContainer X
             ├─ m_urlNavigator   (intern, History, unverändert)
             └─ WhaleFileViewToolBar X            (TB2)
                 ├─ WhaleViewCommandScope X       (lazy, TB2)
                 └─ WhaleLocationBar X            (neu, eager)
                     ├─ DolphinUrlNavigator X  ── connectUrlNavigator(X), nie getrennt
                     └─ Empty Trash | Add Network Folder | Open KDE Connect
```

- **Eager:** `WhaleLocationBar` entsteht im Konstruktor von `WhaleFileViewToolBar`, also mit
  dem Container, und wird sofort verbunden. Invariante: **Jeder Container hat ab seiner
  Erzeugung einen verbundenen sichtbaren Navigator.** Damit bleiben alle Dolphin-Stellen mit
  `urlNavigator()` gültig, auch bei verborgener Leiste oder nie gezeigtem Tab.
- **Lazy bleibt die Platzierung:** Erst `rebuild()` (nach dem ersten Zeigen, mit dem Scope)
  setzt das Widget an seine Stelle in der Leiste (`QToolBar::addWidget`, wie der Spacer).
  Ohne Layout-Eintrag ist es ein verborgenes Kind der Leiste.
- Keine Primary/Secondary/Left/Right-Annahme: alles hängt am Container (Lint
  `whale_toolbars_no_pane_sides` gilt weiter, auch für die neue Datei).

---

## 6. Architekturvarianten

| | A: Doppel-Navigator auftrennen | B: `DolphinUrlNavigator` direkt in `WhaleFileViewToolBar` | **C: Wrapper `WhaleLocationBar`** |
|---|---|---|---|
| Dolphin-Diff | groß (Action, TabPage, TabWidget, MainWindow; Umbau einer Upstream-Klasse) | ~50–70 Zeilen Guards | ~50–70 Zeilen Guards (wie B) |
| Whale-Diff | klein | mittel; Buttons/`remote:` direkt in der Toolbar | mittel; gekapselt in einer Klasse |
| Ownership | weiter fensterweit | Container → Toolbar → Navigator | Container → Toolbar → LocationBar → Navigator |
| Konfigurierbarkeit | Main Toolbar | ein Layout-Element | ein Layout-Element |
| Mergeability | schlecht | gut | gut |
| Docks/dritte View | nein (Splitter) | ja | ja |
| Tests | schwer | gut | gut, Klasse einzeln testbar |
| Lifetime-Risiko | Reparent-Logik bleibt | gering | gering |
| Sizing | – | Navigator hat im Editiermodus `QSizePolicy::Minimum` (`kurlnavigator.cpp:715`) → schrumpft in der Leiste | Wrapper mit `Expanding` gibt dem Navigator Platz in beiden Modi |

**Empfehlung: C.** Der Wrapper löst das Sizing (Editiermodus), hält Buttons und `remote:`
zusammen und ist die Stelle für spätere Docks.

---

## 7. Toolbar-Integration

### 7.1 Neues Layout-Element

- ID **`whale_location_bar`** (nicht `url_navigators`: anderes Objekt, andere Semantik; nicht
  `location_bar`: so heißt das Untermenü im View-Menü).
- In `WhaleViewCommandScope::allowedActions()` aufnehmen; in der Scope-Collection eine
  `QWidgetAction`-artige Platzhalter-Action dieses Namens, deren Widget die bestehende
  `WhaleLocationBar` des Containers ist (kein `createWidget` pro Toolbar, das Widget gibt es
  genau einmal). Alternativ: `rebuild()` erkennt die ID und fügt das Widget direkt ein.
  Entscheidung im Spike; beides ohne Dolphin-Diff.
- **Prototyp im Edit-Client (TB4):** Für diese ID gibt es keine Fenster-Action, von der
  `resetPrototypes()` Text/Icon kopieren kann; Sonderfall mit eigenem Text „Location Bar“ und
  Icon (Vorschlag `edit-select-text`, wie das Location-Bar-Untermenü).

### 7.2 Höchstens einmal

- KEditToolBar erzwingt es selbst: „Available“ = Collection **minus** „Current“
  (`kedittoolbar.cpp:1286–1296`); eine Action steht in einer Leiste nur einmal.
- Gegen handbearbeitete Dateien: `rebuild()` platziert nur das erste Vorkommen (ein Widget kann
  ohnehin nur an einer Stelle sitzen); Test.

### 7.3 Change Text / Change Icon

Für Widget-Actions in KEditToolBar möglich, ohne sichtbare Wirkung (das Widget zeigt weder
Text noch Icon). ActionProperties dafür ignorieren; dokumentieren, nicht verhindern.

### 7.4 Sizing (Spike bestätigt)

- `KUrlNavigator`: Breadcrumb-Modus `Expanding/Fixed`, Editiermodus `Minimum/Fixed`
  (`kurlnavigator.cpp:714–729`); `DolphinUrlNavigator::sizeHint()` ist im Editiermodus die
  Hint des Line-Edits (schmal).
- `QToolBar` verteilt Restbreite auf Elemente mit horizontaler `Expanding`-Policy; mehrere
  teilen sie. Deshalb:
  - `WhaleLocationBar` selbst `Expanding/Fixed`, Navigator mit Stretch darin;
  - **neuer Default ohne Spacer** (die Location Bar ist der Spacer);
  - steht in einem (alten) Benutzerlayout zusätzlich ein Spacer, teilt er sich die Breite mit
    der Location Bar. Vorschlag: Spacer neben einer Location Bar in `rebuild()` nicht
    expandieren lassen (L12).
- Schmale Panes: Der Breadcrumb kürzt sich bis auf seinen Mindestwert, erst danach wandern
  Buttons ins Überlaufmenü. Die Location Bar sollte nie ins Überlaufmenü (Mindestbreite
  festlegen, Spike).
- Höhe: Der Navigator (~30 px) ist höher als kleine Icon-Buttons; die Leiste wird etwas höher
  (Optik im Spike/Wayland prüfen). Hintergrund-Kosmetik wie heute `setBackgroundEnabled(true)`
  in einer Toolbar.

### 7.5 Default-Reihenfolge (L4)

| Variante | Bewertung |
|---|---|
| `go_back, go_forward, go_up, view_redisplay, whale_location_bar, view_settings` | **Empfohlen.** Wie Browser und wie Dolphins heutige Hauptleiste (Navigation links, Pfad wachsend, Ansicht rechts); Reload bleibt bei der Navigation; kein Spacer nötig |
| `go_back, go_forward, go_up, whale_location_bar, view_redisplay, view_settings` | Reload rechts vom Pfad (Firefox alt); trennt Navigation; kein Vorteil |
| mit Spacer zwischen Location Bar und View Settings | zwei Expanding-Elemente teilen die Breite; nein |

---

## 8. Action-Semantik

### 8.1 `url_navigators` (L5)

| Option | UX | Bestandskonfig | XMLGUI | Migration | Merge | Code |
|---|---|---|---|---|---|---|
| **A nicht mehr anbieten** | eine Location Bar pro View, keine doppelte | Eintrag wird still ignoriert, bleibt in der Datei | unbekannte `<Action>` werden von der Fabrik übergangen; KEditToolBar zeigt sie nicht in „Current“ (`kedittoolbar.cpp:1265–1281`) und lässt das DOM-Element bei inkrementellen Änderungen stehen | keine | Guards | klein |
| B Legacy konfigurierbar | zwei Location Bars für dieselbe View möglich | unverändert | – | – | groß | groß (zweiter Spiegel, weil ein Container nur einen verbundenen Navigator hat) |
| C Alias/Toggle für per-View-Bars | verwirrend (eine Action steuert N Widgets) | – | – | – | mittel | mittel |
| D Migration auf neue Action | Whale müsste `dolphinui.rc` umschreiben (TB5-Regel verletzt) | – | – | ja | – | – |

**Empfehlung A.** Bestätigen im Spike: Ein Apply in KEditToolBar entfernt den unbekannten
`url_navigators`-Eintrag nicht aus der Datei (Downgrade).

### 8.2 `replace_location` (Ctrl+L, Alt+D) und `editable_location` (F6) (L6)

- **Global bleiben**, wirken auf die aktive View: `replaceLocation()`/`toggleEditLocation()`
  nehmen `m_activeViewContainer->urlNavigator()` – das ist nach TB7 automatisch deren eigene
  Location Bar. Kein Dolphin-Diff.
- **Nicht** in der File View Toolbar anbieten: Der Navigator hat seinen Umschalter selbst
  (Klick rechts neben den Breadcrumb); ein Button „Replace Location“ pro View hätte keinen
  Mehrwert.
- F6-Haken: `connectViewSignals()` verbindet `editableStateChanged` künftig mit
  `container->urlNavigator()` (statt `primaryViewActive() ? …`).
- **Verborgene oder nicht platzierte Location Bar** (L11): Ctrl+L gibt einem verborgenen Widget
  den Fokus, also passiert nichts Sichtbares. Das ist heute schon so, wenn die Hauptleiste mit
  `url_navigators` verborgen ist. Empfehlung: Präzedenz übernehmen; ein temporäres Einblenden
  wäre ein eigenes Feature.

### 8.3 Weitere Signale des sichtbaren Navigators

`tabRequested` (Mittelklick auf Breadcrumb/Places), `activeTabRequested`, `newWindowRequested`
sind heute nur für den Navigator der aktiven View mit dem Fenster verbunden
(`connectViewSignals`), getrennt in `activeViewChanged`. Ein Mittelklick ruft vorher
`requestActivation()` (`kurlnavigator.cpp:1230`), die Pane wird aktiv, das Fenster verbindet
um, das Release öffnet den Tab. Mit `container->urlNavigator()` an beiden Stellen bleibt das
Verhalten gleich.

---

## 9. Focus/Aktivierung

| Ereignis | Aktiviert die Pane? | Weg |
|---|---|---|
| Klick auf Breadcrumb/Leerraum | ja | FocusIn/Mausdruck → `requestActivation` → `activated` → `activate()` |
| Mittelklick (Tab/Paste der Auswahl) | ja | `mousePressEvent` Mittelklick |
| Wechsel in den Editiermodus | ja | `switchView` → `requestActivation` (`:567`) |
| Texteingabe/Enter | ja (Fokus) | FocusIn; `returnPressed` → `slotReturnPressed` (zurück in Breadcrumb, wenn nicht bevorzugt) |
| Escape (Breadcrumb-Modus) | – | `requestToLoseFocus` → **eigene** View fokussiert |
| History-Popup der Breadcrumb-Buttons | ja | Druck auf den Button |
| Drop auf Breadcrumb | nein (wie heute) | `urlsDropped` → `view->dropUrls(Ziel)` der eigenen View |
| programmatischer URL-Wechsel | `setLocationUrl` ruft `requestActivation` | Dolphin schützt Redirects; normale URL-Wechsel einer inaktiven Pane aktivieren sie heute schon (TB0-Befund) |
| Ctrl+L / F6 | wirkt auf die aktive View, aktiviert keine andere | Fenster-Action |

Kein Fokus-Leak: Alle Rückwege (Escape, Return, Klick in den View-Hintergrund) gehen über den
eigenen Container. `slotClickViewBackground` muss dafür den Navigator des **aktiven** Containers
nehmen (heute Seitenwahl).

Die File View Toolbar aktiviert ihre Pane zusätzlich beim Druck auf einen Button (TB2-Filter);
für das Location-Bar-Widget nicht nötig, KUrlNavigator macht es selbst.

Fokusindikator: Dolphin zeigt ihn nur bei zwei sichtbaren Navigatoren. Pro View: einschalten,
wenn das Fenster mehr als eine File View zeigt (Manager zählt die gezeigten Leisten), sonst
aus (L13, ohne Primary/Secondary).

---

## 10. Split/Tabs/Lifecycle

- **Split an:** neuer Container → neue Toolbar → neue Location Bar, verbunden. Dolphin tut
  nichts mehr mit Navigatoren.
- **Split aus:** Der verbleibende Container behält **seine** Location Bar; der Swap
  (`swapActiveView`) hängt keinen Navigator mehr um (Guard). Die schließende Location Bar stirbt
  mit ihrem Container (auch als Zombie der Animation); History bleibt beim überlebenden
  Container korrekt, weil sie ohnehin dort liegt.
- **Tab-Wechsel:** kein Verbinden/Trennen mehr (Guards in `currentTabChanged`); jede View
  behält ihren Visual State (Editiermodus, Text) einfach im eigenen Widget. Heute wird er beim
  Wechsel über den Container hin- und hergetragen.
- **Neuer Tab** (`openNewActivatedTab`): übernimmt den Visual State vom Navigator des alten
  aktiven Containers (`oldActive->urlNavigator()->visualState()` statt Seitenwahl).
- **Session-Restore:** unverändert über interne Navigatoren; die Restore-Schleife
  (`setCurrentIndex(i)`) verbindet nichts mehr um.
- **Lazy (Tabs, verborgen):** Die Location Bar existiert immer (eager), wird aber erst bei der
  ersten Anzeige der Leiste platziert. Kosten pro Container: ein zweiter `DolphinUrlNavigator`
  (heute: einer intern pro Container plus höchstens zwei pro Fenster). Messung im Spike; ist sie
  zu teuer, Ausweichweg: erzeugen beim ersten `setActive(true)` oder ersten Zeigen, je nachdem
  was zuerst kommt (Invariante aus 5 gilt dann nur für aktive Container; reicht, weil Dolphin
  `urlNavigator()` nur für den aktiven Container aufruft).

---

## 11. Settings/KEditToolBar

- Erscheint in „Available“ der File View Toolbar als „Location Bar“, in „Current“ mit
  Icon-Text; entfernbar (L2: ja), verschiebbar zwischen Actions, höchstens einmal (7.2).
- Entfernt: Widget bleibt existent und verbunden (verborgen), Ctrl+L wie 8.2.
- Defaults: setzt beide Toolbars zurück (KDE), die File View Toolbar hat danach wieder die
  Location Bar, die Hauptleiste den TB7-Default.
- Main Toolbar: `url_navigators` taucht in „Available“ nicht mehr auf.

---

## 12. Migration

### 12.1 Neue Defaults

- **Main Toolbar (L8):** `<Spacer/>, split_view, split_stash, toggle_search, hamburger_menu`.
  Der führende Spacer hält die Buttons rechts, wo sie heute neben dem wachsenden Navigator
  sitzen. Ohne Spacer rutschen sie nach links. Kein Versionssprung (gleiche Begründung wie TB5:
  lokale Kopien gleicher Version gewinnen ganz).
- **File View Toolbar:** 7.5.

### 12.2 Bestandsnutzer

| Gruppe | Ergebnis ohne Migration | Vorschlag |
|---|---|---|
| eigene `dolphinui.rc` mit `url_navigators` | Hauptleiste ohne Location Bar; übrige Buttons unverändert; Datei unverändert | nichts tun (TB5-Regel); Buttons rücken ggf. nach links |
| keine eigene `whalefileviewtoolbarui.rc` | bekommt den neuen Default mit Location Bar | – |
| **eigene `whalefileviewtoolbarui.rc` (TB4–TB6)** | **keine Location Bar** (lokale Datei gewinnt; KDEs Versionshandler übernimmt Benutzer-Toolbars auch bei Versionssprung namentlich, der neue Eintrag käme nicht an) | **L10:** einmalige Migration dieser Whale-eigenen Datei: `whale_location_bar` nach dem letzten von `go_back/go_forward/go_up/view_redisplay` einfügen (sonst vorne), Original als `.pre-tb7` sichern, Merker in der Whale-Konfiguration. Alternative: nur interpretieren (bei fehlendem Merker-Attribut implizit einfügen) – scheitert, sobald der Benutzer die Bar bewusst entfernt und KEditToolBar die alte Datei weiterführt |
| **„Show File View Toolbars“ aus (TB5)** | **gar keine Location Bar mehr** | **L15:** einmalig wieder einschalten (Merker), weil die Location Bar in diese Leiste umzieht; oder bewusst so lassen. Empfehlung: einschalten |

### 12.3 Rollback/Downgrade

- **Whale vor TB7 mit TB7-Dateien:**
  - `dolphinui.rc` des Benutzers mit `url_navigators`: unverändert, alte Whale zeigt die
    globalen Navigatoren wieder.
  - `dolphinui.rc` erst unter TB7 entstanden (ohne `url_navigators`): alte Whale hängt den
    Navigator in die Tab-Page (`insertNavigatorsWidget`), also weiter eine Location Bar.
  - `whalefileviewtoolbarui.rc` mit `whale_location_bar`: alte Whale kennt die ID nicht
    (`m_scope->action()` liefert null → übersprungen), KEditToolBar zeigt sie nicht, die Datei
    bleibt erhalten.
- **Zurück auf den alten Zustand unter TB7:** `.pre-tb7`-Sicherung zurückkopieren bzw. lokale
  `whalefileviewtoolbarui.rc` löschen (Defaults); globale Navigatoren gibt es unter TB7 nicht.

---

## 13. Phones

- `dolphinuiforphones.rc`: `mainToolBar` unten, **ohne** `url_navigators`; der Doppel-Navigator
  sitzt heute in der Tab-Page oben. Die File View Toolbar existiert auch dort (TB2: in jedem
  Container), mit derselben `whalefileviewtoolbarui.rc` (keine Telefonvariante).
- Nach TB7: Location Bar in der File View Toolbar oben, Hauptleiste unten unverändert. Der
  Platzbedarf ist auf schmalen Bildschirmen der eigentliche Punkt (Buttons + Navigator in einer
  Zeile).
- **Empfehlung (L9):** dieselbe Lösung, keine Telefon-RC-Änderung; Phones sind kein Whale-Ziel.
  Falls nötig später ein eigenes File-View-Layout für Phones.

---

## 14. Dock-/Workspace-Zukunft

- Die Kette Container → Toolbar → Location Bar entsteht im Container-Konstruktor; ein dritter
  Container außerhalb einer `DolphinTabPage` bekommt sie genauso (TB2 hat das für die Toolbar
  mit einem Dock-Simulationstest gezeigt).
- Breadcrumb, History, Drop, Escape funktionieren für ihn ohne Fenster.
- Grenze: Ctrl+L/F6 und die Fenster-Verbindungen (`tabRequested` …) hängen an Dolphins Begriff
  „aktive View“, den es nur innerhalb der Tab-Page gibt. Für Docks bräuchte Dolphin später
  ohnehin ein allgemeines Aktivierungsmodell (nicht TB7).

---

## 15. Dolphin-Diff-Risiko

| Datei | warum | geschätzt | ohne Änderung? | Merge-Risiko |
|---|---|---|---|---|
| `dolphinmainwindow.cpp` | Action nicht mehr erzeugen (TabWidget bekommt nullptr); `slotClickViewBackground`, `activeViewChanged` (Disconnect), `connectViewSignals` mit `container->urlNavigator()`; Guards in `updateAllowedToolbarAreas`, `updateNavigatorsBackground`, `saveNewToolbarConfig` | ~20–25 | nein (Assert-Konflikt, Nullzeiger) | mittel: belebte Datei, aber kleine Einzelstellen |
| `dolphintabpage.cpp` | Guards in `setSplitViewEnabled` (Verbinden, Swap, Geometrie), `connectNavigators`, `disconnectNavigators`, `insertNavigatorsWidget` | ~15–20 | nein | mittel; Split-Code ändert sich upstream gelegentlich |
| `dolphintabwidget.cpp` | Guards in `currentTabChanged`; `openNewActivatedTab` über `urlNavigator()` | ~10 | nein | gering |
| `dolphinui.rc` | Main-Default | 1–2 | – | gering |
| `dolphinviewcontainer.*` | – | 0 | ja | – |
| `dolphinurlnavigator.*`, `dolphinurlnavigatorscontroller.*` | – | 0 | ja (neue Navigatoren registrieren sich automatisch für Einstellungen und Places-Selector) | – |
| `dolphinnavigatorswidgetaction.*` | – | 0 | ja (nicht mehr instanziert) | – |
| `dolphinuiforphones.rc` | – | 0 | ja | – |

Summe ~50–70 Zeilen, alle „wenn kein globaler Navigator, dann nichts“ oder „Navigator des
Containers statt der Seite“. Die vier Seitenwahl-Stellen sind auch upstream-tauglich
(gleiche Semantik, einfacher); die Guards sind Whale-spezifisch.

Plugin-API: unverändert 9 (reines Host-UI).

---

## 16. Teststrategie

Neuer QTest `whalelocationbartest` (offscreen, Muster `whalefileviewtoolbartest`), Erweiterung
von `whaletoolbarmigrationtest`/`whaletoolbarconfigtest`, Acceptance über Dev-MCP (17).

| Bereich | Fälle |
|---|---|
| Single View | genau eine Location Bar; URL = View; Breadcrumb ↔ Editiermodus; Enter navigiert; History (Back/Forward) spiegelt sich; `remote:`-Platzhalter; Kontext-Buttons (trash/remote) nur passend |
| Split | zwei Location Bars, verschiedene URLs; Klick/Fokus in B aktiviert B, A unverändert; Escape fokussiert die eigene View; Drop auf B's Breadcrumb geht an B |
| Targeting | Ctrl+L/F6 auf die aktive View (beide Richtungen); F6-Haken folgt der aktiven View; Mittelklick auf Breadcrumb öffnet Tab mit dessen URL; Select All im Editor |
| Tabs | jede View eigene Location Bar; Tab-Wechsel behält Editiermodus/Text pro View; neuer Tab übernimmt Editiermodus; nie gezeigter Tab: Location Bar existiert, verbunden, nicht platziert; Restore |
| Lifecycle | Split schließen (aktive/inaktive/rechte nach `CloseSplitViewChoice`, mit/ohne Animation): kein UAF (ASan), überlebende Location Bar unverändert, History korrekt |
| Visibility | Leiste verborgen → Location Bar verborgen, History funktioniert, Ctrl+L ohne Wirkung/Crash |
| Configure Toolbars | hinzufügen/entfernen/verschieben; höchstens einmal (Dialog + handbearbeitete Datei); Apply/Cancel/Defaults; Neustart; Change Text/Icon ohne Wirkung |
| Migration | `url_navigators` in Benutzer-`dolphinui.rc` (bytegleich, ignoriert); neue Benutzer; TB5-Benutzerlayout der File View Toolbar (L10); verborgene Leisten (L15); Defaults; Downgrade-Dateien |
| Plugin Views | Datei-Liste + Treemap: Location Bar gehört zum Container, bleibt beim View-Wechsel dieselbe |
| Future sanity | dritter Container außerhalb der Tab-Page: Location Bar verbunden, Breadcrumb navigiert ihn |
| Lints | `whale_toolbars_no_pane_sides` auch für `WhaleLocationBar`; Grep-Wächter: Whale nutzt `DolphinNavigatorsWidgetAction` nicht |

Bestehende Suites anpassen: Whale-Tests, die `container->urlNavigator()` nutzen
(`whaleviewhosttest`), funktionieren weiter. Dolphins `dolphinmainwindowtest` prüft Ctrl+L und
F6 über `activeViewContainer()->urlNavigator()` (Fokus im Navigator, `:864–878`), die History
nach Restore über den internen Navigator (`:1499`, `:1542`) und den Navigator der linken Pane
(`:2058`); alle passen zur Invariante aus 5, solange die Location Bar sichtbar platziert ist.
Altbild des Tests beachten (TB5 29.9).

---

## 17. Dev-MCP

Minimal, nach TB6-Muster:

- State `panes.N.toolbar.locationBar`: `{present (in der Leiste platziert), visible,
  editable, url, text (Editor), focused, active, widgetKey}`; `toolbars.instances` +
  `locationBar` (existiert/verbunden) für nie gezeigte Tabs.
- Tool `location_bar_input {toolbar, text, submit}`: Text in den echten Editor (über
  Tastaturereignisse, Editiermodus wie per Klick) und Return. Ctrl+L bleibt
  `trigger_action edit_location` (existiert).
- Breadcrumb-Klicks und Drag&Drop nicht im Dev-MCP (QTest).
- `trigger_toolbar_action` meldet für `whale_location_bar` „ein Widget“ (wie
  `url_navigators` heute).

---

## 18. Wayland-Abnahme (später, nur nach Freigabe)

- Breadcrumb-Popups (Dropdown-Pfeile, Places-Selector, Schema-Combo) als echte Popups;
- Editiermodus: Fokus, Cursor, IME, Vervollständigungs-Popup;
- Drag&Drop von Dateien auf Breadcrumb-Buttons beider Panes;
- Geometrie: Höhe der Leiste, Breite in schmalen Panes, Überlauf, Fokusindikator bei Split;
- Mittelklick-Paste der Auswahl (Wayland-Primary-Selection);
- Optik Breeze-Hintergrund.

---

## 19. Offene Entscheidungen

| # | Frage | Optionen | Empfehlung | Begründung |
|---|---|---|---|---|
| L1 | Location Bar Teil der File View Toolbar? | ja / eigene Zeile über der View / im Container ohne Toolbar | **ja** | ein Element der View, konfigurierbar mit den Buttons; keine zusätzliche Zeile |
| L2 | entfernbar? | ja / nein | **ja** | KDE-Konvention (alles in KEditToolBar entfernbar); Ctrl+L-Verhalten wie bei verborgener Hauptleiste heute |
| L3 | höchstens einmal? | ja / mehrfach | **ja** | ein Widget pro View; KEditToolBar erzwingt es, `rebuild()` zusätzlich |
| L4 | Default-Position | 7.5 | **nach `view_redisplay`, ohne Spacer** | Navigation – Pfad – Ansicht |
| L5 | globale `url_navigators` | A–D (8.1) | **A: nicht mehr anbieten** | ein Navigator pro Container; Bestandsdateien bleiben harmlos |
| L6 | `replace_location`/`editable_location` | global / lokal / beides | **global** (aktive View), nicht in der File View Toolbar | Shortcuts wirken auf die aktive View; lokale Buttons ohne Mehrwert |
| L7 | Leiste verborgen → Location Bar verborgen? | A ja / B bleibt / C eigener Toggle | **A** | gehört zur Leiste; Präzedenz Hauptleiste; kein zweiter Schalter |
| L8 | Main-Toolbar-Default | mit/ohne führenden Spacer | **`<Spacer/>, split_view, split_stash, toggle_search, hamburger_menu`** | Buttons bleiben rechts |
| L9 | Phones | gleich / eigene Defaults / ohne Location Bar | **gleich** | kein Whale-Ziel; keine RC-Änderung |
| L10 | Bestandsnutzer mit eigener File-View-RC | Datei einmalig migrieren / implizit interpretieren / nichts | **einmalig migrieren mit Sicherung** | sonst fehlt die Location Bar ganz; Whale besitzt diese Datei |
| L11 | Ctrl+L bei verborgener/entfernter Location Bar | nichts / temporär einblenden | **nichts** (Präzedenz) | eigenes Feature |
| L12 | Spacer neben Location Bar | teilt Breite / Spacer nicht expandieren | **nicht expandieren** | Location Bar soll die Breite bekommen |
| L13 | Fokusindikator | immer / nur bei >1 sichtbarer View | **nur bei >1** | wie Dolphin |
| L14 | wenig gefüllte Hauptleiste (4 Buttons über eigener Leiste pro View) | so lassen / später in Tab-Leiste o. ä. | **so lassen** (Beobachten) | Vertikalplatz wie TB5; Umbau wäre eigenes Thema |
| L15 | Nutzer mit „Show File View Toolbars“ aus | einmalig einschalten / lassen | **einmalig einschalten** | sonst keine Location Bar; die Bar zieht in diese Leiste um |

---

## 20. Milestones

### 20.1 LB0 Spike (Wegwerf-Branch `feature/location-bar-tb7-spike`, nie mergen/pushen)

Nur diese Fragen, die statische Analyse nicht beantwortet:

1. Kosten eines zusätzlichen `DolphinUrlNavigator` pro Container (Zeit, QObjects) bei Restore
   mit z. B. 20 Tabs → eager oder „beim ersten Aktivieren/Zeigen“.
2. `QToolBar`-Sizing des Wrappers: Breadcrumb und Editiermodus, mit/ohne Spacer, schmale
   Pane, Überlauf, Höhe, Breeze-Hintergrund.
3. KEditToolBar: Bleibt ein unbekanntes `url_navigators` in der Benutzer-`dolphinui.rc` nach
   Apply wirklich stehen (Downgrade)?
4. Reihenfolge Mittelklick → Aktivierung → `tabRequested` mit per-View-Navigatoren.
5. Platzhalter-QWidgetAction vs. direktes `addWidget` in `rebuild()`.

Ergebnis: Plan-Abschnitt „LB0-Ergebnisse“. Dolphin-Diff im Spike erlaubt, wird verworfen.

### 20.2 Produkt-Milestones

| MS | Scope | Dolphin-Diff | Tests | Abnahme | Rollback |
|---|---|---|---|---|---|
| **LB1** Runtime | `WhaleLocationBar` (Navigator, Kontext-Buttons, `remote:`), eager pro Container, verbunden; globaler Navigator nicht mehr erzeugt; Guards und Navigatorwahl in Dolphin; Default-RC der File View Toolbar mit Location Bar; Main-Default ohne `url_navigators` | ~55–70 Zeilen (15) + RC | Single/Split/Targeting/Tabs/Lifecycle/Visibility/Plugin/Future sanity, ASan | offscreen | Commit zurücknehmen; Dateien kompatibel (12.3) |
| **LB2** Settings | Prototyp im Edit-Client, höchstens einmal, Spacer-Regel (L12), Change Text/Icon ignorieren, Defaults | 0 | `whaletoolbarconfigtest` erweitert | offscreen | dito |
| **LB3** Migration | L10 (Datei-Migration mit Sicherung), L15 (Sichtbarkeit), Downgrade-Fälle | 0 | `whaletoolbarmigrationtest` erweitert | offscreen | `.pre-tb7`-Sicherung |
| **LB4** Dev-MCP | State `locationBar`, Tool `location_bar_input`, Acceptance-Szenarien in `test_tb_acceptance.py` oder `test_lb_acceptance.py` | 0 | Acceptance mehrfach | offscreen | – |
| **LB5** Wayland | Punkte aus 18 | 0 | – | nach Freigabe | – |

LB1 ist der einzige Milestone mit Dolphin-Diff und der kritische. LB1 und LB2 könnten
zusammenfallen; getrennt bleiben sie besser prüfbar.

---

## 21. Klare Empfehlung

- **Umsetzen**, Variante C, nach einem kleinen Spike (LB0). Die ursprüngliche Annahme ist
  richtig; zu korrigieren sind nur zwei Details: Die Location Bar muss unabhängig von der
  Platzierung **immer existieren und verbunden sein**, und es gibt **keinen** Legacy-Modus für
  den globalen Doppel-Navigator.
- Vor LB1 entscheiden: L5, L7, L10, L15 (Migration und Verhalten für Bestandsnutzer), L8.
- TB7 ist nach LB0 und diesen Entscheidungen implementierungsbereit.
