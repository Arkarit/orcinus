# LK: Verlinkte Views im Split (Lock-File-Panels) (2026-10-20)

Branch `lock-file-panels` (lokal, von `master` 5739d9f55). Vorarbeit: CX, Aufklapp-Pfeile in der
Kompaktansicht (`whale-compact-expansion-plan.md`).

Anwendungsfall: In einer View klickt man auf eine Datei, die andere View zeigt dieselbe Datei.
Beispiel: Links die Kissenansicht, rechts die Dateiliste. Ein Klick auf eine Fläche zeigt rechts
den Namen, und umgekehrt.

## Grundidee

- **Lock pro Tab**, nur bei offenem Split. Der Zustand gehört zur `DolphinTabPage`.
- **Neues Objekt `WhaleSplitLock`** in `src/whale/splitview/`, nach dem Muster von
  `WhaleSplitViewButton`. Es besitzt den Toggle-Button und den Zustand und gleicht die beiden
  Views ab. Dolphin-Diff wie beim Split-Button: eine Zeile in `dolphintabpage.cpp`.
- **Button:** Kettenglied-Icon (Breeze `link` / `remove-link`) als Toggle. Er sitzt fest ganz
  links in der rechten File View Toolbar und steht nicht in Configure Toolbars.
  `WhaleFileViewToolBar` bekommt dafür ein Gegenstück zu `setTrailingWidget()`:
  `setLeadingWidget()`. Der aktive Lock muss gut sichtbar sein, weil er das Auswahlverhalten
  ändert (siehe Auswahlregel).

## Entscheidungen des Benutzers

| # | Frage | Entscheidung |
|---|---|---|
| E1 | Bindet der Lock auch den Ort? | **Ja.** Beide Views navigieren gemeinsam. Beim Einrasten übernimmt die rechte View den Ort der aktiven View. |
| E3 | Eine Seite navigiert an einen nicht lockbaren Ort (FTP, SFTP, SMB, `trash:`, Suchergebnisse) | **Lock lösen**, mit kurzer Meldung in der View. Zurück an einem lokalen Ort rastet der Lock **nicht** von selbst wieder ein. |
| E4 | Lock-Zustand in der Session speichern? | **Erst mal nicht.** Nach einem Neustart ist der Lock aus. |
| E5 | Ansichtsmodus beim gemeinsamen Navigieren | **Jede View behält ihren Modus** (Befund beim Test des Benutzers: mit „Use common display style for all folders“ übernahmen beide Views beim Betreten eines Ordners den zuletzt gewählten gemeinsamen Modus). Gilt auch bei Stil pro Ordner, solange der Lock aktiv ist. |
| A1 | Mehrfachauswahl bei aktivem Lock | **Nur innerhalb desselben Ordners** (gleicher Elternordner), nicht über Hierarchieebenen. Ohne Lock bleibt die Auswahl wie in Dolphin. |
| A2 | Strg+Klick auf ein Element in einem anderen Ordner | **Abgelehnt:** Die Auswahl bleibt, dazu eine kurze Splash Message, die die Regel erklärt. |
| A3 | Shift+Klick und Gummiband über Ordnergrenzen | **Begrenzt** auf den Ordner des Ankers: Nur der Teil darin wird ausgewählt, dazu die Splash Message. |
| A4 | „Alles auswählen“ im aufgeklappten Baum | **Nur der aktuelle Ordner.** |
| A5 | Shift+Pfeiltasten über Ordnergrenzen | wie A3: Die Auswahl wächst nicht über die Ordnergrenze, die Message erscheint einmal. |
| A6 | „Auswahl umkehren“ im aufgeklappten Baum | wie A4: nur im aktuellen Ordner. |
| A7 | Was heißt „aktueller Ordner“ (A4, A6)? | Der Ordner des aktuellen Elements, im aufgeklappten Baum also gegebenenfalls ein Unterordner. Ohne aktuelles Element: der Ort der View. |
| A8 | Splash Message | Dieselbe Leiste wie `DolphinViewContainer::showMessage()` (`KMessageWidget`), Typ Information. Sie verschwindet nach einigen Sekunden von selbst und wird bei Wiederholung nicht gestapelt. Dieselbe Meldung dient für E3. |

