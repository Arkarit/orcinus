# Whale View-Plugins – Architektur- und Migrationsplan

Stand: 2026-10-03 · Revision 1 · Status: Analyse und Vorschlag; **nichts implementiert**

Nachtrag 2026-10-04: **M0-Spike durchgeführt**, Ergebnisse in Abschnitt 14. K5 ist in einem Punkt widerlegt
(versteckte Widgets erhalten sehr wohl Fokus), die Architektur bleibt tragfähig. In M2 korrigiert (14.9):
Die `DolphinView` selbst wird im Plugin-Modus **nicht** deaktiviert.

Grundlage: Whale `master` = `33359dda2` (Upstream-Stand `a5fa3c402` + Whale-Commits). Alle Aussagen über
bestehenden Code wurden in diesem Stand geprüft. Zeilennummern beziehen sich auf diesen Stand. Pfade sind
relativ zu `src/`.

Kennzeichnung der Entscheidungen:

- **[D]** ist durch die bestehende Dolphin-Architektur erzwungen.
- **[U]** dient der einfachen Upstream-Mergebarkeit.
- **[W]** ist eine neue Whale-Designentscheidung.

---

## 0. Kurzfassung

1. **Die Arbeitshypothese kollidiert an einer zentralen Stelle mit dem Code:** `DolphinView` ist keine
   „View“ im Plugin-Sinn. Sie ist View **plus halber Host**. Sie besitzt das Verzeichnis-Listing
   (`KFileItemModel`/`KDirLister`), die Auswahl, die Dateioperationen, die Ordner-View-Properties, den
   Statusleistentext, den Schreibbarkeitsstatus, den VCS-Observer und die Tooltips. Die Shell
   (`DolphinMainWindow`, `DolphinViewActionHandler`, `DolphinTabPage`, `DolphinContextMenu` …) greift an
   rund 200 Stellen direkt über `DolphinViewContainer::view()` auf diese konkrete Klasse zu. Allein
   45 Stellen fragen dabei die Auswahl ab (Pull-Modell).
2. Deshalb ist **„WhaleView ersetzt DolphinView unter dem Container“** nur mit einem großen Umbau der
   Dateien möglich, die upstream am häufigsten geändert werden (siehe 1.2). Das widerspricht dem Ziel
   der Mergebarkeit.
3. **Empfehlung:** `DolphinView` bleibt in jedem `DolphinViewContainer` **immer** vorhanden. Sie
   übernimmt zwei Rollen:
   - die eingebaute Standard-View (Icons/Compact/Details) über einen dünnen `DolphinViewAdapter`,
   - in der ersten Ausbaustufe zusätzlich das **Backend** der Host-Services (Listing, Filter, Sortierung,
     Auswahlspeicher, Dateioperationen, Statusleiste). Diese Rolle kapselt eine `DolphinViewBridge`.

   Eine Plugin-View wird **anstelle** der `DolphinView` im Container angezeigt (`QStackedWidget`). Die
   `DolphinView` läuft dann unsichtbar weiter. Die Shell merkt davon nichts: Actions, Kontextmenü,
   Statusleiste, Filterleiste, History, Panels und Split-View funktionieren unverändert.
4. Die **Plugin-API** (`WhaleView`, `WhaleViewContext`, Capabilities, Services) kennt weder
   `DolphinView` noch kitemviews. Das Backend ist ein Implementierungsdetail der Bridge und kann später
   durch ein eigenständiges, `KDirLister`-basiertes Backend ersetzt werden, ohne die Plugins zu brechen.
5. **Die Hypothese mit dem Adapter stimmt, aber es braucht zwei Adapter-Richtungen:**
   - `DolphinViewAdapter`: `DolphinView` *als* `WhaleView` (trivial).
   - `DolphinViewBridge`: `DolphinView` *als Datenquelle des Hosts*. Das ist die eigentliche
     Isolationsgrenze, und nur sie kennt Dolphin-Interna.
6. **Größte offene Frage (Abschnitt 6):** Bei der Treemap kann die Auswahl Dateien **unterhalb** des
   aktuellen Verzeichnisses enthalten („tiefe Auswahl“). Dolphins Auswahl ist indexbasiert und auf das
   geladene Modell beschränkt, und die Standard-Actions (Papierkorb, Umbenennen, Kopieren …) holen sich
   die Auswahl direkt aus `DolphinView`. Deshalb braucht die Treemap einen eigenen Stufenplan:
   - zunächst ein Kontextmenü mit expliziter Item-Liste,
   - später ein schmaler „Command-Routing“-Eingriff in Action-Handler und Hauptfenster.
7. **Änderungen an Dolphin-Dateien** für die Test-View (Meilensteine M1–M6):
   - `dolphinviewcontainer.{h,cpp}`: etwa 40–60 Zeilen,
   - `dolphintabwidget.cpp`: etwa 6 Zeilen,
   - `dolphinmainwindow.cpp`: etwa 3–5 Zeilen,
   - `dolphinui.rc`/`dolphinuiforphones.rc`: je 1 Zeile,
   - `src/CMakeLists.txt`: 1–3 Zeilen.

   **`DolphinView` selbst bleibt unverändert.** Optional kommt ein Accessor dazu (siehe 7.2).
8. **Plugin-Mechanismus:** KCoreAddons (`KPluginMetaData::findPlugins("whale/views")`,
   `KPluginFactory::instantiatePlugin<WhaleViewPlugin>`, `K_PLUGIN_CLASS_WITH_JSON`,
   `kcoreaddons_add_plugin`). Das entspricht exakt dem Muster der VCS-Plugins
   (`views/versioncontrol/versioncontrolobserver.cpp:298–302`). KParts wird verworfen (Abschnitt 8).

---

## 1. Kollisionen zwischen Hypothese und tatsächlicher Dolphin-Architektur

Dieser Abschnitt steht absichtlich zuerst. Jede Kollision nennt den Befund im Code und die Konsequenz
für das Design.

### K1 – `DolphinView` ist View + halber Host  **[D]**

Der Konstruktor (`views/dolphinview.cpp:89–282`) erzeugt und besitzt:

| Teil | Code | In der Hypothese eigentlich … |
|---|---|---|
| `KFileItemModel` inkl. `KDirLister` (Listing, Sortierung, Filter, Gruppierung, Hidden Files) | `:134` | Host |
| `KItemListController` + `KItemListSelectionManager` (Auswahl) | `:140`, `:262` | Host (Auswahl) / Plugin (Interaktion) |
| `DolphinItemListView`, `KItemListContainer` (Rendering, Layout) | `:135`, `:168` | Plugin |
| Dateioperationen (Trash, Delete, Cut/Copy/Paste, Duplicate, Rename, Drop) | `:935–1060`, `dropUrls` | Host |
| Ordner-View-Properties lesen/schreiben (`applyViewProperties`, `ViewProperties`) | mehrfach | Host (teilweise View) |
| Statusleistentext, Schreibbarkeit (`writeStateChanged`), Redirects, Fehler | Signale | Host |
| `VersionControlObserver`, `ToolTipManager`, NewFile-Observer, Platzhalter „Ordner ist leer“ | `:126–127`, `:265–277` | Host-Services |

**Konsequenz:** Die saubere Host/Plugin-Grenze verläuft *innerhalb* von `DolphinView`, nämlich zwischen
Modell/Controller und `KItemListContainer`. Ein Schnitt dort wäre genau das Refactoring, das wir
vermeiden wollen. Deshalb behandeln wir `DolphinView` als **unteilbaren Block** und ziehen die Grenze
*neben* ihr (siehe Abschnitt 3).

### K2 – Die Shell ist hart an `DolphinView*` gekoppelt  **[D]**

- `DolphinViewContainer::view()` liefert `DolphinView *` (`dolphinviewcontainer.h:125–126`).
- Direkte Zugriffe auf `DolphinView`/`view()` (ohne `views/dolphinview.cpp`): `dolphinmainwindow.cpp`
  61, `dolphinviewcontainer.cpp` 41, `dolphintabpage.cpp` 26, `dolphinpart.cpp` 26,
  `views/dolphinviewactionhandler.cpp` 24, `dolphintabwidget.cpp` 16, `viewproperties.cpp` 16,
  `dolphincontextmenu.cpp` 9 und weitere.
- `DolphinMainWindow::connectViewSignals()` (`dolphinmainwindow.cpp:2807`) und
  `DolphinViewActionHandler::setCurrentView()` (`views/dolphinviewactionhandler.cpp:45–74`) verbinden
  zusammen etwa 30 konkrete `DolphinView`-Signale.
- Upstream-Churn seit 2025-10-01 (Commits auf `upstream/master`): `dolphinview.cpp` 42,
  `dolphinmainwindow.cpp` 35, `dolphinviewcontainer.cpp` 19, `dolphincontextmenu.cpp` 14,
  `dolphintabpage.cpp` 13, `dolphinview.h` 13, `dolphintabwidget.cpp` 11,
  `dolphinviewactionhandler.cpp` 11, `dolphinui.rc` 3.

**Konsequenz:** Wenn `view()` einen abstrakten Typ liefern oder `nullptr` sein dürfte, müsste man genau
in diesen Dateien Hunderte Zeilen anfassen. Deshalb gilt: `view()` liefert weiterhin immer eine gültige
`DolphinView`.

### K3 – Auswahl ist indexbasiert, lebt im Controller und wird gepullt  **[D]**

- Der Speicher ist `KItemListSelectionManager` (`KItemSet` von Modellindizes). Er sitzt im Controller
  innerhalb von `DolphinView`.
- `DolphinView::selectedItems()` (`:460`) übersetzt Indizes in `KFileItem`s. `selectionMimeData()`
  (`:2752`) verwendet die Indizes direkt.
- **Es gibt keine Auswahl außerhalb des geladenen Modells.** Eine Datei drei Ebenen tiefer kann nicht
  ausgewählt sein.
- Per URL lässt sich die Auswahl nur über einen Umweg setzen: `markUrlsAsSelected()` (`:488`) merkt
  sich die URLs nur vor. Angewandt werden sie erst in `updateSelectionState()` (`:2026`), und dort nur,
  wenn noch keine Auswahl besteht. Das passiert nach dem Laden oder über das öffentliche
  `updateViewState()`.
- Die Shell holt die Auswahl an 45 Stellen per Pull aus `view()->selectedItems()` bzw.
  `selectedItemsCount()`. Beispiele: `updateFileAndEditActions` (`dolphinmainwindow.cpp:2642`),
  Cut/Copy (`:921–934`), alle Datei-Slots im `DolphinViewActionHandler` (`:527–557`, `:898–990`),
  `DolphinTabPage::selectedItems` (`:205–219`).

**Konsequenz:** Eine „gemeinsame Selection als Whale-State“ kann die Auswahl der `DolphinView`
**nicht ersetzen**, ohne die 45 Stellen umzubauen. In Stufe 1 bleibt deshalb die `DolphinView` der
autoritative Speicher für alles, was im aktuellen Listing liegt. Der Host spiegelt diese Auswahl
URL-basiert. Für die tiefe Auswahl siehe Abschnitt 6.

### K4 – Kein öffentlicher Zugriff auf das Modell  **[D]**

`DolphinView` hat `m_model` privat. Öffentlich gibt es nur:

- `items()` (`:442`), das jedes Mal eine Kopie der gesamten Liste liefert,
- `itemsCount()`,
- die groben Signale `itemCountChanged`, `fileItemsChanged`, `directoryLoadingStarted/Completed`.

Feingranulare Einfüge-, Entfernungs- und Verschiebungsereignisse gibt es nur am `KFileItemModel`
(`KItemModelBase`-Signale mit `KItemRangeList`).

**Konsequenz:** Die Bridge braucht Zugang zum Modell. Dafür gibt es zwei Wege:

- **(a)** ohne jede Änderung über `findChild<KItemListContainer *>()` →
  `controller()->model()/selectionManager()`. Das ist korrekt, weil `KItemListContainer` ein direktes
  Kind von `DolphinView` ist (`:168`), aber zerbrechlich.
- **(b)** ein einzelner zusätzlicher Accessor in `dolphinview.h`.

