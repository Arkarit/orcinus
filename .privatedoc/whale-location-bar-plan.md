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
  `QToolBar` (Expanding, Spacer, Overflow, Editiermodus), Höhe/Optik. **Durchgeführt am
  2026-10-17, Ergebnisse in Abschnitt 22**: Zielarchitektur bestätigt, Entscheidungen L5/L7/L8/
  L10/L15 unverändert; Präzisierungen zu 7.1 (Platzhalter-Action) und 7.4 (Sizing).

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

---

## 22. LB0-Ergebnisse

Durchgeführt am 2026-10-17 auf dem lokalen Wegwerf-Branch `feature/location-bar-tb7-spike`
(Commit 4faca7a85, **nie mergen, nie pushen**), eigener Build `../build-whale-spike`, alles
offscreen (kein ydotool, keine sichtbare GUI). Spike-Code wird nicht übernommen.

### 22.0 Festgelegte Entscheidungen (vor dem Spike, 2026-10-17)

| # | Entscheidung |
|---|---|
| L5 | globales `url_navigators` entfällt, kein Legacy-Modus |
| L7 | versteckte File View Toolbar versteckt auch die Location Bar |
| L8 | Main-Default `<Spacer/>, split_view, split_stash, toggle_search, hamburger_menu` |
| L10 | bestehende Whale-File-View-RC einmalig konservativ migrieren, einmalige `.pre-tb7`-Sicherung; Details in LB3 |
| L15 | bei Upgrade einmalig File View Toolbars wieder einschalten; danach gilt die Benutzerentscheidung |

### 22.1 Aufbau des Spikes

- Dolphin-Guards (`dolphinmainwindow.cpp`, `dolphintabpage.cpp`, `dolphintabwidget.cpp`,
  +58/−28 Zeilen) hinter `WHALE_SPIKE_LB=1`: kein `DolphinNavigatorsWidgetAction`, Navigatorwahl
  über `container->urlNavigator()`. Ohne die Variable: unveränderte Baseline im selben Build.
- `WhaleFileViewToolBar` erzeugt im Konstruktor einen Wrapper (`QWidget` + `QHBoxLayout` +
  `DolphinUrlNavigator`, Stretch 1) und verbindet ihn sofort per `connectUrlNavigator()`;
  `rebuild()` platziert die ID `whale_location_bar` (drei Varianten, 22.6).
- QTest `whalelocationbarspiketest` (nur auf dem Spike-Branch), Build mit
  `QT_FORCE_ASSERTS`: Der Assert `!m_urlNavigatorConnected` in `connectUrlNavigator()` war
  also aktiv und hat in keinem Ablauf (Split an/aus, Tabs, Restore, Mittelklick, Fenster
  schließen) ausgelöst.
- Die Kontext-Buttons (Papierkorb usw.) und die `remote:`-Logik waren nicht Teil des Spikes.

### 22.2 Frage 1: Kosten eines zusätzlichen Navigators pro Container

Einzeln (offscreen, warm): ein `DolphinUrlNavigator` = **46 QObjects**, Erzeugen 0,3–0,5 ms,
Löschen 0,1–0,3 ms.

Session-Restore (`DolphinTabWidget::readProperties`, Fenster nicht gezeigt, je Bestwert aus 3,
4 Läufe):

| Tabs | Baseline | Spike (eager) | Differenz | QObjects Baseline → Spike |
|---|---|---|---|---|
| 1 | 12,8–13,5 ms | 12,6–14,5 ms | Rauschen | 456 → 446 |
| 20 | 66,9–69,6 ms | 72,9–78,4 ms | **+6–9 ms (~10 %)** | 4205 → 5010 (+40/Container) |
| 50 | 168–180 ms | 190–203 ms | **+20–25 ms (~13 %)** | 10125 → 12220 |

Lebende `KUrlNavigator` bei 20 Tabs: 21 → 40 (je Container intern + Location Bar; Baseline:
intern + ein globaler).

**Ergebnis:** eager ist vertretbar. Der Ausweichweg aus Abschnitt 10 („erst beim ersten
Aktivieren/Zeigen erzeugen“) wird nicht gebraucht; die Invariante aus 5 (jeder Container hat ab
Erzeugung einen verbundenen sichtbaren Navigator) bleibt ohne Einschränkung.

### 22.3 Frage 2: `QToolBar`-Sizing

Gemessen mit Breeze (Hauptwerte) und Fusion, Layout
`go_back, go_forward, go_up, view_redisplay, whale_location_bar, view_settings`. Screenshots
lagen im Session-Scratchpad (nicht versioniert).

- **Breadcrumb und Editiermodus:** Der Wrapper füllt in beiden Modi dieselbe Restbreite (z. B.
  Pane 450 px: 278/278 px; Split 701 px: 529 px Breadcrumb). Die in 6/7.4 befürchtete Schrumpfung
  im Editiermodus tritt mit dem `QHBoxLayout`-Wrapper **nicht** auf, auch nicht ohne explizite
  `Expanding`-Policy des Wrappers (Variante `no-wrapper-policy` identisch). Die Policy bleibt als
  ausdrückliche Absicht, ist aber nicht tragend. Der Navigator direkt in der Leiste (Variante B)
  wurde nicht gemessen.
- **Spacer:** Ein zusätzlicher `<Spacer/>` teilt die Breite (Split, Fusion: 709 → 460 px
  Breadcrumb, 563 → 295 px Editiermodus). **L12 bestätigt:** Spacer neben einer Location Bar in
  `rebuild()` nicht expandieren lassen.
- **Schmale Panes:**
  - Die Location Bar erhöht die Mindestbreite der Pane nicht (Pane-`minimumSizeHint` 83 px
    Breeze, wie Baseline).
  - Breeze: sichtbar bis zu einer Panebreite von 280 px (Location Bar 108 px, Minimum des
    Navigators 100 px). Bei 220 px gehen **Location Bar und `view_settings` ins
    Überlaufmenü**; die Location Bar verschwindet damit aus der Leiste. Grund: `QToolBar` lagert
    von hinten aus, und die Location Bar steht vor `view_settings`.
  - Baseline zum Vergleich: Der globale Navigator bleibt bei 220 px mit ~110 px sichtbar.
    **Regression nur bei sehr schmalen Panes (< ~250 px).**
  - Eine feste Mindestbreite macht es schlimmer (`min-200`: schon bei 280 px im Überlauf).
    → **Keine** zusätzliche Mindestbreite (korrigiert 7.4 „Mindestbreite festlegen“).
- **Überlauf hängt am sizeHint:** Mit vielen Buttons nach der Location Bar entscheidet ihr
  `sizeHint()` (Pfadlänge: Breadcrumb 309 px, Editiermodus 160 px), wie viele Buttons ins
  Überlaufmenü gehen. Beispiel 700 px: Breadcrumb → 3 Buttons im Überlauf, Editiermodus → 0.
  Buttons springen also beim Navigieren rein und raus. Vorschlag LB1: `WhaleLocationBar::sizeHint()`
  mit konstanter Breite (z. B. Minimum des Navigators), `Expanding` holt sich den Rest. Das ist
  **nicht gemessen**, in LB1 per Test bestätigen.
- **Höhe:**

  | Stil | Navigator | File View Toolbar ohne → mit Location Bar | Hauptleiste mit `url_navigators` → TB7-Default |
  |---|---|---|---|
  | Breeze | 36 px | 40 → 48 px | 48 → 46 px |
  | Fusion | 27 px | 31 → 35 px | 37 → 37 px |

  Netto pro Fenster mit einer View-Zeile: **+6 px** (Breeze), +4 px (Fusion). Passt zu L14.