E2 aus der ersten Fassung (tiefe Auswahl der Kissenansicht in der Dateiliste) ist in
„Darstellung in der anderen View“ aufgegangen.

## Lockbarkeit

- **Abfrage:** Eine Funktion liefert pro View „lockbar ja/nein + Grund“. Ist eine Seite nicht
  lockbar, ist der Button deaktiviert und der Tooltip nennt den Grund.
- **Ort:** Lockbar ist nur ein lokaler Ordner: `QUrl::isLocalFile()` und kein Such-URL (`isSearchUrl()`).
  Die KIO-Protokollklasse `:local` taugt nicht, denn `trash:` gehört auch dazu.
- **View:** Die View muss eine Auswahl abgleichen können. Dafür kommt eine neue Fähigkeit
  `WhaleViewCapability::LinkedSelection` dazu (`src/whale/api/whaleviewcapabilities.h`). Die
  eingebaute Dateiliste (`DolphinViewAdapter`) und die Kissenansicht bekommen sie, Plugins ohne
  diese Fähigkeit sind nicht lockbar. API-Erweiterung: `WHALE_VIEW_API_VERSION` 9 → 10
  (`src/whale/CMakeLists.txt`).
- Wechselt eine gelockte Seite auf eine View ohne `LinkedSelection`, wird der Lock wie bei E3
  gelöst.

## Abgleich

- **Richtung:** in beide Richtungen. Ein Schutz verhindert, dass sich eine Änderung zwischen den
  Views hin- und herspiegelt.
- **Übernommen werden** die Auswahl und das aktuelle Element. Die andere View scrollt zum
  aktuellen Element.
- **Fehlende Elemente** (Filter, versteckte Dateien) werden stillschweigend ignoriert.
- Durch A1 liegt die gespiegelte Auswahl immer in **einem** Ordner: dem Ort selbst oder einem
  Unterordner darunter (aufgeklappter Baum, Kissenansicht).

### Darstellung in der anderen View

| Andere View | Auswahl im Ort | Auswahl in einem Unterordner |
|---|---|---|
| Details, Kompakt (aufklappbar) | auswählen | einen Zweig bis zum Ordner aufklappen (`KFileItemModel::expandParentDirectories()`), dort auswählen |
| Flach (ohne Ordner) | auswählen | auswählen, die Elemente stehen direkt in der Liste |
| Icons | auswählen | den Ordner der obersten Ebene nur als **aktuelles** Element markieren, nicht auswählen (sicher vor Entf usw.) |
| Kissenansicht | aktuelles Element bzw. erstes ausgewähltes | ebenso, auch tief |

Die Kissenansicht kann nur ein Element auswählen (`TreemapView::capabilities()` hat kein
`MultiSelection`). Eine Mehrfachauswahl aus der Dateiliste zeigt sie deshalb als ein Element.

**Tiefe Auswahl:** Die Kissenansicht wählt Dateien unterhalb des Orts aus, die die Dateiliste nur
aufgeklappt kennt. Die API bekommt dafür einen Weg, eine tiefe Auswahl zu setzen und zu melden
(Teil von LK4).

## Ansatzpunkte im Code

- Auswahlregel A1–A8 in `KItemListController` (`src/kitemviews/kitemlistcontroller.cpp`):
  Strg+Klick (`Toggle` in `onPress`/`onRelease`), Shift+Klick (`beginAnchoredSelection`),
  Shift+Pfeil (`keyPressEvent`), Gummiband (`slotRubberBandChanged`). „Alles auswählen“ und
  „Auswahl umkehren“ in `DolphinView::selectAll()` / `invertSelection()`. Eingeschaltet über
  ein Flag am Controller bzw. an der View, das nur `WhaleSplitLock` setzt, damit Dolphin ohne
  Lock unverändert bleibt.