Empfehlung in 7.2.

### K5 – Fokus und „aktive View“ (Split) laufen über Interna von `DolphinView`  **[D]**

- Die Aktivierung entsteht im Eventfilter: `FocusIn` auf `m_container` → `setActive(true)` →
  `activated` (`:1113–1140`).
- `setActive(true)` ruft zusätzlich `m_container->setFocus()` auf (`:301–315`).
- `DolphinViewContainer::setActive`/`isActive`/`setFocus` delegieren direkt an `m_view`
  (`dolphinviewcontainer.cpp:232–243`).

**Konsequenz:** Wenn eine Plugin-View sichtbar ist, muss der Container ihren `FocusIn` selbst in
`activate()` übersetzen. Seine `setFocus`-Aufrufe muss er an das sichtbare Widget leiten. Es bleibt zu
prüfen, ob `setFocus()` auf das versteckte innere Widget keinen sichtbaren Effekt hat. Das ist laut
Qt-Doku so: Versteckte Widgets erhalten keinen Fokus. Das gehört in einen Test (M2).

> **M0 (14.2): widerlegt.** `setFocus()` auf ein verstecktes Widget macht es in Qt 6.10 zum Fokus-Widget,
> und der `FocusIn` aktiviert die Split-Hälfte. Gegenmaßnahme (in M2 korrigiert, siehe 14.9):
> „DolphinView bleibt als Shell-Fokusziel aktiv; ihre interaktiven Kinder werden stillgelegt und ihr Fokus über den WhaleViewHost auf die sichtbare View umgeleitet.“

### K6 – Kein Platz für Plugin-State in den bestehenden Formaten  **[D]/[U]**

- Der History-State pro Ort ist `KUrlNavigator::locationState`. Dafür serialisiert
  `DolphinViewContainer::saveViewState()` (`dolphinviewcontainer.cpp:1191–1206`) die `DolphinView`
  (`DolphinView::saveState`, Version 1, `:1810`).
- Der Tab-State ist ein sequenzielles Format, Version 4 (`dolphintabpage.cpp:296–322`). Ein optionaler
  Tab-Titel hängt am Ende.
- Die Session speichert pro Tab den Eintrag `State` in `DolphinTabWidget::saveProperties`
  (`dolphintabwidget.cpp:82–143`).

**Konsequenz:** Die Formate dürfen nicht erweitert werden. Sobald upstream die Tab-Version 5 einführt,
gäbe es sonst inkompatible Sessions und Merge-Konflikte. Plugin-State braucht **eigene Kanäle**:

- einen eigenen Session-Key neben `State`,
- einen Whale-Umschlag nur um den `locationState` (siehe 9.6).

### K7 – View-Modi sind ein Enum und persistieren pro Ordner in geteilten Daten  **[D]/[U]**

- `DolphinView::Mode` (Icons/Compact/Details).
- Die `view_mode`-`KSelectAction` interpretiert `action->data()` als `Mode`
  (`views/dolphinviewactionhandler.cpp:234–253`, `:515–525`).
- Der Modus wird pro Ordner in `ViewProperties` gespeichert, in der Gruppe `[Dolphin]`. **Diese Daten
  teilt Whale mit dem System-Dolphin** (`CLAUDE.md`, Phase 1).

**Konsequenz:**

- Plugin-Views werden **nicht** als weitere Modi in das Enum oder die `view_mode`-Action eingeschleust.
- Icons/Compact/Details bleiben innere Modi der eingebauten View.
- Welche View pro Ordner aktiv ist, wird **nie** in `.directory`/xattr der Gruppe `[Dolphin]`
  geschrieben.

### K8 – Alle View-Actions sitzen im `DolphinViewActionHandler`  **[D]**

- Sortierung, Gruppierung, Zoom, Vorschau, versteckte Dateien und Zusatzinformationen hängen an
  `DolphinView`-Signalen.
- Den Enabled-Zustand setzt der Handler teilweise selbst, zum Beispiel beim Zoom (`:853–858`) und bei
  `create_dir` (`:774`).

**Konsequenz:** Das Capability-Gating muss *nach* dem Handler greifen. Eine Whale-Klasse setzt
`setEnabled(false)` auf benannte Actions, sobald der Handler sie aktualisiert hat und solange eine
Plugin-View aktiv ist. Den Handler selbst ändern wir dafür nicht.

**Vorteil:** Sortierung, Filter und Hidden-Files wirken automatisch auf jede Plugin-View, die die
Host-Items in Host-Reihenfolge darstellt (Test-View). Die Capability lautet dann nicht „kann
sortieren“, sondern **„zeigt Host-Reihenfolge“**.

### K9 – Previews sind an `KFileItemModel` und den sichtbaren Bereich gebunden  **[D]**

- `KFileItemModelRolesUpdater` wird von `KFileItemListView` erzeugt und über
  `setVisibleIndexRange` gesteuert (`kitemviews/kfileitemlistview.cpp:370–415`).
- Für fremde Views ist er nicht nutzbar.
- Es existieren bereits drei getrennte `KIO::PreviewJob`-Pipelines (View, Info-Panel, Tooltip).

**Konsequenz:** Für Plugins entsteht ein eigener schlanker Preview-Service um `KIO::PreviewJob` mit den
Plugins aus `[PreviewSettings]`. Der Thumbnail-Disk-Cache von KIO wird automatisch geteilt.

### K10 – Rekursives Scannen gibt es nicht als Baum  **[D]**

`KDirectoryContentsCounterWorker` (`kitemviews/private/`, `fts_open`, nur lokal) liefert nur die
Summen (Anzahl, Größe) pro Pfad. Eine Baumstruktur liefert er nicht, und er ist privat.

**Konsequenz:** `WhaleTreeScanner` ist ein neuer, allgemeiner Service. Das `fts`-Muster des Workers
dient als Vorlage.

### K11 – Kontextmenü: datengetrieben, aber mit Selection-gebundenen Standard-Actions  **[D]**

- **Datengetrieben:** `DolphinMainWindow::openContextMenu(pos, item, selectedItems, url)`
  (`dolphinmainwindow.cpp:1534`) arbeitet mit einer **expliziten** Liste. Öffnen, „Öffnen mit“,
  Kopieren/Verschieben nach und Service-Menüs (`KFileItemActions`) verwenden diese Liste
  (`dolphincontextmenu.cpp:287–332`, `:553`).
- **Selection-gebunden:** Die Standard-Actions im selben Menü (Ausschneiden, Kopieren, Papierkorb,
  Löschen, Umbenennen, Eigenschaften, Duplizieren) sind `KActionCollection`-Actions. Sie wirken auf
  `DolphinView::selectedItems()`.

**Konsequenz:**

- Für Items im aktuellen Listing ist das konsistent, solange die Auswahl gespiegelt ist.
- Für tiefe Items wäre es **gefährlich**: Ein Rechtsklick auf `a/b/c.txt` und dann „Papierkorb“ würde
  die gespiegelte Auswahl treffen. Ist diese leer, startet der Selection-Mode. Siehe Abschnitt 6.

### K12 – Unsichtbare `DolphinView` kostet weiterhin Rechenzeit  **[D]**

Der RolesUpdater pausiert nur während Transaktionen und Timern (`kfileitemlistview.cpp:288–310`), aber
nicht bei Unsichtbarkeit. Eine versteckte `DolphinView` erzeugt also weiterhin Icons und Previews für
die erste Seite und Ordnergrößen. Der Aufwand ist begrenzt, aber unnötig.

> **M0 (14.4): gemessen.** Unsichtbarkeit stoppt nichts; die Arbeit pro Ordnerwechsel ist praktisch
> identisch zur sichtbaren View, im Leerlauf ist sie null. M11 bleibt optional.

**Konsequenz:** Für M2 akzeptieren und messen. Falls nötig, gibt es später eine kleine,
**upstreamfähige** Änderung („RolesUpdater pausieren, wenn die View nicht sichtbar ist“), siehe M11.

### K13 – `dolphinpart` nutzt `DolphinView` direkt

`dolphinpart.cpp` (26 Zugriffe) bettet `DolphinView` in Konqueror ein. Das Plugin-System gehört nur in
die App (`dolphinstatic`). Solange `DolphinView` unverändert bleibt, ist der Part nicht betroffen.

---

## 2. Verantwortlichkeiten heute

### 2.1 `DolphinView` (`views/dolphinview.{h,cpp}`, 1108 + 2975 Zeilen)

| Bereich | Heute in `DolphinView` | Ziel-Zuordnung |
|---|---|---|
| Listing, Laden, Redirects, Fehler | über `KFileItemModel` (`loadDirectory` `:2460`, Signale `:228–241`) | Host-Service |
| Sortierung, Gruppierung, Filter, Hidden | Setter + `ViewProperties` | Host-Model-Eigenschaften |
| Auswahl, aktuelles Item | über den Controller | Host-State (gespiegelt) |
| Aktivierung | `slotItemActivated` → Signal `itemActivated` | Plugin meldet, Host entscheidet |
| Kontextmenü-Anfrage | `requestContextMenu` | Plugin meldet, Host baut |
| Dateioperationen | Methoden `:935–1060` | Host-Service |
| Drag & Drop | Controller + `DragAndDropHelper` | Plugin (Geste), Host (Ausführung) |
| Zoom, Previews an/aus, sichtbare Rollen | Setter + `ViewProperties` | View-spezifisch bzw. Host-Einstellung |
| Ordner-State (Scrollposition, aktuelles Item, aufgeklappte Ordner) | `saveState`/`restoreState` | View-State (opak) |
| Statustext, Schreibbarkeit | Signale | Host |
| VCS-Status, Tooltips, Platzhalter | Observer/Manager | Host-Services bzw. View |

### 2.2 Unmittelbar beteiligte Klassen

- **`DolphinViewContainer`** (`dolphinviewcontainer.{h,cpp}`): URL-Navigator mit History, Filter-,
  Such-, Admin- und Selection-Mode-Leisten, Statusleiste, Meldungen. Die Aktivierung von Items liegt in
  `slotItemActivated` (`:758`): Ordner und Archive werden navigiert, Dateien per `KIO::OpenUrlJob`
  geöffnet. Außerdem: Hover → `showItemInfo` (`:872`), History-State (`:1191–1206`). Der Container ist
  bereits der **natürliche Host pro View-Fläche**.
- **`DolphinMainWindow`**:
  - verteilt den Zustand der aktiven View (`activeViewChanged` `:1675`, `connectViewSignals` `:2807`),
  - baut das Kontextmenü (`:1534`),
  - besitzt Cut/Copy/Paste, `updateFileAndEditActions` (`:2640`) und `KFileItemActions`.
- **`DolphinViewActionHandler`**: alle View- und Datei-Actions für die aktuelle `DolphinView`.
- **`DolphinTabPage`/`DolphinTabWidget`**: Split, Tab-State, Session.
- **`DolphinContextMenu`**: Menüaufbau aus Item, Liste und URL.

---

## 3. Die Host/Plugin-Grenze

### 3.1 Wo die Grenze verläuft

```
DolphinMainWindow / ActionHandler / TabPage / ContextMenu      (unverändert: sprechen mit DolphinView)
        │  view() → DolphinView*  (immer gültig)                                   [D][U]
        ▼
DolphinViewContainer                                           (minimal geändert)
 ├─ Leisten, URL-Navigator/History, Statusleiste               (unverändert)
 └─ WhaleViewHost  ───────────────────────────────── Whale-Code, kennt Dolphin nur über die Bridge
      ├─ QStackedWidget  [ Seite 0: DolphinView | Seite 1: Plugin-Widget ]
      ├─ DolphinViewBridge   ← EINZIGE Klasse mit Zugriff auf DolphinView-Interna    [U]
      │     • Items/Listing-Fassade über KFileItemModel
      │     • Auswahl spiegeln (URL ⇄ Index)
      │     • DolphinView-Signale für Plugin-Ereignisse auslösen (Kontextmenü, Tabs …)
      ├─ WhaleViewContextImpl   (implementiert die Plugin-Fassade)
      ├─ aktive WhaleView:  DolphinViewAdapter  |  Plugin-Instanz
      └─ State-Speicher für Plugins (pro History-Eintrag, pro Session)
═══════════════════════════ Plugin-API-Grenze (libwhaleviewapi) ═══════════════════════════
WhaleView · WhaleViewPlugin · WhaleViewContext · WhaleViewCapabilities · WhaleSelection ·
WhaleItemList · Services (WhalePreviewProvider, WhaleTreeScanner, WhaleFileOperations)
```

