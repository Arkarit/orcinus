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

**Umgesetzt in M5 mit präzisierter Semantik, siehe 14.12.**

### 4.6 Allgemeine Services  [W]

| Service | Basis (wiederverwendet) | Neu |
|---|---|---|
| `WhaleItemList` | `KFileItemModel` über die Bridge | Fassade, URL-Index |
| `WhalePreviewProvider` | `KIO::PreviewJob`, `PreviewSettings`-Plugins, KIO-Thumbnail-Cache | Anfragewarteschlange, Prioritäten, Abbruch |
| `WhaleTreeScanner` | `fts`-Muster aus `KDirectoryContentsCounterWorker`; `KIO::listRecursive` als Fallback für Remote-Orte | Baum mit Größen, inkrementelle Signale, Abbruch, Cache pro Wurzel, `QThread`-Worker |
| `WhaleFileOperations` | `KIO::DeleteOrTrashJob`, `KIO::paste`, `KIO::copy/move/copyAs`, `KIO::RenameFileDialog`, `KPropertiesDialog`, `KIO::FileUndoManager`, `DragAndDropHelper::dropUrls`, `KUrlMimeData` | arbeitet auf **expliziten** `KFileItemList`s (wichtig für die tiefe Auswahl) |
| Kontextmenü | `DolphinContextMenu` über `requestContextMenu` | — |
| Metadaten | `KFileItem`, `KFileMetaData` | zunächst kein eigener Service |

**Umgesetzt in M7:** `WhalePreviewProvider` und der Drag-&-Drop-Teil von `WhaleFileOperations`, siehe
14.14. Die übrigen Dateioperationen (explizite `KFileItemList`s für T1) fehlen noch.

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
  - Eine Rückkopplung wird mit einem Herkunfts-Flag und Vergleich verhindert. **(Umgesetzt in M4, siehe
  14.11: Vergleich nach URL in der Bridge, Echo-Unterdrückung pro Context.)**
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
| **T1** – View-lokale Auswahl + Kontextmenü mit expliziter Liste **(umgesetzt, siehe 14.18)** | Die Treemap hält ihre Auswahl selbst und meldet sie dem Host als `WhaleSelection` mit dem Flag „tief“. Der Host spiegelt nur den Anteil im Listing in die `DolphinView`. Rechtsklick → Whale baut ein **eigenes, reduziertes** Menü: `KFileItemActions` (Öffnen mit, Service-Menüs) + `WhaleFileOperations` auf der expliziten Liste (Papierkorb, Löschen, Kopieren, Pfad kopieren, Eigenschaften, „Im Ordner anzeigen“ = Host navigiert und markiert). Tastatur-Shortcuts der Standard-Actions sind in der Treemap deaktiviert (Capability `RecursiveContent` ⇒ Gating der Datei-Actions). | keine | **Empfohlen für die erste Treemap-Version.** Ist sicher, weil keine Aktion das falsche Ziel trifft. |
| **T2** – Command-Routing **(umgesetzt, siehe 14.19)** | Die Datei-Slots in `DolphinViewActionHandler` (Rename, Trash, Delete, Duplicate, Properties, CopyPath: 6 Slots) und in `DolphinMainWindow` (Cut, Copy, Paste, `updateFileAndEditActions`, `slotSelectionChanged`, Compare, Add-to-Places …: etwa 12 Stellen) fragen eine Hilfsfunktion `activeSelection()` bzw. `activeFileOperations()` statt `view()->…` | etwa 20–30 einzeilige Änderungen in 2–3 viel geänderten Dateien | Erst entscheiden, wenn T1 im Alltag nicht reicht. Mechanisch, aber merge-relevant. |
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

**Umgesetzt in M6 (History, Session); tatsächliches Format siehe 14.13.**

| Ebene | Inhalt | Speicherort |
|---|---|---|
| History-Eintrag | aktive View-ID + `saveState()` der aktiven View | **Whale-Umschlag** um `locationState`: `magic "WHL1" + DolphinView-State + viewId + pluginState`. Ein Umschlag ohne `magic` wird als reiner Dolphin-State gelesen. Nur `saveViewState`/`tryRestoreViewState` im Container sind betroffen. |
| Session/Tab | aktive View-ID pro Container + Plugin-State | eigener Key `WhaleViews` in der Tab-Gruppe neben `State` (`DolphinTabWidget::saveProperties`). Das Format von `DolphinTabPage::saveState` bleibt unverändert. |
| Globale Plugin-Einstellungen | z. B. Farbschema der Treemap | `whalerc [WhaleView][<pluginId>]` über `context->viewConfig()` |
| Standard-View | welche View neue Container zeigen | `whalerc [WhaleViews] DefaultView` (Default: Dolphin-View) |
| View pro Ordner | nur die View-ID (Dateiliste oder Plugin), **umgesetzt in O3** | `$XDG_STATE_HOME/whaleviewpreferencesrc [PreferredViews]` (Hash des Location-Keys → View-ID + Key), siehe 14.20. Nie in `[Dolphin]`-ViewProperties, kein xattr. |

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
| **M9** | Treemap-Plugin: Squarified-Layout + Cushion-Rendering, Hover → Info-Panel, Doppelklick auf Ordner → `openUrl` oder internes Hineinzoomen (State), T1-Kontextmenü, eigener State (Wurzel, Tiefe, Farbmodus) **(umgesetzt ohne T1-Kontextmenü, siehe 14.17)** | – | `whaletreemaplayouttest` (deterministisches Layout); manuelle Abnahme: keine Datei-Action trifft ein falsches Ziel |
| **T1** | Operationen auf tiefen Einträgen: `WhaleFileOperations` mit expliziten URLs (API 7), Treemap-Kontextmenü, Rescan nach Mutation **(umgesetzt, siehe 14.18)** | – | `whaledeepoperationstest`; MCP-Abnahme `test_t1_acceptance.py` (offscreen und Plasma/Wayland mit echter Eingabe) |
| **M10** | *Optional:* T2 Command-Routing **(umgesetzt, siehe 14.19)**, falls T1 nicht reicht | `dolphinviewactionhandler.cpp`, `dolphinmainwindow.cpp`, `dolphintabpage.cpp` | Papierkorb, Kopieren usw. per Shortcut auf tiefen Treemap-Items |
| **O3** | Bevorzugte View pro Ort (nur View-ID, Variante A), History/Session vor Präferenz **(umgesetzt, siehe 14.20)** | – | `whaleviewpreferencestest`; MCP-Abnahme `test_o3_acceptance.py` (offscreen und Plasma/Wayland, Neustart) |
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
| O3 | Wo wird die View pro Ordner gespeichert (später)? | eigene Whale-KConfig mit URL-Keys; nie `[Dolphin]` **(umgesetzt, siehe 14.20)** |
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

### 14.11 Ergebnisse aus M4: Auswahl und Aktivierung (2026-10-05)

Commits `ae836a7e5` (M4) und `027ad999a` (Dev-MCP). **Keine Änderung an Dolphin-Dateien.**

**Autoritativer Speicher:** unverändert nach 5.2 der `KItemListSelectionManager` der `DolphinView`.
Plugins halten nur eine Darstellungskopie. Alle Pull-Stellen der Shell (`selectedItems()`) bleiben
unberührt.

**`WhaleSelection` (öffentliche API):** `selected()` (URLs in Host-Reihenfolge, ohne Duplikate),
`current()`. Dolphins Semantik bleibt erhalten: Es gibt fast immer ein aktuelles Item (Standard
Index 0), auch ohne Auswahl, und es muss nicht ausgewählt sein. `isEmpty()` betrachtet nur die
Auswahl. Die Anker-Auswahl (Umschalt) ist in `selectedItems()` enthalten.

**Context:** `selection()`, `setSelection()`, `selectionChanged()`, `activateItems(QList<QUrl>)`.
Abweichung von 4.2: Die Aktivierung ist URL-basiert (`activateItems`) statt `activate(items)`.

**URL↔Index (Bridge):**

- Whale → Dolphin: `KFileItemModel::index(QUrl)`, danach `endAnchoredSelection()`, gegebenenfalls
  `setCurrentItem()` und `setSelectedItems()`, also direkt im SelectionManager. Die normale
  Dolphin-Signalkette folgt (300-ms-Entprellung, Statusleiste, Info-Panel, Actions).
  URLs außerhalb des Listings werden ignoriert, ein leeres oder unbekanntes `current` lässt das
  aktuelle Item unverändert.
- Dolphin → Whale: `selectionChanged`/`currentChanged` des SelectionManagers sowie
  `itemsChanged` des Modells (Umbenennen ändert eine ausgewählte URL ohne Auswahlsignal) werden über
  einen 0-ms-Timer gesammelt, nach URL verglichen und nur bei echter Änderung gemeldet. Bis dahin hat
  auch die `WhaleItemList` den Modellschritt nachvollzogen (vgl. Reihenfolgeproblem in 14.10).

**Rückkopplung:** Die Bridge meldet jede Änderung, egal wer sie ausgelöst hat. Unterdrückt wird nur
das Echo an den **Context, der die Auswahl selbst gesetzt hat**, und nur wenn das Ergebnis genau
dem Gewünschten entspricht. Ein erster Entwurf unterdrückte das Echo global in der Bridge; dadurch
erfuhr die sichtbare View nichts von Änderungen anderer Quellen (Dev-MCP, zweiter Context).
Zusätzlich ignoriert die Test-View Auswahländerungen, während sie selbst anwendet oder ihr Modell
nachführt. Getestet: genau ein `DolphinView::selectionChanged`, keine weiteren Signale.

**Verhalten an Rändern (alles wie in Dolphin):** Gelöschte oder weggefilterte Items verlassen die
Auswahl; nach dem Entfernen des Filters kommen sie nicht ausgewählt zurück. Hidden Files aus
entfernt ausgewählte versteckte Items. Wird das aktuelle Item gelöscht, rückt `current` auf ein
existierendes Item. Navigation leert die Auswahl. F5 stellt die Auswahl wieder her (Dolphins
`m_selectedUrls`). Während des Ladens sind nur bereits gelistete Items auswählbar. Symlinks werden
über ihre angezeigte URL ausgewählt, ohne Kanonisierung. Tiefe URLs werden ignoriert und im Dev-MCP
als `ignored` gemeldet; das ist bewusst keine Vorwegnahme von T1/T2.

**Aktivierung:** Die Bridge aktiviert die Hälfte (`DolphinView::setActive(true)`, Regel 14.8) und
löst die Anfrage-Signale `KItemListController::itemActivated(index)` bzw. `itemsActivated(KItemSet)`
aus (3.2). Damit läuft Dolphins gesamte Logik: Alt für Eigenschaften, Archive, Ordner, `OpenUrlJob`,
Rückfrage bei mehr als 5 Items, Ordner in Tabs bei Mehrfachauswahl. Die Test-View: Doppelklick
aktiviert ein Item, Enter die Auswahl (oder das aktuelle Item).

**Statusleiste/Info-Panel:** funktionieren ohne Zusatzverdrahtung über die bestehende Kette
`DolphinView::selectionChanged` → `DolphinMainWindow::selectionChanged` → `InformationPanel`.

**Beobachtung Entf/Strg+X/Strg+C/Strg+V (kein Command-Routing!):** Die Datei-Actions lesen weiter
die `DolphinView`. Weil die Auswahl jetzt gespiegelt ist, treffen sie im Plugin-Modus die Items, die
die Test-List markiert zeigt. Belegt nicht-destruktiv: Strg+C/Strg+X legen genau diese URLs in die
Zwischenablage; Papierkorb/Löschen (`trashSelectedItems`) würden sie verwenden; Einfügen geht in
den aktuellen Ordner. Das ist eine Folge der Architektur, keine Lösung. Offen für M5:

1. Ohne Auswahl öffnen Entf/Papierkorb den Auswahlmodus mit Bottom-Bar der versteckten
   `DolphinView`.
2. Es gibt kein Capability-Gating: Ein Plugin ohne `Selection` (z. B. Treemap) hätte trotzdem eine
   unsichtbare Auswahl als Ziel.
3. F2 bleibt durch den M2-Schutz gesperrt; Inline-Umbenennen braucht M5.
4. Tiefe Auswahl (T1) fehlt.

**Tests:** `whaleselectiontest` (19 Fälle), Dev-MCP `whale_dev_mcp_m4_e2e` (Fälle A–F) unter offscreen
und auf echtem Plasma/Wayland grün. Dateien öffnen in Tests mit einem Test-Öffner (`.desktop` im
Test- bzw. Whale-Profil), der nur den Pfad protokolliert; der In-Process-Test prüft vorher, dass er
wirklich zuständig ist.

### 14.12 Ergebnisse aus M5: Capability-Gating, Umbenennen, Kontextmenü, Item-Info (2026-10-05)

Commits `88042e655` (API-Version 2), `f0fd2cbab` (M5), `100844dde` (Dev-MCP). Dolphin-Dateien:
nur der Kommentar am F2-Schutz in `dolphinviewactionhandler.cpp` (Code unverändert).

**API-Version 2:** wegen M3/M4/M5 (`WhaleItemList`, `WhaleSelection`, neue pure-virtuelle
Context-Methoden, `WhaleView::startRename`). Exakte Übereinstimmung bleibt Pflicht; Plugins für
Version 1 werden abgelehnt (Test `whaleviewregistrytest` rechnet relativ zur Version).

**Gating-Mechanismus (`WhaleViewActions`):** Jede sperrbare Action bekommt eine **eigene,
nicht-exklusive `QActionGroup` als Sperre**. Dolphin setzt `setEnabled()` weiter wie gewohnt
(`updateFileAndEditActions`, `updateViewActions`, XMLGUI-States). Eine deaktivierte Gruppe hält die
Action aus; beim Öffnen stellt Qt exakt Dolphins letzten Wunsch wieder her. Verworfen wurde ein
Ansatz über `QAction::enabledChanged`: Setzt Dolphin eine schon gesperrte Action auf „aus“, kommt
kein Signal, und der gemerkte Zustand wäre veraltet (Test `testGateRestoresDolphinState`: Zoom am
Maximum). Vorbedingung: Die Action steckt in keiner anderen Gruppe; das gilt für alle gesperrten
Actions. Gruppierte Untereinträge (Sortierrollen, auf-/absteigend, Gruppieren nach …) werden über
ihr Menü (`sort`, `group_by`, `additional_info`) gesperrt. Die Sperren folgen der aktiven Hälfte
(`setCurrentHost`), dem View-Wechsel und **sofort** der Leer/Nicht-leer-Änderung der Auswahl
(`DolphinViewBridge::hasSelectionChanged`, synchron). Ein Shortcut direkt nach einem Klick findet
also den richtigen Zustand vor; der gesammelte 0-ms-Bericht wäre dafür zu spät gewesen.

**Capability-Matrix (gilt nur, solange eine Plugin-View sichtbar ist; die Dateiliste sperrt nichts):**

| Bedingung | gesperrt |
|---|---|
| immer in Plugin-Views | `toggle_selection_mode`, `toggle_selection_mode_tool_bar` (Auswahlmodus der Dateiliste) |
| ohne `Zoom` | `view_zoom_in`, `view_zoom_out`, `view_zoom_reset` |
| ohne `Previews` | `show_preview` |
| ohne `HostOrder` | `sort`, `folders_first`, `hidden_last`, `additional_info`, `group_by` |
| ohne `Grouping` | `group_by` |
| ohne `Selection` oder ohne `MultiSelection` | `edit_select_all`, `invert_selection` |
| ohne `Selection` / mit `RecursiveContent` / Auswahl leer | `renamefile`, `movetotrash`, `deletefile`, `delete_shortcut`, `edit_cut`, `edit_copy`, `duplicate`, `copy_location`, `show_target`, `copy/move_to_inactive_split_view`, `compare_files` |

Nicht gesperrt (arbeiten korrekt auf gespiegelter Auswahl bzw. Ort): `edit_paste` (aktueller Ordner),
`properties` (Auswahl, ohne Auswahl der Ordner), `show_hidden_files`, Filter, `add_to_places`.
`Drag`, `Drop`, `KeyboardNavigation` haben noch keine Wirkung. Test-List: `HostOrder | Selection |
MultiSelection | KeyboardNavigation`.

Zusätzlich: Eine View ohne `Selection` bekommt beim Anzeigen die versteckte Auswahl geleert; sonst
gäbe es eine unsichtbare Auswahl für die Shell. Die View-Modus-Actions (Symbole/Kompakt/Details)
schalten auf die Dateiliste um, statt die unsichtbare Ansicht zu ändern.

**Ohne Auswahl (Plugin-View):** Umbenennen, Papierkorb, Löschen, Umschalt+Entf, Ausschneiden und
Kopieren sind deaktiviert; es öffnet sich kein Auswahlmodus. Eigenschaften zeigen den Ordner,
Einfügen geht in den Ordner. Das ist Dolphins Semantik ohne den Auswahlmodus.

**Umbenennen (F2):** `WhaleViewActions` übernimmt die Rename-Action; der M2-Schutz in
`DolphinViewActionHandler::slotRename` bleibt als Delegationsgrenze. Bei genau einem ausgewählten
Item, Dolphin-Einstellung „inline umbenennen“ und Capability `InlineRename` wird
`WhaleView::startRename(url)` aufgerufen. Sonst läuft **Dolphins eigener Dialogpfad**
(`DolphinView::renameSelectedItems()` mit kurz abgeschaltetem `RenameInline`; die Einstellung hat
keine Notifier und wird synchron gelesen). Damit gelten Dolphins Mehrfach-Umbenennung
(`KIO::RenameFileDialog` mit Nummerierung) und das Neu-Auswählen der umbenannten Items. Ein
unsichtbarer Inline-Editor entsteht nie.

**Kontextmenü:** `context->requestContextMenu(globalPos, url)` → Bridge: Hälfte aktivieren; ein nicht
ausgewähltes Item wird zur Auswahl (wie ein Rechtsklick in Dolphin); dann die Anfrage-Signale
`KItemListController::itemContextMenuRequested` / `viewContextMenuRequested` (3.2). Dolphins Slots
senden ein noch anstehendes `selectionChanged` (Bug 294013) und fordern über
`requestContextMenu` → `DolphinMainWindow::openContextMenu` → `DolphinContextMenu` mit
`KFileItemActions` und Service-Menüs an. Die Test-List leert bei Rechtsklick auf den Hintergrund
die Auswahl (wie Dolphin).

**Item-Info:** `context->requestItemInfo(url)` → Bridge löst direkt `DolphinView::requestItemInfo`
aus. Bewusst nicht über `itemHovered`: Dolphins Hover-Slot würde einen Tooltip an der Position der
versteckten Ansicht zeigen. Das Hauptfenster hört `requestItemInfo` auch für die inaktive Hälfte
(Dolphin-Verhalten). Die Test-List meldet Hover und Verlassen (leere URL).