- Ordner eines Elements im Baum: Elternordner über `KFileItemModel` (Ebene/Elternindex), bei
  Elementen im Ort = Ort.

## Phasen

Jede Phase besteht aus Code, Tests und Plan-Eintrag.

1. **LK0:** dieser Plan.
2. **LK1:** Button, Zustand und Lockbarkeit, ohne Abgleich. Lösen nach E3, nicht in der Session (E4).
3. **LK2:** Ort-Bindung (E1).
4. **LK3:** Auswahlregel A1–A8 mit Splash Message, nur bei aktivem Lock.
5. **LK4:** Abgleich der Auswahl zwischen den eingebauten Dateilisten, Darstellungstabelle.
6. **LK5:** API-Erweiterung (`LinkedSelection`, tiefe Auswahl, API 10) und Kissenansicht.
7. **LK6:** Lock-Zustand und ein Werkzeug zum Setzen im Dev MCP; Wayland-Abnahme nur nach
   Freigabe des Benutzers.

## LK1: umgesetzt (2026-10-20)

- `WhaleSplitLock` (`src/whale/splitview/whalesplitlock.*`), erzeugt in `DolphinTabPage` neben
  `WhaleSplitViewButton`. Button `whale_split_lock_button`, ankreuzbar, Icon `link` (gelockt)
  bzw. `remove-link`; Tooltip „Unlink Views“, „Link Views: …“ oder „The views cannot be linked.
  <Grund>“, dann deaktiviert. `lockability()`, `isLocked()`, `setLocked()`, `lockedChanged()`.
- Lockbar sind nur lokale Ordner (siehe oben) in der eingebauten Dateiliste, auch flach. Plugin-
  Views sind bis LK5 nicht lockbar („This view cannot be linked.“).
- Gelöst wird nach E3 bei Ortswechsel und Wechsel der View, jeweils mit Meldung in der View, die
  nicht mehr lockbar ist; beim Schließen des Splits ohne Meldung. Neuer Split, neuer Tab und
  wiederhergestellter Tab starten ungelockt (E4: `DolphinTabPage::saveState()` enthält den Lock
  nicht).
- `WhaleFileViewToolBar::setLeadingWidget()`: `QToolBarLayout` beachtet einen Innenrand am Anfang
  nicht (es legt nur nach der Größe aus). Deshalb hält ein Platzhalter als erstes Element der
  Zeile den Platz frei (`whale_leading_placeholder`), und der Button liegt darüber. Der
  Platzhalter steht nicht in Configure Toolbars; `isLeadingPlaceholder()` blendet ihn in den
  Layout-Hilfen der Tests und im Dev MCP (`layoutOf`, `actionsState`) aus.
- A8: `DolphinViewContainer::showTransientMessage()` (Dolphin-Diff, ~25 Zeilen): Information,
  verschwindet nach 4 s; dieselbe Meldung startet nur die Zeit neu; eine andere Meldung an ihrer
  Stelle wird nicht ausgeblendet.
- Test `whalesplitlocktest` (11 Fälle). Gesamte Suite seriell grün außer `dolphinmainwindowtest`,
  das schon vorher scheitert (`testThumbnailAfterRename`, `testViewModeAfterDynamicView` hängt;
  auf `master` 5739d9f55 ebenso).
- Noch ohne Wirkung: Der Lock bindet weder Ort noch Auswahl (LK2–LK4).
- Nachtrag: In einer schmalen rechten View belegt der Platzhalter einen Button-Platz, ein Button
  mehr geht ins Überlaufmenü. `testMiddleClickNavigation` scheiterte deshalb manchmal (1 von 10),
  weil die rechte View noch in der Öffnungsanimation war (149 statt 214 px). Die Tests warten in
  `showSplit()` jetzt auf das Ende der Animation (wie die Abnahmetests seit 5739d9f55).

