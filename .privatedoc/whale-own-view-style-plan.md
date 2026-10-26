# VS: Jede View besitzt ihren Stil (globaler Modus) (2026-10-21)

Folgt auf PI (`whale-panel-identity-plan.md`). Löst die beiden offenen Befunde des Benutzers
nach PI und das Grundproblem dahinter.

## Befund

Repro (Benutzer, 2026-10-21), jeweils links Compact, rechts Icons:

1. „Pop out Right View“: Das neue Fenster zeigt Compact statt Icons.
2. „Split View To Tabs“: Der neue Tab zeigt Compact statt Icons. Den zweiten Teil (der neue Tab
   bekam einen eigenen Split) hat `ac272a9b0` behoben.

Ursache (im Code geprüft): **Eine View hat keinen eigenen Stil.** Bei jedem Ordnerwechsel liest
`DolphinView::setUrl()` die Ansichtseinstellungen neu (`ViewProperties props(viewPropertiesUrl(),
m_panelRole)`, dann `applyViewProperties(props)`). Jede Änderung schreibt sofort in den Speicher
(z. B. `setViewMode()`). Das System wurde für Stile pro Ordner entworfen. Der globale Modus lässt
nur jeden Ordner auf dieselbe Datei zeigen (`view_properties/global`, seit PI zusätzlich
`global-right`).

Folgen:
- Zwei Views mit demselben Speicher laufen im Speicher auseinander, und der nächste
  Ordnerwechsel „setzt sie zurück“. Das war der ursprüngliche Bug vor PI und Befund E5 bei LK
  (`setViewModeKept()`).
- Eine verschobene View (Tab, Fenster) liest im neuen Zuhause den Satz ihrer Position.
- Mit nur einem Satz pro Position lassen sich zwei einzelne linke Views mit verschiedenen Stilen
  nicht abbilden.

## Stand (2026-10-21)

Branch `feature/own-view-style` (von `master` ac272a9b0), nicht gepusht:

| Schritt | Commit | Inhalt |
|---|---|---|
| VS0 | 02cfb8197 | Tests zuerst (Repro 2, kein Springen, Papierkorb). |
| VS1 | 132f50c77 | `ViewProperties::usesGlobalStyle()`. |
| VS2 | 55cbab739 (+ f6233e788) | `DolphinView::ownStyle()`, `PropertiesForChange`; kein Neu-Lesen bei Ordnerwechsel und Apply. Nebenbei: `slotSplitViewChanged()` in einem Fenster ohne Tab. |
| VS3 | f787eb3ab | Stil wandert mit in Tab und Fenster (`--whale-view-style`), Dialog zeigt eigenen Stil. |
| VS4 | (dieser Commit) | Aufräumen: `setViewModeKept()`-Doku, `panes.N.ownViewMode` im Dev MCP, Doku-Gliederung 2.4. |

Abweichungen vom Plan:

- **`setViewModeKept()` bleibt.** Es wirkt noch im Ordnermodus beim Ordnerwechsel und bei der
  dynamischen Ansicht (beide Modi). Nur die Beschreibung wurde angepasst.
- **Serialisierung:** `ViewProperties::toData()` / `setDirProperties(const QByteArray &)`. Text
  ohne `[Dolphin]` ändert nichts.
- **Pop out:** Das neue Fenster schließt einen Split, den es wegen „Always“ bekommen hätte,
  ausdrücklich (`closeRightSplitView()` in `main.cpp`). Der E2E-Test hat das gefunden.
- **Neuer E2E-Test** `whale_dev_mcp_vs_e2e` (`test_vs_acceptance.py`), `start_whale()` kann
  zusätzliche Argumente.

Offen / bekannt:

- „Pop out“ startet `whale --new-window` über den `PATH` (Upstream-Verhalten). Ein älteres
  installiertes Whale kennt `--whale-view-style` nicht und bricht ab. Vor dem Test also
  installieren. Alternative bei Bedarf: Übergabe per Umgebungsvariable.