- **Breeze-Hintergrund:** `setBackgroundEnabled(true)` und `(false)` sehen offscreen gleich
  aus. Breadcrumb und Editor sitzen sauber zwischen den Icon-Buttons. Vorschlag: wie Dolphin in
  der Leiste `true`. Endgültig erst bei der Wayland-Abnahme (LB5).
- Nicht geprüft (LB5, Wayland): Inhalt und Verhalten des Überlaufmenüs mit einer ausgelagerten
  Widget-Action.

### 22.4 Frage 3: unbekanntes `url_navigators` nach KEditToolBar-Apply

Benutzer-`dolphinui.rc` (Version 52 = ausgeliefert) mit `url_navigators` und einem
zusätzlichen `go_home`, Whale ohne `url_navigators` in der Collection:

- KXmlGui übergeht den Eintrag still. Die Hauptleiste zeigt
  `go_home, split_view, split_stash, toggle_search, hamburger_menu`, und „Current“ im Dialog
  listet ihn nicht.
- `go_home` entfernen + Apply, `toggle_search` verschieben + Apply, OK, neues Fenster liest die
  Datei: Nach jedem Schritt steht **`url_navigators` unverändert an seiner Position** in der
  Datei. Die echten Änderungen werden korrekt gespeichert
  (`url_navigators, split_view, toggle_search, split_stash, hamburger_menu`).
- „Defaults“ nicht geprüft: Es ersetzt die Datei bewusst.

**Ergebnis:** 8.1 bestätigt. L5 ist downgrade-sicher: Eine ältere Whale findet den Eintrag
wieder vor.

### 22.5 Frage 4: Mittelklick → Aktivierung → `tabRequested` → neuer Tab

Mittelklick auf den Breadcrumb „sub“ der **inaktiven** rechten Pane. Baseline und Spike sind
**identisch**:

`press: navigator.activated → activeViewChanged(right)` (das Fenster verbindet auf den
Navigator der rechten Pane um) `→ release: navigator.tabRequested(sub) → tabCountChanged(2)`,
wobei die aktive View des Fensters zu diesem Zeitpunkt schon „right“ ist. Der neue Tab hat die
richtige URL und öffnet im Hintergrund (aktueller Tab bleibt 0), wie heute.

**Ergebnis:** Es genügt, in `connectViewSignals()` und `activeViewChanged()`
`container->urlNavigator()` zu verbinden bzw. zu trennen (8.3). Keine Sonderbehandlung nötig.

### 22.6 Frage 5: Platzhalter-`QWidgetAction` versus `addWidget()`

Ablauf je Variante: platzieren, Editiermodus mit getipptem Text, Rebuild, aus dem Layout
entfernen, vorne und doppelt (handbearbeitet) wieder einfügen, Leiste verbergen/zeigen, Split
schließen, Fenster schließen.

| | A `addWidget()` pro Rebuild | B Platzhalter mit `setDefaultWidget()` | **C `QWidgetAction`-Unterklasse: `createWidget()` liefert die bestehende Bar, `deleteWidget()` versteckt nur** |
|---|---|---|---|
| sichtbar nach Platzierung | nur mit Workaround: `setDefaultWidget()` setzt die Action unsichtbar, wenn das Widget explizit versteckt war (`action->setVisible(true)` nötig) | ja (Widget vorher **nicht** explizit verstecken, sonst gleiche Falle) | ja |
| Actions nach Entfernen + doppeltem Einfügen | **3**, alle mit demselben `defaultWidget` | 1 | 1 |
| doppelte ID in der Datei | zweimal in `actions()`, Bar springt an die letzte Stelle | einmal | einmal |
| Parent, solange nicht platziert | **null** (Top-Level) | **null** (Top-Level): `setDefaultWidget()`/`releaseWidget()` hängen um; bei 20 Tabs 20 parentlose Navigatoren in `QApplication::topLevelWidgets()` | **immer die Leiste** |
| Zustand (Editiermodus, Text) über Rebuild/Verbergen | erhalten | erhalten | erhalten |
| Split schließen / Fenster schließen | gelöscht / kein Absturz | gelöscht / kein Absturz | gelöscht / kein Absturz |
| Risiko | Löschen alter Actions löscht die Location Bar → `urlNavigator()` null → Dolphin-Abstürze | Top-Level-Widgets ohne Fenster (Stil/Schrift erst beim Platzieren; Code, der Top-Levels zählt) | keins gefunden |

**Ergebnis:** **Variante C** für LB1 (präzisiert 7.1). Eine Action pro Container, Besitz bleibt
`WhaleFileViewToolBar` → `WhaleLocationBar`. `rebuild()` fügt die Action höchstens einmal ein.
Der Manager baut nur bei echter Layout-Änderung neu (`fileViewLayoutChanged`), ein „Rebuild“ mit
gleichem Layout tut nichts.

Nebenbefund (nicht TB7): `QWidget::insertAction: Attempt to insert null action` erscheint auch
in der Baseline, ist also vorbestehend.

### 22.7 Bewertung: Änderungen an Architektur und Entscheidungen

- **Zielarchitektur (5, Variante C): unverändert.** Eager pro Container ist bezahlbar (22.2).
  Der Dolphin-Diff liegt im erwarteten Rahmen (+58/−28 Zeilen in drei Dateien, 15).
  `dolphinviewcontainer.*`, `dolphinurlnavigator.*` und `dolphinnavigatorswidgetaction.*`
  blieben unberührt.
- **Präzisierungen für LB1:**
  1. 7.1: Platzhalter als `QWidgetAction`-Unterklasse mit `createWidget()`/`deleteWidget()`
     (22.6 C), nicht `addWidget()` und nicht `setDefaultWidget()`.
  2. 7.4: keine zusätzliche Mindestbreite. `sizeHint()` des Wrappers konstant (in LB1
     verifizieren). Spacer-Regel L12 nötig. `Expanding` bleibt, ist aber nicht tragend.
  3. 10: kein Lazy-Ausweichweg.
- **Neuer offener Punkt L16 – sehr schmale Panes:** Unter ~250 px (Breeze) verschwindet die
  Location Bar ins Überlaufmenü, heute bleibt sie sichtbar.
  - Optionen: (a) hinnehmen, (b) Pane-Mindestbreite anheben, (c) beim Überlauf zuerst die Buttons
    hinter der Location Bar ausblenden (eigene Logik in `rebuild()`/`resizeEvent`).
  - Empfehlung: **(a)** für LB1, in LB5 auf Wayland ansehen; (c) nur, wenn es im Alltag stört.
    (b) widerspricht dem heutigen Verhalten schmaler Splits.
- **Entscheidungen L5, L7, L8, L10, L15: keine Änderung nötig.**
  - L5 ist durch 22.4 zusätzlich als downgrade-sicher belegt.
  - L7: Verbergen/Zeigen erhält den Zustand der Bar (22.6).
  - L8: Die Hauptleiste mit dem TB7-Default ist 46 px hoch (Breeze), unkritisch.
  - L10 und L15 werden von den Messungen nicht berührt.

---

## 23. LB1/LB2-Ergebnisse

Umgesetzt am 2026-10-17 auf `feature/multi-toolbar`, LB1 (Runtime) und LB2 (Toolbar-Integration)
zusammen. Code 2b3b74faf, Tests 74b911ce6. Nicht gepusht, nicht gemergt. Aus dem Spike wurde
nichts übernommen (neu geschrieben). Migration (L10/L15) ist bewusst **nicht** enthalten.

### 23.1 Aufbau

- **`WhaleLocationBar`** (`src/whale/toolbars/whalelocationbar.*`, neu): `QWidget` mit
  `QHBoxLayout`. Darin:
  - ein eigener `DolphinUrlNavigator` (Stretch 1, `setBackgroundEnabled(true)`);
  - die Buttons Empty Trash (`trash:`), Add Network Folder (`remote:`, nur mit `knetattach`) und
    Open KDE Connect (`kdeconnect:`, nur mit der App);
  - die `remote:`-Logik: Editiermodus, Text leeren, Platzhalter.

  Verhalten und Texte folgen `DolphinNavigatorsWidgetAction`; die i18n-Strings sind identisch,
  Übersetzungen greifen also. Die Herkunft steht im SPDX-Kopf.