## LK2: umgesetzt (2026-10-20)

- Beim Einrasten geht die andere View an den Ort der aktiven View. Danach folgt jede View dem
  Ort der anderen (`WhaleSplitLock::follow()` an `DolphinView::urlChanged`). Gleiche URLs
  (ohne abschließenden Schrägstrich) beenden das Hin und Her.
- Die folgende View navigiert normal und bekommt einen eigenen Eintrag in ihrer Historie.
  „Zurück“ in einer View ist für die andere also ein neuer Ort, kein „Zurück“.
- Sie bleibt dabei inaktiv: neue Dolphin-Methode `DolphinViewContainer::setUrlWithoutActivation()`
  (Dolphin-Diff ~15 Zeilen), nach dem Muster von `redirect()`. Sonst aktiviert der URL-Navigator
  die folgende View bei jeder Navigation.
- An einen nicht lockbaren Ort folgt die andere View nicht; der Lock löst sich (E3).
- Mit „Always follow symbolic links to their target“ landen beide Views am Ziel, die folgende
  bleibt inaktiv.
- Folge für LK5: Bevorzugt ein Ort eine Plugin-View (z. B. die Kissenansicht), wechselt die
  folgende View dorthin, und der Lock löst sich, solange Plugins nicht lockbar sind.
- Test `whalesplitlocktest` jetzt 18 Fälle (+ Einrasten in beide Richtungen, Folgen in beide
  Richtungen, Zurück/Hoch, kein Folgen ohne Lock oder an nicht lockbare Orte, Symlink-Ziel).

## LK3: umgesetzt (2026-10-20)

- `KItemListSelectionManager::setSameGroup()` (Dolphin-Diff, ~110 Zeilen, als Whale markiert):
  eine optionale Regel „gehören zwei Elemente zur selben Gruppe?“. Damit greift A1 an einer
  Stelle für alle Wege: Klick, Strg+Klick, Strg+Leertaste, Shift+Klick, Shift+Pfeil, Gummiband,
  Auswahlmodus.
  - Ein einzelnes Element einer anderen Gruppe kommt nicht hinzu (A2). Abwählen geht immer.
  - Bereiche, auch der verankerte von Shift, behalten nur die Gruppe der bestehenden Auswahl
    bzw. des Ankers (A3, A5). Strg+Shift hängt keinen Bereich einer anderen Gruppe an.
  - Was weggelassen oder abgelehnt wird, meldet `selectionRestricted()`.
  - Beim Einschalten wird eine Auswahl über mehrere Gruppen auf die Gruppe des aktuellen
    Elements reduziert, ebenfalls mit Meldung.
- `DolphinView::setSelectionLimitedToOneFolder()` (Dolphin-Diff, ~55 Zeilen): Gruppe = Elternordner
  der URL. „Alles auswählen“ und „Auswahl umkehren“ wirken dann im Ordner des aktuellen Elements,
  ohne aktuelles Element im Ort (A4, A6, A7). Signal `selectionLimited()`.
- `WhaleSplitLock` schaltet die Regel in beiden Views, solange der Lock aktiv ist. Bei
  `selectionLimited()` zeigt die betroffene View: „While the views are linked, only items of one
  folder can be selected.“ (A8, dieselbe Meldung startet nur die Zeit neu).
- Vom Benutzer bestätigt bzw. entschieden (2026-10-20):
  - **Flache Ansicht:** Die Regel gilt dort nach dem echten Elternordner, obwohl die Ordner nicht
    zu sehen sind. Die Meldung sagt das dort ausdrücklich („This also applies to the flat view,
    which does not show the folders.“).
  - **Abgelehnter Strg+Klick (Maus):** Der Fokus geht zurück auf das Element vor dem Klick, ein
    folgender Shift+Klick beginnt dort (`KItemListController::revertRefusedToggle()`, Dolphin-Diff
    ~30 Zeilen). Strg+Leertaste lässt den Fokus, wo er ist: Dorthin hat man ihn selbst bewegt.