- Wählt man bei aktiver dynamischer Ansicht (Icons) ausdrücklich Icons, bleibt der eigene Stil
  beim vorherigen Modus. Das ist Upstream-Verhalten und unverändert.
- VS5 (Stil in der Sitzung) kommt mit dem Style-System (F4).

## Grundidee

Im **globalen Modus** besitzt jede `DolphinView` ihren Stil selbst, im Speicher (`m_ownStyle`):

- Beim **Anlegen** liest sie ihn aus dem Satz ihrer Rolle, wie heute.
- Ein **Ordnerwechsel** liest nichts neu: Die View wendet ihren eigenen Stil an.
- **Änderungen** gehen in den eigenen Stil und werden zusätzlich in den Satz der Rolle
  geschrieben. Das dient nur noch dazu, neue Views (Start, neue Tabs, Split öffnen) zu
  versorgen. Pro Rolle gilt die letzte Änderung.
- **Orte mit eigenem Stil** (Papierkorb, Suche, Zuletzt benutzt, Snapshots, Downloads) lesen wie
  heute ihren eigenen Satz. Beim Verlassen kehrt die View zu `m_ownStyle` zurück.
- Die **dynamische Ansicht** bleibt vorübergehend: Sie ändert nur die Anzeige, nicht
  `m_ownStyle`.
- Der **Ordnermodus** (`GlobalViewProps=false`) bleibt unverändert. Dort gehört der Stil zum
  Ordner, und das Neu-Lesen ist richtig.

Passt zum Style-System: Ein Style setzt später den Stil der Views (`m_ownStyle`) und die
Startsätze der Rollen, statt Dateien zu überschreiben, die Views dann irgendwann lesen.

## Schritte

### VS0: Tests zuerst

- Neue Tests, die heute fehlschlagen:
  - Repro 1 und 2 (Stil der verschobenen View).
  - „Kein Springen“: zwei linke Views (zwei Tabs) mit verschiedenen Stilen. Ein Ordnerwechsel in
    einer View ändert die andere nicht.
- Vorhandene Tests, die das Verhalten absichern, das bleiben soll:
  `testLeavingASearchRestoresTheFolderViewProperties`,
  `testOpeningOnASearchUrlUsesTheSearchViewProperties`, `testViewModeAfterDynamicView`, die
  Tests der Orte mit eigenem Stil in `viewpropertiestest`.

### VS1: ViewProperties sagt, ob eine URL den globalen Stil liest

- `bool ViewProperties::usesGlobalStyle() const`: `m_filePath` ist `global` oder
  `global-right`. Für Orte mit eigenem Stil und für den Ordnermodus `false`.
- Unit-Test.

### VS2: DolphinView besitzt den Stil

- `std::unique_ptr<ViewProperties> m_ownStyle`: im globalen Modus geladen aus dem Satz der Rolle
  (Konstruktor, Wechsel in den globalen Modus, `setPanelRole()`), im Ordnermodus leer.
- Ein zentraler Helfer statt der heute rund 20 Stellen
  `ViewProperties props(viewPropertiesUrl(), m_panelRole)`: Er liefert `m_ownStyle`, wenn die
  aktuelle URL den globalen Stil liest, sonst wie heute einen Satz für die URL. Nach einer
  Änderung wird `m_ownStyle` in den Satz der Rolle gespeichert.
- `setUrl()`: globale URL → `applyViewProperties(*m_ownStyle)`, sonst wie heute.
- `readSettings()` (jedes Apply im Einstellungsdialog): kein Neu-Lesen im globalen Modus. Nur
  beim Wechsel Ordnermodus → global wird `m_ownStyle` neu geladen, beim Wechsel zurück
  verworfen.
- `isViewSettingsDefaults()` / `restoreViewSettingsToDefaults()` über den Helfer.
- Dynamische Ansicht: wendet Icons nur auf die Anzeige an, `m_ownStyle` bleibt.
- `setPanelRole()`: Mitnahme wie heute, jetzt aus `m_ownStyle`.

