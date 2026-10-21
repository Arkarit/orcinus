# PI: Panel-Identität (2026-10-21)

Arbeit auf `master` (kleine Schritte, siehe Workflow). Vorstufe für das Style-System (siehe unten).

## Befund (Bug-Repro des Benutzers)

Repro: Dual Panel, links Compact, rechts Icons. Dann Whale schließen (oder vorher den Split
schließen). Nach dem Neustart erscheint ein **einzelnes Panel in Icons**.

Ursache (im Code und am Zustand von `~/.local/share/whale/view_properties/global` geprüft):

- `GlobalViewProps` hat den kcfg-Default `true`, und `whalerc` überschreibt ihn nicht. Alle
  Ordner und **beide Panels** teilen sich deshalb einen einzigen Satz in
  `view_properties/global` (`viewproperties.cpp:350`).
- `DolphinView::setViewMode()` schreibt sofort (`dolphinview.cpp:362`). Das zuletzt geänderte
  Panel gewinnt, auch wenn das andere Panel noch offen ist oder übrig bleibt.
- `SplitView` (Default `false`) ist nur eine Startoption. Der Split-Zustand wird nirgends gemerkt.
- `DolphinView::saveState()` (Session) enthält keinen View-Modus.

Upstream-Dolphin verhält sich gleich. Der Code stammt aus der Zeit mit nur einer View.
Verwandt: LK-Entscheidung E5 (`whale-split-lock-plan.md`). Dort kam der Befund aus derselben
Ursache (Navigieren liest den gemeinsamen globalen Satz). Gelöst wurde er mit
`setViewModeKept()`.

## Grundidee

Jedes Panel hat eine **Identität nach Position**: `Left` oder `Right`. Im globalen Modus hat jede
Rolle ihren **eigenen vollständigen ViewProperties-Satz**: Modus, Sortierung, Gruppierung,
versteckte Dateien, Vorschau und Spalten. Jedes Panel liest und schreibt nur seinen eigenen Satz.

- Die Identität hängt an der **Position**, nicht am View-Objekt. Wenn man das linke Panel
  schließt, rückt das rechte nach links (heute schon Zeiger-Swap in `dolphintabpage.cpp:141` ff.).
  Beim Rollenwechsel schreibt die View ihren aktuellen Zustand vollständig in den Satz der neuen
  Rolle (`ViewProperties::setDirProperties()`). Damit ist Repro 2 gelöst.
- Den **Ordnermodus** (`GlobalViewProps=false`) fasse ich nicht an, dort gehören die Properties
  zum Ordner.
- Die **KPart** und Aufrufer ohne Identität verwenden `Left`, also das bisherige Verhalten.
- Mehrere Tabs oder Fenster: Pro Position gewinnt weiterhin die letzte Änderung.

## Entscheidungen des Benutzers

| # | Frage | Entscheidung |
|---|---|---|
| a | Split beim Start | **Enum:** Immer Split / Immer Single / Letzter Zustand. Ersetzt die Checkbox „Open new windows in split view mode“. |
| b | Split neu öffnen: Werte des rechten Panels | **Eigene Werte** der Rolle `Right`. Später als Enum im Style-System möglich, das dann Vorrang hat. |

## Stand (2026-10-21)

Branch `feature/panel-identity` (von `master` 4153886a7), nicht gepusht:

| Schritt | Commit | Inhalt |
|---|---|---|
| PI0 | 44ef0abc7 | Upstream-Bug in „Split View To Tabs“ behoben: Bei „Inactive view“ (und bei „Right view“ mit aktiver linker View) blieb die aktive View stehen und wurde dupliziert, die andere ging verloren. Jetzt wird immer die verschobene (aktive) View geschlossen. |
| PI1 | c9b081c80 | Rollen-Speicher in `ViewProperties` (`global` / `global-right`), Fallback Right → Left. |
| PI2 | f8ba51ca9 | Rollen in `DolphinTabPage`, Mitnahme beim Schließen der linken View, `keepGlobalStyle()`. |
| PI3 | a720711d3 | `SplitViewAtStartup` (LastState/AlwaysSplit/AlwaysSingle), Migration, Combobox. |
| PI4 | bc499fe77 | Dev MCP: `panes.N.panelRole`, `splitViewAtStartup`. Doku-Gliederung 2.4 in `docs/whale-vs-dolphin.md` (ungetrackt wie die übrige Datei). |