- **Lebenszyklus:** Die Location Bar entsteht im Konstruktor von `WhaleFileViewToolBar`, also mit
  dem Container. Sie verbindet sich im eigenen Konstruktor genau einmal per
  `connectUrlNavigator()` und bleibt ein Kind der Leiste.
  - Kein zweites History-System, kein Umhängen.
  - `horizontal Expanding`, keine eigene Mindestbreite. Das Layout nimmt die 100 px, die
    `KUrlNavigator` selbst setzt.
- **Platzierung (22.6 C):** eine private `QWidgetAction`-Unterklasse.
  - `createWidget(parent)` liefert die bestehende Bar, und zwar nur für ihre eigene Leiste.
  - `deleteWidget()` versteckt sie nur.
  - Weder `addWidget()` noch `setDefaultWidget()`.
  - `rebuild()` fügt die Action höchstens einmal ein. Bei einer doppelten ID in einer
    handbearbeiteten Datei zählt das erste Vorkommen.
- **Fokusindikator (L13):** Bei Show/Hide/Zerstörung einer Bar zählt eine gequeute Funktion die
  sichtbaren Location Bars des Fensters und setzt `setHighlightFocusIndicator(count > 1)`.
  - Es gibt keine Kopplung an Tab-Page oder Split und kein Primary/Secondary.
  - Nicht per Test geprüft, weil `KUrlNavigator` keinen Getter hat. Gehört zur Wayland-Abnahme.

### 23.2 Dolphin-Diff (4 Dateien, +42/−40)

| Datei | Änderung |
|---|---|
| `dolphinmainwindow.cpp` | keine `DolphinNavigatorsWidgetAction` mehr (TabWidget bekommt `nullptr`); `slotClickViewBackground`, `activeViewChanged` (Disconnect) und `connectViewSignals` nehmen `container->urlNavigator()`; Guards in `updateNavigatorsBackground` und `saveNewToolbarConfig` (`updateAllowedToolbarAreas` braucht keinen, `contains(nullptr)` ist false) |
| `dolphintabwidget.cpp` | `openNewActivatedTab` übernimmt den Visual State von `activeViewContainer()->urlNavigator()`; Guards in `currentTabChanged` |
| `dolphintabpage.cpp` | Guards in `setSplitViewEnabled` (Verbinden, Swap, Geometrie) |
| `dolphinui.rc` | Main-Default `<Spacer />` statt `url_navigators` (L8), Version bleibt 52 |

`dolphinviewcontainer.*`, `dolphinurlnavigator.*`, `dolphinnavigatorswidgetaction.*` und
`dolphinuiforphones.rc` sind unverändert. Die Plugin-API bleibt 9.

### 23.3 Toolbar und Einstellungen

- File-View-Default (`whalefileviewtoolbarui.rc`, Version bleibt 1):
  `go_back, go_forward, go_up, view_redisplay, whale_location_bar, view_settings`, kein Spacer.
- **Spacer-Regel (L12):** Enthält das Layout `whale_location_bar`, bekommt jeder Spacer in
  `rebuild()` die Policy `Minimum` statt `MinimumExpanding`. Ohne Location Bar expandiert er wie
  bisher.
- **KEditToolBar:**
  - eigener Prototyp im Edit-Client („Location Bar“, Icon `edit-select-text`, keine Shortcuts),
    nur für die File View Toolbar;
  - das Fenster hat keine Action dieses Namens, die Hauptleiste bietet sie also nicht an;
  - Duplikate verhindert der Dialog selbst;
  - Change Text/Icon wird gespeichert, ohne sichtbare Wirkung (`applyActionProperties` findet
    keine Scope-Action).
- `WhaleViewCommandScope::allowedActions()` ist unverändert (35 Button-Actions). Der Deskriptor
  der File View Toolbar listet zusätzlich `whale_location_bar`.
- Bestehende lokale File-View-RCs ohne `whale_location_bar` bleiben ohne Location Bar (keine
  stille Ergänzung, Migration folgt in LB3).
- Ctrl+L/F6 bleiben globale Fenster-Actions und wirken über `activeViewContainer()->urlNavigator()`
  auf die Bar der aktiven View. Der F6-Haken folgt der aktiven View. Sie sind nicht in der File
  View Toolbar.

### 23.4 Sizing

- LB0 vermutete Breitensprünge, wenn sich Pfad oder Modus ändern, und wollte sie mit einem
  konstanten `sizeHint()` verhindern.
- Gegenprobe mit dem neuen Test `testStableWidth`:
  - Fenster von 640 bis 360 px in 10-px-Schritten, 13 Einträge, Breeze und Fusion;
  - geprüft: kurzer und langer Pfad, Breadcrumb und Editiermodus;
  - Ergebnis: Breite und Überlaufmenge sind auch **ohne** Override identisch.
- Der LB0-Befund war ein Artefakt der instabilen Splitter-Geometrie im Spike. Der Override ist
  deshalb nicht im Code (nicht prophylaktisch), der Test bleibt als Regressionsschutz.
- L16 (sehr schmale Panes → Location Bar im Überlaufmenü) ist wie entschieden hingenommen.

### 23.5 Tests

- Neu: **`whalelocationbartest`** (19 Fälle, offscreen):
  - Single View: genau eine Bar, URL folgt, Breadcrumb- und Eingabe-Navigation, Editiermodus.
  - Split: zwei Bars; ein Klick in B aktiviert B, A bleibt unabhängig; Editieren aktiviert die
    eigene View.
  - History: Back/Forward über den internen Navigator; Entfernen, Wiedereinfügen und Verbergen
    verlieren nichts.
  - Tabs: eigene Bars; kein Umhängen beim Wechsel; Zustand pro View; ein neuer Tab übernimmt den
    Editiermodus.
  - Split schließen: 5 Varianten (`CloseSplitViewChoice` × aktive Seite). Die überlebende Bar
    bleibt dieselbe, die History stimmt, ein neuer Split bekommt eine neue Bar.
  - Ctrl+L/F6.
  - Verborgene Leiste (L7), auch ein nie gezeigter Tab.
  - Layout: doppelte ID, entfernen/wieder einfügen mit erhaltenem Zustand, unbekanntes
    `url_navigators`.
  - Spacer-Regel, `testStableWidth`.
  - Kontext-Buttons: `trash:` und `remote:` mit Platzhalter. Netzwerkordner und KDE Connect hängen
    an installierten Diensten und sind nicht getestet.
  - Plugin View: Treemap ↔ Dateiliste, dieselbe Instanz.
  - Dritter Container außerhalb der Tab-Page.
- `whaletoolbarconfigtest::testLocationBar`: entfernen/Apply, wieder einfügen (einmal),
  Change Text, Cancel, Defaults, nicht in der Hauptleiste.
- Angepasst an die neuen Defaults: `whalefileviewtoolbartest`, `whaletoolbarconfigtest`,
  `whaletoolbarmigrationtest` (historische Dateien mit `url_navigators` bleiben bytegleich;
  angezeigt wird es nicht mehr), `whaledevautomationtest`, `test_tb_acceptance.py` (nur
  Konstanten, nicht ausgeführt).
- Gesamtsuite offscreen: 70/71 grün.
  - `dolphinmainwindowtest` hängt im Gesamtlauf wie seit TB3 bekannt.
  - Einzeln laufen alle Funktionen bis auf `testInlineRename`. Der scheitert im Ausgangsstand
    bb394d924 identisch (per Worktree-Build geprüft), ist also vorbestehend.