### VS3: Verschieben und Dialog

- **Split View To Tabs** (Fenster-Aktion und `<<`-Menü): Die neue View übernimmt den Stil der
  verschobenen (`DolphinView::setOwnStyle(const DolphinView &)`).
- **Pop out**: Das neue Fenster ist ein eigener Prozess (`Dolphin::openNewWindow()` →
  `whale --new-window`). Der Stil muss per Kommandozeile mit (siehe F3).
- **`ViewPropertiesDialog`**: Er liest im globalen Modus den Stil der View, nicht den Satz der
  Rolle. Heute kann der Dialog etwas anderes zeigen als die View, wenn eine andere View derselben
  Rolle zuletzt geschrieben hat.

### VS4: Aufräumen

- `setViewModeKept()` (LK/E5) wird im globalen Modus überflüssig. Im Ordnermodus bleibt es
  nötig. Prüfen und vereinfachen.
- Tests anpassen. `testEachViewOfASplitViewKeepsItsGlobalStyle` prüft heute „Ordnerwechsel liest
  den Satz der Position“, künftig „behält den eigenen Stil“. Dazu die LK-Tests zu E5.
- Dev MCP: optional `panes.N.ownStyle.viewMode` (eigener Stil, ≠ `viewMode` bei dynamischer
  Ansicht oder Orten mit eigenem Stil).
- `docs/orcinus-vs-dolphin.md` 2.4 anpassen.

### VS5 (optional): Stil in der Sitzung

- `DolphinView::saveState()` / `restoreState()` speichern den eigenen Stil mit (View-State
  Version 2; Version 1 bleibt lesbar).
- Mit „Folders, tabs, and window state from last time“ kommt dann jeder Tab mit dem Stil jeder
  View zurück, nicht nur mit dem Startsatz der Rolle.

## Entscheidungen (Vorschläge)

| # | Frage | Vorschlag |
|---|---|---|
| F1 | Was wird in den Satz der Rolle geschrieben? | Der **ganze eigene Stil** der View, die zuletzt etwas geändert hat. Heute ist es nur das geänderte Feld; das ergäbe eine Mischung aus zwei Views. |
| F2 | Speichert das Verschieben (Tab/Fenster) den Stil? | **Nein**, nur im Speicher. Gespeichert wird erst bei einer Änderung durch den Benutzer. |
| F3 | Transport beim Pop out | **Versteckte Kommandozeilen-Option** mit dem serialisierten Stil (die KConfig-Gruppen `[Dolphin]`/`[Settings]` als Text). Exakt und unabhängig von den Sätzen. Alternative: nur die Rolle übergeben; das ist ungenau, wenn eine andere View dieser Rolle zuletzt geschrieben hat. |
| F4 | VS5 jetzt oder mit dem Style-System? | **Später**, mit dem Style-System. VS0–VS4 lösen die Befunde. |
| F5 | Einstellungsdialog „Apply“ | Kein Neu-Lesen (siehe VS2). Sonst springt eine View, sobald irgendeine Einstellung bestätigt wird. |

## Risiken

- Eingriff in die Kernlogik von `DolphinView`, einer Datei, die upstream oft geändert wird. Die
  Änderung bleibt deshalb im Helfer und in wenigen Haken (`setUrl`, `readSettings`, Konstruktor),
  damit Upstream-Merges handhabbar bleiben.
- Verhalten, das sich der globale Modus bisher „geschenkt“ hat, muss ausdrücklich erhalten
  bleiben: Rückkehr aus der Suche, dynamische Ansicht, Orte mit eigenem Stil. Siehe VS0.
- KPart (Konqueror): baut mit, wird von Whale aber nicht installiert. Dieselbe Logik mit Rolle
  `Left`.
- Nicht betroffen: Zoom im globalen Modus (`ViewModeSettings` pro Modus, nicht pro View) und die
  Plugin-View pro Ordner (`whaleviewpreferencesrc`). Beides ggf. später im Style-System.
