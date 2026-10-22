# FB: Filterleiste (2026-10-22)

Anlass: Der Benutzer hätte das Verhalten des Lock-Knopfs fast zum zweiten Mal als Bug gemeldet.
Dazu zwei Wünsche: Filter nur auf Dateien und die Filterleiste in den Styles (ST).

## Befund im Quelltext

- `FilterBar` (`src/filterbar/filterbar.cpp`): Der Lock-Knopf („Keep Filter When Changing
  Folders“) ist beim Erzeugen aus. `DolphinView::urlChanged` → `FilterBar::clearIfUnlocked()`
  löscht ohne Lock den Text bei jedem Ordnerwechsel. `closeFilterBar()` schaltet den Lock beim
  Schließen wieder aus. „Match case“ und der Modus (Plain/Glob/Regex) werden im State-Config
  (`[FilterBar]`, `caseSensitive`, `filterMode`) gemerkt, der Lock nicht.
- `KFileItemModelFilter::matchesPattern()` prüft den Namen jedes Eintrags, auch von Ordnern. Ein
  Muster wie `*.jpg` versteckt also alle Ordner. In der Baumansicht (Details) bleiben Eltern
  passender Einträge sichtbar (`KFileItemModel`, Zeile ~1363).

## Entscheidungen des Benutzers

| # | Frage | Entscheidung |
|---|---|---|
| E1 | Lock | **Standard an, gemerkt** wie Groß/Klein und Modus. Schließen der Leiste löscht nur den Text. |
| E2 | Worauf wirkt der Filter? | **Einstellung mit drei Werten: Dateien (Standard), Ordner, Dateien und Ordner.** Bei „Dateien“ bleiben alle Ordner sichtbar, bei „Ordner“ alle Dateien. |
| E3 | Style-Element pro View | **Links und rechts getrennt** (`filterBar.left`, `filterBar.right`), wie `viewStyle.*`. |

## Schritte

### FB1: Lock standardmäßig an und gemerkt

- `FilterBar`: Lock aus dem State-Config lesen (`[FilterBar] keepFilter`, Standard `true`), im
  Destruktor schreiben wie die anderen beiden. `closeFilterBar()` lässt den Lock in Ruhe.
- Test: neue Leiste → Lock an; Ordnerwechsel behält den Text; Lock aus → Text weg; Schließen
  behält den Lock-Zustand; neues Fenster liest den gemerkten Zustand.

### FB2: „Filter gilt für“ (Dateien / Ordner / Dateien und Ordner)

- `KFileItemModelFilter`: `enum class Scope { Files, Folders, FilesAndFolders }`,
  `setScope()`. `matchesPattern()`: Einträge außerhalb des Bereichs passen immer (Ordner bei
  `Files`, Dateien bei `Folders`). Mime-Filter bleiben unverändert.
- `KFileItemModel`/`DolphinView`: Durchreichen wie `setNameFilterCaseSensitive`/`FilterMode`.
- `FilterBar`: Auswahl als Knopf mit Menü (oder Combobox) neben dem Modus, Icon je Wert, gemerkt
  im State-Config (`filterScope`, Standard `Files`). Signal `filterScopeChanged`.
- Achtung: Standard „Dateien“ weicht von Dolphin ab (dort wirkt der Filter auch auf Ordner).
- Whale-Besonderheiten prüfen: Plugin-Views (Treemap) über `WhaleViewContext`, die Ansicht
  „Unterordner einschließen“ (zeigt ohnehin nur Dateien), Dev MCP `set_filter_text`.
- Tests: Modell-Test pro Wert (mit Ordnern und Dateien, Glob und Plain), Leiste merkt den Wert.

### FB3: Filterleiste in den Styles

- Element-Typen `filterBar.left` und `filterBar.right` (nach `viewStyle.*`, vor `zoom`).
  Wert z. B. `visible=true,lock=true,case=false,mode=Glob,scope=Files,text=…` (mit Filtertext, F1).
- Die Filterleiste gehört zur View der Position im aktuellen Tab; ohne rechte View wirkt
  `filterBar.right` nicht (es gibt keinen gespeicherten Satz wie beim View-Stil).
- Dev MCP: `styles.current` zeigt das Element automatisch; Abnahme in `test_st_acceptance.py`
  ergänzen.

## Offene Frage (2026-10-22 entschieden: ja)

Der Benutzer: „Auf jeden Fall. Das ermöglicht es, z. B. auf einer Seite nur Medien anzuzeigen,
und auf der anderen Seite die Begleittexte dazu.“

| # | Frage | Entscheidung |
|---|---|---|
| F1 | Gehört der **Filtertext** zum Style? | Ja, optional: Ein Style „Bilder“ könnte gleich `*.jpg` filtern (mehrere Endungen nur per Regex, z. B. `\.(jpg|png)$`). Leerer Text = kein Filter. Die anderen Einstellungen (Lock, Groß/Klein, Modus, Bereich) gehören in jedem Fall dazu. |

Reihenfolge: FB1 → FB2 → FB3, je ein Commit direkt auf `master` (kleine Usability-Fixes).

## Stand

- **FB1** (2026-10-22, `a1795ae34`): Lock standardmäßig an, `[FilterBar] keepFilter` im
  State-Config, sofort geschrieben (neue Views und Tabs starten damit); Schließen lässt den Lock.
  Test `whalefilterbartest`. Befund: Die Knöpfe einer versteckten Filterleiste sind deaktiviert
  (`AnimatedHeightWidget`).
- **FB2** (2026-10-22, `621dd39dd`): `KFileItemModelFilter::Scope` (Standard dort
  `FilesAndFolders`, also KPart und andere unverändert), durchgereicht über `KFileItemModel`,
  `DolphinView`, `DolphinViewContainer`. In der Filterleiste ein Knopf mit Menü nach dem Modus
  (Icons `text-x-generic`, `folder`, `folder-documents`), `[FilterBar] filterScope`, Standard
  `Files`. Dev MCP: `panes.N.filter.scope`; `test_m3_acceptance.py` und
  `whaledevautomationtest` erwarten jetzt den Ordner `sub` beim Filter `b.` (gewollte Änderung).
  Breite: Leiste mindestens 303 px, der neue Knopf 30 px davon.
- **FB3** (2026-10-22): Elemente `filterBar.left`/`filterBar.right` (nach `viewStyle.*`). Wert
  `visible=…,lock=…,case=…,mode=PlainText|Glob|Regex,scope=Files|Folders|FilesAndFolders,text=…`
  (Text zuletzt, darf Kommas und `=` enthalten; versteckt = kein Text). Verfügbar, wenn es die View
  im aktuellen Tab gibt (rechts nur mit Split). Anwenden lässt den Fokus, wo er war. Neue Setter in
  `FilterBar` (Modus, Groß/Klein, Lock, Text). Tests in `whalestyleelementstest`, Abnahme H in
  `test_st_acceptance.py` (links `*.jpg`, rechts `*.txt`, gespeichert und wieder angewendet).