- **ASan/UBSan** (`../build-whale-asan`): `whalelocationbartest`, `whalefileviewtoolbartest`,
  `whaletoolbarconfigtest` ohne Befund.

### 23.6 Abweichungen vom Plan

1. Kein stabilisierender `sizeHint()` (23.4).
2. Dev-MCP: eine Zeile in `triggerToolbarAction`. `whale_location_bar` gilt in der File View
   Toolbar als bekannt und wird als „Widget, kein Befehl“ beantwortet, wie früher
   `url_navigators`. Keine neue Funktion; State `locationBar` und `location_bar_input` folgen in
   LB4.
3. Der Deskriptor der File View Toolbar enthält `whale_location_bar`; `allowedActions()` des
   Scopes bleibt bei den Buttons.

### 23.7 Stand

- Bereit für LB3 (Migration L10/L15): ja.
- Offen für die Wayland-Abnahme (LB5):
  - schmale Panes und Inhalt des Überlaufmenüs (L16);
  - Fokusindikator bei Split;
  - Breeze-Hintergrund und Höhe (+6 px);
  - Breadcrumb- und Places-Popups sowie Vervollständigung in der Leiste;
  - IME;
  - Drag & Drop auf Breadcrumbs beider Panes;
  - Mittelklick-Paste;
  - Ctrl+L bei verborgener Leiste (ohne sichtbare Wirkung, L11).

---

## 24. LB3-Ergebnisse (Migration)

Umgesetzt am 2026-10-17 auf `feature/multi-toolbar`: Code 34256d7e1, Tests f34ea0332. Nicht
gepusht, nicht gemergt. Dolphin-Diff 0. Location-Bar-Runtime, Sizing, Dev-MCP und Plugin-API
(9) sind unberührt.

### 24.1 Aufbau

- **`WhaleToolbarMigration`** (`src/whale/toolbars/whaletoolbarmigration.*`, Namespace).
  `run()` läuft im Konstruktor von `WhaleToolbarManager`, also bei jedem neuen Fenster, bevor
  es das File-View-Layout liest und die Action „Show File View Toolbars“ anlegt. Ist alles
  erledigt, ist der Aufruf ein No-op (zwei Config-Lesezugriffe, kein Dateizugriff).
- **Marker:** zwei getrennte Einträge in `[WhaleToolbars]` von `whalerc`, der Gruppe von
  `WhaleToolbarSettings`. `GeneralSettings` ist nicht erweitert.
  - `MigratedFileViewLayoutTB7`: Fall 1, Layout-Datei.
  - `MigratedFileViewVisibilityTB7`: Fall 2, Sichtbarkeit.

  Getrennt, damit ein fehlgeschlagener Fall 1 beim nächsten Start nicht Fall 2 wiederholt; der
  würde sonst ein inzwischen erneutes Ausblenden des Benutzers überschreiben. Ein Marker wird
  erst gesetzt, wenn sein Teil gelungen ist oder nichts zu tun war. Danach `sync()`; ein
  Fehler dabei wird protokolliert.
- **Neue Benutzer** bekommen beide Marker sofort, ohne Dateiänderung. Eine später mit TB7
  gebaute Datei ohne Location Bar ist damit Benutzerwille und wird nie migriert.
- `WhaleToolbarSettings::config()` ist neu (Zugriff auf dieselbe Konfiguration).

### 24.2 Fall 1: lokale `whalefileviewtoolbarui.rc` (L10)

- **Einfüge-Regel**, in der ersten `<ToolBar name="whaleFileViewToolBar">` der Datei:
  1. direkt nach dem **letzten** (in Dokumentreihenfolge) direkten Kind-`<Action>` mit
     `go_back`, `go_forward`, `go_up` oder `view_redisplay`;
  2. sonst vor dem ersten Eintrag, der nicht `<text>` ist (Plan 12.2: „vorne“);
  3. sonst nach `<text>`;
  4. sonst vor `</ToolBar>`.

  Keine Änderung, wenn:
  - die Leiste `whale_location_bar` schon enthält (nie duplizieren);
  - die Datei keine File View Toolbar hat;
  - sie als leeres `<ToolBar/>` vorliegt (bewusst leer).
- **Bytegleich:** Die Datei wird als Text bearbeitet. `QXmlStreamReader` liefert nur die
  Positionen; da er bei Textknoten ein Zeichen vorausliest, werden Tag-Anfang und -Ende aus dem
  Text bestimmt. Eingefügt wird `<Action name="whale_location_bar"/>`: steht das
  Bezugselement allein in seiner Zeile, auf eigener Zeile mit dessen Einrückung, sonst direkt
  daneben. Reihenfolge, Spacer, Separatoren, Kommentare, Entities, ActionProperties und
  Nicht-ASCII-Zeichen bleiben unverändert. Ist die Datei nicht verlustfrei als UTF-8 lesbar,
  scheitert die Migration.
- **Ablauf:**
  1. lesen und Ergebnis berechnen (unverändert → Marker, fertig);
  2. Sicherung `<datei>.pre-tb7` per `QFile::copy`, nur wenn noch keine existiert (nie
     überschrieben), danach Inhalt verifizieren;
  3. Schreiben atomar per `QSaveFile`;
  4. erst dann Marker setzen.
- **Fehler** (Datei unlesbar, kaputtes XML, Sicherungspfad belegt oder kein Dateiname,
  Sicherung abweichend, Schreiben/Commit gescheitert):
  - Das Original bleibt unverändert (`QSaveFile`), kein Marker.
  - `qCWarning(WhaleViewDebug)` mit Ursache.
  - Der nächste Fensterstart versucht es erneut. Eine Sicherung aus einem früheren Versuch
    bleibt die maßgebliche.

### 24.3 Fall 2: verborgene File View Toolbars (L15)

- Ohne Marker und mit `ShowFileViewToolBars=false`: einmal `setFileViewToolBarsVisible(true)`
  (mit Signal, Hinweis im Log), dann Marker.
- Blendet der Benutzer sie danach wieder aus, bleibt das so.

### 24.4 Nicht migriert

- `dolphinui.rc` des Benutzers (L5): unverändert, auch mit `url_navigators`; getestet bytegleich
  nach Start und Schließen.
- Keine weitere UI-Bereinigung.

### 24.5 Rollback/Downgrade

- Eine ältere Whale mit migrierter File-View-RC: XML gültig, dieselbe Leiste, ein Eintrag mehr
  (`whale_location_bar`, von TB6 übergangen), dieselben ActionProperties (Test vergleicht die
  Layouts mit und ohne den Eintrag).
- Zurück zum alten Layout: `.pre-tb7` über die Datei kopieren. TB7 migriert sie nicht erneut
  (Marker) und zeigt die Leiste ohne Location Bar.
- `dolphinui.rc` mit `url_navigators`: unverändert erhalten, eine ältere Whale zeigt die globalen
  Navigatoren wieder (12.3, LB0 22.4).
- Nicht getestet: der tatsächliche Start einer TB6-Whale (kein zweiter Build).

### 24.6 Tests (`whaletoolbarmigrationtest`, jetzt 33 Fälle)

Neu, offscreen:

| Test | Prüft |
|---|---|
| `testInsertLocationBar` (8 Zeilen) | KDE-Formatierung, handbearbeitete Reihenfolge auf einer Zeile, ohne Navigation, nur `<text>`, Kommentare/Umlaute/Entities, schon vorhanden, andere Leiste, leeres `<ToolBar/>`; jeweils bytegenau, Idempotenz, nie doppelt |
| `testLayoutMigration` | TB5/TB6-Datei mit Spacer und ActionProperties: Ergebnis bytegenau, Sicherung = Original, Marker, Leiste zeigt die Location Bar nach Reload, Benutzertext für Up wirkt, alter Spacer nimmt keine Breite (L12) |
| `testLayoutMigrationOnce` | Benutzer entfernt die Bar danach → bleibt entfernt; ohne Marker, aber mit Bar → keine Änderung, Sicherung unverändert (Inhalt und mtime); bestehende Sicherung wird nie überschrieben |
| `testLayoutMigrationFailure` | Sicherungspfad ist ein Ordner → Original unverändert, kein Layout-Marker, Sichtbarkeits-Marker trotzdem; Ordner schreibgeschützt → Original unverändert, kein Marker; danach erneuter Versuch erfolgreich |
| `testUnreadableFile` | kaputtes XML → unverändert, keine Sicherung, kein Marker |
| `testVisibilityMigration` | L15 einmal an; danach vom Benutzer aus → bleibt aus |
| `testNewUserTB7` | keine Dateien → keine Datei, keine Sicherung, Marker, TB7-Defaults; spätere TB7-Datei ohne Bar bleibt |
| `testMainFileNotMigrated` | `dolphinui.rc` mit `url_navigators` bytegleich, keine Sicherung davon |
| `testDowngradeFiles` | migrierte Datei für ältere Whale lesbar; Rückkehr per `.pre-tb7` |

- Die bestehenden TB5-Tests setzen in `init()` beide Marker; sie hängen nicht mehr von der
  Testreihenfolge ab.
- Gesamtsuite offscreen ohne `dolphinmainwindowtest` (vorbestehend, 23.5) grün. Ausnahme ist
  `whaletreecachetest::testChangeRightAfterScan`: ein Timing-Flake im Tree-Scanner (5050 statt
  5000 ms, 1 von 5 Läufen), ohne Bezug zu den Toolbars.
- ASan/UBSan: `whaletoolbarmigrationtest`, `whalelocationbartest` ohne Befund.

### 24.7 Abweichungen

- Ein leeres `<ToolBar name="whaleFileViewToolBar"/>` gilt als Benutzerwille und wird nicht
  ergänzt. Der Auftrag nannte den Fall nicht; so gibt es auch keine Endlos-Wiederholung.
- Die Migration läuft pro neuem Fenster, bis die Marker gesetzt sind, nicht nur einmal pro
  Prozess. Das ist nötig, damit „nächster Start erneut versuchen“ auch für weitere Fenster eines
  laufenden Prozesses gilt, und billig.

### 24.8 Stand

Bereit für LB4 (Dev-MCP): ja.

---

## 25. LB4-Ergebnisse (Dev-MCP und Acceptance)

Umgesetzt am 2026-10-17 auf `feature/multi-toolbar`: Code 30a05a602, Tests 144ab3384. Nicht
gepusht, nicht gemergt. Nur `src/whale/devmcp` geändert. Kein Dolphin-Diff, keine
Produktänderung, Plugin-API 9.

### 25.1 State

- **`panes.N.toolbar.locationBar`:**
  - `exists`
  - `connected`: es ist der sichtbare Navigator der View.
  - `present`: die Action ist in der Leiste platziert.
  - `visible`, `editable`, `url`
  - `text`: Editortext, nur im Editiermodus.
  - `placeholder`: z. B. `remote:`.
  - `focused`: das Fokus-Widget des Fensters liegt im Navigator; dieselbe Grundlage wie
    `focus.role`, damit es auch offscreen nach Dialogen stimmt.
  - `active`: Container ist aktiv.
  - `widgetKey`: `location-bar-N`, Property am Widget, konstant für dessen Lebensdauer.
- **`toolbars.instances[*].locationBar`:** `exists`, `connected`, `present`, `widgetKey`, auch
  für nie gezeigte Tabs.
- Der State liest nur: `toolBar->actions()` und die Widgets der eager erzeugten Location Bar.
  Kein Scope wird erzeugt (geprüft: Tab bei verborgenen Leisten bleibt `initialized=false`).

### 25.2 `location_bar_input {toolbar, text, submit}`

- `toolbar`: Pane-Index, `active` oder instanceId; die Auflösung teilt sich der Code mit
  `trigger_toolbar_action` über einen neuen privaten Helfer `fileView()`.
- Ablauf wie beim Benutzer:
  1. im Breadcrumb-Modus Mausklick (über das Fenster, `qt_handleMouseEvent`) auf den echten
     `KUrlNavigatorToggleButton`;
  2. ein inaktives Fenster wird vorher aktiviert, wie es ein Fenstermanager beim Klick täte
     (Muster der Treemap-Eingabe);
  3. Ctrl+A, dann der Text Taste für Taste (die KDE-Vervollständigung reagiert wie beim
     Benutzer);
  4. optional Return.
- Ergebnis: `{ok, text}` mit dem tatsächlichen Editortext. Die Vervollständigung hängt bei
  Ordnern z. B. `/` an.
- Fehler:
  - unbekannte Pane, alte instanceId, `global` (die Hauptleiste hat keine Location Bar);
  - nicht verbunden, nicht platziert, nicht sichtbar (verborgen, anderer Tab, Überlaufmenü),
    deaktiviert;
  - kein Umschalt-Button, Editiermodus nicht erreicht, Editortext beginnt nicht mit dem Text.
- `trigger_toolbar_action whale_location_bar` meldet weiter „widget, not a button“, wie früher
  `url_navigators` (seit LB1). Ctrl+L bleibt `trigger_action edit_location`.

### 25.3 Acceptance `test_lb_acceptance.py` (CTest `whale_dev_mcp_lb_e2e`, offscreen)

Verwendet die Session- und Prüf-Helfer aus `test_tb_acceptance.py`, kein neues Framework.
42 Schritte, 3 Whale-Starts, 6 Läufe in Folge grün.

| Teil | Abdeckung |
|---|---|
| M Migration | TB6-Profil (eigene File-View-RC mit Spacer und ActionProperties, `ShowFileViewToolBars=false`): erster Start → Leisten sichtbar, Bar nach Reload, Datei bytegenau nur um eine Zeile ergänzt, `.pre-tb7` = Original, beide Marker; wieder ausgeblendet → nach Neustart weiter aus, keine zweite Migration; Defaults lassen die Sicherung |
| A Single View | exists/connected/present/visible, URL = View, widgetKey; Ctrl+L → editable+focused; Eingabe+Submit navigiert, danach Breadcrumb; Eingabe ohne Submit → editable, Text, View bleibt; `whale_location_bar` per Toolbar-Tool → „widget“ |
| B Split | zwei widgetKeys, verschiedene URLs; Eingabe in B (Index) ändert nur B, B wird aktiv; Eingabe in A (instanceId) ändert nur A, A wird aktiv; `active` folgt |
| C History | Back/Forward des Fensters und der File View Toolbar; `locationBar.url` folgt; B's History getrennt; keine Annahme über die History des sichtbaren Navigators |
| D Plugin View | Dateiliste → Treemap → Dateiliste: derselbe widgetKey; Eingabe in der Treemap-View navigiert deren Container |
| E Configure Toolbars | einmal in „current“, nicht in „available“; nicht in der Hauptleiste, kein `url_navigators`; entfernen → `present=false`, existiert/verbunden/gleicher Key, Eingabe abgelehnt; wieder einfügen → gleiche Instanzen, nicht erneut einfügbar; entfernen + Cancel; Defaults |
| F Visibility | verborgen → `visible=false`, `present=true`, verbunden; Eingabe abgelehnt; Ctrl+L ohne Absturz; wieder sichtbar |
| G Tabs | drei Views, drei Keys; Eingabe im neuen Tab; Tab-Wechsel ändert keine Zuordnung; Bar im anderen Tab „not shown“; nie gezeigter Tab: exists+connected, nicht platziert, Toolbar nicht initialisiert; geschlossener Tab: instanceId ungültig |
| H Lifecycle | Split schließen (aktive B schließt): A und ihr Key bleiben, B's instanceId → Fehler, Pane 1 → Fehler, `global` → Fehler; verbleibende Bar bedienbar |
| I Restart | Layout mit Home, Sichtbarkeit, Marker, Sicherung unverändert, keine zweite Migration, Bar bedienbar |