Warum diese Grenze:

- Die Shell-Kopplung bleibt bestehen und funktioniert, weil `view()` gültig bleibt **[D][U]**.
- Alle Dolphin-Eigenheiten konzentrieren sich in `DolphinViewBridge`. Bei einem Upstream-Merge ist
  höchstens diese eine Whale-Datei anzupassen, statt dass Konflikte in Dolphin-Dateien entstehen **[U]**.
- Plugins sehen nur `KFileItem`, `QUrl`, Qt-Typen und Whale-Fassaden. Interne Typen wie
  `KFileItemModel`, `KItemSet` oder `DolphinView` bleiben verborgen **[W]**.

### 3.2 Einen nützlichen Trick ausdrücklich zulassen: Signale von `DolphinView` aus der Bridge auslösen  [U]

In Qt 6 sind Signale öffentliche Member. `Q_EMIT dolphinView->requestContextMenu(...)`, `tabRequested`,
`windowRequested`, `activeTabRequested`, `urlActivated`, `requestItemInfo`,
`goBack/Forward/UpRequested` sind deshalb aus der Bridge erlaubt. Damit nutzen Plugin-Ereignisse die
gesamte bestehende Verdrahtung in `connectViewSignals()`, **ohne** eine Zeile im Hauptfenster.

Qt rät generell davon ab, Signale fremder Objekte auszulösen. Deshalb gilt die Regel: Das passiert nur
in der Bridge und ist dort dokumentiert. Die Aktivierung (`itemActivated`) leitet der Container dagegen
direkt an seinen eigenen privaten Slot `slotItemActivated` weiter, weil der Host ein Kind des
Containers ist.

### 3.3 Verworfene Alternativen

| Alternative | Warum nicht (jetzt) |
|---|---|
| **A. `DolphinView` erbt von `WhaleView`** | Zieht Plugin-API-Typen in die meistgeänderte Upstream-Datei. `WhaleView` müsste das gesamte `DolphinView`-Interface abbilden, oder die Shell bliebe trotzdem an `DolphinView` gebunden. Damit bringt das keinen Nutzen gegenüber dem Adapter. |
| **B. Shell auf eine abstrakte View umstellen** (`view()` → `WhaleView*`) | Etwa 200 Änderungen in `dolphinmainwindow.cpp`, `dolphinviewactionhandler.cpp`, `dolphintabpage.cpp` usw. Das wären dauernde Merge-Konflikte (K2). |
| **C. Eigenes `KDirLister`-Backend für Plugins, `DolphinView` bei aktivem Plugin zerstören** | Alle `view()`-Aufrufe müssten mit `nullptr` umgehen können. Filterleiste, Statusleiste, Hidden Files, Sortierung, Schreibbarkeit, Fehler und Redirects müssten neu gebaut werden. **Bleibt aber als späteres Backend hinter derselben Plugin-API möglich.** |

---

## 4. Die Klassen im Einzelnen

### 4.1 `WhaleView` – dünne Basisklasse  [W]

**Abweichung von der Hypothese:** Ich empfehle `WhaleView : QObject` mit `widget()` statt
`WhaleView : QWidget`. Die Gründe:

- Der `DolphinViewAdapter` kann die bestehende `DolphinView` als Widget zurückgeben, ohne sie in ein
  Zwischen-Widget umzuhängen.
- Die Test-View kann einen `QListView` direkt liefern. Die Treemap kann ein `QWidget`, ein
  `QOpenGLWidget` oder später ein `QQuickWidget` liefern.
- Die Lebensdauer von Logik und Widget lässt sich getrennt steuern. Das ist dasselbe Muster wie bei
  `KParts::Part::widget()`.

Mit `QWidget` als Basis ginge es auch, dann bräuchte der Adapter aber ein Wrapper-Widget. Ich halte das
für eine Geschmacksfrage mit leichtem Vorteil für `QObject` (**offene Entscheidung O1**).

```cpp
class WHALEVIEWAPI_EXPORT WhaleView : public QObject {
    Q_OBJECT
public:
    WhaleView(WhaleViewContext *context, QObject *parent);
    ~WhaleView() override;

    virtual QWidget *widget() = 0;                  // vom View erzeugt, vom Host eingebettet
    virtual WhaleViewCapabilities capabilities() const;   // darf von context()->url() abhängen

    // Lifecycle (Defaults: leer)
    virtual void activate();                        // sichtbar, Context-Signale verbunden
    virtual void deactivate();                      // unsichtbar, teure Arbeit pausieren

    // Opaker View-State (Default: leer)
    virtual QByteArray saveState() const;           // pro Ort + Session
    virtual void restoreState(const QByteArray &state);

    // Optionale Operationen – nur aufgerufen, wenn die Capability gesetzt ist
    virtual void setZoomLevel(int level);
    virtual int zoomLevel() const;
    virtual void scrollToItem(const QUrl &url);     // z. B. nach "Neuer Ordner"
    virtual void startRename(const QUrl &url);      // Default: Host zeigt RenameDialog

Q_SIGNALS:
    void capabilitiesChanged();
    void zoomLevelChanged(int level);

protected:
    WhaleViewContext *context() const;

private:
    std::unique_ptr<WhaleViewPrivate> d;            // ABI-Reserve
};
```

`WhaleView` enthält ausdrücklich **keine**:

- Navigation, Dateioperationen oder Kontextmenü-Aufbau,
- Sortier- und Gruppierlogik,
- Auswahlspeicher.

Das alles gehört zum Context.

### 4.2 `WhaleViewContext` – Fassade für Plugins  [W]

Der Context ist eine abstrakte Klasse in der API-Bibliothek. Implementiert wird er von
`WhaleViewContextImpl` im Host, mit einer Instanz pro View-Instanz.

| Gruppe | API (Skizze) | Implementierung Stufe 1 |
|---|---|---|
| Ort | `url()`, `rootItem()`, `isWritable()`; Signale `urlChanged`, `loadingStarted/Completed/Canceled` | Bridge ← `DolphinView`-Signale |
| Items | `WhaleItemList *items()`: geordnete `KFileItem`-Liste in Host-Reihenfolge (sortiert/gefiltert); Signale `reset`, `itemsInserted(int, KFileItemList)`, `itemsRemoved(KFileItemList)`, `itemsMoved`, `itemsChanged(KFileItemList)` **(umgesetzt in M3 mit leicht anderen Signalen, siehe 14.10)** | Bridge spiegelt `KFileItemModel`-Ranges |
| Auswahl | `selection()` → `WhaleSelection{urls, current}`; `setSelection(...)`; Signal `selectionChanged` | Bridge ⇄ `KItemListSelectionManager` |
| Aktionen anfordern | `activate(items)`, `openUrl(url)`, `openInNewTab/Window(url)`, `goUp/Back/Forward()`, `requestContextMenu(globalPos, item, items)`, `requestItemInfo(item)`, `setActive()` | Container-Slot bzw. ausgelöste `DolphinView`-Signale |
| Meldungen | `showInfo/Error(text)` | Statusleiste/`showMessage` |
| Einstellungen | `previewsEnabled()`, `hiddenFilesShown()`, `KConfigGroup viewConfig()` (globale Plugin-Konfiguration) | Bridge, `whalerc [WhaleView/<id>]` |
| Services | `previews()`, `treeScanner()`, `fileOperations()`, `standardAction(name)` | Host-Instanzen |
| Erweiterung | `QObject *service(const char *iid)` | für spätere Services ohne vtable-Bruch |

Regeln:

- Der Context **kennt keine bestimmte View** (es gibt also kein `getDataForCushionView()`).
- Services sind allgemein: Ein Baumscanner liefert Knoten mit Größen. Was daraus eine Treemap wird,
  entscheidet allein das Plugin.

### 4.3 `WhaleViewHost` – intern, nicht Teil der Plugin-API  [W]

Es gibt einen Host pro `DolphinViewContainer`. Er besitzt:

- den Stack, die Bridge, die Registry-Referenz und die aktive View (`std::unique_ptr`),
- die Plugin-States pro History-Eintrag und pro Session.

Er verantwortet:

- den View-Wechsel und den Lifecycle (Abschnitt 9),
- das Capability-Gating der Actions im Zusammenspiel mit `WhaleViewActions`,
- die Umsetzung von Fokus in Aktivierung (K5).

Dem Container gegenüber stellt er eine kleine Schnittstelle bereit: `setActive`, `isActive`,
`setFocus`, `currentWidget`, `saveLocationState`/`restoreLocationState` sowie das Signal
`itemActivated(KFileItem)`.

### 4.4 `WhaleViewPlugin` + Metadaten  [W]

```cpp
class WHALEVIEWAPI_EXPORT WhaleViewPlugin : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual WhaleView *createView(WhaleViewContext *context, QObject *parent) = 0;
    virtual bool supportsUrl(const QUrl &url) const;     // Default: true
};
```

JSON-Metadaten (`K_PLUGIN_CLASS_WITH_JSON`):

```json
{
  "KPlugin": { "Id": "whale_treemap", "Name": "Treemap", "Icon": "view-treemap",
               "Description": "…", "Version": "0.1", "License": "GPL-2.0-or-later" },
  "X-Whale-ViewApiVersion": 1,
  "X-Whale-Capabilities": ["Selection", "Zoom", "RecursiveContent"],
  "X-Whale-RequiresLocal": true
}
```

Die Capabilities stehen zweimal zur Verfügung:

- **statisch** im JSON, für Menüs ohne das Laden der Bibliothek,
- **dynamisch** über `WhaleView::capabilities()`, maßgeblich zur Laufzeit.

### 4.5 Capabilities  [W]

```cpp
enum class WhaleViewCapability {
    Selection, MultiSelection,
    HostOrder,          // stellt items() in Host-Reihenfolge dar → Sortier-/Filter-Actions aktiv
    Grouping,           // zeigt Host-Gruppen an (später)
    Zoom, Previews,     // reagiert auf Zoom bzw. Vorschau-Schalter
    Drag, Drop, InlineRename,
    RecursiveContent,   // zeigt mehr als das aktuelle Listing (Auswahl kann „tief“ sein)
    KeyboardNavigation,
};
Q_DECLARE_FLAGS(WhaleViewCapabilities, WhaleViewCapability)
```

**Hinweis:** `Sorting` aus der Hypothese wird zu `HostOrder`. Sortieren ist in Dolphin eine Eigenschaft
des Modells und keine Leistung der View (K8).

| Capability fehlt | Wirkung im Host |
|---|---|
| `HostOrder` | Sortier-, Gruppier- und Zusatzinfo-Actions werden deaktiviert |
| `Zoom` | `view_zoom_in/out/reset` werden deaktiviert |
| `Previews` | `show_preview` wird deaktiviert |
| `InlineRename` | „Umbenennen“ öffnet `KIO::RenameFileDialog` |
| `Selection` | Datei-Actions gelten für den Ordner selbst bzw. sind deaktiviert |

### 4.6 Allgemeine Services  [W]

| Service | Basis (wiederverwendet) | Neu |
|---|---|---|
| `WhaleItemList` | `KFileItemModel` über die Bridge | Fassade, URL-Index |
| `WhalePreviewProvider` | `KIO::PreviewJob`, `PreviewSettings`-Plugins, KIO-Thumbnail-Cache | Anfragewarteschlange, Prioritäten, Abbruch |
| `WhaleTreeScanner` | `fts`-Muster aus `KDirectoryContentsCounterWorker`; `KIO::listRecursive` als Fallback für Remote-Orte | Baum mit Größen, inkrementelle Signale, Abbruch, Cache pro Wurzel, `QThread`-Worker |
| `WhaleFileOperations` | `KIO::DeleteOrTrashJob`, `KIO::paste`, `KIO::copy/move/copyAs`, `KIO::RenameFileDialog`, `KPropertiesDialog`, `KIO::FileUndoManager`, `DragAndDropHelper::dropUrls`, `KUrlMimeData` | arbeitet auf **expliziten** `KFileItemList`s (wichtig für die tiefe Auswahl) |
| Kontextmenü | `DolphinContextMenu` über `requestContextMenu` | — |
| Metadaten | `KFileItem`, `KFileMetaData` | zunächst kein eigener Service |