- Tests: `whaleselectionlimittest` (11 Fälle, echte `DolphinView` mit aufgeklapptem Baum, echte
  Tasten für Strg+Leertaste und Shift+Pfeile) und `whalesplitlocktest` (19 Fälle).

## LK4: umgesetzt (2026-10-20)

- **Richtung:** Gespiegelt wird immer von der **aktiven** in die inaktive View. Ein Klick macht
  eine View zuerst aktiv, also geht es in beide Richtungen. Was sich in der inaktiven View ändert
  (Laden nach dem Folgen, Aufklappen, Indexverschiebungen), erreicht die aktive nie. Das ersetzt
  den Schutz gegen Hin- und Herspiegeln.
- Beim Einrasten bekommt die andere View auch die Auswahl der aktiven.
- `DolphinView::showLinkedSelection(selected, current)` (Dolphin-Diff, ~150 Zeilen insgesamt für
  LK4), `currentItemUrl()`, Signal `linkedSelectionChanged()` (sofort, nicht nach 300 ms wie
  `selectionChanged()`; nicht für die eigenen Änderungen durch `showLinkedSelection()`).
  Darstellung nach der Tabelle oben:
  - Elemente in der Liste (Ort, flache Ansicht, schon aufgeklappte Zweige): auswählen; das
    aktuelle Element wird aktuell, Anker dort, die View scrollt hin.
  - Unterordner in aufklappbarer View: `expandParentDirectories()` je Element einmal, Auswahl,
    sobald die Elemente da sind.
  - Unterordner in Icons: keine Auswahl, der Ordner der obersten Ebene wird aktuelles Element.
  - Gefilterte oder versteckte Elemente fallen weg. Ausstehend bleibt nur, was noch geladen bzw.
    aufgeklappt wird; danach wird nichts mehr nachträglich ausgewählt.
  - Ausstehendes verfällt, sobald die View aktiv wird (der Benutzer übernimmt) oder eine neue
    Spiegelung kommt.
- Befund: Die Reaktion auf `KFileItemModel::itemsInserted` muss **verzögert** laufen (queued),
  sonst verschiebt der Auswahlmanager die eben gesetzten Indizes noch einmal.
- Tests: `whalesplitlocktest` jetzt 26 Fälle (+ Spiegeln in beide Richtungen, beim Einrasten,
  nicht aus der inaktiven View, Aufklappen in Details, Icons nur aktueller Ordner, nach dem Folgen
  inkl. „Hoch“, gefilterte Elemente).

### Nachtrag zu LK2: Ansichtsmodus (E5, 2026-10-20)

- Ursache: `DolphinView::setUrl()` wendet die Ansichtseigenschaften des neuen Orts an. Mit einem
  Stil für alle Ordner ist das **ein** gemeinsamer Modus, den die View zuletzt geschrieben hat,
  in der man ihn geändert hat (Dolphins Verhalten, auch ohne Lock). Mit Lock betreten beide
  Views jeden Ordner gemeinsam, also hatten danach beide denselben Modus.
- `DolphinView::setViewModeKept(bool)` (Dolphin-Diff, ~20 Zeilen): `setUrl()` behält dann den
  eigenen Modus (Eigenschaften ohne Speichern, Modus überschrieben); Sortierung, versteckte
  Dateien usw. gelten wie gewohnt. `WhaleSplitLock` setzt es für beide Views, solange gelockt.
- Die „dynamische Ansicht“ (Icons für Bilderordner, `applyDynamicView()`) schaltet eine
  verlinkte View ebenfalls nicht um.