**Split:** Sperren und Actions gelten nur für die aktive Hälfte. Getestet: Papierkorb trifft nur die
Auswahl der aktiven Hälfte; die Auswahl der inaktiven bleibt unberührt.

**Befunde:**

1. Papierkorb in Tests: KIO nutzt den Home-Papierkorb (`XDG_DATA_HOME/Trash`) nur für Dateien auf
   dem Gerät von `$HOME`. Sonst bietet es „Endgültig löschen“ an. Testprofile mit Dateioperationen
   bekommen deshalb ein eigenes `HOME` im Profil.
2. Wayland zeigt Popups (Kontextmenüs) nur als Antwort auf echte Eingabe („Failed to create
   grabbing popup“). Automatisch geöffnete Menüs sind auf echtem Wayland nicht prüfbar; offscreen
   und im In-Process-Test schon. Echte Rechtsklicks liefern die nötige Eingabe.
3. Offscreen aktiviert nach einem Dialog niemand das Hauptfenster wieder; dort ist nur der logische
   Fokus prüfbar. Auf Wayland reaktiviert KWin korrekt (geprüft).

**Grenzen vor T1/T2:** M5 gilt nur für Items des aktuellen Listings. Keine Auswahl außerhalb des
Listings, kein Command-Routing, nichts wird in `DolphinView::selectedItems()` eingeschleust. Eine View
mit `RecursiveContent` bekommt alle Actions auf ausgewählten Items gesperrt, bis T1
(View-eigene tiefe Auswahl + eigenes reduziertes Menü) kommt.

**Tests:** `whaleactionstest` (22 Fälle). Dev-MCP `whale_dev_mcp_m5_e2e`: Szenarien A–G offscreen;
auf echtem Wayland A, B, D, F, G (C wegen deiner Zwischenablage und E wegen Wayland-Popups
übersprungen). M3/M4-Abnahmen als Regression auf Wayland grün.

### 14.13 Ergebnisse aus M6: Persistenz (2026-10-05)

Commits: `795e2838b` (Action-ID-Existenztest, vor M6), `a9acd2731` (M6), `9dd5c6de1` (Dev-MCP).

**Action-ID-Test:** `whaleactionstest::testGateableActionsExist` prüft jede Action aus
`WhaleViewActions::gateableActions()` (die Produktionsliste). Sie muss in der Collection existieren und
eine eigene Sperrgruppe haben. Die Gegenprobe mit einem nicht existierenden Namen schlägt hart fehl.

**API-Version 3:** neues Signal `WhaleView::capabilitiesChanged()` und `WhaleViewContext::isLoading()`.
Ein neues Qt-Signal wäre ABI-technisch verträglich. Die Projektregel ist aber exakte Versionsgleichheit
ohne Kompatibilitätsversprechen, also wird bei jeder Änderung der öffentlichen API erhöht.

**Persistenzformat (eigene Version `WhalePersistence::FormatVersion = 1`, unabhängig von der API):**

| Ebene | Ort | Aufbau |
|---|---|---|
| History | Location-State des `KUrlNavigator`-Eintrags | `"WHALEVS\x01"` + `quint32` Version + `QByteArray` Dolphin-State (unverändert `DolphinView::saveState`) + `WhaleViewsState` |
| Session | Tab-Gruppe, Key `WhaleViews` neben `State` | `"WHALESS\x01"` + Version + Anzahl (1–2) + je Hälfte `WhaleViewsState` |
| `WhaleViewsState` | – | aktive View-ID (Dateiliste: `org.kde.dolphin.itemview`) + sortierte Liste (View-ID, State-Blob) |

`QDataStream` mit fester Version `Qt_6_0`. Daten ohne Magic: History = reiner Dolphin-State (alte
Einträge), Session = keine Whale-Daten. Beschädigte oder neuere Daten werden mit Warnung ignoriert;
der Dolphin-Teil eines beschädigten Umschlags wird **nicht** an die `DolphinView` gegeben.
`DolphinView::saveState` und `DolphinTabPage::saveState` bleiben unverändert.

**History-Integration:** `DolphinViewContainer::saveViewState`/`tryRestoreViewState` reichen den
Location-State durch `WhaleViewHost::wrapLocationState`/`unwrapLocationState` (je eine Zeile). Beim
Zurückkehren zeigt der Host die gespeicherte View. Bei neuen Orten und alten Einträgen bleibt die
gezeigte View, aber ohne gespeicherte States.

**Session-Integration:** `DolphinTabWidget::saveProperties`/`readProperties` schreiben bzw. lesen
`WhaleViews` pro Tab über `WhaleViewHost::save/restoreSessionState` (Helfer `whaleViewHosts` +
2 Zeilen). **Zusätzliche Dolphin-Zeile:** `DolphinMainWindow::setSessionAutoSaveEnabled` startet den
Session-Speicher-Timer auch bei `WhaleViewActions::sessionChanged`. Grund: Dolphin speichert die
Session beim Beenden nur, wenn eine Speicherung ansteht (URL-, Tab-, Aktivitätswechsel). Ein reiner
View-Wechsel ginge sonst verloren.

**Lifecycle (tatsächliche Reihenfolge):** State der alten View sichern → neue View erzeugen (mit
Context) → alte `deactivate()`, Stack umschalten, alte zerstören → neue `restoreState()` (nur wenn ein
State existiert) → Fokus → `activate()` → `currentViewChanged` (Gating) → `sessionChanged`. Bei der
History-Wiederherstellung ist der Ort schon gewechselt; die Items können noch laden (`isLoading()`).
Gleiche View-ID: `restoreState()` auf der bestehenden View. States sind pro View-ID getrennt; eine View
bekommt nie den State einer anderen.

**Split:** Jede Hälfte hat ihren eigenen `WhaleViewsState`. In der Session gibt es einen Datensatz pro
Hälfte.

**Fallback:** Unbekannte, deaktivierte, inkompatible oder nicht ladbare Plugins → Dateiliste,
Warnung, `lastRestore().fallback`, kein Dialog. Der State des fehlenden Plugins bleibt in
`WhaleViewsState` und wird weiter geschrieben. Ein Lauf ohne das Plugin überschreibt die gespeicherte
View-Wahl nur, wenn er die Session aus anderen Gründen neu speichert. Dann steht dort die gezeigte
Dateiliste, und der Plugin-State bleibt erhalten (E2E F).

**`capabilitiesChanged`:** Der Host leitet es weiter (ohne `Selection` wird die versteckte Auswahl
geleert), `WhaleViewActions` aktualisiert die Sperren sofort. Getestet über das Test-Plugin
`whale_test_dynamic` (Zoom nach Ort oder State), auch nach History-Wiederherstellung.

**Test-List-State:** das oberste sichtbare Item (URL). Es wird bis zum Ende des Ladens und bis die
Liste eine Größe hat festgehalten. Beim Übernehmen des aktuellen Items vom Host ist `autoScroll` aus.
Sonst würde Dolphins Standard-Current (Index 0) die Wiederherstellung überschreiben.

**Globale Plugin-Konfiguration:** in M6 nicht benötigt und nicht verdrahtet (`viewConfig()` bleibt
Plan). History- und Session-Blobs enthalten nur Ortszustand.

**Befunde:**

1. Das Dev-MCP antwortet, bevor Dolphin die Session wiederherstellt (`main()`): erst auf den
   wiederhergestellten Zustand warten.
2. Dolphin stellt die Session nur als einzige Instanz wieder her (`dolphinGuiInstances`); Neustart-Tests
   laufen deshalb auf einem privaten D-Bus (`dbus-run-session`). Dort gestartete Dienste halten Pipes
   offen, daher geht die Ausgabe über eine Datei.
3. **Vermutlich vorbestehend in Dolphin, nicht weiter untersucht:** Nach dem Wiederherstellen einer
   Split-View löst Navigation in der aktiven Hälfte erst dann eine Session-Speicherung aus, wenn die
   aktive Hälfte einmal gewechselt hat. `DolphinTabPage` verbindet `activeViewUrlChanged` nur beim
   Wechsel neu. Kandidat für ein Upstream-Issue.

**Tests:** `whalepersistencetest` (16), `whale_dev_mcp_m6_e2e` mit A–G (36 Schritte) offscreen und auf
echtem Wayland inklusive vier Neustarts; M3–M5-Abnahmen auf Wayland weiter grün.

**Weiter offen:** O3 (View pro Ordner) bleibt offen; keine `.directory`/xattr-Lösung. T1/T2
unverändert offen.

### 14.14 Ergebnisse aus M7: Previews und Drag & Drop (2026-10-05)

Commits: `bfd7621c4` (M7), `72eda5fe0` (Dev-MCP). **Keine Änderung an Dolphin-Dateien.**

**API-Version 4:** neue Klassen `WhalePreviewProvider` und `WhaleFileOperations`, dazu die
pure-virtuellen Context-Methoden `previews()` und `fileOperations()`. Beide Objekte gehören dem
Context und entstehen beim ersten Aufruf (wie `items()`). Über die Grenze gehen nur `KFileItem`,
`QUrl`, `QPixmap`, `QSize` und Qt-Drag-Events, keine Indizes und keine kitemviews-Typen. Die
Capabilities `Previews` wirkt jetzt (siehe unten), `Drag` und `Drop` bleiben informativ.

**`WhalePreviewProvider`:**

| Methode/Signal | Bedeutung |
|---|---|
| `requestPreviews(KFileItemList, QSize, dpr)` | asynchron, je Item genau eine Antwort; schon angefragte Items mit gleicher Größe werden nicht doppelt angefragt, eine andere Größe ersetzt die alte Anfrage |
| `cancel(QList<QUrl>)`, `cancelAll()` | danach kommt für diese Items kein Signal mehr |
| `isPending(QUrl)`, `isEnabled()` | Zustand |
| `previewAvailable(item, pixmap)`, `previewFailed(item)`, `enabledChanged(bool)` | Antworten bzw. Schalter |

Umsetzung (`WhalePreviewProviderImpl`): pro `requestPreviews()` ein `KIO::PreviewJob` mit den
Thumbnail-Plugins aus `[PreviewSettings] Plugins` (wie `KFileItemModelRolesUpdater`), Skalierung
`ScaledAndCached`. Der KIO-Thumbnail-Cache wird damit automatisch genutzt; es gibt keinen eigenen Cache.

**Lifecycle/Abbruch:** Jede Anfrage merkt sich ihren Job. Sobald für einen Job niemand mehr wartet (alles
beantwortet oder abgebrochen), wird er nach Rückkehr in die Event-Loop gestoppt, sofern er nicht schon von
selbst fertig ist. Nie wird er in seinem eigenen Signal gestoppt. Items, die ein Job ohne Antwort beendet,
melden `previewFailed`. Ortswechsel und Deaktivierung brechen alles ab. Beim Zerstören (View-Wechsel,
Fenster zu) werden alle Jobs gestoppt. Mehrere Anfragen und Jobs laufen parallel, nichts blockiert.
**Aktiv** ist der Provider nur, wenn Dolphins „Vorschau anzeigen“ am Ort an ist
(`DolphinView::previewsShown()`, über die Bridge) **und** die View `Previews` deklariert. Der Host
meldet die Capabilities nach dem Erzeugen und bei `capabilitiesChanged()` an den Context. Ohne
Capability bleibt `show_preview` gesperrt (M5), und der Provider macht keine Arbeit, auch wenn eine
View anfragt (getestet mit Hello).

**Sichtbarer Bereich/Priorisierung (Test-List):** Angefragt werden die sichtbaren Zeilen plus eine
Seite darüber und darunter. Beim Scrollen, Größenänderung und Item-Änderungen wird gesammelt (0-ms-Timer)
neu berechnet: Neue Zeilen werden angefragt, Anfragen außerhalb des Bereichs abgebrochen und deren
Thumbnails verworfen. Geänderte Dateien bekommen neue Previews; Items ohne Preview werden bis zur
nächsten Änderung nicht erneut angefragt. Gemessen (E2E B, 400 Bilder, 9 sichtbare Zeilen, offscreen): beim Laden
18 Anfragen (sichtbare Seite plus eine darunter); nach Scrollen zu fünf Stellen insgesamt 150 Anfragen,
davon 81 abgebrochen, höchstens 27 gleichzeitig offen (drei Seiten). Eine eigene Scheduling-Engine gibt es nicht.

**Test-List:** Bei aktiven Previews 48-px-Zeilen mit Thumbnail (`QPixmap` statt `QIcon` in der
`DecorationRole`), sonst die bisherigen Icons in Standardgröße. Capabilities zusätzlich `Previews | Drag | Drop`.

**`WhaleFileOperations` (Drag & Drop):**

| Methode | Wiederverwendet |
|---|---|
| `startDrag(QList<QUrl>, pixmap = {})` | Drag-Daten exakt wie `KItemListController::startDragging()`: `KFileItemModel::createMimeData()` (URL + `mostLocalUrl` über `KUrlMimeData`) + `KUrlMimeData::exportUrlsToPortal()`, `QDrag` am aktiven Fenster, Move/Copy/Link mit Copy als Vorgabe. Bild: das übergebene oder das Icon des ersten Items |
| `updateDropAction(QDragMoveEvent*, QUrl)` | Regeln aus `KItemListController::dragEnter/MoveEvent`: Cache-Reset bei DragEnter, nie auf sich selbst (`DragAndDropHelper::urlListMatchesUrl`), `KFileItemModel::supportsDropping` (beschreibbarer Ordner, Desktop-Datei, lokales Programm). Wie in Dolphin wird ein Drag über eine normale Datei abgelehnt |
| `drop(QDropEvent*, QUrl, QWidget*)` | Zielregeln aus `DolphinView::slotItemDropEvent()` (Ordner/Desktop-Datei/Programm empfängt, sonst der Ort), dann **`DolphinView::dropUrls()`**, also `DragAndDropHelper::dropUrls()` → `KIO::drop` (Modifier oder Drop-Menü, Ark-Extraktion, Places-Ausnahme), Fehlermeldungen und Auswahl der in den Ort gedroppten Items, danach `setActive(true)`. Das Menü erscheint an der Event-Position im übergebenen Widget |

Copy/Move/Link entscheidet allein KIO aus den Modifiern: Strg = Kopieren, Umschalt = Verschieben,
Strg+Umschalt = Verknüpfen, ohne Modifier fragt KDEs Drop-Menü („Hierher verschieben/kopieren/verknüpfen“,
„In neuen Ordner verschieben“, Abbrechen). Whale rät nichts. Nur Items des aktuellen Listings nehmen teil:
Fremde URLs werden beim Drag ignoriert, ein Drop-Ziel außerhalb des Listings wird abgelehnt. Tiefe
Auswahl (`RecursiveContent`) bleibt ausgeschlossen.

**Split:** Die Quellseite wird wie bei einem echten Klick durch das Drücken aktiv; der Drop aktiviert die
Zielseite (wie Dolphins Drop). Die Auswahl der Quelle bleibt erhalten, verschobene Items fallen heraus.
Die gedroppten Items sind die Auswahl des Ziels. Der Fokus liegt danach in der Ziel-View (Plugin-Widget
bzw. Dateiliste), nie in einer versteckten `DolphinView`. Das Gating folgt der aktiven Hälfte. Getestet sind
Test-List → Dateiliste, Dateiliste → Test-List, Test-List → Test-List und der Drop auf einen Unterordner.

**Dev-MCP:** Neu in Pane-State: `previews` (shown, capability, provider, enabled, pending, runningJobs,
requested/generated/failed/canceled, jobsStarted/jobsKilled, maxPending), `pluginPreviewRows` und
`pluginVisibleRows`. Neue Tools: `get_preview_state`, `request_preview`, `perform_drag_drop`,
`choose_popup_entry`; `trigger_action show_preview`. `perform_drag_drop` macht **keine eigene
Dateioperation**: Eine Plugin-Quelle bekommt die echte Geste (Press, Moves), die View ruft selbst
`startDrag()`. Nur `QDrag::exec()` wird für diesen einen Drag durch einen Runner ersetzt
(`WhaleFileOperationsImpl::setDragRunner`, statischer Entwicklungs-Haken), der dem Ziel-Widget
DragEnter/DragMove/Drop schickt. Bei der Dateiliste als Quelle kommen die Daten aus derselben Funktion wie
ihre eigenen Drags. Das Ziel behandelt die Events selbst (Plugin-Viewport bzw. der Viewport der
`QGraphicsView` der Dateiliste). Dateiänderungen nur mit `WHALE_DEV_MCP_FILE_OPERATIONS=1`.

**Remote-URLs:**

| Weg | Verhalten |
|---|---|
| Drag-Daten | wie Dolphin: KIO-URL plus `mostLocalUrl`. KDE-Anwendungen bekommen sftp/smb-URLs, andere Anwendungen können damit oft nichts anfangen. Der Portal-Export betrifft nur lokale Dateien |
| Drop | `KIO::drop`/`CopyJob` können jedes beschreibbare KIO-Ziel; die Zielregeln nutzen die Rechte aus dem Listing |
| Previews | `KIO::PreviewJob` mit den Remote-Grenzen aus `[PreviewSettings]` (wie die Dateiliste). Die versteckte `DolphinView` macht weiter ihre eigene Preview-Arbeit (M0, K12), bei Remote-Orten über das Netz |

Getestet wurde **nur lokal** (`file://` in `/tmp`). Nicht getestet: sftp, smb, mtp, Archive (`zip:/`
usw.), `trash:/`, Suchergebnisse und der Portal-Pfad in einer Sandbox. Eine eigene Remote-Übertragung
gibt es nicht.

**Befunde:**

1. Ein laufender Drag gehört dem Zeiger: Nach dem Start des Drags bekommt die View weder Moves noch das
   Loslassen. Simulierte Gesten müssen danach aufhören. Sonst fällt `QAbstractItemView` in DragSelecting
   bzw. löst die verschobene Auswahl beim Release auf genau ein Item auf.
2. Die Events der Dateiliste gehen an den Viewport der `QGraphicsView`, die selbst der Viewport von
   `KItemListContainer` ist.
3. Einträge im Drop-Menü tragen ihren Shortcut nach einem Tab („Copy Here\tCtrl“).
4. `whaleactionstest::testItemInfo` scheitert auf echtem Wayland auch mit dem M6-Stand (QTest-Hover);
   offscreen ist er grün. Vorbestehend, nicht M7.
5. Doppelte Preview-Arbeit: Die versteckte `DolphinView` erzeugt bei aktiver Vorschau weiter Previews
   (bis 500 Items, K12); der KIO-Cache teilt die Ergebnisse. Das Pausieren bleibt optionaler Kandidat M11.