Abweichungen vom Plan:

- **`WhalePanelRole`** ist ein eigener Header (`whale/splitview/whalepanelrole.h`), in
  `DolphinView` als `PanelRole` verfügbar. Grund: Include-Zyklus (`dolphinview.h` →
  `dolphintabwidget.h` → `dolphintabpage.h`).
- **Die Rolle kommt über den Konstruktor** (`DolphinView`, `DolphinViewContainer`). Sonst läse
  eine neue rechte View zuerst den linken Satz und `setPanelRole()` würde ihn mitnehmen.
- **`keepGlobalStyle()`:** Eine rechte View, die erscheint, schreibt den geerbten Stil sofort
  als eigenen fest. Sonst bleibt `setViewMode(Icons)` bei schon gezeigtem Icons wirkungslos,
  und die rechte View folgt später doch der linken. Das hätte Repro 1 nicht behoben.
- **Einstellungsseite „Ansicht“:** Sie schreibt beide Rollen **nur beim Umschalten** von „pro
  Ordner“ auf „global“. Bei jedem Apply würde sonst der rechte Satz mit dem linken
  überschrieben.
- **Letzter Zustand:** wird geschrieben bei jedem Öffnen/Schließen des Splits
  (`DolphinTabPage::setSplitViewEnabled`) und beim Schließen des Fensters (aktueller Tab).
- **Tests:** `TestHelpers::openNewWindowsInSingleView()` in allen Fenster-Tests,
  `keep_single_view_at_startup()` in `mcp_test_support.start_whale()`. Ohne diese Helfer
  würde ein offen gebliebener Split in den nächsten Test durchschlagen.

Offen / bekannt:

- „Split View To Tabs“: Der neue Tab hat eine einzelne (linke) View und zeigt deshalb den
  **linken** Stil, auch wenn die rechte View verschoben wurde. Den Stil mitzunehmen würde den
  linken Satz der verbleibenden View überschreiben. Gehört in das Style-System.
- Die Integration der Aktion in den `<<`-Button bleibt offen (siehe Memory „Upstream Split
  View To Tabs“).
- Vorbestehend und unabhängig davon (auch auf `master`): `dolphinmainwindowtest`
  `testThumbnailAfterRename` schlägt fehl, `testViewModeAfterDynamicView` läuft in einen
  Timeout und bricht den Lauf ab. `testAccessibilityTree` ist im Gesamtlauf gelegentlich rot.

## Schritte

### PI1: Rollen-Speicher für ViewProperties

- Neue Rolle `Whale::PanelRole { Left, Right }`, z. B. unter `src/whale/panels/`.
- `ViewProperties(const QUrl &url, PanelRole role = Left)`: Im globalen Modus ist der Speicherort
  `global` für `Left` und `global-right` für `Right`. Alle anderen Zweige (Ordner, trash, search …)
  bleiben unverändert.
- **Migration ohne Kopierschritt:** Solange `global-right` noch keine gespeicherten Properties
  hat, liest `Right` aus `global`. Beim ersten Schreiben entsteht der eigene Satz. Für bestehende
  Nutzer ändert sich dadurch zunächst nichts.
- `DolphinView` bekommt `setPanelRole()` / `panelRole()`. Die Aufrufe
  `ViewProperties props(viewPropertiesUrl())` reichen die Rolle mit (mechanisch, ca. 25 Stellen).
- `ViewPropertiesDialog` übernimmt die Rolle seiner View.
- Unit-Test: zwei Rollen, verschiedene Modi, neu laden. Jede Rolle behält ihren Modus.
  Fallback von `Right` auf `Left` testen.