- Test `testViewModesKept` (beide Einstellungen, beide Richtungen, nach dem Lösen wieder der
  Modus des Ordners); scheitert ohne die Korrektur.

## LK5: umgesetzt (2026-10-20)

- **API 10:** `WhaleViewCapability::LinkedSelection` (`1 << 12`) und
  `virtual void WhaleView::showLinkedSelection(const WhaleSelection &)`. Die Standard-Umsetzung
  macht die Elemente des Listings zu Whales Auswahl (`context()->setSelection()`), was einer View
  reicht, die Whales Auswahl zeigt. Die Auswahl darf tief sein (unterhalb des Listings).
  `WHALE_VIEW_API_VERSION` 9 → 10. Dateiliste (Adapter) und Kissenansicht deklarieren die
  Fähigkeit, die Test-Liste nicht (dient im Test als nicht verlinkbare View).
- **Lockbarkeit:** statt „nur Dateiliste“ jetzt die Fähigkeit der angezeigten View
  (`currentView()->capabilities()`); `capabilitiesChanged` des Hosts löst eine Prüfung aus.
- **Host:** `WhaleViewHost::linkedSelection()` (Dateiliste: Auswahl + aktuelles Element; Plugin:
  `selectedItems()`, also bei der Kissenansicht ihre eigene, ggf. tiefe Auswahl; aktuelles Element
  ist ein ausgewähltes) und `showLinkedSelection()` (Dateiliste → `DolphinView::showLinkedSelection()`,
  Plugin → `WhaleView::showLinkedSelection()`). `WhaleSplitLock` spiegelt zusätzlich bei
  `viewSelectionChanged` des Hosts (eigene Auswahl eines Plugins).
- **Kissenansicht:** zeigt ein Element: das aktuelle, wenn es ausgewählt ist, sonst das erste
  ausgewählte; nichts ausgewählt → Auswahl leer. Tief ausgewählte Einträge gehen als tiefe Auswahl
  in die andere View (Details/Kompakt klappen auf, Icons markieren den Ordner der obersten Ebene).
- **Befund (Lücke aus LK4):** Beim Aufklappen über mehrere Ebenen meldet `KFileItemModel`
  `directoryLoadingCompleted` schon, bevor der letzte Ordner gelistet ist
  (`expandParentDirectories()` klappt ihn zusätzlich direkt nach dem Einfügen auf, `slotCompleted()`
  findet ihn dann schon aufgeklappt). Die Auswahl verfiel dadurch. Jetzt wartet die View, solange
  der aufgeklappte Ordner noch keine Kinder zeigt. Das Modell bleibt unverändert.
- **Tests:** `whalesplitlocktest` jetzt 32 Fälle (+ Lock mit Kissenansicht, Spiegeln aus ihr inkl.
  tief, Spiegeln in sie inkl. Mehrfach- und tiefer Auswahl, Test-Liste nicht verlinkbar);
  `whaletreemaptest::testCapabilityGating` erwartet `LinkedSelection`.
- **Hinweis Installation:** Die installierte Kissenansicht (API 9) wird von einem Whale mit API 10
  abgelehnt; nach dem Merge neu installieren (privates Prefix und systemweit).

## LK6, Teil 1: Dev MCP (2026-10-20)

- **Zustand:** `get_ui_state` → `splitLock` (null ohne Lock-Objekt): `locked`, `buttonVisible`,
  `buttonEnabled`, `buttonChecked`, `buttonIcon`, `toolTip`, pro Pane `lockable`, `reason`,
  `selectionLimited`. Die Meldungen stehen wie alle in `panes.N.message`.
- **Werkzeug:** `set_split_lock(locked)`, ein Setter über einen Klick auf den Button. Er scheitert,
  wenn der Button verborgen (keine geteilte Ansicht) oder deaktiviert ist, und nennt dann den
  Tooltip als Grund.