Zusätzlich `whaledevautomationtest` (+3 Fälle):
- State inklusive nie gezeigtem Tab und Lazy-Garantie;
- Eingabe mit und ohne Submit, über den Umschalt-Button;
- alle Fehlerpfade.

`test_m3_acceptance.py` kennt das neue Tool in der erwarteten Tool-Liste.

### 25.4 Befund: Navigation ohne Return (Qt/KIO, nicht TB7)

- Offscreen beobachtet: Tippt man in den Editor einen Text, der **exakt** einem Eintrag der
  Verlaufsliste der `KUrlComboBox` entspricht (z. B. ein schon besuchter Ordner mit `/`), öffnet
  die Vervollständigung ein Popup.
- Der Wechsel des aktiven Fensters löst `QLineEdit::focusOutEvent` aus. `QComboBox` wertet das
  Bearbeitungsende mit passendem Text als Auswahl (`activated`), und `KUrlNavigator` navigiert
  über `urlActivated` dorthin. Backtrace: `QApplicationPrivate::setActiveWindow` →
  `setFocusWidget` → `QLineEdit::focusOutEvent` → `QComboBox::activated`.
- Das galt ebenso für Dolphins globalen Navigator; es ist kein TB7-Verhalten.
- Ob es unter Wayland auftritt (Popup ohne Aktivierungswechsel), ist ein Punkt für LB5.
- Die Tests tippen für „nicht abschicken“ deshalb einen Text, der kein Verlaufseintrag ist.

### 25.5 Testläufe

- `whaledevautomationtest` 23/23 (3× in Folge); LB-Acceptance 6× grün, TB-Acceptance 2× grün.
- Gesamtsuite offscreen ohne `dolphinmainwindowtest` (vorbestehend, 23.5): 71/71.
  `whaletoolbarconfigtest`, `whaletoolbarmigrationtest`, `whalelocationbartest`,
  `whalefileviewtoolbartest`, die Treemap-Tests und alle Dev-MCP-E2E-Tests sind darin.
- **ASan/UBSan:** `whaledevautomationtest` im ASan-Build. Dafür ist dort jetzt
  `WHALE_ENABLE_DEV_MCP=ON` gesetzt, es ist der Whale-eigene Build `../build-whale-asan`.
  Ergebnis 23/23, kein ASan-Befund. Die UBSan-Meldungen kommen aus dem Test-Plugin
  `whaletestlistplugin.cpp:436/439` (Downcast während der Zerstörung); sie standen schon im
  alten Testbinary und betreffen keinen neuen Code.

### 25.6 Abweichungen

1. Eigene Datei `test_lb_acceptance.py` statt Erweiterung von `test_tb_acceptance.py`; sie
   importiert dessen Helfer. Die TB-Acceptance bleibt unverändert lauffähig und getrennt
   ausführbar.
2. `focused` nutzt das Fokus-Widget des Fensters, nicht `QApplication::focusWidget()`
   (offscreen-tauglich, wie `focus.role`).
3. Zusätzliches State-Feld `placeholder`.

### 25.7 Stand

- Bereit für LB5: ja.
- Für die echte Wayland-Abnahme bleiben:
  - schmale Panes und Überlaufmenü (L16);
  - Fokusindikator bei Split;
  - Breeze-Hintergrund und Höhe;
  - Breadcrumb- und Places-Popups;
  - Vervollständigungs-Popup und der Befund aus 25.4;
  - IME;
  - Drag & Drop auf Breadcrumbs;
  - Mittelklick-Paste;
  - echte Tastatur-/Mauseingabe statt synthetischer Ereignisse;
  - Fokus nach Dialogen mit Fenstermanager.

---

## 26. LB5 Wayland-Abnahme (Zwischenstand 2026-10-17, unterbrochen)

Noch nicht abgeschlossen. Code ist bisher **nicht** geändert.

### 26.1 Aufbau

- Echtes Wayland (KDE Plasma, Breeze, Skalierung 1, Ausgabe DP-4).
- Build `../build-whale`; Start mit `whale-dev-session.sh --profile <scratchpad>/lb5/profile --file-operations`.
- Testordner `<scratchpad>/lb5/files/{alpha/sub1/deep, alpha/sub2, beta/target, "gamma ünï/ordner"}`.
- Fenster 1400×800, Single View und Split.
- Gesteuert über den Dev-MCP (Shell-Adapter) und Screenshots.
- Echte Klicks, Drag & Drop und Texteingabe hat der Benutzer selbst gemacht; kein ydotool.
- Achtung für spätere Läufe: `trash:/` zeigt im Testprofil den **echten** Papierkorb des
  Benutzers, weil der KIO-Worker außerhalb des Profils läuft. „Empty Trash“ nie anklicken.

### 26.2 Selbst geprüft (Dev-MCP + Screenshots)

| Punkt | Ergebnis |
|---|---|
| Single View | Location Bar füllt die File View Toolbar; lange Pfade werden links gekürzt; Hauptleiste ist eine fast leere Zeile mit Split/Suche/Menü (L14, so entschieden) |
| Split, verschiedene Pfade | zwei Bars, Umlaut-Pfad korrekt; inaktive Pane wie in Dolphin getönt, ihr Pfad grau |
| Fokusindikator | aktive Pane: kräftiger blauer Balken über der Location Bar, inaktive: blasser Balken; folgt Aktivierung, Ctrl+L und Eingabe in der anderen Pane. Wird eine Pane aktiviert, deren Bar im Editiermodus ist, bekommt der Editor den Fokus (Dolphin-Verhalten) |
| Höhe/Breeze | Hauptleiste 45 px + 1 px Linie, File View Toolbar 48 px, beide Panes gleich (Pixelmessung); Leiste in Fensterfarbe, darunter die Dateiliste: saubere Kante, kein Rahmenbruch |
| L16 schmale Panes | Fenster 1400, Places sichtbar. Ab Anteil ~0.21 geht zuerst `view_settings` ins Überlaufmenü, ab ~0.20 (Pane ~250 px) auch die Location Bar. Bei 0.20–0.18 werden die vier übrigen Buttons über die Breite **auseinandergezogen**, darunter wieder kompakt. An der Grenze springt beim Navigieren (kurzer/langer Pfad, Editiermodus) nichts; Höhe stabil |
| Completion-Befund aus LB4 (25.4) | Auf Wayland **kein** Popup und **keine** Navigation ohne Return beim Tippen eines besuchten Pfads. Der LB4-Befund ist offscreen-spezifisch (Wechsel des aktiven Fensters) |
| Kontext-Buttons | `trash:` → „Empty Trash“ in der eigenen Pane; `remote:` → Editiermodus, Platzhalter, „Add Network Folder“ (knetattach und KDE Connect installiert); Geometrie passt |
| L11 | Leisten aus + Ctrl+L: kein Absturz, nichts sichtbar. Der Dev-MCP meldet die Tastatur im **unsichtbaren** Editor (`KUrlComboBox`), wie früher bei verborgener Hauptleiste (Präzedenz 8.2) |

### 26.3 Beobachtungen des Benutzers (echte Eingabe)

1. **Überlaufmenü:** Die Location Bar lässt sich dort auswählen, erscheint aber nicht, ist also
   nicht bedienbar.
   - **Wunsch:** statt Ausklapper ein **Umbruch** der File View Toolbar (zweite Zeile) bei geringer
     Breite, mit leicht erhöhter Mindestbreite, damit die Location Bar immer sichtbar und
     bedienbar bleibt. Der Benutzer sieht dabei „noch ein Problem, das lösbar sein wird“ (noch
     nicht benannt).
   - Revidiert L16 voraussichtlich; neue Entscheidung nötig, kein Fix in LB5 ohne Absprache.