---

## 5. Auswahl heute und der gemeinsame Selection-State

### 5.1 Heute

Wo die Auswahl entsteht und wie sie die App erreicht:

1. `KItemListController` (Maus, Tastatur, Rubberband) setzt sie im `KItemListSelectionManager`.
2. Dessen Signal geht an `DolphinView::slotSelectionChanged`. Dort wird entprellt (0 ms bzw. 300 ms).
3. Danach folgt das Signal `selectionChanged(KFileItemList)`. Empfänger sind Container, Statusleiste,
   BottomBar, ActionHandler und `DolphinMainWindow::slotSelectionChanged` (`:417`). Letzteres
   aktualisiert `KFileItemActions` und gibt die Auswahl an das Info-Panel weiter.

Auch die Persistenz läuft über URLs: `saveState` speichert die URL-Liste, und `updateSelectionState`
wendet sie nach dem Laden an.

### 5.2 Vorschlag (Stufe 1: gespiegelt, Bereich = aktuelles Listing)  [D][U]

- `WhaleSelection` ist **URL-basiert** (`QList<QUrl> selected; QUrl current;`) und kennt keine Indizes.
  Das ist die Sprache der Plugin-API.
- **Autoritativer Speicher bleibt die `DolphinView`** (K3). Die Bridge übersetzt in beide Richtungen:
  - Dolphin → Host: `selectionChanged` der `DolphinView` → `WhaleSelection` → `Context::selectionChanged`.
  - Host → Dolphin (Plugin ändert die Auswahl): Die Bridge setzt sie direkt über
    `KItemListSelectionManager::setSelectedItems(KItemSet)` und `setCurrentItem`. Das ist sofort
    wirksam, im Gegensatz zum verzögerten `markUrlsAsSelected`. Zugriff gibt es über den Weg aus K4.
  - Eine Rückkopplung wird mit einem Herkunfts-Flag und Vergleich verhindert.
- Folgen:
  - Beim Wechsel der View **bleibt die Auswahl automatisch erhalten**, weil sie nie die `DolphinView`
    verlässt.
  - Alle 45 Pull-Stellen der Shell funktionieren unverändert.
  - Das Plugin führt nur eine **Darstellungs-Kopie**.
- Die Entprellung der `DolphinView` gilt auch für Plugins. Das ist gewollt, denn Info-Panel und
  `KFileItemActions` sollen nicht bei jedem Rubberband-Schritt neu rechnen.

### 5.3 Grenze von Stufe 1

URLs außerhalb des Listings lassen sich nicht in die `DolphinView` spiegeln. Für die Treemap ist das
das Kernproblem (Abschnitt 6).

---

## 6. Sonderfall tiefe Auswahl (Treemap) – Stufenplan

Ziel: Die Treemap soll **keine** Dateiliste imitieren müssen. Ein Klick auf `a/b/c.txt` wählt genau
diese Datei aus.

| Option | Beschreibung | Dolphin-Änderung | Bewertung |
|---|---|---|---|
| **T1** – View-lokale Auswahl + Kontextmenü mit expliziter Liste | Die Treemap hält ihre Auswahl selbst und meldet sie dem Host als `WhaleSelection` mit dem Flag „tief“. Der Host spiegelt nur den Anteil im Listing in die `DolphinView`. Rechtsklick → Whale baut ein **eigenes, reduziertes** Menü: `KFileItemActions` (Öffnen mit, Service-Menüs) + `WhaleFileOperations` auf der expliziten Liste (Papierkorb, Löschen, Kopieren, Pfad kopieren, Eigenschaften, „Im Ordner anzeigen“ = Host navigiert und markiert). Tastatur-Shortcuts der Standard-Actions sind in der Treemap deaktiviert (Capability `RecursiveContent` ⇒ Gating der Datei-Actions). | keine | **Empfohlen für die erste Treemap-Version.** Ist sicher, weil keine Aktion das falsche Ziel trifft. |
| **T2** – Command-Routing | Die Datei-Slots in `DolphinViewActionHandler` (Rename, Trash, Delete, Duplicate, Properties, CopyPath: 6 Slots) und in `DolphinMainWindow` (Cut, Copy, Paste, `updateFileAndEditActions`, `slotSelectionChanged`, Compare, Add-to-Places …: etwa 12 Stellen) fragen eine Hilfsfunktion `activeSelection()` bzw. `activeFileOperations()` statt `view()->…` | etwa 20–30 einzeilige Änderungen in 2–3 viel geänderten Dateien | Erst entscheiden, wenn T1 im Alltag nicht reicht. Mechanisch, aber merge-relevant. |
| **T3** – Selection-Override in `DolphinView::selectedItems()` | `DolphinView` liefert bei gesetztem Override eine externe Liste | wenige Zeilen in `dolphinview.cpp` | **Abgeraten.** `selectionMimeData` (`:2752`), das Inline-Rename und das „nächstes Item auswählen“ nach dem Löschen arbeiten mit Indizes. Es entstehen stille Inkonsistenzen in der Datei, die upstream am häufigsten geändert wird. |

---

## 7. Abläufe heute und künftiger Weg für Plugin-Views

| Thema | Heute | Plugin-View (Stufe 1) |
|---|---|---|
| **Navigation** | Container: `setUrl` → `KUrlNavigator` (History) → `slotUrlNavigatorLocationChanged` → `DolphinView::setUrl` → `KFileItemModel::loadDirectory` | unverändert. Die versteckte `DolphinView` lädt, die Bridge meldet `urlChanged`/`loading*`, das Plugin liest `items()`. Ein Plugin navigiert mit `context->openUrl()` → `container->setUrl()`. |
| **Aktivierung** | Controller → `DolphinView::slotItemActivated` → `itemActivated` → `DolphinViewContainer::slotItemActivated` (`:758`) | `context->activate(items)` → Host-Signal → **derselbe** Container-Slot. Die Logik für Ordner, Archive und Dateien wird also wiederverwendet. |
| **Kontextmenü** | Controller → `DolphinView::requestContextMenu` → `DolphinMainWindow::openContextMenu` → `DolphinContextMenu` | `context->requestContextMenu(...)` → Bridge löst `DolphinView::requestContextMenu` aus (3.2). Für tiefe Items gilt T1. |
| **Drag** | `KItemListController::startDragging` → `KFileItemModel::createMimeData` | Das Plugin erkennt die Geste. `fileOperations()->mimeData(items)` (`KUrlMimeData`, `mostLocalUrl`, Portal-Export) → `QDrag` |
| **Drop** | Controller → `DolphinView::slotItemDropEvent` → `DragAndDropHelper::dropUrls` | `fileOperations()->drop(destUrl, event, widget)` → `DragAndDropHelper::dropUrls` (statisch, direkt nutzbar) |
| **Dateioperationen** | Actions → `DolphinView::*SelectedItems()` | Im Listing unverändert (die Auswahl ist gespiegelt). Für tiefe Items `WhaleFileOperations` (T1). |
| **Previews** | RolesUpdater für den sichtbaren Bereich | `WhalePreviewProvider` (K9) |
| **Sortierung/Gruppierung** | Eigenschaften von `KFileItemModel`, Actions im Handler, Persistenz in `ViewProperties` | Bei `HostOrder` kommen sie automatisch über die Reihenfolge von `items()`. Sonst werden die Actions deaktiviert. Gruppen zunächst nicht in der API (später `WhaleItemList::groups()`). |
| **View Properties** | pro Ordner `ViewProperties` (geteilt mit Dolphin) | bleiben für die `DolphinView`. Plugin-Einstellungen kommen nur in eigene Whale-Speicher (9.6). |
| **Hover/Info-Panel** | `requestItemInfo` → Container → Info-Panel | `context->requestItemInfo(item)` → Bridge löst `DolphinView::requestItemInfo` aus |
| **Statusleiste, Filterleiste, Platzhalter** | an `DolphinView`-Signale gebunden | unverändert. Die Filterleiste wirkt auf `KFileItemModel` und damit auf `items()`. Für eine rekursive Treemap ist ein Namensfilter zunächst ohne Wirkung. |

### 7.1 Zu ändernde Dolphin-Dateien

| Datei | Änderung | Umfang | Meilenstein |
|---|---|---|---|
| `src/CMakeLists.txt` | `add_subdirectory(whale)`, Host-Quellen in `dolphinstatic`, Link auf `whaleviewapi` | 1–3 Zeilen | M1/M2 |
| `dolphinviewcontainer.h` | Member `WhaleViewHost *m_viewHost`, Forward-Deklaration | ~3 Zeilen | M2 |
| `dolphinviewcontainer.cpp` | Host statt `m_view` ins Layout (`:180`); `setActive`/`isActive`/`setFocus` über den Host (`:232–243` + 4× `setFocus`); `saveViewState`/`tryRestoreViewState` mit Whale-Umschlag (`:1191–1206`); `connect(host, itemActivated, this, slotItemActivated)` | ~40–60 Zeilen | M2/M4/M6 |
| `dolphintabwidget.cpp` | in `saveProperties`/`readProperties` einen Eintrag `WhaleViews` pro Tab neben `State` | ~6 Zeilen | M6 |
| `dolphinmainwindow.cpp` | `WhaleViewActions` erzeugen (Konstruktor) und in `activeViewChanged` aktualisieren | ~3–5 Zeilen | M2/M5 |
| `dolphinui.rc`, `dolphinuiforphones.rc` | Action `whale_view_switcher` im Ansicht-Menü (beide, weil ein Test die Synchronität prüft) | je 1 Zeile | M2 |
| `views/dolphinview.h/.cpp` | **keine**, optional ein Accessor (7.2) | 0 bzw. 2 Zeilen | M3 |
| T2 (nur falls gewählt) | `dolphinviewactionhandler.cpp`, `dolphinmainwindow.cpp`, `dolphintabpage.cpp` | ~20–30 Zeilen | M10 |

So bleiben die Änderungen klein **[U]**:

- Jede Änderung in einer Dolphin-Datei ist ein **Delegationsaufruf** an Whale-Code. Logik steht nie in
  Dolphin-Dateien.
- Neue Member und Includes stehen in klar markierten Blöcken (`// Whale: view plugins`) am Ende von
  Abschnitten. Das reduziert Konflikte mit upstream-Einfügungen.
- Änderungen an den Formaten von `DolphinView::saveState` und dem Tab-State sind tabu (K6).
- Die Verdrahtung im Hauptfenster bleibt dank 3.2 unangetastet.

### 7.2 Modellzugriff der Bridge  [U]

Es gibt zwei Möglichkeiten:

- **Null Änderungen:** `dolphinView->findChild<KItemListContainer *>(QString(), Qt::FindDirectChildrenOnly)`.
- **Ein Accessor:** `KItemListContainer *DolphinView::itemListContainer() const` in einem Whale-Block.

Ich empfehle den **Accessor**. Er ist explizit, er fällt beim Kompilieren auf, falls upstream umbaut,
und er erzeugt nur eine winzige, konfliktarme Diff. Mit `findChild` gibt es kein Diff, dafür bricht es
still. Das ist **offene Entscheidung O2**. In beiden Fällen nutzt nur `DolphinViewBridge` den Zugriff.

**Entschieden in M3:** Accessor `KItemListContainer *DolphinView::itemListContainer() const`, inline im
Header, markiert mit `// Whale: view plugins` (6 Zeilen). Das Modell holt die Bridge über
`itemListContainer()->controller()->model()`. Details in 14.10.

---

## 8. Plugin-Mechanismus