**Tests:** `whalepreviewtest` (10 Fälle), `whaledragdroptest` (11 Fälle) und
`whale_dev_mcp_m7_e2e` (A–H, 34 Schritte, Dateisystem nach jedem Drop geprüft). Offscreen ist alles
grün; auf echtem Plasma/Wayland ebenfalls, nur das Drop-Menü (G) wird dort übersprungen. M3–M6-Abnahmen
auf Wayland weiter grün. Volle Suite offscreen: 47/48; der Rest ist der bekannte
`DolphinMainWindowTest::testAccessibilityTree`.

**Offen für M8 und später:**

- `WhaleTreeScanner` (M8) ist unabhängig davon.
- Für die Treemap fehlen noch Drag & Drop auf tiefe Items und Previews außerhalb des Listings. Der
  Provider kann zwar beliebige `KFileItem`s, Drag und Drop sind aber auf das Listing begrenzt (T1).
- `WhaleFileOperations` für explizite `KFileItemList`s (T1).
- Auto-Aktivierung beim Überfahren eines Ordners während eines Drags (Dolphin hat sie, die Test-List nicht).
- Remote-Abnahme.

**Weiter offen:** O3 (View pro Ordner) und T1/T2 unverändert.

### 14.15 Ergebnisse aus M8a: lokaler `WhaleTreeScanner` (2026-10-05)

Commit `fa219d6d1`. **Keine Änderung an Dolphin-Dateien.** M8 wurde geteilt: M8a ist der lokale Scanner,
M8b bringt Cache, Invalidierung und ggf. Remote.

**API-Version 5:** öffentlicher Service, weil die Treemap (M9) ihn direkt nutzen soll. Neu sind
`WhaleTreeScanner`, `WhaleTreeScanOptions` und `WhaleTreeNode` sowie `WhaleViewContext::treeScanner()`.

**Lebensdauer:** **ein Scanner pro Context**, beim ersten Aufruf erzeugt, Kind des Contexts (wie
`items()`, `previews()`, `fileOperations()`). Er führt einen Scan zur Zeit aus. Verschwindet der Context
während eines Scans, wird dieser still abgebrochen. Mehrere Contexts scannen parallel. Der Scanner ist
abstrakt mit Host-Implementierung, damit M8b den Cache host-seitig einbauen kann, ohne die API zu ändern.

**API:**

| | |
|---|---|
| `scan(QUrl, WhaleTreeScanOptions)` | startet, sendet sofort `started()`; `false` (ohne Wirkung), solange ein Scan läuft |
| `cancel()` | sendet `canceled()` noch im Aufruf; danach kommt für diesen Scan nichts mehr, `isRunning()` ist `false`, ein neuer Scan darf sofort folgen |
| `isRunning()` | |
| Signale | `started(url)`, `progress(besuchte Einträge)` (höchstens alle 100 ms), `finished(WhaleTreeNode)`, `canceled()`, `failed(Error, Meldung)` |
| Optionen | `maxDepth` (−1 = unbegrenzt), `stayOnFilesystem` (Vorgabe an), `includeHidden` (Vorgabe an) |
| `Error` | `InvalidUrl`, `NotLocal`, `NotFound`, `NotAFolder`, `AccessDenied` (nur für die Wurzel) |

Jeder Scan endet mit genau einem von `finished`, `canceled` oder `failed`. **Ein Teilbaum wird nie
geliefert.** Auch Fehler der Wurzel und abgelehnte URLs kommen asynchron.

**`WhaleTreeNode`:** implizit geteilter, **unveränderlicher Snapshot** (`QExplicitlySharedDataPointer`
auf konstante Daten). Er ist in jedem Thread lesbar, Kopien sind billig, und ein Teilbaum lebt, solange
eine Kopie existiert. Es gibt keine rohen Zeiger. Felder:

| Feld | Bedeutung |
|---|---|
| `path()`, `url()`, `name()` | lokaler Pfad ohne abschließenden Slash |
| `type()` | `File`, `Directory`, `Symlink` (auch kaputte Links), `Other` (Gerät, Pipe, Socket) |
| `depth()` | Wurzel 0 |
| `size()` | eigene Größe: Dateiinhalt (scheinbare Größe, `st_size`); 0 für Ordner, Links und Dateien ohne Blöcke (wie Dolphins Zähler, z. B. `/proc/kcore`) |
| `totalSize()` | `size()` plus alles darunter, **auch jenseits von `maxDepth`** |
| `fileCount()`, `directoryCount()` | alle Nicht-Ordner bzw. Ordner darunter, in jeder Tiefe |
| `isComplete()` | `false`, wenn darunter ein unlesbarer oder verschwundener Ordner oder ein nicht betretener Mount-Punkt liegt |
| `children()` | nach Namen sortiert (Byte-Reihenfolge); leer jenseits von `maxDepth` |

Ein Node speichert den vollen Pfad. Das ist einfach und sicher, kostet aber Speicher; siehe Performance.
Die private Datenklasse liegt in `api/whaletreenode_p.h` und ist nur für Whale gedacht.

**Threading:** Die öffentliche API läuft im GUI-Thread. Jeder Scan ist ein Task eines **eigenen**
Thread-Pools (höchstens 4 Threads, damit Qts globaler Pool nicht blockiert wird), umgesetzt mit
`QtConcurrent::run` und `QPromise`. Ergebnisse und Fortschritt kommen über `QFutureWatcher` im GUI-Thread
an (Test: `finished` und `progress` im GUI-Thread, kein Eintrag im GUI-Thread besucht; ein 10-ms-Timer tickt
während eines Scans von 1,1 s etwa 105-mal).

**Abbruch:** `cancel()` bricht die `QFuture` ab. Der Worker prüft nach jedem Eintrag und schließt `fts`;
sein Ergebnis wird verworfen. Der Scanner wartet nie auf einen Worker. Der Watcher wird gelöst und
entsorgt, und der Destruktor bricht still ab. Gemessen liegt die Latenz unter 50 ms (Abtastintervall des
Tests) bei einem künstlich gebremsten Scan nach rund 390 von 6660 Einträgen. 25 Abbrüche direkt nach dem
Start: alle Worker sind sofort fertig, danach ist der Baum korrekt. Zehnfach wiederholt stabil.

**Symlinks:** `FTS_PHYSICAL`. Ein Link ist ein eigener Node vom Typ `Symlink` mit Größe 0, ohne Kinder,
und wird nie verfolgt. Es gibt keine Schleifen (getestet mit einem Link auf die Wurzel) und keine doppelte
Zählung. Kaputte Links (`FTS_SLNONE`) sind ebenfalls `Symlink`. **Nur ein Link als Wurzel** wird verfolgt
(`FTS_COMFOLLOW`, wie Dolphins Zähler). `FTS_DC` (nur über Bind-Mounts möglich) wird als unvollständiger
Ordner ohne Kinder geführt. Harte Links zählen pro Link.

**Hidden:** Ein Name, der mit einem Punkt beginnt, gilt als versteckt. Standardmäßig wird alles gescannt.
Mit `includeHidden=false` fehlen versteckte Einträge in jeder Tiefe; versteckte Ordner werden nicht
betreten. Eine versteckte Wurzel wird trotzdem gescannt. Das ist **eine eigene Option des Scanners**
und nicht an Dolphins Einstellung gekoppelt.

**Dateisystem-Grenzen:** `stayOnFilesystem` (Vorgabe an) setzt `FTS_XDEV` und vergleicht zusätzlich
`st_dev` mit der Wurzel. Ein Mount-Punkt erscheint als Ordner ohne Kinder mit `isComplete()=false`.
Getestet ohne Mount-Manipulation an der echten Grenze `/sys/fs/cgroup` unter `/sys/fs` (nur lesend; der
Test wird übersprungen, wenn das kein Mount-Punkt ist). Mit `stayOnFilesystem=false` wird hineingegangen.

**Fehler:**

| Fall | Verhalten |
|---|---|
| leere/ungültige URL | `failed(InvalidUrl)` |
| nicht lokal (sftp, smb, trash, zip, …) | `failed(NotLocal)`, „Only local folders can be scanned, not …“ |
| Wurzel fehlt oder verschwindet | `failed(NotFound)` |
| Wurzel ist kein Ordner | `failed(NotAFolder)` |
| Wurzel unlesbar | `failed(AccessDenied)` |
| unlesbarer Unterordner (Rechte) | Node ohne Kinder, unvollständig; der Rest wird gescannt |
| Ordner verschwindet während des Scans | wie unlesbar (Node ohne Kinder, unvollständig) |
| Datei verschwindet, bevor ihr Ordner gelesen ist | fehlt im Baum |
| Datei verschwindet oder ändert sich, nachdem ihr Ordner gelesen ist | bleibt mit den Daten zum Lesezeitpunkt |
| neue Datei in einem noch nicht gelesenen Ordner | erscheint |

**Performance (offscreen, lokale SSD, Seiten-Cache warm):**

| Baum | Scan | Speicher mit gehaltenem Baum |
|---|---|---|
| klein (2 Einträge) | < 1 ms | – |
| 6000 Dateien, 330 Ordner | ca. 11 ms | ca. 1,7 MB (frischer Prozess, ca. 270 B pro Node) |
| 100 000 Dateien, 2100 Ordner, lange Namen | ca. 205 ms | ca. 47 MB (ca. 470 B pro Node, vor allem der volle Pfad als QString) |

Die Arbeit wächst linear. Der GUI-Thread bleibt frei.

**Dev-MCP:** Es gibt bewusst **kein** neues Tool. Der Scanner hat keine UI, und die Abnahme läuft über
`whaletreescannertest`. Ein `scan_tree` kann mit M9 kommen, wenn eine View den Scanner zeigt.

**Tests:** `whaletreescannertest` (17 Funktionen, 25 Fälle): einfacher Baum, `maxDepth`, Symlinks (Datei,
Ordner, Schleife, kaputt, Link als Wurzel), Hidden, Abbruch sofort (25×) und während des Scans, Dateien und
Ordner ändern sich oder verschwinden während des Scans (über einen Test-Haken im Worker), fehlende Rechte,
abgelehnte Wurzeln (8 Fälle), ein Scan zur Zeit und paralleles Scannen zweier Scanner, Threads, Wiederholbarkeit
(viermal identischer Baum), großer Baum, echter Mount-Punkt, Scanner des Contexts (Lebensdauer, Abbruch
beim Zerstören). Volle Suite offscreen 48/49 (bekannter `testAccessibilityTree`), auf Wayland ist der
Scanner-Test grün.

**Bewusste Grenzen von M8a:** kein Cache, keine Wiederverwendung alter Scans, keine Überwachung, keine
Persistenz, nur `file:`-URLs, keine Teilbäume bei Abbruch, keine inkrementellen Node-Signale, harte Links
nicht dedupliziert, scheinbare statt belegter Größe.

**Plan für M8b:**

- Cache pro Wurzel im Host, gemeinsam für alle Contexts: einen Scan wiederverwenden, Teilbäume aus dem
  Baum einer Oberwurzel liefern.
- Invalidierung über `KDirWatch` bzw. die Änderungen des `KCoreDirLister`, verbunden mit dem Listing.
- Ein Speicherbudget, ggf. kompaktere Nodes (Name statt vollem Pfad, Pfad aus dem Elternknoten).
- Optional ein `KIO::listRecursive`-Fallback für Remote-Orte (Plan 4.6), getrennt vom `fts`-Pfad.
- Optional inkrementelle Signale, falls die Treemap sie braucht.

**Weiter offen:** M9 (Treemap), T1/T2 (tiefe Auswahl) und O3 (View pro Ordner) unverändert.

### 14.16 Ergebnisse aus M8b: geteilter Cache, Invalidierung, Budgets, kompakte Nodes (2026-10-05)

Commit `ac8ea65b4`. **Keine Änderung an Dolphin-Dateien. API-Version bleibt 5:** Die öffentlichen
Header (`WhaleTreeScanner`, `WhaleTreeNode`) sind unverändert. Geändert haben sich nur die interne
Datenklasse und die Host-Seite.

**Aufbau (Host):**

| Klasse | Aufgabe |
|---|---|
| `WhaleTreeWalker` | der `fts`-Lauf aus M8a, herausgelöst, mit Rückrufen (abgebrochen?, Fortschritt, Ordner beobachten) |
| `WhaleTreeScanJob` | ein laufender Scan (Worker im eigenen Pool) mit Consumern |
| `WhaleTreeWatch` | inotify-Watches pro Ordner mit Besitzern und Budget |
| `WhaleTreeCache` | Schlüssel, gehaltene Bäume, laufende Scans, Budgets, LRU |
| `WhaleTreeScannerImpl` | der Scanner eines Contexts, jetzt Consumer des Caches |

**Ownership:** ein `WhaleTreeCache` **pro Hauptfenster**, Kind von `WhaleViewActions` (wie die
View-Registry). Es gibt kein Singleton. `WhaleViewHost` gibt den Cache beim Erzeugen eines Plugin-Contexts
mit (`setTreeCache`). Alle Views eines Fensters (Tabs, Split) teilen also die Scans; mehrere Fenster haben
getrennte Caches. Ein Scanner ohne Cache (z. B. ein Context außerhalb eines Fensters) scannt selbst wie in M8a.

**Schlüssel:** bereinigter lokaler Pfad (Slash am Ende egal), `maxDepth` (negativ = unbegrenzt),
`stayOnFilesystem`, `includeHidden`. Sonst nichts, keine View-Parameter.

**Hit/Miss-Semantik (für alle drei Fälle gleich):** `started()` kommt noch in `scan()`, danach genau eines
von `finished`/`failed`/`canceled`; `finished` und `failed` nie innerhalb von `scan()`.

- **Hit:** gehaltener Baum, zugestellt per Queued-Aufruf, ohne `progress()`.
- **Join:** Läuft für den Schlüssel schon ein Scan, hängt sich der Aufrufer an. Es läuft ein Worker,
  und beide bekommen denselben Baum.
- **Miss:** neuer Scan.

**Invalidierung:** Jeder Ordner bekommt eine inotify-Watch (Erstellen, Löschen, Umbenennen, Inhalt,
Attribute, Ordner selbst), **und zwar aus dem Worker, bevor `fts` die Einträge des Ordners liest.**
Jede Änderung unter der Wurzel verwirft den ganzen Baum. Teilbäume werden nicht repariert, Nodes nicht
verändert. Ein Überlauf der Kernel-Queue verwirft alles.

- **Warum nicht `KDirWatch`:** geprüft per Prototyp. Es kostet ebenfalls eine Watch pro Ordner und
  meldet Inhaltsänderungen. Es lebt aber im GUI-Thread, Watches gäbe es also erst nach dem Scan, und
  Änderungen in der Lücke gingen verloren. `WatchSubDirs` listet den Baum außerdem synchron im GUI-Thread
  neu (20.000 Ordner: 242 ms gegenüber 109 ms beim Hinzufügen einzelner Ordner).
- **Kosten:** eine Watch pro Ordner (= `directoryCount()+1`). 2.101 Watches für den 100k-Baum kosten
  rund 7 ms im Worker; laut Prototyp ca. 0,5 KB Speicher pro Watch.

**Race-Semantik:**

| Fall | Verhalten |
|---|---|
| Änderung während des Scans | Der Baum wird ausgeliefert (gleiche Semantik wie M8a), aber nicht gehalten |
| Änderung zwischen Scan-Ende und Aufnahme | Kommt das Ereignis vor der Aufnahme, wird nicht gehalten; danach wird der Eintrag sofort verworfen. Getestet in 20 Runden: Der nächste Scan sieht die Änderung immer |
| zwei Contexts, gleicher Schlüssel | ein Worker, beide bekommen denselben Baum |
| ein Consumer bricht ab | Der Scan läuft für die anderen weiter |
| alle Consumer brechen ab | Der Worker stoppt (unter 60 ms), es wird nichts gehalten, die Watches sind frei |
| gleicher Scanner, zweiter `scan()` | `false` wie in M8a, auch während ein gehaltener Baum zugestellt wird |

**Nicht gehalten werden:** fehlgeschlagene Scans, nicht-lokale Wurzeln, Bäume, die sich während des Scans
geändert haben, Bäume, deren Ordner nicht alle beobachtet werden konnten (Watch-Budget), und Bäume, die
allein das Speicherbudget sprengen. Diese werden zugestellt, aber nicht gehalten.

**Budgets und Eviction:**

- Konfiguration: Gruppe `TreeScanCache` in Whales Konfiguration, ohne GUI.
  - `MemoryBudgetMiB`: Vorgabe **128**
  - `WatchBudget`: Vorgabe **10.000** Ordner, höchstens ein Viertel von `max_user_watches`
- Speicher wird pro Node geschätzt (Daten, Name, Listenplatz, Allokator, geteilter Ordnerpfad). Geschätzt
  189 B/Node, gemessen rund 200 B/Node.
- Beim Aufnehmen eines Baums gehen die am längsten nicht benutzten Bäume, bis Speicher und Watches
  passen. Nur fertige Bäume werden verdrängt, nie laufende Scans.
- Ein Scan darf höchstens das Watch-Budget nutzen; alle zusammen kurzzeitig das Doppelte, solange ein neuer
  Scan neben gehaltenen Bäumen läuft.
- Ausgelieferte Bäume leben unabhängig vom Cache weiter (implizit geteilte, unveränderliche Nodes).

**Kompakte Nodes:** Eine Node speichert ihren Namen und den Pfad ihres Ordners als `QByteArray` (lokale
Kodierung). Alle Einträge eines Ordners teilen sich diesen Pfad per implizitem Sharing. Einen vollen Pfad
gibt es also einmal pro Ordner statt pro Eintrag, und es gibt keine Parent-Zeiger. `path()`, `url()` und
`name()` werden bei Bedarf berechnet. Kinderlisten werden nach dem Ordner auf die nötige Größe gekürzt
(`squeeze`).

**Messwerte (offscreen, SSD, warmer Seiten-Cache):**

| | M8a | M8b |
|---|---|---|
| 6.000 Dateien / 330 Ordner: Scan | 11 ms | 11–12 ms (mit 331 Watches) |
| 6.000: Speicher des Baums (frischer Prozess) | 1,7 MB | 1,3 MB |
| 6.000: Cache-Hit | – | ca. 21 µs |
| 100.000 Dateien / 2.100 Ordner: Scan | 205 ms | 190 ms ohne, 197 ms mit Watches |
| 100.000: Speicher des Baums | 47 MB | **20 MB** (Schätzung 21,6 MB) |
| 100.000: Hit / Hit aus zweitem Scanner | – | 24 µs / 11 µs |
| 100.000: Scan nach Änderung | – | 197 ms (echter Scan) |