2. **Breadcrumb-Popups:** Die Pfeile funktionieren. Aber Teile des Breadcrumbs werden
   **ausgefadet** (lange Segmente mit Verlauf abgeschnitten, z. B. `57876965-e7b5-49e9-…`,
   `scratchpa…`, `fil…`), was verwirrend wirkt.
   - Das ist die Kürzung von `KUrlNavigator` bei Platzmangel; in schmalen per-View-Bars fällt sie
     stärker auf als im früheren breiten globalen Navigator.
   - Einordnung offen: KIO-Verhalten oder Whale-Thema.
3. **Places-/Pfad-Popup** (Klick auf `>` am Anfang: Liste der übergeordneten Ordner): korrekt
   positioniert, funktioniert.
4. **Completion:** funktioniert. Beim Wechsel der Views ändert sich nichts an den Pfaden; der
   Fokusverlust übernimmt also keinen getippten Verlaufstext.
5. **Texteingabe** (Umlaute, Copy/Paste, Auswahl, Escape, Return): funktioniert.
6. **Drag & Drop:** Alles wie erwartet. Bei direktem D&D von links nach rechts wird die
   **rechte View aktiv**.
   - Ob erwünscht oder nicht, will der Benutzer erst nach längerer Nutzung entscheiden.
   - Offen, nicht ändern.
7. **Mittelklick:**
   - a) Mittelklick auf ein Breadcrumb öffnet einen neuen Tab: funktioniert.
   - b) Primary-Selection-Paste: noch nicht geprüft. Die Frage war unklar; nächstes Mal erklären:
     Text irgendwo markieren (ohne Ctrl+C), in der Location Bar mit der mittleren Maustaste
     klicken → wird der markierte Text eingefügt bzw. dorthin navigiert?

### 26.4 Fortsetzung 2026-10-18

Gleicher Aufbau wie 26.1, Profil und Testordner neu angelegt (der Scratchpad der Vorsitzung war
weg), Build unverändert vom 2026-10-17. Echte Eingabe durch den Benutzer.

| Punkt | Ergebnis |
|---|---|
| 7b Primary-Selection-Paste | funktioniert (Text markieren ohne Ctrl+C, Mittelklick in die Location Bar). Für den Benutzer sehr unerwartet, ist aber X11-/KDE-Verhalten, kein Whale-Thema |
| 8 L11 mit echter Tastatur | Ctrl+L bei verborgener Leiste: funktioniert, kein Absturz |
| 8 Befund | **Bug:** Die File View Toolbars lassen sich aus-, aber nicht wieder einschalten. Ein Neustart hilft nicht, weil `[WhaleToolbars] ShowFileViewToolBars=false` in der whalerc steht. Im Test half nur, den Wert in der Profil-whalerc von Hand zurückzusetzen |
| 9 F6 | funktioniert |
| 9 Fokus nach Configure Toolbars | funktioniert |
| 9 Tab-Taste (Fokusdurchlauf) | **Bug:** Im Split mit aktiver rechter Pane läuft Tab durch die Location Bar der **inaktiven** linken Pane statt durch die eigene |

Ursachen (Vermutung, noch nicht verifiziert):

- 8: `WhaleToolbarManager::plugIntoViewMenu()` hängt den Schalter nur in das klassische
  View-Menü der Menüleiste. Whale zeigt standardmäßig das Hamburger-Menü, das Dolphin selbst
  aufbaut und das den Schalter nicht kennt. Das Kontextmenü „Hide File View Toolbars“ hängt an
  der Leiste selbst und verschwindet mit ihr. Ohne Menüleiste (Ctrl+M) gibt es keinen Rückweg;
  der Weg über Ctrl+M ist ungeprüft.
- 9: Weder Dolphin noch Whale legen eine Tab-Reihenfolge fest (kein `setTabOrder`). Die Location
  Bars werden früh erzeugt, aber erst später über `createWidget()` in die Toolbar eingesetzt und
  rutschen dadurch in Qts Fokuskette ans Ende. Mit dem früheren globalen Navigator fiel das nicht
  auf. Möglicher Fix: Tab-Reihenfolge pro Container ausdrücklich festlegen; erst offscreen
  nachstellen und mit Test absichern.

Kein Code geändert. Fixes erst nach Absprache.

### 26.5 Noch offen

- 10: Alltagstest.
- Bug 9 ist behoben (72bf31d0d Code, d6e39ccf2 Test `testFocusOrder`), auf Wayland mit echter
  Eingabe am 2026-10-18 geprüft:
  - Ursache bestätigt: Qts Fokuskette folgt der Erzeugungsreihenfolge. Die Toolbar steht hinter
    View und Statusleiste; Breadcrumb-Buttons, die beim Navigieren neu entstehen, landen am Ende
    des Fensters (Qt 6.10).
  - Umsortieren der Kette verworfen: `setTabOrder()` verschiebt ganze Bereiche um Fokus-Proxys
    und kann Widgets ohne eigenen Fokus nicht bewegen, an denen Qt trotzdem hält
    (`KUrlNavigator`, `DolphinView`, `WhaleViewHost`). `setFocusPolicy()` wirkt auf Proxys durch,
    und Qt sortiert die Kette bei jeder Aktivierung (`setFocusProxy`) selbst um.
  - Lösung: `DolphinMainWindow::focusNextPrevChild()` fragt `WhaleFocusOrder` (Dolphin-Diff
    +9 Zeilen). Tab-Ziele in Qts Reihenfolge, pro Pane zusammengefasst, Toolbar zuerst in
    sichtbarer Reihenfolge.
  - Fokus per Tastatur in der Toolbar einer inaktiven Pane aktiviert diese Pane.
- Bug 8 ist behoben (2189af27c Code, 9ecaad401 Test `testWaysBack`): „Show File View Toolbars“
  steht zusätzlich im Hamburger-Menü (nach „Panels“), im Kontextmenü der Hauptleiste (vor
  „Configure Toolbars…“, über `aboutToShow` des KToolBar-eigenen Menüs, ohne Dolphin-Diff) und,
  nur solange die Leisten ausgeblendet sind, im Kontextmenü des Dateiansicht-Hintergrunds.
  Wayland mit echter Eingabe am 2026-10-18 geprüft: alle drei Wege funktionieren, ebenso die
  Gegenprobe (kein Eintrag im Hintergrundmenü bei sichtbaren Leisten).
- Entscheidungen:
  - Umbruch statt Überlaufmenü (Revision von L16?);
  - Umgang mit dem Breadcrumb-Fade;
  - D&D-Aktivierung (beobachten).

---

## 27. L16 revidiert: Umbruch der Location Bar statt Überlaufmenü (2026-10-18)

Anlass: LB5-Beobachtung 26.3/1. Im Überlaufmenü ist die Location Bar nicht bedienbar.

### 27.1 Entscheidungen des Benutzers

- **Nur die Location Bar bricht um.** Sie rutscht bei geringer Breite in eine zweite Zeile über
  die volle Breite der File View Toolbar. Die Buttons bleiben in Zeile 1. Wird selbst die zu
  schmal, gehen nur Buttons ins Überlaufmenü (dort bedienbar). Kein Fließ-Layout, die Toolbar
  bleibt eine KToolBar.
- **Schwelle 200 px:** Umbruch, sobald der Location Bar in Zeile 1 weniger als 200 px blieben.
- **Im Split gemeinsam:** Bricht eine Pane eines Tabs um, brechen alle Panes dieses Tabs um, damit
  die Dateilisten auf gleicher Höhe beginnen.

### 27.2 Umsetzung