| Kandidat | Bewertung |
|---|---|
| **KCoreAddons `KPluginMetaData` + `KPluginFactory`** (`kcoreaddons_add_plugin`, `K_PLUGIN_CLASS_WITH_JSON`) | **Gewählt.** Dolphin nutzt das bereits für `dolphin/vcs` (`versioncontrolobserver.cpp:298`) und `kf6/overlayicon`, KIO für `kf6/kfileitemaction`. Es bietet JSON-Metadaten ohne Laden der Bibliothek, Suche über `QT_PLUGIN_PATH`, statische Plugins (`kcoreaddons_add_plugin(... STATIC)` + `kcoreaddons_target_static_plugins`) für eingebaute Views und Fehlertexte über `KPluginFactory::Result`. |
| KParts (`ReadOnlyPart`, Konqueror-Modell) | Verworfen. Schwergewichtig (XMLGUI-Merging, Navigation-Extension), keine gemeinsame Auswahl, kein Kontextmenü-Modell. `dolphinpart` zeigt die Richtung „Dolphin in andere Programme“, nicht umgekehrt. |
| Reines `QPluginLoader` + `Q_PLUGIN_METADATA` | Möglich, aber das wäre das Neubauen dessen, was KCoreAddons bereits bietet. |
| `KAbstractFileItemActionPlugin`, `KOverlayIconPlugin` | Andere Aufgabe (Menü-Actions, Embleme). Plugin-Views können diese Mechanismen aber über das Kontextmenü mitnutzen. |

Plugin-Namespace: **`whale/views`** **[W][U]**. Nie `dolphin/…`, damit es keine Kollision mit dem
System-Dolphin gibt (AGENTS.md).

---

## 9. Lifecycle und Ownership

### 9.1 Discovery

`WhaleViewRegistry` ist ein Singleton pro Prozess. Es arbeitet in drei Schritten:

1. Es ruft `KPluginMetaData::findPlugins("whale/views")` auf.
2. Es filtert nach `X-Whale-ViewApiVersion == WHALE_VIEW_API_VERSION`. Ablehnungen landen mit Grund
   in `qCWarning`.
3. Es nimmt die eingebaute Pseudo-View `org.kde.dolphin.itemview` (den Adapter) dazu.

Geladen wird lazy. Die Liste wird beim Start bzw. beim ersten Öffnen des Umschalters gelesen.
Aktivieren und Deaktivieren erfolgt über `whalerc [WhaleViews] enabledPlugins` mit **Plugin-IDs**,
nicht wie bei VCS über übersetzte Namen (bekannter Fehler dort, siehe `dolphin-architektur.md` 8.2).

### 9.2 Erzeugung

`KPluginFactory::instantiatePlugin<WhaleViewPlugin>(md, registry)` wird einmal pro Plugin aufgerufen
und gecacht. Danach gilt pro Wechsel und Container: `plugin->createView(contextImpl, host)` und
anschließend `view->widget()`. Der Host setzt das Widget in Stack-Seite 1.

### 9.3 Aktivierung, Deaktivierung und Wechsel

Ablauf bei einem Wechsel A → B in einem Container:

1. `A->saveState()` speichert in den Host-Speicher für den aktuellen Ort.
2. `A->deactivate()`.
3. Der Stack schaltet um.
4. `B->restoreState()` für den aktuellen Ort, falls vorhanden.
5. `B->activate()`.
6. Das Capability-Gating wird aktualisiert.
7. Der Fokus geht auf `B->widget()`.

Die Auswahl bleibt dabei automatisch erhalten (5.2).

Für das Ende der alten View gibt es zwei Varianten:

- **Empfehlung:** Die alte Plugin-Instanz wird nach `deactivate()` **zerstört**. Es gibt keinen Cache
  inaktiver Plugin-Views. Teure Daten wie Scan-Bäume liegen in Services mit eigenem Cache.
- Die `DolphinView` wird **nie** zerstört, weil sie dem Container gehört.

### 9.4 Zerstörung

Der Host besitzt die `WhaleView` (`unique_ptr`). Die View besitzt ihr Widget, das der Host lediglich
in den Stack hängt; Reihenfolge: aus dem Stack nehmen, dann die View löschen. Die Plugin-Instanz gehört
der Registry. **Bibliotheken werden nie entladen**, wie bei Qt üblich. Das vermeidet Abstürze durch
vtables in entladenem Code.

### 9.5 Ereignisse der Shell

| Ereignis | Wirkung |
|---|---|
| Neuer Ort | Bridge-Signale; zusätzlich holt die View `restoreState` aus dem History-Eintrag (9.6) |
| Split an/aus | Jeder Container hat seinen eigenen Host. Die aktive View pro Container ist unabhängig. |
| Tab schließen | Der Container stirbt und mit ihm Host und View |

### 9.6 Persistenz  [W][U]

| Ebene | Inhalt | Speicherort |
|---|---|---|
| History-Eintrag | aktive View-ID + `saveState()` der aktiven View | **Whale-Umschlag** um `locationState`: `magic "WHL1" + DolphinView-State + viewId + pluginState`. Ein Umschlag ohne `magic` wird als reiner Dolphin-State gelesen. Nur `saveViewState`/`tryRestoreViewState` im Container sind betroffen. |
| Session/Tab | aktive View-ID pro Container + Plugin-State | eigener Key `WhaleViews` in der Tab-Gruppe neben `State` (`DolphinTabWidget::saveProperties`). Das Format von `DolphinTabPage::saveState` bleibt unverändert. |
| Globale Plugin-Einstellungen | z. B. Farbschema der Treemap | `whalerc [WhaleView][<pluginId>]` über `context->viewConfig()` |
| Standard-View | welche View neue Container zeigen | `whalerc [WhaleViews] DefaultView` (Default: Dolphin-View) |
| View pro Ordner | (später) | **offene Entscheidung O3**. Nie in `[Dolphin]`-ViewProperties. Möglich ist eine eigene KConfig-Datei mit URL-Keys oder ein xattr `user.whale.view`. |

---

## 10. API- und Versionierungsprobleme binärer C++-Plugins

1. **ABI-Bruch durch vtable-Änderungen.** Neue virtuelle Funktionen verschieben die vtable. Dagegen
   helfen:
   - wenige virtuelle Methoden,
   - d-Pointer in allen exportierten Klassen,
   - Erweiterungen über `context->service(iid)` bzw. `qobject_cast` auf neue Interface-Klassen statt
     neuer Virtuals,
   - eine Reserve-Methode `virtual void virtual_hook(int id, void *data)`.
2. **Versionsprüfung *vor* dem Laden.** Die Registry prüft `X-Whale-ViewApiVersion` im JSON, ohne
   `dlopen`. Bis Whale die API stabil erklärt, gilt: **Die Version muss exakt passen, und Plugins werden
   im selben Baum gebaut**. Das ist ehrlicher als eine vorgetäuschte Stabilität.
3. **Keine Dolphin-Interna in der API.** `KFileItemModel`, `KItemSet`, `DolphinView` und kitemviews
   liegen in `libwhaleprivate` ohne ABI-Garantie, und upstream ändert sie ständig. Die API exportiert
   nur Qt-, KF6-Typen (`KFileItem`, `QUrl`, `KConfigGroup`) und eigene Typen mit d-Pointer.
4. **Eigene Bibliothek `libwhaleviewapi`** (SHARED, eigenes SOVERSION), nach dem Vorbild von
   `libdolphinvcs` + `DolphinVcsConfig.cmake.in`. Plugins linken nur gegen diese Bibliothek, nie gegen
   `libwhaleprivate`.
5. **Toolchain.** Qt-Hauptversion, KF6 und die C++-Standardbibliothek müssen übereinstimmen. Gegen
   Qt-Patch-Versionen hilft `QT_VERSION_CHECK`. KF6 ist ABI-stabil.
6. **Threading-Vertrag.** Alle `WhaleView`- und Context-Aufrufe laufen im GUI-Thread. Die Services
   arbeiten intern mit Threads (Scanner) und melden sich per queued Signal.
7. **Absturzisolation:** Es gibt keine. In-Process-Plugins können Whale abstürzen lassen. Das ist für
   native Plugins akzeptiert und wird dokumentiert.
8. **Signale über Bibliotheksgrenzen.** Neue Signale in exportierten Klassen sind ABI-verträglich.
   Signaturänderungen sind es nicht.

---

## 11. Verzeichnis-, Datei- und Klassenstruktur  [W]

```
src/whale/                                   eigenes CMakeLists.txt, 1× add_subdirectory
  api/                                       → libwhaleviewapi (SHARED)
    whaleviewapi_export.h                    (generiert)
    whaleviewapiversion.h                    WHALE_VIEW_API_VERSION
    whaleview.{h,cpp}
    whaleviewplugin.{h,cpp}
    whaleviewcontext.{h,cpp}                 abstrakt
    whaleviewcapabilities.h
    whaleselection.{h,cpp}
    whaleitemlist.{h,cpp}                    abstrakte Item-Fassade
    services/
      whalepreviewprovider.{h,cpp}           konkret, KIO-basiert
      whaletreescanner.{h,cpp}               konkret, + private/whaletreescannerworker
      whalefileoperations.{h,cpp}            abstrakt (Host implementiert)
  host/                                      → in dolphinstatic (App-intern)
    whaleviewhost.{h,cpp}
    whaleviewcontextimpl.{h,cpp}
    whaleviewregistry.{h,cpp}
    whaleviewactions.{h,cpp}                 Umschalter-Action, Capability-Gating
    dolphinviewbridge.{h,cpp}                EINZIGER Zugriff auf DolphinView-Interna
    dolphinviewadapter.{h,cpp}
    whaleitemlistimpl.{h,cpp}
    whalefileoperationsimpl.{h,cpp}
    whaleexplicitcontextmenu.{h,cpp}         T1-Menü für tiefe Items
  plugins/
    testlist/    whaletestlistview.{h,cpp}, whaletestlistview.json   (Namespace whale/views)
    treemap/     treemapview.{h,cpp}, treemaplayout.{h,cpp}, cushionrenderer.{h,cpp}, treemap.json
  tests/
    whaleviewregistrytest.cpp, dolphinviewbridgetest.cpp, whaleviewhosttest.cpp,
    whaletreescannertest.cpp, whaletreemaplayouttest.cpp
```

Alle neuen Dateien erhalten SPDX-Header und KDE-clang-format (CI). Die Strings werden mit KI18n
übersetzt, Domain `whale`/`dolphin` je nach Rename-Plan.

---

## 12. Gestufter Implementierungsplan

Jeder Meilenstein ist für sich baubar, testbar und committbar. Der übertragbare Kern und die
Whale-Integration kommen in getrennte Commits.