**Tests:** `whaletreecachetest` (17 Funktionen, 24 Fälle plus eine optionale Messung): Hit, Optionen als
Schlüssel, zwei Scanner und zwei Contexts eines Fensters teilen einen Scan, Abbruch eines und aller
Consumer, Invalidierung (Datei erstellt, gelöscht, umbenannt, gewachsen, tief geändert, Ordner erstellt,
gelöscht, umbenannt), Eviction (LRU), Snapshot überlebt den Cache, Änderung während und direkt nach dem
Scan, Watch-Budget, nicht gehaltene Fehlschläge, Speicherschätzung und -budget. Race- und Cancel-Tests sind
zehnfach stabil. `whaletreescannertest` (M8a) bleibt unverändert grün. Volle Suite offscreen 49/50 (bekannter
`testAccessibilityTree`); Scanner- und Cache-Tests auch auf Wayland grün.

**Dev-MCP:** kein Ausbau. Die Statistik (`WhaleTreeCache::statistics()`) reicht für die Tests.

**Bewusste Grenzen / vertagt auf M8c oder später:**

- Teilbäume aus dem Baum einer Oberwurzel liefern (Schlüssel nur exakt)
- Reparatur einzelner Teilbäume nach Änderungen, Live-Updates
- Remote (`KIO::listRecursive`)
- Dedup harter Links, belegte statt scheinbarer Größe
- Persistenz auf Disk
- gemeinsamer Cache über mehrere Fenster
- Optional nicht gehaltene große Bäume trotzdem kurz halten (TTL)
- feinere Ereignisfilter (z. B. versteckte Dateien bei `includeHidden=false`)

**Weiter offen:** M9 (Treemap), T1/T2 und O3 unverändert.

### 14.17 Ergebnisse aus M9: Treemap-View `whale_treemap` (2026-10-05)

Commits `93f1e63f0` (API v6, Host), `3fd59b085` (Plugin, Tests), `a89e24122` (Dev-MCP).
**Keine Änderung an Dolphin-Dateien.** Das Plugin nutzt nur die öffentliche API, Qt und KF6
(Boundary-Lint der Includes plus neuer Test `whale_view_plugin_dependencies`, der die gelinkten
Bibliotheken der gebauten Plugins prüft). Der Stresstest der Architektur ist bestanden: Für eine
View, die keine Dateiliste ist, waren keine Dolphin- oder kitemviews-Interna nötig, wohl aber
**zwei kleine Erweiterungen der Context-API**.

**API-Version 6** (öffentliche API erweitert):

| Erweiterung | Warum | Umsetzung (Host) |
|---|---|---|
| `WhaleViewContext::openFolder(url)` | `activateItems()` kennt nur das Listing; ein tiefer Ordner ließ sich nicht öffnen | Bridge aktiviert die View-Fläche und löst `DolphinView::urlActivated` aus (3.2) → `DolphinMainWindow::handleUrl()` → `setUrl` mit History-Eintrag. Lokale Nicht-Ordner werden ignoriert, andere URLs gehen wie ein Link an die Shell |
| `requestItemInfo(url)` auch für tiefe Einträge | bisher Null-Item für alles außerhalb des Listings | Bridge erzeugt `KFileItem(url)` aus dem Dateisystem für existierende lokale Einträge; fehlende und nicht-lokale URLs gelten als „keins“ |
| `supportsUrl()` wird jetzt genutzt | stand seit M1 in der API, wurde aber nie gefragt | `showPluginView()` verweigert nicht unterstützte Orte; beim Ortswechsel dorthin ersetzt der Host die View durch die Dateiliste (`lastRestore.source = "location"`), die History bringt sie zurück. Der Umschalter deaktiviert Views mit `X-Whale-RequiresLocal` für nicht-lokale Orte (aus den Metadaten, ohne Laden) |

**Plugin und Metadaten:** ID `whale_treemap`, Name „Treemap“, Icon `folder-chart`,
`X-Whale-Capabilities` `Selection`, `RecursiveContent`, `KeyboardNavigation`,
`X-Whale-RequiresLocal: true`; `supportsUrl()` nur `file:`.

**Capabilities:** `Selection | RecursiveContent | KeyboardNavigation`.

- **Kein `Zoom`:** Die Zoom-Actions des Hosts gehen an die `DolphinView`; die API hat keinen
  Zoom-Request an Plugins. Das Gating sperrt sie also zu Recht; die Treemap zoomt mit eigener
  Bedienung (Rad, +/−, Knopf).
- **Kein `MultiSelection`:** Es gibt genau ein ausgewähltes Element („Alles auswählen“ bleibt gesperrt).
- Nicht gesetzt: `HostOrder`, `Grouping`, `Previews`, `Drag`, `Drop`, `InlineRename`.
- Wirkung (unverändertes M5-Gating): Sortieren/Gruppieren/Zusatzinfo, Zoom, Vorschau, Auswahlmodus
  und **alle Actions auf ausgewählten Elementen** sind gesperrt, Letztere mit dem Grund
  `RecursiveContent`, und zwar auch dann, wenn ein Element des Listings ausgewählt ist.

**Aufbau (alles im Plugin):**

| Klasse | Aufgabe |
|---|---|
| `TreemapLayout` | reines Layout, ohne Widgets; testbar ohne View (statische Hilfsbibliothek `whaletreemapcore`) |
| `TreemapRenderer` | Cushion-Bild eines Layouts, Farben, Labels |
| `TreemapCanvas` | `QWidget` + `QPainter`: Zeichnen, Eingabe, Hit-Test |
| `TreemapWidget` | Leiste (Zoom-Knopf, Pfad, Info, Farbmodus, Namen) + Canvas; `Q_INVOKABLE`-Beschreibung für Dev-Tools |
| `TreemapView` | `WhaleView`: Scan, Auswahl, Navigation, Zoom, State |

**Datenquelle:** nur `context->treeScanner()` (Optionen: Vorgaben; versteckte Einträge zählen
immer, weil es um Platz geht, unabhängig von Dolphins Einstellung; keine fremden Dateisysteme).
Ortswechsel bricht den alten Scan ab und startet einen neuen; der alte Baum verschwindet sofort.
`loadingStarted` am selben Ort (F5, Dolphins Reload nach Änderungen) scannt erneut und behält Zoom und
Auswahl. Keine eigene Rekursion, kein eigener Cache.

**Layout:** Squarified (Bruls, Huizing, van Wijk 2000).

- Flächen exakt proportional zu `totalSize()` (relativer Fehler < 1e-6 im Test).
- Deterministisch: absteigend nach Größe, Gleichstand in der Namensreihenfolge des Scanners
  (`stable_sort`), Positionen aus Größenanteilen akkumuliert.
- Lückenlos und überlappungsfrei: Nachbarn teilen sich Kanten, das letzte Element einer Reihe und die
  letzte Reihe enden exakt am Rand. Getestet bis auf das Pixel (jedes Pixel genau einmal per
  Pixelmitte).
- Einträge der Größe 0 bekommen keine Zelle; eine Wurzel ohne Größe zeigt „Nichts belegt Platz“.
- Kleine Einträge: Die kleinsten Kinder eines Ordners, deren Fläche unter 12 px² läge, werden zu
  **einer Rest-Zelle** mit ihrer Gesamtgröße zusammengefasst; ein einzelnes kleines Kind bleibt
  einzeln. Ordner unter 3 px Seitenlänge werden nicht weiter geteilt. So hängt die Zahl der Zellen
  von der Fläche ab, nicht von der Baumgröße, und Proportionen und Hierarchie stimmen trotzdem.
- Zellen in Breitensuche-Reihenfolge mit Eltern-/Kind-Indizes; Hit-Test halboffen (ein Punkt auf
  einer Kante gehört genau einer Zelle).

**Rendering:** `QWidget` + `QPainter`, kein OpenGL, kein QML.

- Echtes Cushion-Shading (van Wijk, van de Wetering 1999): parabolische Grate pro Zelle in beide
  Richtungen (Höhe 0,5, Faktor 0,75 pro Ebene), Licht von oben links, pro Pixel der Blätter in ein
  `QImage` (Gerätepixel, DPR-fähig). Rahmen für die obersten zwei Ordnerebenen.
- Namen, wo sie passen: Ordner der ersten zwei Ebenen oben links, Blätter mittig; Namen überlappen
  nie (Ordner zuerst), zu stark gekürzte Namen entfallen.
- Farben: nach Dateityp (MIME per Endung, Cache pro Endung) oder nach Top-Ordner (Goldener Winkel;
  beim Hineinzoomen bleibt die Farbe des Top-Ordners).
- Das Bild entsteht nur bei neuem Layout, Option, Palette, Schrift oder DPR neu. Hover und Auswahl
  zeichnen nur Umrisse auf das gecachte Bild. Layout lazy vor Paint oder Hit-Test.

**Hover / Info:** Hover hebt die Zelle hervor und meldet sie über `requestItemInfo` (auch tiefe
Einträge, Info-Panel und Statusleiste reagieren). Ohne Hover wird eine **tiefe Auswahl** gemeldet, eine
Auswahl im Listing kennt das Info-Panel ohnehin. Rest-Zellen melden nichts. Die Leiste zeigt den
Eintrag unter dem Zeiger (sonst den ausgewählten) mit Gesamtgröße und Dateizahl.

**Auswahl:** genau ein Element, view-lokal, URL-basiert.

| Fall | Treemap | Whales Auswahl (`DolphinView`) |
|---|---|---|
| Element des Listings | ausgewählt | genau dieses Element (Statusleiste, Info-Panel, Dateiliste nach View-Wechsel) |
| tiefes Element | ausgewählt | **leer** (der Host ignoriert URLs außerhalb des Listings), damit keine unsichtbare Auswahl bleibt |
| Whales Auswahl ändert sich (z. B. neuer Ordner) | übernimmt das erste Element | – |
| Whales Auswahl wird leer | eine Listing-Auswahl verschwindet, eine tiefe bleibt | – |

Keine Datei-Action wird freigeschaltet (siehe Capabilities). Shortcuts wie Entf, Umschalt+Entf,
F2, Strg+X/C/D laufen ins Leere (getestet mit echter Shortcut-Verarbeitung; die Dateien bleiben).

**Navigation / Drilldown:** bewusst getrennte Eingaben.

| Eingabe | Wirkung |
|---|---|
| Klick | wählt das tiefste Element unter dem Zeiger |
| Pfeil links/rechts | Nachbar in der Layout-Reihenfolge |
| Pfeil hoch / runter | übergeordneter Ordner / größtes Kind |
| Enter | öffnet den ausgewählten Ordner bzw. den Ordner der ausgewählten Datei als **Whale-Ort** (`openFolder`) |
| Doppelklick | öffnet den Ordner des Elements unter dem Zeiger als Whale-Ort (Dateien direkt im Ort: nichts) |
| Rad hoch / `+` | zoomt **innerhalb** der Treemap eine Ebene zum Zeiger bzw. zur Auswahl hin (Ort bleibt) |
| Rad runter / `−` / Knopf | zoomt eine Ebene heraus |
| Esc | hebt die Auswahl auf |

Zurück, Vor und Hoch sind Whales normale Navigation. Dateien werden nicht geöffnet (kein Aktivieren
tiefer Elemente vor T1).

**View-State** (`saveState`, Format 1, `QDataStream`): Zoom-Pfad (Namen relativ zum Ort), ausgewähltes
Element (relativ), Farbmodus, Namen an/aus. Über die M6-Infrastruktur: View-Wechsel, History und
Sitzung. Kommt der State vor dem Baum (Ortswechsel, Cache-Miss), wartet er und wird beim Ergebnis
angewendet; nicht mehr existierende Pfade werden so weit wie möglich aufgelöst. Eine inzwischen in
der Dateiliste getroffene Auswahl hat Vorrang vor der gespeicherten.

**Cache-Verhalten:** automatisch über M8b, keine Treemap-Schicht. Zweiter Besuch: Hit (3–4 ms bis
zur Anzeige). Änderung unter dem Ort → Invalidierung → beim nächsten Besuch echter Scan mit neuem
Ergebnis. Eine angezeigte Treemap aktualisiert sich nicht live (Ausnahme: Dolphins Reload des
Listings bzw. F5); Live-Updates bleiben M8c.

**Performance** (Plasma/Wayland, Canvas 1459 × 916, warmer Seiten-Cache):

| Baum | Scan (Worker) | Zellen | Layout | Render | Hover-Repaint | Cache-Hit | Resize (Layout + Render) |
|---|---|---|---|---|---|---|---|
| 1k Dateien | 13 ms | 1.693 | 0,3 ms | 10 ms | ~10 µs | 3 ms | 0,3 + 7 ms |
| 10k | 30 ms | 11.110 | 1,3 ms | 11 ms | ~10 µs | 3 ms | 1,2 + 7 ms |
| 100k | 199 ms | 17.598 (1.111 Rest) | 8,5 ms | 12 ms | ~10 µs | 4 ms | 8 + 7 ms |
| 100k in einem Ordner (Layout-Test, 1600 × 1000) | – | 22.076 | 24 ms | 12 ms | – | – | – |

Normale Bäume sind interaktiv; große degradieren über die Rest-Zellen. Der GUI-Thread scannt nie; Layout
und Render liegen auch bei 100k unter 40 ms. Die Ladeanzeige erscheint nach 150 ms (bei 300k Dateien
sichtbar, Cache-Hits flackern nicht).

**Dev-MCP:** `panes.N.treemap` (Selbstbeschreibung über `treemapState()` des Widgets per Meta-Objekt,
ohne Plugin-Header), `treeCache`, `shell.itemInfoUrl/itemInfoIsDir`, `window.width/height`. Tools
`treemap_hover`, `treemap_select`, `treemap_activate`, `treemap_zoom`, `treemap_key`,
`treemap_set_options` mit **echter Eingabe** (Maus über das Fenster wie QTest, also Klick-Fokus und
Doppelklick-Erkennung; Tasten über die Shortcuts des Fensters zuerst), dazu `set_split_ratio`,
`set_window_size`, `trigger_action reload/show_information_panel`. Ein `hit_test`-Tool war nicht nötig.

**MCP-Abnahme** `test_m9_acceptance.py` (54 Schritte, privater Session-Bus, Dateioperationen erlaubt):
A Grundansicht, B Hover/Info, C Navigation, D View-State, E History, F Split (Fokus, Aktivität, Resize
beider Hälften), G Neustart, H Cache, I Sicherheit der tiefen Auswahl, L Laden/Fehler/`trash:/`.
Grün offscreen (ctest `whale_dev_mcp_m9_e2e`) und auf **echtem Plasma/Wayland** (zweimal), Screenshots
geprüft.

**Tests:** `whaletreemaplayouttest` (18 Fälle), `whaletreemaptest` (22 Funktionen), drei neue Fälle in
`whaleactionstest` (API v6). Volle Suite offscreen seriell 53/54 (bekannter `testAccessibilityTree`).
M3–M6 und M9 auf Wayland grün. **M7 auf Wayland ist flaky** (Schritt C, Drop nicht akzeptiert): auf dem
unveränderten Stand M8b 1 von 6 Läufen, mit M9 3 von 10; offscreen stabil. Vorbestehend, nicht M9.

**Abweichungen vom Plan:**

- API-Version 6 statt 5 (zwei Context-Erweiterungen, `supportsUrl` im Host).
- **Kein T1-Kontextmenü**, überhaupt kein Kontextmenü in der Treemap (Vorgabe: T1/T2 nicht in M9).
- Doppelklick = echte Navigation, Drilldown separat (Rad, +/−); beides nie auf derselben Eingabe.
- Capability `Zoom` nicht gesetzt (s. o.); der State enthält keine „Tiefe“, sondern den Zoom-Pfad.
- Die Treemap folgt nicht Dolphins Hidden-Files-Einstellung und Filterleiste (zeigt immer alles, was
  Platz belegt).

**Offen für T1/T2:** Kontextmenü für tiefe Elemente mit reduzierten Operationen über
`WhaleFileOperations` auf expliziten Listen; „Im Ordner anzeigen“; Datei-Shortcuts in der Treemap;
Öffnen tiefer Dateien; Mehrfachauswahl.

**Weiter offen:** O3 (View pro Ordner), M8c (Teilbäume aus Oberwurzeln, Live-Updates, Remote), M10 (T2),
M11 (RolesUpdater pausieren). Ideen für später: Live-Aktualisierung der angezeigten Treemap über ein
Invalidierungssignal, animiertes Zoomen, Tooltip, Layout im Worker bei sehr großen Flächen.

### 14.18 Ergebnisse aus T1: Operationen auf tiefen Einträgen (2026-10-06)

Commits `cf42f1b50` (API 7, Host), `38fcbedf3` (Treemap-Kontextmenü, Tests), `fcfe12aff` (Dev-MCP).
**Keine Änderung an Dolphin-Dateien.** Kein globales Command-Routing: Whales Actions und ihre
Shortcuts bleiben für die Treemap gesperrt (T2).

**Grundsatz:** Die tiefe Auswahl gehört der View. Sie wird nicht in Dolphins Listing gepresst;
Whales (Dolphins) Auswahl bleibt für tiefe Einträge leer (wie M9). Die View nennt die Einträge
ihrer Operationen selbst, mit URLs, über `WhaleFileOperations`. Der Host führt sie mit Dolphins
bzw. KDEs eigener Logik aus.

**API-Version 7** (`WhaleFileOperations`, nur generische Erweiterungen; Metadaten aller Plugins
kommen weiter aus der CMake-Variablen, exakte Versionsprüfung unverändert):

| Erweiterung | Umsetzung im Host |
|---|---|
| `supportedOperations(urls)` | Regeln von `DolphinMainWindow::updateFileAndEditActions()` über `KFileItemListProperties`: Umbenennen/Ausschneiden nur mit `supportsMoving`, Papierkorb zusätzlich nur lokal, Löschen mit `supportsDeleting`; fehlende lokale Einträge: nur `ShowInFolder` (und das nur, wenn der Ordner existiert); der Ort der Ansicht und Ordner darüber: nie Umbenennen/Papierkorb/Löschen/Ausschneiden |
| `openItems(urls)` | Bridge löst `DolphinView::itemActivated(KFileItem)` aus (3.2) → `DolphinViewContainer::slotItemActivated`: Ordner/Archive werden Ort (mit History, Modifier wie in Dolphin), Dateien per `KIO::OpenUrlJob` mit Rückfrage bei Programmen. Mehrere: Ordner in neuen Tabs, Dateien gemeinsam (wie `DolphinView::slotItemsActivated`) |
| `openItemsWith(urls)` | `KIO::ApplicationLauncherJob` ohne Anwendung = KDEs „Öffnen mit“-Dialog (wie „Andere Anwendung…“ in `KFileItemActions`) |
| `showItemInFolder(url)` | siehe unten |
| `showProperties(urls)` | `KPropertiesDialog` wie `DolphinViewActionHandler::slotProperties` (mehrere Einträge gehen auch) |
| `renameItems(urls)` | `KIO::RenameFileDialog` wie der Dialogpfad von `DolphinView::renameSelectedItems` (nie inline) |
| `trashItems` / `deleteItems` | `KIO::DeleteOrTrashJob` mit `DefaultConfirmation` wie `DolphinView::trashSelectedItems/deleteSelectedItems`: Bestätigung nach Benutzereinstellung, Undo für den Papierkorb, Warnungen durch KIO, Fehler/Erfolg als Meldung im Ansichtsbereich (Bridge löst `DolphinView::errorMessage`/`operationCompletedMessage` aus) |
| `copyItems` / `cutItems` | Zwischenablage wie `copySelectedItemsToClipboard`/`cutSelectedItemsToClipboard` (URLs + most local URLs, Portal-Export, Cut-Markierung); Whales Einfügen funktioniert danach normal |
| `copyLocations` | Pfade als Text wie „Pfad kopieren“, eine Zeile pro Eintrag |
| Signal `operationFinished(op, urls, result, newUrls)` | für Rename/Trash/Delete; `Done`/`Canceled`/`Failed`; bei Rename die neuen URLs |

