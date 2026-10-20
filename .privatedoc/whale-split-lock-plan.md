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