| # | Meilenstein | Dolphin-Dateien | Tests / Abnahme |
|---|---|---|---|
| **M0** | **Spike**, wegwerfbar, eigener Branch: Container mit Stack, versteckte `DolphinView`. Prüfen: Fokus/Split-Aktivierung (K5), externes Auslösen von `requestContextMenu` (3.2), RolesUpdater-Last einer versteckten View (K12), Auswahl über `setSelectedItems` | – | Messwerte und Ergebnis als Nachtrag in diesem Dokument; kein Merge |
| **M1** | `libwhaleviewapi`-Gerüst + `WhaleViewRegistry` + Plugin „Hello“ (zeigt ein Label) | `src/CMakeLists.txt` (1 Zeile) | `whaleviewregistrytest`: findet das Plugin über `QT_PLUGIN_PATH` im Build-Verzeichnis, lehnt eine falsche API-Version ab, instanziert es |
| **M2** | `WhaleViewHost` im Container, `DolphinViewAdapter`, Umschalter-Action, Fokus/Aktivierung | `dolphinviewcontainer.*`, `dolphinmainwindow.cpp`, `*.rc` | Umschalten Dolphin ⇄ Hello in Einzel- und Split-View; aktive Seite korrekt; `dolphinmainwindowtest` bleibt grün |
| **M3** | `DolphinViewBridge`: Ort, Laden und `WhaleItemList` (Range-Spiegelung) + Test-View (`QListView` über ein eigenes `QAbstractListModel` auf `WhaleItemList`) | optional Accessor in `dolphinview.h` (O2) | `dolphinviewbridgetest`: Items identisch zur `DolphinView` nach Laden, Erstellen, Löschen, Umbenennen, Filterleiste und Hidden-Toggle |
| **M4** | Auswahl-Spiegelung + Aktivierung (Doppelklick/Enter → Container-Slot) | `dolphinviewcontainer.cpp` | Auswahl bleibt beim Wechsel Test-View ⇄ Dolphin erhalten; Statusleiste und Info-Panel zeigen die Plugin-Auswahl; Ordner öffnet, Datei startet |
| **M5** | Kontextmenü, `requestItemInfo`, Capability-Gating (`HostOrder`, `Zoom`, `Previews`, `InlineRename`) | evtl. 1 Zeile in `dolphinmainwindow.cpp` | Rechtsklick in der Test-View zeigt das volle Dolphin-Menü; Papierkorb, Umbenennen (Dialog) und Kopieren wirken auf die richtigen Items; die Zoom-Actions sind deaktiviert |
| **M6** | Persistenz: History-Umschlag, Session-Key, globale Plugin-Konfiguration, Standard-View | `dolphinviewcontainer.cpp`, `dolphintabwidget.cpp` | Back/Forward stellt die View und ihren State wieder her; Neustart mit „Tabs merken“ stellt sie wieder her; alte Sessions ohne Whale-Daten laden fehlerfrei |
| **M7** | `WhalePreviewProvider` + Drag/Drop über `WhaleFileOperations` | – | Test-View zeigt Thumbnails; Drag aus der Test-View in die Dolphin-View (Split) und Drop in die Test-View kopieren bzw. verschieben korrekt |
| **M8** | `WhaleTreeScanner` (lokal, `fts`, Thread, Abbruch, inkrementell, Cache; `KIO`-Fallback später) | – | `whaletreescannertest` mit `QTemporaryDir`-Bäumen: Größen, Symlinks (nicht folgen), Abbruch, keine Grenzen von Dateisystemen überschreiten (`FTS_XDEV`) |
| **M9** | Treemap-Plugin: Squarified-Layout + Cushion-Rendering, Hover → Info-Panel, Doppelklick auf Ordner → `openUrl` oder internes Hineinzoomen (State), T1-Kontextmenü, eigener State (Wurzel, Tiefe, Farbmodus) | – | `whaletreemaplayouttest` (deterministisches Layout); manuelle Abnahme: keine Datei-Action trifft ein falsches Ziel |
| **M10** | *Optional:* T2 Command-Routing, falls T1 nicht reicht | `dolphinviewactionhandler.cpp`, `dolphinmainwindow.cpp`, `dolphintabpage.cpp` | Papierkorb, Kopieren usw. per Shortcut auf tiefen Treemap-Items |
| **M11** | *Optional, upstreamfähig:* RolesUpdater einer unsichtbaren View pausieren | `kitemviews/kfileitemlistview.cpp` | nur bei messbarer Last aus M0; als eigener, upstream-tauglicher Commit |

### 12.1 Abgleich mit den drei Testfällen

| Testfall | Ergebnis mit dieser Architektur |
|---|---|
| **1. Dolphin-View** | Code bleibt praktisch unverändert. Der Adapter zeigt nur die bestehende Instanz. Upstream-Fixes an `DolphinView`, kitemviews und Shell wirken direkt. Das Risiko konzentriert sich auf etwa 60 Zeilen im Container und auf `DolphinViewBridge`. |
| **2. Primitive Test-View** | Deckt Discovery, Laden, Items, Auswahl, Aktivierung, Kontextmenü, Wechsel und Persistenz ab (M1–M6). Sortierung, Filter und Hidden Files gibt es über `HostOrder` gratis. |
| **3. Treemap** | Wird nicht in Listenkonzepte gezwungen: keine Sortierung oder Gruppierung (Actions aus), keine Pflicht zu `items()`, eigener Scanner, eigener State, tiefe Auswahl über T1. Das ehrliche Limit: Standard-Shortcuts (Entf, F2, Strg+C) wirken auf tiefe Items erst mit T2. |

---

## 13. Offene Entscheidungen

| | Frage | Empfehlung |
|---|---|---|
| O1 | `WhaleView : QObject` + `widget()` oder `: QWidget`? | `QObject` + `widget()` |
| O2 | Modellzugriff: Accessor in `DolphinView` (2 Zeilen) oder `findChild` (0 Zeilen)? | Accessor **(umgesetzt in M3)** |
| O3 | Wo wird die View pro Ordner gespeichert (später)? | eigene Whale-KConfig mit URL-Keys; nie `[Dolphin]` |
| O4 | Umschalter: Icons/Compact/Details als drei Einträge neben den Plugins oder ein Eintrag „Dolphin“? | Ein Eintrag „Dateiliste (Dolphin)“; die Modi bleiben in der bestehenden `view_mode`-Action. Später ggf. visuell zusammenführen. |
| O5 | Tiefe Auswahl: T1 reicht vorerst? | Ja, T2 erst nach Praxis mit der Treemap |
| O6 | Plugin-API vorerst nur für Plugins im eigenen Baum (exakte Version)? | Ja |
| O7 | Soll M0 (Spike) wirklich vorgeschaltet werden? | Ja. Drei Annahmen (K5, 3.2, K12) sind kritisch und billig zu prüfen. |

---

## 14. Nachtrag: Ergebnisse des M0-Spikes (2026-10-04)

Branch `feature/view-plugins-m0-spike` (von `master` = `1c897bdfc`, inhaltlich gleich `33359dda2` plus
Doku-Commits). **Wegwerf-Code, kein Merge.** Umgebung: Qt 6.10.2, KF6, Ubuntu 26.04, Tests mit
`QT_QPA_PLATFORM=offscreen` (echte Fokus- und Aktivierungslogik von Qt, aber ohne Compositor).

### 14.1 Aufbau

- `DolphinViewContainer`: `QStackedWidget` an der Layout-Position der View. Seite 0 ist die
  unveränderte `DolphinView`, Seite 1 ein `QListWidget` als Ersatz-View. Schalter (nur Spike):
  - `spikeSetAlternativeShown(bool)`: Ersatz-View zeigen, `DolphinView` läuft versteckt weiter.
  - `spikeSetFocusRedirect(bool)`: Host-artige Umleitung. `FocusIn` der Ersatz-View → `activate()`;
    `setActive(true)` und `requestFocus()` fokussieren das sichtbare Widget.
  - `spikeSetDisableHiddenView(bool)`: versteckte `DolphinView` zusätzlich `setEnabled(false)`.
  - Env `WHALE_M0_SPIKE_ALT=1` / `WHALE_M0_SPIKE_REDIRECT=1` für manuelles Ausprobieren in der App.
- `src/tests/whalem0spiketest.cpp`: Mess- und Sondierungsprogramm (kein ctest-Test), Aufruf
  `QT_QPA_PLATFORM=offscreen bin/whalem0spiketest [Funktion]`. Modellzugriff über
  `findChild<KItemListContainer *>(…, Qt::FindDirectChildrenOnly)` (Weg (a) aus K4), funktioniert.

### 14.2 Annahme 1 – Versteckte `DolphinView`, Fokus, Split-Aktivierung (K5)

**Ergebnis: bestätigt mit Einschränkungen.** Der Host kann Fokus und Aktivierung zuverlässig auf das
sichtbare Widget leiten, aber nur mit *beiden* Maßnahmen (Umleitung + versteckte View deaktivieren).
Die Annahme „versteckte Widgets erhalten keinen Fokus“ aus K5 ist **falsch**.

Szenario: Split-View, links Ersatz-View (DolphinView versteckt), rechts normale DolphinView.

| Schritt | naiv (nur Stack) | + Umleitung | + Umleitung + `setEnabled(false)` |
|---|---|---|---|
| A Klick in linke Ersatz-View (rechts aktiv) | Fokus links, **links bleibt inaktiv** | links aktiv ✔ | links aktiv ✔ |
| B Klick in rechte View | rechts aktiv ✔ | rechts aktiv ✔ | rechts aktiv ✔ |
| C `switchActiveView` (Tastatur) → links | links aktiv, **Fokus in versteckter View** | Fokus Ersatz-View ✔ | Fokus Ersatz-View ✔ |
| D `requestFocus()` (Filterleiste u. a.) | **Fokus in versteckter View** | ✔ | ✔ |
| E Navigation links (`setUrl`, verzögertes `requestFocus`) | **Fokus in versteckter View** | ✔ | ✔ |
| F zurück zur DolphinView (links aktiv) | ✔ (Fokus in DolphinView) | ✔ | ✔ |
| G zur Ersatz-View, während DolphinView fokussiert | ✔ (`QStackedWidget` übergibt Fokus) | ✔ | ✔ |
| I `setFocus()` auf versteckten `KItemListContainer`, rechts aktiv | **versteckte View bekommt Fokus + `FocusIn` → links wird aktiv** | **links wird aktiv** (Fokus danach Ersatz-View) | **keine Wirkung** ✔ |
| L Aktion „Umbenennen“ (F2), 1 Item ausgewählt | **Inline-Editor in versteckter View hat Fokus** | **dito** | Fokus bleibt in Ersatz-View; Editor wird unsichtbar erzeugt |
| `FocusIn` auf versteckter View (gesamt) | 4 | 6 (transient bei jeder Aktivierung) | 0 (nur wenn sichtbar) |

Beobachtungen:

1. Qt 6.10: `QWidget::setFocus()` auf ein verstecktes, aber aktiviertes Widget im aktiven Fenster
   macht es zu `QApplication::focusWidget()` und liefert `FocusIn`. Tastatureingaben landen dann in
   der unsichtbaren View.
2. Über `DolphinView::eventFilter` (`FocusIn` → `setActive(true)` → `activated`) wird daraus eine
   **Aktivierung der Split-Hälfte**. Jeder `setFocus()`-Aufruf auf die versteckte View ist damit ein
   versteckter Aktivierungs-Trigger. Bekannte Quellen: `DolphinView::setActive`
   (`m_container->setFocus()`) und die vier `m_view->setFocus()`-Stellen im Container (Lambda
   `requestToLoseFocus` in `connectUrlNavigator`, Ende des Selection-Mode, `closeFilterBar`,
   `requestFocus`). `requestFocus` wird zusätzlich indirekt bei jedem URL-Wechsel aufgerufen
   (`slotUrlNavigatorLocationChanged`, verzögert) und von der Filterleiste (`focusViewRequest`).
3. `setEnabled(false)` auf die versteckte `DolphinView` macht `setFocus()` zum No-op (Qt prüft
   `isEnabled()`). Das schließt alle Quellen auf einmal, auch unbekannte und upstream neu
   hinzukommende. Programmatische Nutzung (Laden, Modell, Auswahl, Actions, Signale) funktioniert
   weiterhin. Getestet: Alle Prüfungen aus 14.3 und 14.5 laufen auch mit deaktivierter View.

   > **In M2 korrigiert (14.9):** Diese Maßnahme kollidiert mit den acht direkten
   > `view()->setFocus()`-Aufrufen der Shell, die M0 nicht betrachtet hat. Ersetzt durch:
   > „DolphinView bleibt als Shell-Fokusziel aktiv; ihre interaktiven Kinder werden stillgelegt und ihr Fokus über den WhaleViewHost auf die sichtbare View umgeleitet.“
4. `QStackedWidget` überträgt den Fokus beim Seitenwechsel selbst, wenn er auf der alten Seite lag.
5. F2 bzw. Inline-Rename auf der versteckten View erzeugt einen unsichtbaren Editor. Das
   Capability-Gating `InlineRename` (→ `KIO::RenameFileDialog`) ist daher **Pflicht**, nicht optional.
6. Die Datei-Actions (Entf, F2, Strg+C …) sind fensterweite Shortcuts und wirken unabhängig vom Fokus
   auf `view()->selectedItems()`. Das ist wie geplant (gespiegelte Auswahl).

### 14.3 Annahme 2 – `DolphinView`-Signale von außen auslösen (3.2)

**Ergebnis: bestätigt mit Einschränkungen.** Bei versteckter View (auch deaktiviert) getestet:

| Signal | Ergebnis |
|---|---|
| `requestContextMenu(pos, item, {item}, url)` | volles Dolphin-Item-Menü (20 Einträge: Öffnen mit, Ausschneiden, Kopieren, Umbenennen, Papierkorb, Service-Menüs, Eigenschaften …) ✔ |
| `requestContextMenu(pos, KFileItem(), {}, url)` | Viewport-Menü (Neu, Sortierung, Ansichtsmodus, Terminal …) ✔ |
| `requestItemInfo(item)` | kommt unverändert bei `DolphinMainWindow::requestItemInfo` an ✔ (2× ausgelöst, weil das Hauptfenster schon heute doppelt verbunden ist; unabhängig vom Auslöser) |
| `tabRequested(url)` | Hintergrund-Tab mit der URL ✔ |
| `itemActivated(dirItem)` | Container navigiert hinein ✔ |
| `goUpRequested()` | aktiver Container geht nach oben ✔ |

- Die Parameter werden unverändert durchgereicht. `sender()` ist die `DolphinView`, auch bei Emission
  von außen. `DolphinTabPage::slotViewActivated` ist der einzige relevante `sender()`-Nutzer und
  funktioniert deshalb.
- **Einschränkung 1:** `DolphinMainWindow::connectViewSignals` verbindet nur die View der **aktiven**
  Hälfte (außer `requestItemInfo`). Ein `requestContextMenu` der inaktiven Hälfte bewirkt nichts
  (gemessen: kein Menü). Die Bridge muss den Container vorher aktivieren (`setActive(true)`). Das passt
  ohnehin, weil ein Rechtsklick in die Plugin-View `FocusIn` und damit Aktivierung auslöst.
- **Einschränkung 2:** `goUp/Back/ForwardRequested`, `urlActivated` und `tabRequested` wirken auf den
  *aktiven* Container bzw. das Hauptfenster, nicht auf den Sender. Gleiche Konsequenz.
- **Gefährlich: Zustandssignale.** `Q_EMIT view->activated()` von außen lässt `DolphinView::m_active`
  unverändert. Im Test blieb der Zustand nur zufällig konsistent, weil nachgelagerter Code den Fokus
  setzt. Regel für die Bridge: **Zustandsänderungen nur über Methoden** (`setActive`,
  Selection-Manager), **nur „Anfrage“-Signale emittieren** (`requestContextMenu`, `requestItemInfo`,
  `tabRequested`, `activeTabRequested`, `windowRequested`, `goUp/Back/ForwardRequested`,
  `urlActivated`). `itemActivated` wie geplant direkt an den Container-Slot.
- **Bekannt (K11), bestätigt:** Die Standard-Actions im Menü (Umbenennen, Papierkorb …) wirken auf die
  Auswahl der `DolphinView`, nicht auf die übergebene Liste. Mit leerer Auswahl sind sie trotzdem
  aktiviert. Die Auswahl muss vor dem Menü gespiegelt sein (M4/M5), und für tiefe Items bleibt T1
  zwingend.

### 14.4 Annahme 3 – Kosten der unsichtbaren `DolphinView` (K12)

**Ergebnis: bestätigt mit Einschränkungen.** Die Kosten sind für den Alltag plausibel, aber nicht
null: Die versteckte View verrichtet beim Laden eines Ordners **dieselbe** Arbeit wie die sichtbare.
Im Leerlauf kostet sie nichts.

Messung: Hauptfenster 1200×800, je Zeile ein frischer Ordner (kein Thumbnail- und kein
KDirLister-Cache), Ende = 2 s ohne Rollen-Änderung am Modell. CPU = eigener Prozess; die
Thumbnail-Erzeugung lief messbar im Prozess (Kind-Prozess-CPU ≈ 0). `hiddenFromStart` = Ersatz-View
vor dem ersten Anzeigen (Plugin als Standard-View), `hiddenAfterVisible` = erst sichtbar, dann
umgeschaltet, dann navigiert.

| Ordner | Zustand | Previews | View-Größe | Dauer bis Ruhe | CPU | Previews erzeugt | Ordnergrößen gezählt | Timer-Events | Leerlauf 3 s |
|---|---|---|---|---|---|---|---|---|---|
| normal (40 txt, 10 png, 10 Ordner) | sichtbar | an | 1063×733 | 533 ms | 159 ms¹ | 60 | – | 357 | 7 ms, 0 Timer |
| | hiddenFromStart | an | **100×30** | 373 ms | 61 ms | 60 | – | 227 | 7 ms, 0 |
| | hiddenAfterVisible | an | 1063×737 | 173 ms | 50 ms | 60 | – | 185 | 7 ms, 0 |
| | sichtbar / hidden | aus | | 125–325 ms | 28–49 ms | 0 | – | 41–75 | 7 ms, 0 |
| 2000 PNG | sichtbar | an | 1063×733 | 1554 ms | 795 ms | 500 | – | 1876 | 8 ms, 0 |
| | hiddenFromStart | an | 100×30 | 1602 ms | 748 ms | 500 | – | 1823 | 7 ms, 0 |
| | hiddenAfterVisible | an | 1063×737 | 1399 ms | 759 ms | 500 | – | 1806 | 7 ms, 0 |
| | sichtbar | aus | | 339 ms | 132 ms | 0 | – | 58 | 6 ms, 0 |
| | hidden (beide) | aus | | 86–340 ms | 77–89 ms | 0 | – | 40–80 | 7–8 ms, 0 |
| 300 Ordner à 20 Dateien, Details | sichtbar | an | | 1741 ms | 421 ms | 300 Ordner-Icons | **300** | 1256 | 8 ms, 0 |
| | hiddenFromStart | an | 100×30 | 1645 ms | 371 ms | 300 | **300** | 1273 | 8 ms, 0 |
| | hiddenAfterVisible | an | | 1488 ms | 361 ms | 300 | **300** | 1231 | 7 ms, 0 |

¹ erster Lauf im Prozess, enthält Start-Overhead (Plugin-Laden, Worker-Start).

Beobachtungen:

1. **Unsichtbarkeit stoppt nichts automatisch.** `KFileItemModelRolesUpdater` ist nie pausiert;
   `DolphinView::hideEvent` versteckt nur den Tooltip.
2. Die Arbeit ist **nach oben begrenzt** durch `ResolveAllItemsLimit = 500`
   (`kfileitemmodelrolesupdater.cpp:56`). Auch eine nie angezeigte View (100×30, keine sichtbaren
   Items) löst Rollen und Previews für bis zu 500 Items auf. Bei kleinen Ordnern sind das alle Items.
3. Ordnergrößen (`KDirectoryContentsCounter`) laufen in der versteckten Details-View vollständig mit.
4. Die Kosten fallen **pro Ordnerwechsel** an, maximal etwa 0,8 s CPU und 1,6 s Laufzeit bei 2000
   Bildern. Danach herrscht Ruhe: im Leerlauf 0 Timer-Events und etwa 2 ms CPU pro Sekunde, gleich
   wie bei sichtbarer View.
5. **Doppelte Arbeit** entsteht nur, wenn die Plugin-View selbst Previews oder Ordnergrößen erzeugt.
   Die Previews teilen sich den KIO-Thumbnail-Disk-Cache (zweiter Zugriff = Cache-Treffer). Bei einer
   Treemap ist das Hauptduplikat nicht der RolesUpdater, sondern ihr eigener rekursiver Scan.
6. Grenzen der Messung: offscreen-Plattform, ein Lauf pro Zeile, lokales SSD-Dateisystem; Remote-Orte
   (sftp, smb) nicht gemessen. Dort ist die versteckte Preview-Arbeit teurer (Netzwerk).

### 14.5 Zusatzprüfungen (versteckte View, auch deaktiviert)

- **Laden:** Navigation mit versteckter View lädt normal; `directoryLoadingCompleted` und Items sind
  korrekt ✔.
- **Hidden Files** (`setHiddenFilesShown`), **Namensfilter** (Container-Slot `setNameFilter`, also der
  Filterleisten-Pfad), **Sortierung** (`setSortOrder`) wirken sofort auf `items()` ✔.
- **Auswahl:** `KItemListSelectionManager::setSelectedItems` und `setCurrentItem` sind sofort über
  `selectedItems()` lesbar. Das entprellte `DolphinView::selectionChanged` und
  `DolphinMainWindow::selectionChanged` kommen an. „Alles auswählen“ wirkt auf die versteckte View. Die
  Auswahl überlebt das Umsortieren (URL-basiert) ✔.

### 14.6 Geänderte Dateien (nur Spike-Branch)

| Datei | Änderung |
|---|---|
| `src/dolphinviewcontainer.h` | +21 Zeilen: Spike-Methoden und -Member, Markierung `WHALE-M0-SPIKE` |
| `src/dolphinviewcontainer.cpp` | +71/−1: Stack statt direkter View im Layout; Umleitung in `setActive`, `requestFocus`, `eventFilter`; Spike-Methoden |
| `src/tests/CMakeLists.txt` | +4: eigenes Executable `whalem0spiketest` (nicht in ctest) |
| `src/tests/whalem0spiketest.cpp` | neu (875 Zeilen): Mess- und Sondierungsprogramm |

Regression: `dolphinmainwindowtest` mit und ohne Spike identisch (47 bestanden, 1 fehlgeschlagen).
Der Fehler `testAccessibilityTree` tritt unter `offscreen` auch auf `master` auf. Der ctest-Lint
`no_bare_qwait_in_tests` schlägt wegen der `qWait`s im Spike-Programm an; das ist für Wegwerf-Code
akzeptiert.

### 14.7 Bewertung der Architektur

**Die Architektur mit dauerhaft vorhandener, versteckter `DolphinView` bleibt tragfähig.**

- Die Shell arbeitet ohne Änderung mit derselben `DolphinView`. Kontextmenü, Info-Panel, Tabs,
  Navigation, Filter, Sortierung und Auswahl funktionieren mit versteckter (und deaktivierter) View.
- Das Fokusproblem ist real, aber mit zwei kleinen, lokalen Maßnahmen im Host vollständig beherrschbar
  (in M2 präzisiert, siehe 14.9).
- Die Kosten sind begrenzt (≤ 500 Items pro Ordnerwechsel) und im Leerlauf null. Für den Alltag ist
  das plausibel; für große Bilderordner oder Remote-Orte lohnt sich M11 später.

### 14.8 Empfohlene Änderungen am Plan

1. **K5 / M2 – Fokus:** Der `WhaleViewHost` macht beim Umschalten auf eine Plugin-View **beides**:
   - ~~`dolphinView->setEnabled(false)`, beim Zurückschalten `true`.~~ **In M2 ersetzt (14.9):**
     „DolphinView bleibt als Shell-Fokusziel aktiv; ihre interaktiven Kinder werden stillgelegt und ihr Fokus über den WhaleViewHost auf die sichtbare View umgeleitet.“
   - Umleitung: `FocusIn` des Plugin-Widgets (Eventfilter, rekursiv für Kind-Widgets oder über
     `QApplication::focusChanged`) → `container->activate()`; `setActive(true)` und `requestFocus`
     fokussieren `currentWidget()`.
   Durch den umgeleiteten Focus-Proxy landen alle `m_view->setFocus()`-Stellen (Container und Shell)
   automatisch beim Plugin; der Container leitet nur `setActive` an den Host weiter. Der Umfang in `dolphinviewcontainer.cpp` sinkt damit eher
   (Spike: etwa 15 Zeilen Logik).
2. **M2 – Test:** Die Szenarien A–L aus 14.2 werden zu `whaleviewhosttest`. Wichtig sind I (kein
   Fokus und keine Aktivierung über die versteckte View) und L (F2).
3. **M5 – Gating:** `InlineRename` ist Pflicht-Gating. Ohne Capability wird
   `KIO::RenameFileDialog` erzwungen, sonst entsteht ein unsichtbarer Editor. Prüfen, ob weitere
   View-interne UI ebenfalls in der versteckten View aufgeht (Selection-Mode-Leisten liegen im
   Container und sind sichtbar; der Platzhalter „Ordner ist leer“ liegt in der View und ist unsichtbar,
   das ist gewollt).