- Zweite Zeile **innerhalb** der `WhaleFileViewToolBar`: unterer Innenrand
  (`setContentsMargins`). QToolBarLayout legt die Buttons in `contentsRect()`, `sizeHint()`
  zählt den Rand mit (Experiment mit Qt 6.10 bestätigt). Die Location Bar bleibt Kind der Toolbar
  und wird im Rand von Hand platziert. **Dolphin-Diff 0**, keine neue Zeile im Container-Layout.
- Umgebrochen: `rebuild()` platziert die Location-Bar-Action nicht. Der Spacer verhält sich wie
  ohne Location Bar (Buttons an den Rändern). `LocationBarAction::deleteWidget()` versteckt die
  Bar, danach wird sie in Zeile 2 gezeigt. Zurück: Rand weg, `rebuild()` platziert die Action
  wieder (`createWidget()` liefert dieselbe Bar).
- Bedarf: Breite der Toolbar minus Breite der übrigen sichtbaren Elemente, Abstände und Ränder
  (Style-Metriken) < 200 px. Hängt nur von der Breite ab; der Umbruch ändert nur die Höhe, also
  kein Hin- und Herspringen.
- Gemeinsam: alle File View Toolbars derselben `DolphinTabPage`. Neu bewertet bei Resize und nach
  `rebuild()` (queued).
- Tab-Reihenfolge (`WhaleFocusOrder`): Zeile 1 von links, dann Zeile 2, dann die View.
- Dev-MCP: `panes.N.toolbar.locationBar.wrapped`.

### 27.3 Tests

- Single View schmal: umgebrochen, Bar in Zeile 2 über die volle Breite, nicht in den Actions
  der Toolbar, Toolbar höher. Breit: zurück in Zeile 1, alte Höhe.
- Split mit einer schmalen Pane: beide umgebrochen, gleiche Toolbar-Höhe.
- Neuaufbau (Configure Toolbars) während umgebrochen; Leisten aus/an; Layout ohne Location Bar
  bricht nie um.
- Tab-Reihenfolge im umgebrochenen Zustand.
- Danach Wayland-Gegenprobe durch den Benutzer.

### 27.4 Ergebnis

- Umgesetzt wie geplant, Dolphin-Diff 0.
- Zusatz: Bei Breitenänderungen fällt die Entscheidung erst, wenn die Breite **25 ms** stabil ist.
  Sonst brechen beim Öffnen eines Splits (die neue Pane wächst per Animation von 0) beide Panes
  kurz um und springen zurück (gefunden durch die Dev-MCP-Abnahme TB). Erst 150 ms, der Benutzer
  fand das zu träge; 50 ms deutlich besser, 25 ms auf Wayland „läuft perfekt“. Risiko: Fällt die
  Animation unter ~40 fps, kann es wieder kurz flackern; dann 35–40 ms.
- `testStableWidth` geht bis 260 px Fensterbreite und prüft, dass die Location Bar nie ins
  Überlaufmenü kommt. Neue Tests `testWrap`, `testWrapSplit`, `testWrapFocusOrder`.
- Tests warten auf das Ende der Split-Animation (`QVariantAnimation`, direktes Kind des Splitters).
- Breadcrumb-Fade (26.3/2): Nach dem Umbruch laut Benutzer nicht mehr zu sehen. Die Kürzung langer
  Segmente verhält sich exakt wie beim früheren globalen Navigator. Vermutlich kam der Eindruck von
  den sehr schmalen Bars vor dem Umbruch. Erledigt, keine Änderung nötig.

---

## 28. E8 revidiert: Suche pro View (2026-10-18)

- Wunsch des Benutzers: ein Such-Button in der File View Toolbar. Bisher ausgeschlossen
  (Multi-Toolbar-Plan, Abschnitt 20 / E8 „vorerst nein“), weil `toggle_search` eine Fensteraktion
  der aktiven Pane ist.
- Neu: `toggle_search` pro View in `WhaleContainerCommands`, in `allowedActions()` (jetzt 36 IDs).
  Öffnet/schließt die Suchleiste der eigenen Pane, setzt beim Öffnen den Fokus hinein, Häkchen
  folgt `searchBarVisibilityChanged`. Kein Kürzel. Ctrl+F und der globale Button wirken weiter auf
  die aktive Pane. Dolphin-Diff 0.
- Test `testSearchPerView`; `testAllowedActions` und `testAvailableActions` angepasst. Auf Wayland
  vom Benutzer geprüft („funzt hervorragend“).

---

## 29. Split-Button in der File View Toolbar (2026-10-18)

Wunsch: Ein Split-Button nur in der Toolbar der linken View, ganz rechts, immer sichtbar.
Doppelpfeil `>>` öffnet die rechte View, `<<` (bei offenem Split) schließt sie wieder.

### 29.1 Entscheidungen des Benutzers

- Hauptleiste unverändert: `split_view` bleibt dort, auch in der Vorgabe.
- `<<` schließt immer die rechte View, unabhängig von „Beim Schließen des Splits“
  (aktive/inaktive/rechte View). Der globale Split-Button behält Dolphins Verhalten.
- Fest: nicht in Configure Toolbars, immer ganz rechts in Zeile 1, nie im Überlaufmenü, kein
  Kürzel, kein Dropdown. Icons Breeze `arrow-right-double` / `arrow-left-double` (bei
  Rechts-nach-links-Layout gespiegelt).

### 29.2 Umsetzung

- `WhaleFileViewToolBar::setTrailingWidget(QWidget *)`: ein festes Widget am Ende der
  Button-Zeile, außerhalb von QToolBarLayout (rechter bzw. bei RTL linker Innenrand, von Hand
  platziert, wie die umgebrochene Location Bar). Seitenneutral, damit der Lint
  `whale_toolbars_no_pane_sides` gilt. Der Umbruch rechnet den Platz automatisch mit
  (`contentsRect()`).
- Neues `src/whale/splitview/whalesplitviewbutton.*` (außerhalb von `toolbars/`, weil es die
  linke View kennt): ein Button pro `DolphinTabPage`, hängt sich an die Toolbar von
  `primaryViewContainer()`. Neu zugeordnet, wenn sich die Kinder des Splitters ändern (Schließen
  mit Tausch der Views über den globalen Button).
- Öffnen: die Fensteraktion `split_view` (Dolphins Verhalten, auch „ausgewählten Ordner rechts
  öffnen“).
- Schließen: neue Methode `DolphinMainWindow::closeRightSplitView()` (Dolphin-Diff, rein
  additiv): setzt die Einstellung für den Aufruf vorübergehend auf `RightView`, ruft
  `DolphinTabPage::setSplitViewEnabled(false)` und aktualisiert wie `toggleSplitView()`. Dazu
  1 Zeile in `DolphinTabPage` zum Anlegen des Buttons.

### 29.3 Tests

- Ohne Split: nur die eine Toolbar hat den Button, ganz rechts, `>>`, nicht in den Actions der
  Toolbar und nicht in Configure Toolbars.
- Klick: Split offen, Button nur links, `<<`. Klick mit „aktive View schließen“ und aktiver
  linker View: trotzdem schließt die rechte; die Einstellung ist danach unverändert.
- Schließen über den globalen Button mit Tausch: Der Button wandert zur verbleibenden View.
- Schmal: Button bleibt sichtbar in Zeile 1, nichts überlappt; mit Umbruch.
- Leisten aus: Button weg. Neuer Tab: eigener Button. Tab-Reihenfolge: letzter der Zeile 1.

### 29.4 Ergebnis

- Umgesetzt wie geplant. Dolphin-Diff: `DolphinMainWindow::closeRightSplitView()` (rein additiv),
  2 Zeilen in `dolphintabpage.cpp`.
- Tests `testSplitButton`, `testSplitButtonWhenLeftCloses`, `testSplitButtonNarrow`.
- Ab 2026-10-18 testet der Benutzer Whale global installiert einige Tage im Alltag (LB5 Punkt 10).