Jobs gehören dem Ansichtsbereich (Elternobjekt `DolphinView`), nicht der View: ein View-Wechsel
bricht eine laufende Operation nicht ab; Meldungen hängen an der Bridge, das Signal am Context.
Fehlende lokale Einträge werden vor der Operation mit KIOs eigener Formulierung gemeldet
(`ERR_DOES_NOT_EXIST`), ohne Dialog.

Nicht ergänzt (bewusst): Copy To/Move To (Ordnerwahl-Dialog; Kopieren/Ausschneiden + Einfügen
deckt den Export ab), Duplicate, „Öffnen mit“-Untermenü mit Anwendungen, Service-Menüs/
`KFileItemActions`-Plugins. Kein eigener Menü-Service: das Menü baut die View.

**„Im Ordner anzeigen“** (wie Dolphins „Open Path“): Ordner = `KIO::upUrl` (ohne
Schrägstrich am Ende). Ist er schon der Ort, wird der Eintrag sofort ausgewählt (bzw. markiert,
falls noch geladen wird). Sonst wechselt die Shell per `urlActivated` (History, aktiver
Ansichtsbereich). Weil `handleUrl` asynchron ist und `DolphinView::setUrl` Markierungen löscht,
markiert die Bridge den Eintrag erst bei `DolphinView::urlChanged` für den Zielordner
(`markUrlsAsSelected` + `markUrlAsCurrent`, d. h. Dolphins eigenes Auswählen nach dem Laden);
bei `directoryLoadingCompleted` setzt sie die Auswahl notfalls selbst. Geht der Benutzer
vorher woanders hin, verfällt der Auftrag. Die gezeigte View bleibt (Treemap bleibt Treemap,
Datei ist dort Whales Auswahl, also ein Eintrag des Listings; nach Wechsel zur Dateiliste ist
sie dort markiert). Entscheidung: kein automatischer View-Wechsel (wäre View-Typ-Politik im Host).

**Treemap-Kontextmenü:** Rechtsklick (wählt den Eintrag vorher aus, wie in Dolphin) oder
Menü-Taste (für die Auswahl; nötig für Ordner, die von ihren Einträgen verdeckt sind). Einträge
mit Objektnamen: Datei `open`, `open_with`, `show_in_folder` („Show in Folder“), `copy`, `cut`,
`copy_location`, `rename`, `trash`, `delete`, `properties`; Ordner ohne `open_with`, mit „Show in
Parent Folder“. Nur was `supportedOperations` erlaubt, wird gezeigt (nicht deaktiviert); kein Menü
für den Ort selbst, Leerfläche oder Ziele außerhalb; Rest-Zelle → Menü ihres Ordners (der
ausgewählt wird). Keine Whale-Actions, keine Shortcuts im Menü. `popup()`, nicht `exec()`.
Gilt einheitlich für tiefe und gelistete Einträge (ein gezeigtes Dolphin-Menü hätte wegen
`RecursiveContent` nur gesperrte Datei-Actions).

**Selection-Semantik:** unverändert Einzelauswahl wie M9 (kein `MultiSelection`). Tiefer Eintrag:
Whales Auswahl leer, die Treemap hält ihre Auswahl. Gelisteter Eintrag: M9-Spiegelung.

**Verhalten nach Mutation:** Variante A (automatischer Rescan). Bei `Done` und `Failed` (Teil-
änderung möglich) scannt die Treemap neu (`startScan(true)`: Zoom und Auswahl bleiben, soweit es
sie noch gibt; ein umbenannter ausgewählter Eintrag bleibt unter neuem Namen ausgewählt; ein
entfernter verschwindet). `Canceled` ändert nichts. Keine inkrementelle Baumreparatur, kein M8c.
Fund: Ein Rescan direkt nach dem Job konnte den alten Baum aus dem Cache bekommen, weil die
inotify-Meldung zwar im Kernel liegt, der `QSocketNotifier` aber erst später liest.
`WhaleTreeCache::lookup` liest daher vor jedem Treffer anstehende Ereignisse
(`WhaleTreeWatch::processPendingEvents`, nicht blockierend). Das hilft auch F5.
Properties-Dialog (Umbenennen/Rechte dort) meldet kein `operationFinished`; F5 genügt.

**Split-Verhalten:** Operationen betreffen nur die genannten URLs; die rechte Hälfte behält Ort
und Auswahl. Öffnen/„Im Ordner anzeigen“ aktivieren den Ansichtsbereich der View (`setActive`),
Fokus landet in dessen Treemap. Getestet (Unit, MCP, echte Wayland-Eingabe).

**Security/Gating:** unverändert M5/M9: `RecursiveContent` sperrt alle Actions auf ausgewählten
Elementen (Entf, Umschalt+Entf, F2, Strg+X/C/D, Pfad kopieren, Vergleichen, in andere Hälfte
kopieren/verschieben …), auch nach Operationen und bei offenem Menü (getestet, auch mit echter
Shortcut-Verarbeitung). Zusätzlich verweigert der Host Entfernen/Umbenennen des Ortes und seiner
Oberordner (Warnung im Log, `Failed`).

**Grenze zu T2:** Nur das Treemap-Menü (und künftige View-eigene Bedienung) erreicht tiefe
Einträge. Globale Shortcuts/Toolbar/Hauptmenü wissen nichts davon. Für T1 war **kein** Umleiten
globaler Actions nötig.

**Fehlerfälle:** verschwundener Eintrag (Meldung, `Failed`, Menü nur „Im Ordner anzeigen“),
fehlende Rechte (Menüeinträge fehlen; trotzdem aufgerufen: KIO-Fehler, Datei bleibt),
Abbruch (Bestätigung, Rename-Dialog → `Canceled`), Rename-Ziel existiert (KIOs Dialog bleibt
offen mit „A file named … already exists.“), Elternordner weg (keine Operation), Symlinks
(der Link selbst, auch defekte; Ziel bleibt), Ort selbst (verweigert). Papierkorb nicht verfügbar:
KIO-Semantik (Angebot, endgültig zu löschen) – nicht automatisch getestet (bräuchte ein zweites
Gerät außerhalb der Testordner).

**Dev-MCP:** `treemap_context_menu`, `file_operation`, `answer_dialog`; `treemap_key Menu` wie eine
echte Menü-Taste; Zustand `fileOperations`, `message`, `dialogs` (class/text/buttons/urls),
`popupMenu.active`, Treemap `selectedMenu`/`openMenus`/`operationScans`/`lastOperation`.
`test_t1_acceptance.py` (A Öffnen, B Im Ordner anzeigen, C Eigenschaften, D Umbenennen,
E Papierkorb, F Löschen mit KIO-Bestätigung abgebrochen/bestätigt, G Gating, K Kopieren/
Ausschneiden (nur offscreen), H Split, I Rescan/F5/Cache). **Echte Eingabe auf Wayland:**
`--popups real` mit `wayland_real_input.py`: `ydotool` (laufender `ydotoold` des Benutzers),
Zeigerposition über ein KWin-Skript (Ausgabe übers Journal), geregelt bis zum Ziel; geklickt
wird erst, wenn die Treemap die Zielzelle als „hovered“ meldet; Menüeinträge mit echten
Pfeil-/Enter-Tasten. Testfenster per PID gefunden und oben gehalten.

**Funde unterwegs:**

- Echte Menü-Taste (Wayland, geprüft mit PyQt-Probe und `ydotool`): Qt schickt erst ein
  `QContextMenuEvent` (Keyboard), dann den Tastendruck. Eine zusätzliche `Key_Menu`-Behandlung
  hätte zwei Menüs geöffnet; das Menü entsteht nur noch im `contextMenuEvent`. Die Abnahme prüft
  `openMenus == 1`.
- Synthetischer Rechtsklick (Qt 6.10): Das Fenster stellt das Kontextmenü-Ereignis dem Widget
  unter der letzten *Bewegung* zu; eine Bewegung an dieselbe Position wird verworfen. Tests und
  Dev-MCP bewegen daher erst einen Pixel daneben. `QTest::mouseClick` auf Widgets erzeugt gar
  kein Kontextmenü-Ereignis.
- Offscreen wird das Fenster nach Menü/Dialog nicht wieder aktiv; ein synthetischer Treemap-Klick
  aktiviert es jetzt wie ein Fenstermanager.
- Split-Animation: Zellen wandern kurz nach dem Öffnen/Schließen des Splits; die echte Eingabe
  wartet auf stabile Canvas-Größe (die Hover-Prüfung hat das vorher korrekt abgefangen).
- KIOs Löschbestätigung heißt „Delete Permanently“.

**Tests:** `whaledeepoperationstest` (29 Testfunktionen) offscreen und auf Wayland grün
(Zwischenablage nur offscreen). Volle Suite offscreen 55/56 (bekannter `testAccessibilityTree`).
MCP: `test_t1_acceptance.py` offscreen (synthetische Popups und `--popups off`), auf Wayland mit
`--popups off` und dreimal mit `--popups real` grün (56 Schritte, zuletzt am finalen Stand); M3–M7 und M9 auf Wayland grün.
Wayland-Unit-Tests: `whaletreemaptest::testHover` scheitert dort – **vorbestehend**, am
unveränderten M9-Stand `ff9e3c201` in eigenem Build identisch reproduziert (offscreen grün),
dieselbe Ursache wie `whaleactionstest::testItemInfo`. `whale_dev_mcp_m7_e2e` offscreen einmal
in 15 Läufen nach 0,6 s gescheitert, danach 14 grün; Ursache nicht ermittelt (früher Abbruch,
vermutlich Start-Konkurrenz auf dem Bus), nicht T1-spezifisch.

**Abweichungen vom Plan:**

- Kein `KFileItemActions`/Service-Menüs im T1-Menü (Abschnitt 6 hatte sie genannt): „Öffnen mit“
  als KDE-Dialog statt Untermenü. Copy To/Move To, Duplicate vertagt.
- Das Treemap-Menü gilt auch für gelistete Einträge (statt Dolphins Menü).
- „Im Ordner anzeigen“ wechselt nicht zur Dateiliste.
- Nicht ausführbare Einträge werden ausgeblendet statt (wie in Dolphin) deaktiviert.

**Vertagt auf T2:** Shortcuts (Entf, Umschalt+Entf, F2, Strg+C/X/D), Hauptmenü-/Toolbar-Actions
auf tiefe Auswahl, Copy/Move in die andere Hälfte, Vergleichen, Mehrfachauswahl.

**Weiter offen:** O3 (View pro Ordner), M8c (Live-Updates, Remote), M11 (RolesUpdater pausieren),
Toolbar-Themen; Live-Aktualisierung einer zweiten, nicht operierenden Treemap auf denselben Baum
(heute: Cache invalidiert, F5).

### 14.19 Ergebnisse aus T2/M10: Command-Routing (2026-10-06)

Commits `2ed57098c` (API 8, Router, Dolphin-Delegationen), `d30096905` (Treemap, Tests),
`bebbf4f8f` (Dev-MCP). Kein Umbau von Dolphin: nur Delegationspunkte (unten).

**Prinzip:** `globale Action → aktive Selection-Quelle → passender Pfad`. Keine Fake-Selection in
der `DolphinView`. Dolphin bleibt Backend der Dateiliste und Ausführender seiner eigenen Pfade.

**Selection-Quelle (`WhaleCommandRouter`, Host):** immer nur der Ansichtsbereich der aktiven
`DolphinView` (Dolphins `m_activeViewContainer` bzw. `m_currentView` – die M2-Fokus-/Aktivierungs-
mechanik bleibt maßgeblich). Drei Fälle:

| Quelle | wann | Auswahl | Pfad |
|---|---|---|---|
| `file-list` | Dateiliste gezeigt | Dolphins | Dolphin unverändert |
| `plugin-mirrored` | Plugin-View ohne `RecursiveContent` (Testliste) | Whales = Dolphins (gespiegelt) | Dolphin unverändert (M4/M5) |
| `plugin` | Plugin-View mit `RecursiveContent` (Treemap) | `WhaleView::selectedItems()` der View | Router → `WhaleFileOperations` (T1) bzw. Dolphins Code mit den Router-Items |

Der Router findet den Host über `DolphinView::parentWidget()` (der Host zeigt die `DolphinView` in
seinem Stack). Er arbeitet mit `QList<QUrl>`; MultiSelection braucht keinen Umbau.

**API 8:** `WhaleView::selectedItems()` (Default: Whales Auswahl) + `selectedItemsChanged()`;
`WhaleFileOperations::duplicateItems()` + `Operation::Duplicate`. Kein `perform(operation)` in der
öffentlichen API: die View liefert nur ihre Auswahl, der Host entscheidet.

**Geroutete Actions** (zentrale Produktionsliste `WhaleViewActions::routedActions()`, vom
Action-ID-Test geprüft):

| Action | Shortcut | Operation | Ausführung |
|---|---|---|---|
| `renamefile` | F2 | Rename | `WhaleViewActions::slotRename` → Dialog (nie inline) |
| `movetotrash` | Entf | Trash | Handler-Slot → Router |
| `deletefile` | Umschalt+Entf | Delete | Handler-Slot → Router (KIO-Bestätigung) |
| `delete_shortcut` | Entf bei nicht trashbaren Items | Delete | wie Dolphin (aktiv nur, wenn kein Papierkorb möglich) |
| `edit_cut` / `edit_copy` | Strg+X / Strg+C | Cut / Copy | MainWindow `cut()`/`copy()` → Router |
| `duplicate` | Strg+D | Duplicate | Handler-Slot → Router → `DolphinView::duplicateItems` |
| `copy_location` | Strg+Alt+C | CopyLocation | Handler-Slot → Router |
| `properties` | Alt+Enter | Properties | Handler-Slot → Router; ohne Auswahl Dolphins Ort-Eigenschaften |
| `copy_to_inactive_split_view` / `move_to_inactive_split_view` | – | Copy / Cut (Freigabe) | Dolphins `DolphinTabWidget`-Code mit Router-Items (KIO, Undo, Ziel = andere Hälfte) |

Enter → Öffnen ist die Taste der View (Treemap: `openItems`, T1-Pfad; Doppelklick unverändert).
Paste bleibt Dolphins (Ziel = aktiver Ort). „Im Ordner anzeigen“ hat keine globale Action
(nur Treemap-Menü). Nicht geroutet: `show_target`, `compare_files` (Grund im Gate).

**Enablement:** Dolphins `updateFileAndEditActions` rechnet mit den Router-Items (also auch tiefen
`KFileItem`s) – Dolphins eigene Regeln (Rechte, Papierkorb lokal, Ziel der anderen Hälfte schon
enthaltend/schreibgeschützt). Darüber die M5-Gates, für `plugin`-Quellen jetzt selektionsbasiert
statt pauschal: leer → `nothing selected` (kein Selection-Mode in Plugin-Views); Operation nicht in
`supportedOperations` → `not supported for the selection` (Ort selbst/Oberordner, schreibgeschützt,
verschwundene Einträge); nicht geroutet → `not routed to a view's own selection`. Die
Selection-Änderung der View aktualisiert Dolphins Action-Zustände und Gates synchron
(`commandSelectionChanged` → `DolphinMainWindow::slotSelectionChanged`), ebenso View-Wechsel und
Capability-Änderung; Back/Forward über Ortswechsel/Restore (getestet ohne Event-Loop-Lücke).

**Kontextmenü (T1):** unverändert; Menü, Shortcuts, Hauptmenü und Toolbar enden alle in
`WhaleFileOperations` (Router bzw. Menü direkt), keine doppelte Business-Logik.

**Split/Fokus:** Quelle ist stets die aktive Hälfte; Klick in die Treemap aktiviert sie (M2).
Die versteckte `DolphinView` gewinnt nie: bei `plugin`-Quelle wird ihre Auswahl auch dann nicht
benutzt, wenn sie nicht leer ist (Test mit eigener Test-View) oder die View-Auswahl leer ist.

**Duplicate:** Dolphins `duplicateSelectedItems` in `duplicateItems(items)` aufgeteilt (Namensschema
„x copy.ext“, `autoRename`, Undo, Fehler wie Dolphin). Keine Kopie der Logik.

**Compare:** Dolphin vergleicht über `DolphinTabPage::selectedItems()` beider Hälften und braucht
Kompare, das hier nicht installiert ist (Action dauerhaft aus). Technisch wäre es ein 2-Zeilen-
Delegationspunkt in `dolphintabpage.cpp`; ohne Testmöglichkeit vertagt, Gate-Grund gesetzt.

**Dolphin-Dateien (produktiv, ohne Kommentare):**

| Datei | Zeilen | Warum |
|---|---|---|
| `views/dolphinviewactionhandler.h/.cpp` | Hook (Member, Setter, Include ≈ 8) + 5 × 3 Zeilen Guard | Handler ist in `dolphinprivate`, kann `whaleviewhost` nicht linken → Callback statt direktem Aufruf |
| `dolphinmainwindow.cpp` | 1 geänderte Ctor-Zeile, 1 `connect`, 2 × 3 Guard (cut/copy), 3 geänderte Zeilen (Items aus Router), 1 Include | Copy/Cut sitzen im MainWindow; Enablement und Copy/Move-to-other-pane-Prüfung brauchen die Router-Items |
| `dolphintabwidget.cpp` | 2 geänderte Zeilen + Include | Copy/Move to other pane mit Router-Items, Dolphins KIO-Pfad |
| `views/dolphinview.h/.cpp` | Funktion aufgeteilt (≈ 5) | Duplicate ohne Logik-Kopie |

Summe ≈ 35 Zeilen in 6 Dateien, alle als „Whale“ markiert.