- **Abnahme offscreen:** `test_lk_acceptance.py` (ctest `whale_dev_mcp_lk_e2e`), 20 Schritte A–H:
  Button nur in der geteilten Ansicht, Einrasten übernimmt den aktiven Ort, Folgen inkl. „Hoch“,
  Auswahl in die andere View, Kissenansicht in beide Richtungen inkl. tiefem Eintrag (Icons:
  Ordner der obersten Ebene wird aktuell), Lösen durch die Test-Liste, durch `trash:/` und beim
  Schließen der geteilten Ansicht; kein Wieder-Einrasten von selbst. `test_m3_acceptance.py` führt
  das Werkzeug in der festen Werkzeugliste.

## LK6, Teil 2: Wayland-Abnahme mit echter Eingabe (2026-10-20, freigegeben vom Benutzer)

- `test_lk_wayland_real.py` (ydotool + KWin-Skript, privater D-Bus, nicht in ctest wie F1),
  16 Schritte R1–R7 grün: echter Klick auf den Lock-Button (rechte View übernimmt den Ort,
  behält Icons), Klick auf ein Element (gespiegelt), Rechts-Taste klappt auf, abgelehnter
  Strg+Klick in einen Unterordner (Auswahl und aktuelles Element bleiben, Meldung), Shift+Klick
  über Ordnergrenzen auf den Ordner des Ankers begrenzt (Meldung), Klick in die andere View
  (sie wird aktiv, Spiegeln in Gegenrichtung), Klick auf einen tiefen Eintrag der Kissenansicht
  (Details klappt auf und wählt ihn aus), echter Klick löst den Lock. Screenshots geprüft.
- `splitLock.buttonX/buttonY` im Dev MCP (Mittelpunkt in Fensterkoordinaten).
- **Befund 1 (behoben, Lücke aus LK4):** Die inaktive View zeichnete einen falschen Bereich
  (`sub, a.txt, b.txt` statt `b.txt`), obwohl ihre Auswahl stimmte. Ein Shift-Anker von vorher
  (aus einem tiefen Eintrag in Icons) blieb aktiv; `setCurrentItem()` meldete den Bereich bis zum
  neuen Element, `beginAnchoredSelection()` verwarf ihn danach ohne Signal. Jetzt beendet
  `applyLinkedSelection()` einen alten Anker zuerst. Test `testMirrorShowsWhatIsSelected`
  (scheitert ohne die Korrektur).
- **Befund 2 (UX, behoben, Benutzer: „Overlay ist vermutlich deutlich besser“):** Die Meldung („only items of one folder“) fährt **über** der Dateiliste
  ein und schiebt sie um eine Zeile nach unten; nach 4 s rückt die Liste wieder hoch. Mitten beim
  Klicken springt also das Ziel unter dem Zeiger, im Test traf ein Shift+Klick deshalb erst
  nichts. Dolphins eigene Meldungen tun dasselbe, sind aber selten und verschwinden nicht von
  selbst. Vorschlag: die Lock-Meldungen als Overlay oder in der Statusleiste zeigen.

### Lock-Meldungen als Overlay (2026-10-20)

- `WhaleTransientMessage` (`src/whale/splitview/whaletransientmessage.*`): ein eigenes
  `KMessageWidget` (Information, ohne Schließen-Knopf), schwebend unten mittig über dem View-Host,
  so breit wie der Text (höchstens der Bereich), deckend hinterlegt, **für die Maus durchlässig**,
  nach 4 s weg; derselbe Text startet nur die Zeit neu. Kind des `DolphinViewContainer` (nicht des
  View-Hosts: dort holt ein View-Wechsel die gezeigte View nach oben und verdeckte das Overlay).
- `DolphinViewContainer::showTransientMessage()` und sein Timer sind wieder entfernt (Dolphin-Diff
  kleiner); Dolphins Meldungsleiste bleibt unberührt und unabhängig.