### PI2: Rollen setzen und wechseln (DolphinTabPage)

- Beim Anlegen: primär `Left`, sekundär `Right`.
- Beim Schließen des linken Panels und beim Tauschen: Rollen neu setzen. Die View, die ihre
  Rolle wechselt, schreibt ihren Zustand in den neuen Satz.
- Split neu öffnen: Die rechte View liest den Satz `Right` (Entscheidung b).
- Prüfen, ob `setViewModeKept()` (LK/E5) im globalen Modus damit überflüssig wird. Im Ordnermodus
  bleibt es nötig.
- Test (offscreen, dev MCP): Repro 1 und 2 nachstellen, ohne Desktop-Übernahme.

### PI3: Split-Zustand beim Start (Enum)

- Ein neuer kcfg-Eintrag (Enum `LastState`, `AlwaysSplit`, `AlwaysSingle`) ersetzt `SplitView`
  (bool). Default ist `LastState`.
- Migration: alter Wert `SplitView=true` → `AlwaysSplit`. `false` war der Default und keine
  bewusste Wahl, daher → `LastState`.
- `LastState`: Der Zustand liegt in `whalestaterc` (z. B. `[Panels] SplitViewEnabled`). Er wird
  beim Öffnen und Schließen des Splits geschrieben, die letzte Aktion gewinnt.
- Aufrufer: `main.cpp:209`, `dolphintabpage.cpp:56` (neue Tabs), `dbusinterface.cpp:36/48`,
  `DolphinMainWindow::slotSplitViewChanged()`, `FoldersTabsSettingsPage` (Checkbox → Combobox).
- Die Session-Wiederherstellung (`RememberOpenedTabs`) hat wie bisher Vorrang. Sie stellt den
  Split pro Tab selbst wieder her.

### PI4: Abschluss

- Dev MCP: Rolle und Speicherort pro Panel in `get_ui_state` anzeigen.
- `docs/whale-vs-dolphin.md`: Abschnitt „Panels merken sich ihre Ansicht“.

## Offene Punkte

- **Einstellungsseite „Ansicht“** (`generalviewsettingspage.cpp:234`): Sie schreibt heute den
  globalen Satz aus der aktuellen View. Vorschlag: Sie schreibt **beide** Rollen.
- **Neue Tabs bei `LastState`**: Folgen sie dem gemerkten Zustand? Vorschlag: ja.
- `Dolphin::sortOrderForUrl()` (D-Bus, `dbusinterface.cpp:65`) verwendet `Left`.

## Ausblick: Styles (nach dem Vorbild der Lister-Styles von Directory Opus)

Ein späteres Feature: Ein Klick schaltet das gesamte Aussehen um. Dazu gehören Single/Dual,
Lock, Ratio, Panels (Ordner, Info, Terminal), Vorschau und Toolbars, **pro Seite** die View
(Modus, Format, Plugin-View) und optional der Ordner. Bei DOpus hat jedes Element eine Checkbox:
Ist sie aus, lässt der Style das Element unverändert.

Was PI dafür vorbereitet:

- Die Rolle (`Left`/`Right`) ist die Adresse, über die ein Style „die linke View“ setzt. Ohne
  Rolle gibt es keinen Ort für „links“.
- Ein Rollen-Satz ist ein vollständiger ViewProperties-Satz. Einen Style anwenden heißt: seine
  Sätze per `setDirProperties()` in die Rollen-Speicher kopieren und auf die Views anwenden. Den
  aktuellen Zustand als Style speichern ist die Umkehrung.
- Die Split-Startoption und die Regel „Split neu öffnen“ (b) werden später vom Style
  übersteuert.
- Noch nicht in PI: die Plugin-View pro Rolle (`whaleviewpreferencesrc` ist heute pro Ordner)
  und Ratio und Lock im gemerkten Zustand.