**Tests:** `whalecommandroutingtest` (22 Funktionen; Clipboard-Fälle nur offscreen),
`whaledeepoperationstest` (31), angepasst `whaletreemaptest`/`whaleactionstest`/M9-/T1-Abnahme.
Volle Suite offscreen 57/58 (bekannter `testAccessibilityTree`). Wayland-Unit-Tests: nur die
bekannten `whaletreemaptest::testHover` und `whaleactionstest::testItemInfo` (vorbestehend, siehe
14.18). MCP: `test_t2_acceptance.py` offscreen, Wayland synthetisch und mit `--real-keys`
(F2/Entf als echte Tasten) grün; T1 mit `--popups real`, M3–M7, M9 auf Wayland grün.

**Abweichungen/Restpunkte:** Compare und `show_target` nicht geroutet; Copy To…/Move To…-Dialoge,
Mehrfachauswahl-UI weiter offen; „Im Ordner anzeigen“ nur im Treemap-Menü. Treemap-Enter öffnet
Dateien jetzt (M9: Ordner der Datei) – auf Wunsch aus T2. Weiter offen: O3, M8c, M11, Toolbar-Themen.

### 14.20 Ergebnisse aus O3: bevorzugte View pro Ort (2026-10-06)

Commits `c3404ac36` (Host, Tests), `9af282fcd` (Dev-MCP). **Keine Änderung an Dolphin-Dateien**,
**keine Änderung der öffentlichen API** (Version bleibt **8**). Dolphins ViewProperties (`[Dolphin]`,
Icons/Compact/Details) bleiben unberührt.

**Was O3 speichert:** nur „Preferred View for Location“ = eine View-ID pro Ort
(`org.kde.dolphin.itemview` für die Dateiliste oder eine Plugin-ID). Nicht: Scrollposition, Zoompfad,
Auswahl, Farbmodus, Labels, Plugin-Einstellungen, History- oder Session-State – die bleiben in M6
(History-Umschlag, Session-Key) bzw. bei den Views. Keine zweite Persistenzschicht für View-State.

**Variante A (wie vorgegeben):** O3 kennt nur „Dateiliste“ vs. Plugin-Views. Der Modus der Dateiliste
bleibt Dolphins ViewProperties-Sache. Ein Moduswechsel in der Dateiliste schreibt keine Präferenz; die
Wahl eines Modus *in einer Plugin-View* (Icons/Compact/Details-Action, M5) ist eine Wahl der Dateiliste
und wird als „Dateiliste“ gemerkt.

**Klasse `WhaleViewPreferences`** (Host, `host/whaleviewpreferences.{h,cpp}`), prozessweit
(`instance()`), nicht pro Fenster (anders als der M8b-Cache):

| Punkt | Umsetzung |
|---|---|
| Location-Key (`locationKey()`) | Lokal: `QDir::cleanPath` des Pfads (kein Schrägstrich am Ende, kein `//`, `.`, `..`), ohne Query/Fragment; **Symlinks werden nicht aufgelöst** (Link und Ziel sind zwei Orte, wie der Benutzer sie sieht). Andere URLs: `NormalizePathSegments`, `StripTrailingSlash`, ohne Fragment und **ohne Passwort**, leerer Pfad = `/` (`trash:` = `trash:/`), Query bleibt (Teil des Orts, z. B. Suche). Schema/Host klein (QUrl). Schlüssel = voll kodierte URL (`QUrl::FullyEncoded`), damit Unicode/Sonderzeichen genau eine Schreibweise haben. Leere/ungültige URLs: kein Ort. |
| Datei | `KSharedConfig::openStateConfig("whaleviewpreferencesrc")` → `$XDG_STATE_HOME/whaleviewpreferencesrc` (dauerhafte Benutzerentscheidungen, kein Teil von `whalerc`, kein `.directory`/xattr) |
| Format | Gruppe `[PreferredViews]`, Eintrag `<32 Hex-Zeichen von SHA-256(Location-Key)>=<View-ID>,<Location-Key>` (KConfig-Liste). Grund für den Hash: KConfig-Schlüssel vertragen keinen beliebigen Text (`[` leitet z. B. eine Locale ein), URLs können sehr lang sein. Der Wert enthält den Key im Klartext: lesbar, und beim Laden wird geprüft, dass er zum Hash passt (beschädigte/fremde Einträge werden still übergangen). |
| Speicher | `QHash<Location-Key, View-ID>`; Lookup O(1), keine lineare Suche. Schreiben einer Änderung = `sync()` sofort (KConfig mischt in die Datei auf der Platte, Einträge anderer Prozesse bleiben). |
| Mehrere Fenster | Gleicher Prozess: dieselbe Instanz, Änderung gilt ab der nächsten Navigation. Anderer Prozess: ein `stat` pro Lookup vergleicht die Änderungszeit der Datei und liest bei Bedarf neu („spätestens beim nächsten Navigieren“). Gezeigte Views anderer Fenster/Hälften werden **nicht** umgeschaltet. Kein KConfigWatcher nötig. |
| Plugin-Unabhängigkeit | Der Speicher weiß nichts über Plugins: eine Präferenz für ein fehlendes Plugin bleibt stehen. |

**Wann wird geschrieben (User-Intent):** nur über `WhaleViewHost::selectView(viewId)` = die Wahl des
Benutzers: Eintrag im View-Umschalter (`WhaleViewActions::slotViewSelected`) und Icons/Compact/Details
in einer Plugin-View (`slotViewModeTriggered`). Nur wenn die View tatsächlich gezeigt werden konnte.
Auch die Bestätigung der schon gezeigten View wird gemerkt. **Nie** geschrieben: History-, Session- und
Präferenz-Anwendung, `supportsUrl`-Fallback, Fallback wegen fehlendem Plugin, das „Mitnehmen“ der View
an einen neuen Ort, programmatische `showPluginView`/`showDolphinView` (Tests, Dev-MCP ohne `remember`).

**Wann wird angewendet (Priorität):**

| Ereignis | Was entscheidet | Umsetzung |
|---|---|---|
| Back/Forward (History-Eintrag mit Whale-Umschlag) | **History** | `unwrapLocationState()` → `applyViewsState(…, "history")` wie M6; Präferenz wird nicht gefragt |
| Neuer Ort (neuer History-Eintrag, alter Dolphin-State ohne Umschlag) | **Präferenz** des Ziels, sonst bleibt die gezeigte View (Verhalten vor O3) | `unwrapLocationState()` → `applyPreferredView()` |
| Session-Restore | **Session** | `restoreSessionState()` läuft nach dem Setzen der URL; es überschreibt eine gerade angewendete Präferenz bzw. verhindert die Erstanwendung |
| Erster Ort eines neuen Ansichtsbereichs (neues Fenster, Tab, Split-Hälfte) | Präferenz | geht nicht durch `unwrapLocationState()` (Dolphin setzt die URL im Konstruktor); der Host wendet die Präferenz beim ersten `showEvent` an, sofern bis dahin nichts anderes (Session, History, Benutzer, Programm) über die View entschieden hat (`m_initialPreferencePending`). Hintergrund-Tabs also erst beim ersten Zeigen. |
| Danach `checkUrlSupported()` | `supportsUrl()` (M9) | unverändert |

**Default ohne Präferenz:** bestehendes Verhalten, keine Heuristik: die gezeigte View bleibt
(`viewOrigin = "kept"`), anfangs also die Dateiliste. Bewusst so, weil sonst M9-Drilldown (Doppelklick
in der Treemap öffnet einen Unterordner) bei jedem Unterordner zur Dateiliste zurückspränge.
„Clear“ heißt daher: kein Eintrag mehr → Default (bleibt die gezeigte View; von einer Dateiliste aus
also Dateiliste).

**Keine Vererbung:** nur exakt gespeicherte Orte. `~/Pictures` → Treemap macht `~/Pictures/x` nicht zur
Treemap; dass die Treemap beim Hineinnavigieren *sichtbar bleibt*, ist das alte Mitnehmen, nicht O3
(und wird nicht gespeichert).

**Fallback-Semantik:** Präferenz-Plugin fehlt / API inkompatibel (beides: keine gültigen Metadaten) /
nicht ladbar / `supportsUrl()` false → Dateiliste, `lastRestore = {preference, <id>, dolphin,
fallback}`, `viewOrigin = "fallback"`, nur `qCDebug` (die Registry wird bei fehlenden Metadaten gar
nicht erst nach dem Plugin gefragt, so entsteht auch ihre Warnung nicht), **Präferenz bleibt**. Ist das
Plugin wieder da, greift sie beim nächsten Besuch. Eine bereits gezeigte Plugin-View bleibt, auch wenn
ihr Plugin inzwischen als fehlend gilt (die Instanz ist geladen; wie eine Deinstallation zur Laufzeit).

**supportsUrl:** Lokaler Ordner bevorzugt Treemap, Navigation nach `trash:/` → Dateiliste (M9,
`restore.source = "location"`), für `trash:/` wird nichts gespeichert; neue Navigation zurück → Treemap
(Präferenz), Back → Treemap (History). Der Umschalter deaktiviert die Treemap dort wie bisher
(zusätzlich deaktiviert er Views ohne gültige Metadaten, also fehlende Plugins).

**Split / Mehrfenster:** Präferenzen gehören zum Ort, nicht zur Hälfte. Jede Hälfte wendet beim
Navigieren die Präferenz des Ziels an; beide Hälften sehen am selben Ort dieselbe bevorzugte View. Eine
Wahl in der einen Hälfte schaltet die andere nicht um, gilt dort aber ab der nächsten Navigation.
Eine neue Split-Hälfte am selben Ort zeigt dessen Präferenz.

**Reset:** `WhaleViewPreferences::clearPreferredView(url)` + Dev-MCP `clear_preferred_view`; keine
sichtbare UI (Scope).

**Kein Toolbar-/Umschalter-Umbau.** Der Umschalter ruft jetzt `selectView()` statt
`showDolphinView`/`showPluginView` (gleiches Verhalten + Merken).

**Diagnose:** `WhaleViewHost::viewOrigin()`: `user`, `preference`, `history`, `session`, `fallback`,
`kept`, `initial`. `RestoreInfo.source` kennt zusätzlich `preference`.

**Test-Hook:** `WhaleViewRegistry::setPluginAvailable(id, bool)`: Plugin gilt als fehlend (nicht
gelistet, keine Metadaten, keine Instanz), geladene Instanzen bleiben.

**Dev-MCP:** Zustand pro Pane `preferredView`, `viewOrigin`; Tools `get_preferred_view`,
`set_preferred_view`, `clear_preferred_view`, `set_view_plugin_available`; `set_view` mit
`remember: true` geht über den echten Umschalter-Eintrag (aktiviert dazu die Hälfte). **Default von
`remember` ist `false`** (alte Semantik): Die M3–T2-Abnahmen benutzen `set_view` als programmatischen
Umschalter; mit Merken hätten sich ihre Abläufe geändert (z. B. neue Split-Hälfte am selben Ort zeigt
die Präferenz), ohne dass dort etwas kaputt wäre. Einzige Änderung an alten Abnahmen: die erwartete
Tool-Liste in `test_m3_acceptance.py`.

**Tests:** `whaleviewpreferencestest` (16 Testfunktionen, 33 Fälle inkl. 17 Key-Fälle): Key-Semantik
(Schrägstriche, `//`, `.`/`..`, Unicode roh/kodiert, `[`,`=`,`,`,`#`,`?`,`%`, `trash:`/`trash:/`,
Passwort, Query, ungültig), Persistenz/Neustart, lange URLs, Schreiben eines anderen Prozesses,
beschädigte Einträge, kein Default-Wechsel ohne Präferenz, A/B-Wechsel, nur Benutzerwahl wird
gemerkt, `trash:/`, fehlendes Plugin (ohne Warnungen), History-Priorität, Session-Priorität, Split,
neues Fenster/Tab/Hintergrund-Tab, Clear, Icons/Compact/Details. `whaledevautomationtest::testPreferredViews`.
MCP-Abnahme `test_o3_acceptance.py` (38 Schritte, privater Bus, Neustart): A Simple, B zwei Ordner
(+ Unterordner ohne Vererbung), C `trash:/`, D History-Priorität, E Session-Neustart (Präferenz vorher
geändert, Session gewinnt; Präferenzen überleben den Neustart; neue Navigation nutzt sie), F Split
gekreuzt, G fehlendes Plugin (keine Warnungen im Log), H Clear. Grün offscreen (ctest
`whale_dev_mcp_o3_e2e`, 4 Läufe) und auf echtem Plasma/Wayland (2 Läufe, Screenshots geprüft).
Unit-Test 3× offscreen, 3× Wayland grün.

**Abweichungen vom Auftrag/Plan:**

- „Clear → Default-Dateiliste“: Default ist das bisherige Verhalten (gezeigte View bleibt), nicht
  zwingend die Dateiliste; aus der Dateiliste heraus ergibt das die Dateiliste. Grund siehe oben
  (Drilldown, „keine neue Heuristik“).
- Speicherort XDG-State statt `whalerc`/eigener Config-Datei in `XDG_CONFIG_HOME`.
- Mehrfenster-Synchronisation über die Änderungszeit statt KConfigWatcher (kein D-Bus, deterministisch).

**Weiter offen:** M8c (Live-Updates, Remote), M11 (RolesUpdater pausieren), Toolbar-Themen; sichtbares
„Zurücksetzen“ der Präferenz; Vererbung auf Unterordner (bewusst nicht in O3); Compare/`show_target`
(T2-Rest).

### 14.21 Ergebnisse aus F1: „Unterordner einbeziehen" (rekursive Dateien in der Dateiliste) (2026-10-06)

Commits `461a99742` (Test-Views aus dem Produkt), `1b1f8c19f` (Session-Tests, Wayland-Fund),
`9fa715d1f` (Model-Grundlage, Dolphin-Dateien), `04e0bb884` (F1), `d7fde2b7c` (Dev-MCP).
**Öffentliche Plugin-API unverändert (Version 8).** F1 ist keine View: Präsentation (Icons/Compact/Details)
und Inhalts-Scope (Current Folder / Recursive Files) sind getrennt.

**Vorab – Test-Views aus dem Produkt:** `Hello` und `Test List` sind Test-Fixtures. Quellen nach
`src/whale/tests/plugins/`, gebaut nur mit `BUILD_TESTING` als einfache Module in ein eigenes
Test-Plugin-Verzeichnis (`<build>/src/whale/tests/testviewplugins`), nie installiert. Ein Whale aus dem
Build-Tree findet nur noch `whale_treemap` (geprüft: `availableViews = [dolphin, whale_treemap]`).
Unit-Tests fügen das Testverzeichnis zum Library-Path hinzu, MCP-Abnahmen bekommen es über `--plugin-dir`
(Liste mit `:`), `whale-dev-session.sh --test-views` für Handarbeit. Neue Prüfungen:
`whaleviewregistrytest::testProductViews`, `whale_view_plugin_dependencies` schlägt fehl, wenn eine
Test-View im Produktverzeichnis liegt; der Boundary-Lint deckt die Test-Plugins mit ab. Veraltete
`whale_hello.so`/`whale_testlist.so` im Build-Tree und ein altes `whale_hello.so` (API 1) im privaten
Install-Prefix gelöscht.

**Ergebnis des Architektur-Spikes: Die bestehenden Basic Views tragen rekursiven Inhalt mit einer
lokalen Erweiterung des Models.** Entscheidend war ein Befund im Code: `KFileItemModel` kann Items aus
vielen Ordnern bereits *flach* halten. Ein Item ist nur dann Kind (Einrückung, `expandedParentsCount`,
hierarchischer Vergleich in `lessThan`), wenn sein Elternordner selbst im Model steht
(`createItemDataList()`: Parent per `index(parentUrl)`). Ohne Ordnerzeilen ist jedes Item Top-Level.
Filter (`m_filter`, `m_filteredItems`), Sortierung, Gruppierung, Selection (Indexe), Drag-Daten
(`createMimeData`), RolesUpdater (Previews, Icons, MIME), Kontextmenü und alle Actions arbeiten auf den
`KFileItem`s mit ihren echten URLs. Es fehlte nur eine zweite Item-Quelle neben dem `KDirLister`. Kein
Umbau von `DolphinView`/`KDirLister`, keine nachgebauten Views.