- Dev MCP: `panes.N.transientMessage`. Test `testTransientMessage` prüft, dass der View-Host sich
  nicht verschiebt, die Lage unten im Bereich und die Mausdurchlässigkeit. Im Wayland-Test fällt
  die Wartezeit vor R4 weg.


## LK7: Befunde aus dem Alltag (2026-10-22, Plan)

Drei Befunde des Benutzers nach dem ersten Einsatz.

**B1: Aufklappen in der anderen View taugt nicht.** Mit dem Lock klappt die inaktive View für
eine Auswahl in einem Unterordner diesen Zweig auf (LK4, `DolphinView::applyLinkedSelection()`,
`expandParentDirectories()`). In echten Ordnern öffnen sich so riesige Bäume, und besuchte Zweige
klappen nie wieder zu.

**B2: Icons links, Kissen rechts, von rechts nach links „geht gar nicht“.** Offscreen
nachgestellt: Ein Klick in die Treemap trifft immer den tiefsten Eintrag (ein Ordner ist von seinen
Einträgen bedeckt), die Auswahl liegt also fast immer in einem Unterordner. Icons kann nicht
aufklappen und macht nur den obersten Ordner zum aktuellen Element (LK4-Rückfall), ohne Auswahl;
in Icons kaum sichtbar. Vom Benutzer bestätigt. Liegt der Eintrag direkt im Ordner, funktioniert
es.

**B3: Der Lock soll Teil eines Styles sein.**

### Entscheidungen

- **E6 (ersetzt das Aufklappen aus LK4):** Der Ordner des aktuellen Elements der aktiven View (wie
  A7) bestimmt die andere View. Kann die andere View den Eintrag **an Ort und Stelle** zeigen
  (Treemap unter ihrem Ort, ein vom Benutzer selbst aufgeklappter Zweig in Details/Kompakt),
  wählt sie ihn dort aus, ohne zu wechseln. Sonst **wechselt sie in den Ordner des Eintrags** und
  wählt ihn dort aus. Kehrt die Auswahl zurück (z. B. auf einen Eintrag direkt am Ort der aktiven
  View), wechselt sie wieder zurück. Von sich aus klappt sie nie mehr etwas auf. Der Wechsel
  nimmt die aktive View nicht mit (heute folgt jede View jedem `urlChanged` der anderen: Sperre
  nötig). E1 gilt weiter für die Navigation der aktiven View.
- **E7:** Solche durch die Auswahl ausgelösten Wechsel erzeugen **keinen Verlaufseintrag**; der
  Verlauf einer View enthält nur ihre eigene Navigation (Weg wie bei „Symlinks folgen“:
  `goBack()` mit blockierten Signalen, dann neuer Ort; vorher prüfen, ob es sauberer geht).
- **E8:** Neues Style-Element `splitLock` (an/aus), nach `viewStyle.left/right` (die Lockbarkeit
  hängt von der gezeigten View ab), nur bei `split = true` wirksam. Ältere Styles ohne das
  Element lassen den Lock unverändert. Ist eine Seite nicht lockbar, bleibt er aus (mit der
  üblichen Meldung). Gilt **nur für den aktuellen Tab**, nicht für neue Tabs/Fenster (wie E4).

### Schritte

- **LK7a:** E6 + E7 in `WhaleSplitLock` / `DolphinView::applyLinkedSelection()`; Aufklappcode
  (`m_linkedExpansions`) und Icons-Rückfall entfernen. Tests anpassen (`whalesplitlocktest`,
  `test_lk_acceptance.py` Schritt E), neue Fälle: Icons folgt in den Unterordner und zurück, Treemap
  bleibt an Ort und Stelle, aufgeklappter Zweig rechts bleibt, aktive View bleibt stehen,
  Verlauf der anderen View unverändert.
- **LK7b:** E8 (`WhaleStyleElementTypes`, `whalestyleelementstest`, Dev-MCP-Abnahme).