4. **3.2 – Bridge-Regeln präzisieren:** nur Anfrage-Signale emittieren, nie Zustandssignale
   (`activated`, `selectionChanged`, `urlChanged` …). Vor jedem Emit den Container aktivieren, weil das
   Hauptfenster nur die aktive View verbindet.
5. **K12 / M11:** Die Priorität bleibt „optional“. Die Begründung wird ergänzt: Die versteckte View
   verursacht pro Ordnerwechsel bis zu 500 Previews und alle Ordnergrößen. Der kleinste sinnvolle
   Eingriff wäre ohne Upstream-Änderung möglich: Die Bridge schaltet bei aktiver Plugin-View die
   Previews der versteckten View ab. Das geht aber nicht über `setPreviewsShown()`, weil das die
   `[Dolphin]`-ViewProperties schreibt. Deshalb besser upstreamfähig: RolesUpdater pausieren, wenn die
   View nicht sichtbar ist (M11).
6. **O2 (Modellzugriff):** `findChild` hat im Spike problemlos funktioniert. Die Empfehlung „Accessor“
   bleibt aus den genannten Gründen bestehen; `findChild` ist ein brauchbarer Fallback.
7. **7.1:** Die Zeile zu `dolphinviewcontainer.cpp` ergänzen: Die „4× `setFocus`“ müssen nicht einzeln
   umgeleitet werden. In M2 genügt der Focus-Proxy der `DolphinView` (14.9); nur `setActive` geht an den Host.

### 14.9 Korrektur aus M2: Focus-Proxy statt deaktivierter `DolphinView` (2026-10-04)

**Neue Regel:** „DolphinView bleibt als Shell-Fokusziel aktiv; ihre interaktiven Kinder werden stillgelegt und ihr Fokus über den WhaleViewHost auf die sichtbare View umgeleitet.“

Befund: Neben den vier Stellen im Container ruft die Shell an **acht** Stellen direkt
`view()->setFocus()` auf:

- `dolphinmainwindow.cpp`: `showEvent`, `replaceLocation`, `slotTerminalPanelVisibilityChanged`,
  `slotPlacesPanelVisibilityChanged`, `slotPlaceActivated`, `toggleTerminalPanelFocus`,
  `togglePlacesPanelFocus`
- `dolphintabwidget.cpp`: `openNewActivatedTab`

Auf einer per `setEnabled(false)` deaktivierten `DolphinView` bricht Qt `setFocus()` sofort ab. Der Fokus
erreicht dann das Plugin nicht. Beim Ausblenden eines fokussierten Panels wandert er nach Qt-Regel ins
nächste Widget, eventuell in die andere Split-Hälfte, die dadurch aktiv wird.

Umsetzung im `WhaleViewHost`:

1. Die `DolphinView` bleibt aktiviert und versteckt.
2. Ihre direkten Kind-Widgets (u. a. `KItemListContainer`) werden deaktiviert. Interne `setFocus()`-Aufrufe
   (`DolphinView::setActive`) und ein Inline-Editor können den Fokus so nicht übernehmen.
3. Ihr Focus-Proxy zeigt auf das Plugin-Widget. Alle `view()->setFocus()`-Aufrufe der Shell und des
   Containers landen damit ohne Änderung an MainWindow/TabWidget beim Plugin.
4. Beim Zurückwechseln werden Kinder und ursprünglicher Focus-Proxy exakt wiederhergestellt.

Wichtige Qt-Eigenheit (Gegenprobe in `whaleviewhosttest`): Ohne die Proxy-Umleitung setzt
`view()->setFocus()` den Fokus sogar auf den **deaktivierten** versteckten `KItemListContainer`, weil
Qt nur prüft, ob das aufgerufene Widget aktiviert ist, nicht das Proxy-Ziel. Die Umleitung ist also
zwingend nötig.

F2: Der Schutz prüft nicht `isEnabled()`, sondern ob die `DolphinView` die sichtbare View ist
(`isVisibleTo(window())`).

### 14.10 Ergebnisse aus M3: Bridge, `WhaleItemList`, Test-Liste (2026-10-05)

**Modellzugriff (O2):** `DolphinView::itemListContainer()` als Inline-Accessor im Header
(`views/dolphinview.h`, Whale-Block, 6 Zeilen, keine `.cpp`-Änderung). Einziger Produktivnutzer ist
`DolphinViewBridge`. Die Bridge holt das Modell über `controller()->model()` und folgt
`KItemListController::modelChanged`, obwohl `DolphinView` das Modell heute nie austauscht.

**Aufbau:**

- `DolphinViewBridge` (Host, eine pro `WhaleViewHost`): `url()`, Signale `urlChanged`,
  `loadingStarted/Completed/Canceled` (von den `DolphinView`-Signalen) und
  `createItemList(parent)`. Die gespiegelte Liste (`MirroredItemList`, intern in
  `dolphinviewbridge.cpp`) ist die einzige Stelle mit `kitemviews`-Includes.
- `WhaleViewContextImpl` kennt nur noch die Bridge, nicht mehr `DolphinView`.
  `items()` legt die Liste **beim ersten Aufruf** an, als Kind des Contexts. Der Context der
  `DolphinView` selbst ruft `items()` nie auf, eine Spiegelung entsteht also nur für Plugin-Views und
  endet mit deren Context (Qt-Ownership, keine hängenden Verbindungen).

**Tatsächliche `WhaleItemList`-API (Abweichung von 4.2):**

| Signal | Bedeutung |
|---|---|
| `itemsReset()` | ganzer Inhalt ersetzt, neu lesen |
| `itemsInserted(int index, KFileItemList)` | neue Items ab `index` |
| `itemsRemoved(int index, KFileItemList)` | entfernte Items, die ab `index` standen (mit Index, nicht nur Liste) |
| `itemsMoved(int index, QList<int> movedToIndexes)` | Permutation des Bereichs ab `index`, absolute Zielindizes |
| `itemsChanged(int index, KFileItemList)` | Items ab `index` durch geänderte Fassungen ersetzt |

Lesend: `count()`, `isEmpty()`, `item(int)`, `items()`. Jeder Schritt wird **nach** der Änderung
gemeldet, Indizes beziehen sich auf die Liste direkt davor. Mehrere Model-Ranges werden zu mehreren
Einzelschritten. Grund für den Index bei `itemsRemoved`: Ein Plugin-Modell (`QAbstractListModel`)
braucht ihn für `beginRemoveRows`. Die Modifikatoren sind `protected`; nur der Host füllt Listen.

**Range-Spiegelung und eine wichtige Vereinfachung:** Insert, Remove und Move werden inkrementell
gespiegelt (Insert-/Remove-Ranges beziehen sich auf das Modell *vor* der Änderung, Move liefert
absolute Zielindizes). Befund aus den Tests: Die Liste wird **nach** `DolphinView`/RolesUpdater mit dem
Modell verbunden. `KFileItemModelRolesUpdater::slotItemsInserted` und `slotItemsMoved` rufen
synchron `setData()` auf. Dadurch erreicht ein `itemsChanged` die Liste, **bevor** sie das auslösende
`itemsInserted`/`itemsMoved` gesehen hat, und dessen Indizes beziehen sich schon auf den neuen Stand.
Regel der Bridge:

- `itemsChanged` meldet nur Items, deren `KFileItem` sich wirklich unterscheidet (`KFileItem::cmp`).
  Reine Rollenänderungen (Previews, Ordnergrößen) erzeugen kein Signal.
- Stimmt die Anzahl nicht (Insert/Remove ausstehend) oder steht am Index ein anderes Item, das noch im
  Modell ist (Move ausstehend), wird die Änderung übersprungen. Solche verschachtelten Änderungen
  betreffen nur Rollen, und der ausstehende Schritt liest seine Items ohnehin aus dem Modell.
- Steht am Index ein anderes Item, das **nicht** mehr im Modell ist, wurde es dort ersetzt. So meldet
  das Modell ein Umbenennen.
- Passt ein Schritt sonst nicht (Bereichsprüfung, Permutation, Anzahl, URL-Abgleich nach Move), lädt
  die Liste alles neu (`itemsReset`) und warnt. Die Tests schlagen bei dieser Warnung fehl; in allen
  Testszenarien tritt kein Reset auf.

**Test-View:** Plugin `whale_testlist` („Test List“): `QListView` über ein eigenes
`QAbstractListModel`, das eine Kopie der Items führt (die Liste meldet nachträglich, ein Qt-Modell
muss vorher ankündigen). Anzeige: Name und Ordner-/Datei-Icon. Capability `HostOrder`. Keine eigene
Sortierung. Bewusst ohne Auswahl (`NoSelection`), ohne Editieren, Drag/Drop, Aktivierung und
Kontextmenü (`Qt::PreventContextMenu`, auch nicht an den Parent weitergereicht).

**Verhalten:** Sortierung, Hidden Files, Namensfilter und Ordnerwechsel kommen ohne Plugin-Logik
über die Host-Reihenfolge. Im Details-Modus aufgeklappte Unterordner stehen als flache Einträge mit in
der Liste, weil die Liste exakt das Modell spiegelt (`testExpandedFolders`).

**Tests:** `dolphinviewbridgetest` (16 Fälle: Laden, Erstellen, Löschen, Umbenennen per KIO und extern,
Änderung, Sortierung, Hidden, Filter, Navigation, aufgeklappte Ordner, Lebensdauer, Test-View,
Read-only). Löschen läuft im Test über `KIO::del`, weil KDirWatch Löschungen im Test nicht zuverlässig
meldet (Dolphins eigene Tests rufen dafür `updateDirectory()` auf). Die M2-Fokus- und Split-Tests laufen
zusätzlich mit der Test-Liste. Testisolation: Whale nutzt standardmäßig gemeinsame
Ansichtseinstellungen, deshalb setzt der Test Sortierung/Hidden in `init()` zurück.

**Offene Risiken für M4/M5:**

1. **Unverändert (aus M2):** Im Plugin-Modus wirken Entf, Umschalt+Entf, Strg+X/C/V und weitere
   Datei-Shortcuts auf die **unsichtbare Auswahl der `DolphinView`**. M3 ändert daran nichts und
   kaschiert es nicht. Die Test-Liste zeigt deshalb absichtlich keine eigene Auswahl, damit nichts wie
   ein Ziel der Datei-Actions aussieht. Lösung: Auswahl-Spiegelung (M4) und Capability-Gating (M5).
2. Für M4 fehlt der Liste ein schneller URL→Index-Zugriff (heute linear über `items()`); erst mit
   der Auswahl-Spiegelung einbauen.
3. Die API-Version bleibt 1, obwohl `WhaleViewContext` eine neue pure-virtuelle Methode hat. Das ist
   unkritisch, solange es nur In-Tree-Plugins gibt (O6). Vor einer Veröffentlichung oder bei
   installierten Fremd-Plugins muss jede ABI-Änderung die Version erhöhen.
4. Die Robustheitsregel für verschachtelte `itemsChanged` beruht darauf, dass nur
   `setData()`-Rollenänderungen verschachtelt ankommen. Ändert upstream das (z. B. synchrones Resort
   statt Timer), greift der Reset-Fallback mit Warnung; dann die Bridge anpassen.
5. Aufgeklappte Details-Ordner erscheinen flach in `HostOrder`-Views. Für eine Baum-Darstellung bräuchte
   die API später Tiefe/Eltern (wie Gruppen, vgl. 7).
6. Vorbestehend, nicht durch M3: `filenamehighlightingviewtest` stürzt auf der echten
   Wayland-Plattform in `KItemListViewAccessible::slotAnnounceCurrentItemTimerTimeout` ab (gleich im
   M2-Stand `7997f1893` geprüft). Unter `offscreen` ist er grün.

**Abnahme (2026-10-05):** Statt manuell über das Dev-MCP (`.privatedoc/whale-dev-mcp.md`):
`test_m3_acceptance.py` mit 20 Schritten grün unter offscreen und auf echtem Plasma/Wayland, jeweils
mit echtem Tastaturfokus und ohne Fokus-Anomalien.