**Model-Erweiterung (`kfileitemmodel.{h,cpp}`, ≈ 140 Zeilen, alle „Whale"):**

| API | Verhalten |
|---|---|
| `setExternalItemsEnabled(bool)` | leert das Model; der Lister lädt weiter (Root-Item, Ladezustand, Fehler, Redirects, `clear` bei neuem Ort), seine `itemsAdded/Deleted/refreshItems` werden ausgelassen (Guard in den drei Slots, merkt sich „ausgelassen"); die Löschung des Ortes selbst wird weiter gemeldet (`currentDirectoryRemoved`). Ausschalten zeigt die Lister-Items wieder (sofort aus `items()`, wenn er fertig ist; lädt er noch und hat schon geliefert: `openUrl(Reload)`) |
| `setExternalItems(KFileItemList)` | Diff per URL gegen gezeigte **und** gefilterte Items → bestehende Slots (`slotItemsDeleted`, `slotRefreshItems` mit `KFileItem::cmp`, `slotItemsAdded`). Unveränderte Items bleiben samt Selection/Scrollposition |
| verzögertes `directoryLoadingCompleted` | jedes (Neu-)Laden des Ortes (Lister-`clear`) wartet auf die nächsten externen Items; erst dann `updateViewState()` der `DolphinView` (aktuelles Item, Position, vorgemerkte Selection aus History/Reload) |

Empirisch geprüft (Probe gegen `KCoreDirLister`): `clear` und `started` kommen synchron in `openUrl`,
auch bei `Reload` und bei gecachten Ordnern; Items kommen asynchron. Darauf beruht die Pending-Logik.

**Projektion:** `WhaleTreeScanner` (Cache M8b) → `WhaleRecursiveProjection::project()` im Worker
(`QtConcurrent`, abbrechbar) → `KFileItemList` → `setExternalItems`. Keine zweite Rekursion: der Baum
gibt Struktur, Typ und Hidden-Semantik vor; pro Datei ein `statx` für die Details, die der Baum nicht
hält. Der `UDSEntry` entspricht exakt dem des KIO-file-Workers (NAME, LINK_DEST, FILE_TYPE, ACCESS, SIZE,
LOCAL_USER_ID/GROUP_ID, M/A/CREATION_TIME; kaputte Links `S_IFMT-1`, 0777, Größe 0), MIME verzögert wie
beim Lister. Geprüft gegen echte Lister-Items (`testProjectionEqualsListing`, `KFileItem::cmp`).
URL, Name, Größe, Typ, Datum kommen damit aus Dolphins eigener Infrastruktur; Parent/relativer Pfad über
die neue Rolle (unten). Keine parallele Datenmodellwelt.

**Relative Path:** neue Rolle `relativePath` („Relative Path"/„Relativer Pfad", Gruppe „Other") in der
bestehenden Rollen-Tabelle, ≈ 15 Zeilen: `retrieveData()` (Ordner relativ zu `directory()`, z. B.
`assets/ui/`, leer für direkte Items), natürliche Sortierung, generische Gruppierung. Sichtbar wie jede
Zusatzinfo (Details-Spalte, Sortieren nach). Hinweis: per Ordner gespeicherte `VisibleRoles` mit
`relativePath` kennt ein System-Dolphin nicht (Whale nutzt standardmäßig globale View-Properties).

**Scope-Semantik:** nur Dateien (`File`, `Other`, Links auf Dateien, kaputte Links), **keine
Ordnerzeilen**, auch keine leeren Ordner; Links auf Ordner fallen weg (Ordner für den Benutzer, der
Scanner folgt ihnen nicht). Nur lokale Orte (`file:`); anderswo (trash:, sftp, Suche) ist die Action aus
und die Dateiliste zeigt den Ort selbst. `stayOnFilesystem` wie beim Scanner (Mount-Punkte nicht betreten).

**Hidden Files:** folgt „Versteckte Dateien anzeigen" über die Scanner-Option `includeHidden`: aus →
versteckte Dateien fehlen **und** versteckte Ordner werden nicht betreten (Dateien darunter fehlen); an
→ alles. Umschalten scannt neu (anderer Cache-Schlüssel). Dolphins `.hidden`-Dateien (KIOs `UDS_HIDDEN`)
kennt der Scanner nicht. Backup-Dateien (`~`) über Dolphins MIME-Filter wie gewohnt.

**Filter / Sortierung:** unverändert Dolphins (Filterleiste wirkt auf die rekursiven Items, gefilterte
Items überleben Rescans), Sortierung nach allen Rollen inkl. `relativePath`; keine Ordnersortierung, weil
keine Ordner. Hinweis: Dolphin wählt für Größe/Datum absteigend als bevorzugte Reihenfolge.

**Selection / Operationen:** Dolphins Selection (Indexe, Multi, Select All) über die echten URLs. Alle
Operationen laufen auf den Items der Selection, also auf den echten URLs, nie `Ort + Name`: Öffnen,
Umbenennen (Dialog und inline; im eigenen Ordner), Papierkorb, Löschen, Kopieren/Ausschneiden, Pfad
kopieren, Duplizieren (neben dem Original, danach ausgewählt), Eigenschaften, In andere Hälfte
kopieren/verschieben, Drag-out. Nach Mutationen: die Cache-Invalidierung (M8b) meldet jetzt
`WhaleTreeCache::treeChanged(root)` (gehaltene Bäume und laufende Scans) → `WhaleRecursiveContent`
scannt nach 300 ms neu (während eines laufenden Scans: danach) → Diff. Kein M8c: keine Baumreparatur,
keine Node-Signale; Bäume, die der Cache nicht hält (Budget), werden nicht beobachtet (dann F5).
Vorgemerkte Auswahl (Duplicate, Rename-Dialog) greift über `DolphinView::updateViewState()` nach dem Diff.

**Drag & Drop / Paste – „Recursive darf Quelle sein, nie Ziel":**

- Drag-out unverändert (Dolphins Drag-Daten mit echten URLs).
- Drop-in komplett gesperrt: Event-Filter auf dem Viewport der Dateiliste (`fileListDragWidget()`),
  `DragEnter/Move/Drop` → `IgnoreAction`, ignoriert, nichts erreicht den Controller (kein Ziel-Highlight,
  kein Auto-Öffnen, kein `KIO::drop`). Hintergrund, Dateien, leere Bereiche gleich.
- Erklärung im Nachrichtenbereich der View (`DolphinViewContainer::showMessage`, Information): beim
  `DragEnter` eines fremden Drags **oder** bei einem trotzdem zugestellten Drop, je Drag (`QMimeData`) nur
  einmal, nie bei `DragMove`. Grund für `DragEnter`: Unter Wayland liefert der Compositor einen
  abgelehnten Drop gar nicht aus. Eigene Drags der View (Maus-Press im Viewport vor dem Drag) schweigen.
  Text: „Cannot drop files into a recursive view because it contains items from multiple folders. Turn
  off “Include Subfolders” to choose a destination." (de: „…Schalte „Unterordner einbeziehen“ aus, um ein
  Ziel auszuwählen.") – statt „Switch to Current Folder", weil die UI nur diesen Schalter hat.
- Paste: `DolphinMainWindow::paste()` fragt `WhaleViewActions::routeAction("edit_paste")` (wie T2) →
  keine Operation, Erklärung „Cannot paste into a recursive view because no single destination folder
  is selected. Turn off “Include Subfolders” first." Die Action bleibt im Dolphin-Zustand aktiv
  (abgefangen statt deaktiviert), damit auch Strg+V erklärt wird. Kontextmenü und Selection-Mode-Leiste
  nutzen dieselbe Action.
- Zurück auf Current Folder: Drop und Paste sind wieder Dolphins (getestet).
- Nicht gesperrt: „Neu erstellen" (Ziel ist eindeutig der Ort; ein neuer Ordner erscheint nicht).

**Previews:** Dolphins RolesUpdater für die sichtbaren Items, keine zweite Pipeline. Fund dabei:
`KFileItemModelRolesUpdater::slotItemsInserted` dedupliziert die Elternordner per `QList::contains` –
quadratisch bei Items vieler Ordner (betrifft upstream auch Suchergebnisse): 100k Dateien/2000 Ordner,
Sortierung nach Größe: 1,38 s → `QSet`, Model-Einfügen 1747 → 688 ms.

**Persistenz / History / Session / O3 (Entscheidung A: pro Ort, eigene Präferenz):**

- Laufzeit: `WhaleViewHost` hält den Scope der Fläche; wirksam nur bei gezeigter Dateiliste an lokalem
  Ort (sonst bleibt er gemerkt und greift, sobald die Dateiliste wieder sichtbar ist).
- Präferenz: `WhaleViewPreferences`, gleiche Datei und Schlüssel-Hash wie O3, aber Gruppe
  `[ContentScopes]` (Wert `recursive-files,<location key>`). Nur die Wahl des Benutzers schreibt; „aus"
  löscht den Eintrag (Default = Current Folder). Kein „kept" wie bei O3 und keine Vererbung: sonst würde
  „hoch zu ~" ungefragt das ganze Home scannen. View- und Scope-Präferenz nie in einer ID vermischt.
- History/Session: der Scope ist der **View-State der Dateiliste** (`states["org.kde.dolphin.itemview"]`,
  `QDataStream` v1 mit Name, nur bei Recursive) in der bestehenden M6-Infrastruktur – kein neues Format,
  `FormatVersion` bleibt 1. History hat Vorrang vor der Präferenz, Session ebenso.
- Ablauf beim Ortswechsel: View und Scope werden zusammen entschieden (`m_holdRecursiveContent`), dann
  einmal angewendet. Ursprung `contentScopeOrigin()`: user/preference/default/history/session/initial.
- O3 unverändert: Treemap ignoriert den Scope (Plugin-Views sehen immer das Listing; vor dem Erzeugen
  einer Plugin-View wird der rekursive Inhalt abgeschaltet). Scope wechseln ändert keine View-Präferenz,
  View wechseln keinen Scope.
- Scope-Wechsel: keine Navigation; Einschalten an geladenem Ort per `DolphinView::reload()` (echter
  Ladezyklus: „Loading…", Statusleiste, State-Wiederherstellung), Auswahl wird vorgemerkt; Ausschalten
  stellt die Lister-Items synchron wieder her, Auswahl direkter Items bleibt.

**UI:** `KToggleAction` „Include Subfolders"/„Unterordner einbeziehen" (`whale_include_subfolders`,
Icon `folder-tree`) im Ansicht-Menü neben „View Type", im `view_settings`-Dropdown der Toolbar und im
Hamburger-Menü. Aktiv nur bei Dateiliste an lokalem Ort. `dolphinui.rc` v51, `dolphinuiforphones.rc` v8.
Deutsche Texte: neuer kleiner Whale-Katalog `po-whale/de/whale.po`, beim Build per `msgcat --use-first`
vor Dolphins Katalog gemischt (Dolphins `po/` bleibt unberührt).

**Performance** (offscreen, SSD, warm; Ordner à 50 Dateien in zwei Ebenen; „GUI-Lücke" = längste Pause
eines 10-ms-Timers):

| Schritt | 1k | 10k | 100k |
|---|---|---|---|
| Scan (Worker) / Projektion (Worker) / Model (GUI) | 11 / 10 / 22 ms | 21 / 31 / 188 ms | 186 / 264 / 259 ms |
| Scope an (Details, Name) gesamt (GUI-Lücke) | 73 ms (33) | 268 ms (200) | 719 ms (269) |
| Sortieren nach Größe | 8 ms | 50 ms | 383 ms |
| Filter / Filter aus | 2 / 17 ms | 24 / 183 ms | 265 / 733 ms |
| Wechsel nach Icons | 23 ms | 220 ms | 732 ms (702) |
| Scope aus | 1 ms | 7 ms | 108 ms |
| Scope wieder an (Cache-Hit, Icons, Größe) gesamt (GUI-Lücke) | 56 ms (20) | 745 ms (98) | 2183 ms (701) |

Vergleich Dolphins eigenes Listing eines flachen Ordners: 10k 801 ms (Lücke 197), 100k Details 6575 ms
(Lücke 264), Icons 6101 ms (171), Icons nach Größe 6846 ms (372). F1 zeigt 100k Dateien also rund 9× schneller
als Dolphin einen 100k-Ordner listet, mit vergleichbarer GUI-Lücke in Details. Scan und Projektion laufen
nie im GUI-Thread. Was im GUI-Thread bleibt, ist Dolphins eigenes Einfügen in Model und View (sortieren,
Layout; in Icons nach Größe bis ≈ 0,7 s bei 100k, weil alles in einem Block kommt statt in KIO-Batches) –
dieselben Kosten wie bei Dolphins Sortier-/Filter-/Ansichtswechseln dieser Größe. Kein Chunking eingebaut:
Teilblöcke würden ineinander gemischt und mehr Bereiche erzeugen (siehe „Filter aus"); offen für später.

**Dolphin-Dateien (produktiv):**

| Datei | Änderung | Warum |
|---|---|---|
| `kitemviews/kfileitemmodel.h/.cpp` | externe Items (≈ 120 Z.), Rolle `relativePath` (≈ 20 Z.) | zweite Item-Quelle; Spalte |
| `kitemviews/kfileitemmodelrolesupdater.cpp` | `QSet` statt `QList::contains` (3 Z.) | quadratisch bei vielen Ordnern |
| `dolphinmainwindow.cpp` | Paste fragt Whale (3 Z.), Action im Hamburger-Menü (1 Z.) | Paste sperren; Erreichbarkeit ohne Menüleiste |
| `dolphinviewcontainer.cpp` | `messageRequested` → `showMessage` (3 Z.) | Erklärungen im bestehenden Nachrichtenbereich |
| `dolphinui.rc`, `dolphinuiforphones.rc` | Action + Version | Ansicht-Menü |
| `CMakeLists.txt` (oben) | Whale-Katalog mischen | deutsche Texte |

`DolphinView` und `KDirLister` sind unverändert.

**Dev-MCP:** Zustand `panes.N.contentScope` (scope, shown, supported, origin, preferred, busy, fileCount,
relativeItems, timing, pasteRefusal, lastRefusal, refusals), `fileListPoints` (Item-/Hintergrundpunkte für
echte Eingabe), `loading` inkl. ausstehender rekursiver Items; Tool `set_content_scope`, `trigger_action`
`view_icons/view_compact/view_details`. Kein Drop-Tool nötig: `perform_drag_drop` prüft den Filter mit
denselben Events wie ein echter Drop.

**Tests:**

- `whalerecursivecontenttest` (42 Fälle): Scope 1–5, Basic Views 6–8 (data-driven), Projektion = Lister,
  Model-API allein (Diff, Filter, verzögertes Completed, Ausschalten), Ort gelöscht, Relative Path,
  Selection 9–11, Operationen 12–20, Drop/Paste 21–31, Sort/Filter 32–35, Änderungen/F5, State 36–38
  (Präferenz, History, Session mit Split, O3 zusammen), Treemap ignoriert Scope, Remote, Performance 39–41
  (1k/10k/100k) plus Dolphin-Baseline. Offscreen 42/42, Wayland 41/42 (Clipboard übersprungen).
- `whaledevautomationtest::testContentScope`.
- MCP-Abnahme `test_f1_acceptance.py` (39 Schritte offscreen inkl. K/Paste; 32 auf echtem Wayland, Paste
  dort übersprungen) – A Details, B Icons, C Compact, D Filter, E Sort, F Multi-Selection + Duplicate/Trash,
  G Split (Quelle ja, Ziel nein, Erklärung), H History, K Paste/Drop, J O3+F1, I Session-Neustart; ctest
  `whale_dev_mcp_f1_e2e`.
- Echte Wayland-Eingabe `test_f1_wayland_real.py` (ydotool + KWin-Skript, nötig, weil der Compositor
  DnD vermittelt): echter Klick, Strg+Klick, echter Drag-out (kopiert), echter Drag-in (abgelehnt, eine
  Erklärung trotz weiterer Bewegungen – Zustand mitten im Drag geprüft), ohne Scope normaler Drop. Grün.
- Regression: volle Suite offscreen 61/62 (bekannt `testAccessibilityTree`); M3–M7, M9, T1 (`--popups off`
  und `real`), T2 (inkl. `--real-keys`), O3 auf echtem Wayland grün. Wayland-Unit: bekannt
  `whaletreemaptest::testHover`, `whaleactionstest::testItemInfo`.
- Fund unterwegs (Commit `1b1f8c19f`): Session-Tests zeigten ein neues Hauptfenster vor
  `readProperties()`; unter Wayland löst der Fokus bei gefülltem Session-Clipboard `dataChanged` aus und
  Dolphins `updatePasteAction()` dereferenziert den fehlenden View-Container. Tests restaurieren jetzt wie
  `KMainWindow::restore()` (erst Session, dann `show()`); die App selbst ist nicht betroffen.

**Abweichungen vom Auftrag:**

- Meldungstexte „Turn off “Include Subfolders”…" statt „Switch to Current Folder" (UI hat nur den Schalter).
- Drop-Erklärung schon beim Hineinziehen eines fremden Drags (Wayland stellt abgelehnte Drops nicht zu).
- Paste wird abgefangen (mit Erklärung), nicht deaktiviert.
- Updates nach Änderungen kommen allgemein über die Cache-Invalidierung (auch Änderungen anderer
  Programme), nicht nur nach eigenen Operationen; gedrosselt, kein Live-Repair.
- Neuer Whale-Übersetzungskatalog für die deutschen Texte.
- Kein Chunking des 100k-Einfügens (s. Performance).

**Offen für „Recursive Files + Folders":** Ordnerzeilen bräuchten echte Elternbeziehungen im Model (dann
greifen Einrückung/`lessThan`-Hierarchie und das Ausklappen der Details-Ansicht) oder eine bewusste flache
Darstellung mit Ordnern; Drop auf Ordnerzeilen wäre wieder ein eindeutiges Ziel (Sperre verfeinern);
Ordnergrößen/Zähler (RolesUpdater zählt Ordner per Lister-URL); Links auf Ordner; Sortierung „Ordner
zuerst" pro Ebene.

**Weiter offen:** M8c (Teilbäume aus Oberwurzeln, inkrementelle Updates, Remote), M11 (RolesUpdater
pausieren), Toolbar-Neugestaltung (Scope-Schalter sichtbarer), Chunking großer Einfügungen,
`.hidden`-Dateien im Scanner, sichtbares Zurücksetzen der Präferenzen, Compare/`show_target` (T2-Rest).

### 14.22 Abschluss: Finalisierung des View-Plugin-Branches (2026-10-07)

Review- und Testrunde ohne neue Features. Stand: `feature/view-plugins`, 53 Commits vor `master` (`master`
= `origin/master` = `e160f2980` ist Vorfahr, Merge ist ein Fast-Forward). Einziger neuer Code-Commit:
`4faff6191` (Test-Fix, siehe unten).

**Was der Branch kann:**

- **View-Plugin-Architektur:** öffentliche API `libwhaleviewapi` (nur Qt/KF, keine Dolphin-/kitemviews-
  Typen; Boundary-Lint), Host `whaleviewhost` (statisch, in `dolphinstatic`), Registry pro Hauptfenster,
  `DolphinViewBridge` als einzige Klasse mit kitemviews-Zugriff. `DolphinView` bleibt das kompatible
  Legacy-Backend (die „Dateiliste“, View-ID `dolphin`), Plugin-Views ersetzen sie sichtbar in der
  View-Fläche; der Host kennt keine Treemap-Sonderfälle.
- **API-Version 8**, exakte Übereinstimmung (Metadaten-Schlüssel `X-Whale-ViewApiVersion` und SOVERSION aus
  einer CMake-Variable; Treemap, Test-Plugins und Falsch-Version-Test leiten sich davon ab).
- **Produkt-Views:** Dateiliste (Icons/Compact/Details) und Treemap (`whale_treemap`). `Hello` und
  `Test List` sind nur Test-Fixtures (`<build>/src/whale/tests/testviewplugins`), nie installiert.
- **Auswahl:** drei getrennte Quellen – `file-list` (Dolphin), `plugin-mirrored` (Plugin spiegelt die
  Auswahl des `DolphinView`, M4), `plugin` (eigene tiefe Auswahl bei `RecursiveContent`, T1/T2).
- **Command-Routing (T2):** `WhaleCommandRouter` + zentrale Tabelle `WhaleViewActions::routedActions()`;
  Dolphins Slots fragen zuerst den Router, Gates sperren nicht geroutete Actions; die versteckte
  Dolphin-Auswahl gewinnt nie bei einer Plugin-Quelle.
- **Persistenz:** Session (`WhaleViews`, `WHALESS\x01`), History (Hülle `WHALEVS\x01` um Dolphins
  Location-State), O3 bevorzugte View pro Ort und F1-Scope pro Ort (`whaleviewpreferencesrc`, Gruppen
  `[PreferredViews]` / `[ContentScopes]`), F1-Scope in History/Session als View-State der Dateiliste.
  Vorrang: Session/History vor Präferenz; Präferenzen schreibt nur die Wahl des Benutzers.
- **Rekursive Infrastruktur:** genau eine Rekursion (`WhaleTreeWalker`/fts im `WhaleTreeScanner`), geteilt
  über `WhaleTreeCache` (Invalidierung per inotify, Budgets); Treemap und F1 (`WhaleRecursiveContent` →
  Projektion → `KFileItemModel::setExternalItems`) sind Konsumenten, keine eigene Rekursion.
- **F1 „Unterordner einbeziehen“:** Inhalts-Scope der Dateiliste (Icons/Compact/Details), lokal, nur
  Dateien; Drop/Paste in rekursive Ansicht gesperrt und erklärt.
- **Dev-MCP:** nur mit `WHALE_ENABLE_DEV_MCP=ON` (Default OFF) **und** `WHALE_DEV_MCP=1`; Dateioperationen
  zusätzlich nur mit `WHALE_DEV_MCP_FILE_OPERATIONS=1`; nichts davon wird installiert.

**Review-Ergebnis (Diff gegen `master`):**

- Dolphin-Dateien außerhalb `src/whale`: 17 Dateien, +300/−14 Zeilen, jede Stelle mit „Whale“ markiert
  und begründet (Host-Einbindung, T2-Delegationen, F1-Model-Erweiterung, `QSet` im RolesUpdater,
  Session-/History-Haken, Menü-/Hamburger-Einträge, `rc`-Versionen 51/8). Dazu `CMakeLists.txt`
  (Option `WHALE_ENABLE_DEV_MCP`, Whale-Katalog per `msgcat`), `cmake/WhaleDevMcpOffTest.cmake`.
  Dolphins `po/` unverändert.
- Keine Debug-Ausgaben im Produktcode (nur `qCWarning` für echte Fehlerfälle; `qInfo` nur in Tests), keine
  hardcodierten lokalen Pfade oder Benutzerwerte, keine TODO/FIXME-Reste.
- Test-Haken (`setVisitHookForTesting`, `setDragRunner`, `setPluginAvailable`) liegen im Host (statisch,
  nicht öffentlich), nicht in der Plugin-API.
- Historie: logisch, ein Commit pro Meilenstein, Doku-Commits getrennt, keine WIP-/Debug-Commits. Die
  Zwischenstände mit API-Bumps oder Dolphin-Änderungen bauen einzeln mit Tests und Dev-MCP ohne Warnung
  (geprüft: `93f1e63f0`, `cf42f1b50`, `2ed57098c`, `c3404ac36`, `461a99742`, `9fa715d1f`).
- `clang-format` sauber, `dolphinui.rc`/`dolphinuiforphones.rc` wohlgeformt, SPDX in allen neuen Quellen
  (`*.json.in` über `REUSE.toml` abgedeckt).

**Teststrategie und Ergebnisse dieser Runde:**

| Lauf | Ergebnis |
|---|---|
| Sauberer Produktbuild (neues Build-Verzeichnis, `BUILD_TESTING=ON`, Dev-MCP OFF) | baut, 0 Warnungen; ctest offscreen 50/51 |
| Dev-Build `../build-whale` (Dev-MCP ON) | ctest offscreen 61/62 inkl. Boundary-/Dependency-Lint, `whale_dev_mcp_default_off`, Action-IDs, Scanner/Cache, Treemap, T1, T2, O3, F1 und MCP-E2E M3–F1 |
| Whale-Unit-Tests auf echtem Plasma/Wayland | 16/18, nur die bekannten Altfehler |
| MCP-Abnahmen auf echtem Wayland | M3, M4, M5, M6, M9, O3, F1 grün; M7 1× Flake, 3/3 Wiederholungen grün; T1 `--popups real`, T2 `--real-keys`, `test_f1_wayland_real.py` (echte Drags) grün |
| Produktstart (installiert, ohne Dev-MCP, isoliertes Profil, offscreen + Wayland) | keine Whale-Warnungen; nur Umgebungs-/Portalmeldungen und Dolphins eigenes „Unknown class“ bei leerem Profil |
| Produktdurchgang (Wayland, isoliertes Profil) | Views = [Dateiliste, Treemap]; Scope an in Details/Compact/Icons rekursiv, aus = Dolphin; Treemap; Split; Info-Panel mit Vorschau; Duplizieren |
| State-Randfälle (isoliertes Profil) | unbekannte Plugin-ID → Dateiliste (`fallback`), Präferenz bleibt; kaputter Eintrag ignoriert; `trash:/`: Treemap und Scope verweigert, Dateiliste normal; Neustart/Session über M6/O3/F1-Abnahmen (privater Bus) |

Hinweis Session-Restore beim Handtest: Läuft auf demselben Session-Bus schon ein Whale (Alltags- oder
Dev-Whale), stellt Dolphins `main.cpp` bewusst keine Session wieder her (`instancesCount == 1`); deshalb
laufen Neustart-Abnahmen auf einem privaten Bus.

**Fix während der Finalisierung:** `4faff6191` – `whaletreemaptest::testSessionRestore` zeigte das neue
Fenster noch vor `readProperties()` (die in `1b1f8c19f` für die anderen Tests entfernte Reihenfolge). Auf
Wayland mit gefülltem Clipboard → `updatePasteAction()` ohne View-Container → SIGSEGV im Test. Nur Test,
das Produkt stellt wie `KMainWindow::restore()` zuerst die Session her.

**Bekannte Altfehler (keine Branch-Regression):**

- `DolphinMainWindowTest::testAccessibilityTree` (offscreen; upstream identisch, Umgebung Kubuntu Qt/KF 6.24).
- Wayland: `whaletreemaptest::testHover`, `whaleactionstest::testItemInfo` (seit M6/T1 so, Hover-Signal
  ohne echte Zeigerbewegung).
- Wayland-Timing: `whalepreviewtest`/`whaledeepoperationstest` können am Anfang eines vollen Laufs an
  `qWaitForWindowActive` scheitern (Aktivierung durch KWin), einzeln und im Wiederholungslauf grün.
- M7-Abnahme auf Wayland gelegentlich (Drag der Test-Liste), wie seit M8b beobachtet.
- `filenamehighlightingviewtest` segfaultet auf echtem Wayland (seit vor M3, UX-002/Accessibility).

**Performance-Sanity** (offscreen, wie 14.21): 100k Dateien Scan 185 ms, Projektion 274 ms, Model 260 ms;
Scope an Details 721 ms (GUI-Pause 270 ms); Icons 737 ms (Pause 706 ms); Dolphin-Baseline 100k Details
6596 ms. Treemap-Layout 100k (ein Ordner) 25 ms, Render 11,5 ms. Keine Regression.

**Packaging/Installation:** Installiert werden zusätzlich zu Phase 1 nur `libwhaleviewapi.so.8`(+Link)
und `plugins/whale/views/whale_treemap.so` (bei `/usr`: `qt6/plugins/whale/views`, Qt-Standardpfad). Keine
Test-Plugins, kein Dev-MCP (Manifest geprüft, Produkt-Binary ohne Automation-Strings). Altlast im privaten
Prefix `install-whale`: `libwhaleviewapi.so.1*` (M1, 2026-10-04) bleibt liegen, weil `cmake --install`
nichts entfernt – bei Bedarf von Hand löschen; schadet nicht (anderes SONAME). Für die System-Installation
via `global-install-whale.sh` kommen die drei Dateien neu ins Manifest.

**Übersetzung:** `po-whale/de/whale.po` (6 F1-Texte) wird vor Dolphins Katalog gemischt und landet im
installierten `whale.mo`. Die übrigen ≈ 30 Whale-Texte (View-Umschalter, Treemap-Menü/-Meldungen) sind
noch nicht übersetzt (Englisch als Fallback) – Future Work.

**Known Issues / Future Work (bewusst vertagt, nicht implementiert):**

- **M8c:** Teilbäume aus einer gescannten Oberwurzel, inkrementelle Updates statt Rescan, Remote-Scanner,
  Hardlink-Dedup, belegte statt logische Größe, Disk-Persistenz des Caches, fensterübergreifender Cache,
  feinere Invalidierung.
- **M11:** versteckte `DolphinView` billiger machen (RolesUpdater pausieren usw.).
- **Toolbar:** View-/Scope-UX überarbeiten; doppelte/unschöne View-Auswahl (Dolphins Ansichtsmodi und
  Whale-View-Umschalter nebeneinander); Scope-Schalter sichtbarer.
- **Recursive Files:** „Recursive Files + Folders“ (Ordnerzeilen, Hierarchie, Drop auf Ordnerzeilen);
  optional häppchenweises Einfügen für sehr große Icon-Views (≈ 0,7 s Pause bei 100k); `.hidden`-Dateien
  im Scanner.
- **T2-Rest:** Compare, `show_target`, Kopieren-nach/Verschieben-nach-Dialoge für die Plugin-Auswahl.
- **O3-Rest:** sichtbares Zurücksetzen der Präferenzen; Vererbung optional.
- **Future architecture:** dockbare / mehrfach instanzierbare Views, Workspace-Idee.
- Kleinere: übrige Whale-Texte übersetzen; zweite Treemap ohne Live-Update; Statusleisten-Geometrie folgt
  der versteckten View (kosmetisch); `supportsUrl()`/statische Capabilities im Umschalter nur teilweise
  genutzt; `.privatedoc/whale-dev-mcp.md` ohne SPDX (wie andere private Doku auf `master`); Upstream-Kandidat
  `updatePasteAction()` ohne aktiven Container (nur bei Fenster ohne Tab).

**Merge-Readiness:** Keine echten Blocker gefunden (kein Absturz im Produkt, keine falsche Dateioperation,
Session/History/Präferenzen robust gegen alte/ungültige Daten, Plugins laden, Dateiliste ohne Scope
verhält sich wie Dolphin, keine Test-Plugins oder Dev-Automation im Produkt, API konsistent, keine
Performance-Regression). **`feature/view-plugins` ist merge-ready** (Fast-Forward auf `master`; Merge und
Push nur auf Anweisung).

### 14.23 View-Menü-Cleanup: View-Modi in einer Ebene, Optionen nach Capability (2026-10-07)

Branch `feature/view-menu-cleanup` (von `master` `f6726ab7a`): `3517b21b5` (Produkt + Tests), `c83bb4d7e`
(Dev-MCP). UX-Begründung: `whale-ux-diary.md` UX-004.

- **Eine View-Mode-Liste** `WhaleViewActions::viewModeActions()`: Dolphins `icons`/`compact`/`details`, dann
  ein `QAction` pro Plugin-View (Registry, jetzt nach Plugin-ID sortiert). Die Plugin-Einträge stecken in
  Dolphins exklusiver `view_mode`-Gruppe (damit auch in „Change View Mode“ von Kontext- und Hamburger-Menü).
  `whale_view_switcher` und der Eintrag „File List“ entfallen. `viewModeAction(id)`/`viewModeId()`;
  `viewModeAction(dolphinViewId())` = Eintrag des aktuellen Dateilisten-Modus.
- **Grenze:** `MaxDirectViewModes = 5`; darüber ein `KActionMenu` „View Modes“ mit allen Modi
  (`viewModeMenuEntries()`). Gezählt werden alle Einträge (auch disabled). Anzahl steht pro Fenster fest.
- **Dolphin-Dateien:** `DolphinViewActionHandler::setShownViewEntry()` (Hook, damit `updateViewActions()`
  den Plugin-Eintrag statt des Modus markiert) + Guard in `slotViewModeActionTriggered()` für
  Plugin-Einträge; `dolphinmainwindow.cpp`: `plugViewModes()` nach `setupGUI`, nach
  `addHamburgerMenuToToolbar()`s `createGUI()` und in `saveNewToolbarConfig()`; Hamburger zeigt
  `view_mode` statt des Switchers; `dolphinui.rc` v52 / `dolphinuiforphones.rc` v9 (View-Menü umgruppiert).
  Menüleiste: Einträge vor „Sort By“ eingefügt (eine `ActionList` landete im mit `ui_standards.rc`
  gemergten Menü an verschobener Position).
- **Capabilities:** `WhaleViewHost::supportsViewOption(FlatContent|Previews|HiddenFiles)`; neue Capability
  `HiddenFiles` → **API 9** (exakt). Dateiliste (Adapter) und Test-List deklarieren sie, Treemap nicht
  (zählt versteckte Einträge immer). `show_hidden_files` ist jetzt gateable. Flat: enabled =
  `supportsRecursiveContent()`, Haken = gespeicherter Scope (auch wenn disabled).
- **O3:** Ein Dateilisten-Modus wählt die Dateiliste nur aus einer Plugin-View heraus (wie bisher); die
  frühere Bestätigung der sichtbaren Dateiliste über „File List“ entfällt (Test angepasst).
- **Texte:** „Show flat (no folders)“, „View Modes“ + DE in `po-whale/de/whale.po`; F1-Meldungen und der
  `relativePath`-Tooltip nennen den neuen Namen.
- **Dev-MCP:** State `viewMenu` (placement, modes, viewSettings, options); `set_view remember` über den
  View-Mode-Eintrag.
- **Tests:** `whaleviewmenutest` (neu, 15 Fälle, offscreen und Wayland grün); volle Matrix offscreen grün
  außer dem bekannten `dolphinmainwindowtest::testAccessibilityTree`; Wayland bekannt
  `whaleactionstest::testItemInfo`. Dev-MCP-Abnahmen O3/F1/T1/T2/M9 offscreen grün; Wayland-Sequenz per
  Dev-MCP grün (ohne ydotool, Menü nicht real aufgeklappt).
- **Hinweis Installation:** Die systemweit installierte Treemap (API 8) wird vom Build-Whale (API 9)
  abgelehnt; Tests stellen die Build-Plugin-Pfade deshalb vor die Systempfade. Für das private Prefix
  vor Nutzung neu installieren.
- **Nachtrag `bc827f46b`:** „Show flat (no folders)“ wird beim Zurückwechseln aus einer Plugin-View in
  die Dateiliste wieder aktiviert (`updateContentScopeAction()` auch bei `currentViewChanged`).

### 14.24 Kein Anlegen ohne eindeutigen Zielort; Cushion Treemap (2026-10-07)

Direkt auf `master` (nach `bc827f46b`), nicht gepusht: `2c8dd08e6` (Umbenennung), `fdf3b8389`
(Restzellen), `d6bc1c2d3` (Zielort-Regel + Rückfragen). API bleibt 9. UX-Begründung:
`whale-ux-diary.md` UX-005.

- **`WhaleViewHost::hasDestination()`:** false, solange die Dateiliste flach zeigt (F1) oder eine
  Plugin-View mit `RecursiveContent` sichtbar ist. Dazu `refusal(Refusal::Paste|Drop|Create|Transfer)`
  (Texte der flachen Liste aus `WhaleRecursiveContent::refusal()`, eigene neutrale Texte für Plugin-Views)
  und `refuse()` (Meldung im View-Bereich, gezählt in `WhaleRecursiveContent`, Dev-MCP
  `contentScope.lastRefusal/refusals`). `refusePaste()` entfällt.
- **Gating:** neue gateable Actions `new_menu`, `create_dir`, `create_file`, `create_subdir`, `edit_paste`
  (vor „Selecting“, damit `onSelection` unverändert bleibt); zusammen mit `duplicate` Grund
  „no destination folder“, unabhängig davon, ob die Dateiliste sichtbar ist. Gates folgen jetzt auch
  `contentScopeChanged`. `routeAction()` lehnt diese Actions bei fehlendem Zielort ab (auch Duplicate über
  Dolphins Handler-Hook, D-Bus).
- **Shortcut-Erklärung:** `WhaleViewActions::setDisabledReasonHook()` an Dolphins
  `DisabledActionNotifier`; `updateDisabledReasons()` setzt Whales Grund erneut, nachdem Dolphin eigene
  Gründe gesetzt hat (`updatePasteAction()`, `slotWriteStateChanged()`-Timer). Beim Öffnen des Gates wird
  der Grund gelöscht (Dolphins eigener kommt mit dessen nächstem Update zurück).
- **Drops in Plugin-Views ohne Zielort:** Der Host setzt `acceptDrops` und bekommt alle Drops, die die
  Widgets der View nicht annehmen (Qt sucht den ersten Vorfahren mit `acceptDrops`). Fremder Drag: einmal
  erklärt; eigener Drag der View (`WhaleFileOperationsImpl::currentDragData()`) still abgelehnt. Annahme:
  `RecursiveContent`-Views nehmen selbst keine Drops an. Die flache Liste behält ihren Viewport-Filter.
- **Split View:** `copy/move_to_inactive_split_view` disabled mit Grund, wenn die Zielhälfte keinen Zielort
  hat (`updateFileAndEditActions()`); `DolphinTabWidget` lehnt zusätzlich bei Ausführung ab (Meldung in der
  Zielhälfte). Ändert die andere Hälfte erst danach Scope/View, greift nur die Ablehnung.
- **Rückfragen (Treemap):** `WhaleFileOperationsImpl::setConfirmRemoval()` (vom Host bei fehlendem
  Zielort gesetzt): Papierkorb/Löschen mit `KIO::AskUserActionInterface::ForceConfirmation`, Ausschneiden
  mit nicht-modalem `KMessageDialog` (Liste relativ zum Ort). `setConfirmationRunner()` beantwortet die
  Rückfragen in Tests; danach gelten die KIO-Einstellungen.
- **Erlaubt bleiben** (Entscheidung des Benutzers): Umbenennen, Kopieren, Copy Location, Eigenschaften,
  Öffnen, Papierkorb, Löschen, Ausschneiden. Service-Menü-Aktionen im Kontextmenü der flachen Liste
  unverändert (arbeiten im Ordner der jeweiligen Datei).
- **Treemap:** Name „Cushion Treemap“ (Plugin-JSON). Restzellen flach statt Kissen, immer mit weißem
  `folder-symbolic` (8–24 px, ab 12 px Zellkante), unabhängig von „Names“; „…“ nur ohne Icon-Theme.
- **Dolphin-Dateien:** `dolphinmainwindow.cpp` (Hook, 2 × `updateDisabledReasons()`, Zielhälften-Prüfung),
  `dolphintabwidget.cpp` (2 Guards).
- **Tests:** neu `whalerecursivecontenttest::testCreatingIsDisabled`,
  `testCopyAndMoveIntoRecursiveAreRefused`, `whaletreemaptest::testDropsAreRefused`,
  `whaledeepoperationstest::testRemovalIsAlwaysConfirmed`, `whaletreemaplayouttest::testRestCells`;
  angepasst: Capability-Matrix, Routing (Duplicate/Paste/Transfer jetzt abgelehnt), Deep-Ops, Dev-MCP
  T1/T2/M9/F1. Volle Matrix offscreen grün außer dem bekannten
  `dolphinmainwindowtest::testAccessibilityTree` (auch auf `bc827f46b`). Wayland nicht getestet.
- **Offen:** deutsche Übersetzungen der neuen Texte (Meldungen, Cut-Rückfrage) fehlen in
  `po-whale/de/whale.po`; Gating der Zielhälfte folgt späteren Änderungen der anderen Hälfte nicht live.
