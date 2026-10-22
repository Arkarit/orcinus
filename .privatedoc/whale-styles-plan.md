# ST: Styles (2026-10-21)

Ein Klick oder ein Kürzel schaltet das ganze Aussehen eines Fensters um. Vorbild sind die Lister
Styles von Directory Opus (Preferences → Layouts and Styles → Styles). Grundlage sind PI
(Rollen links/rechts, `whale-panel-identity-plan.md`) und VS (jede View besitzt ihren Stil,
serialisierbar, `whale-own-view-style-plan.md`).

## Anlass und Ziel

Der Benutzer hat in DOpus zwei Styles mehrmals täglich umgeschaltet, „praktisch unverzichtbar“:

- **Schlicht:** eine View, Compact, keine Menüleiste, keine Toolbars, keine Panels, nur die
  Statusleiste.
- **Bilder:** eine View, Icons in mittlerer Größe, Vorschaubilder an, dazu ein Vorschau-Panel
  (in Whale: das Informationspanel).

Vorgehen: **iterativ über längere Zeit.** Zuerst diese zwei Styles wiederherstellen, aber auf
einem Fundament, das Serialisieren, Erstellen und Bearbeiten von Anfang an trägt. Spätere
Iterationen fügen nur Elemente hinzu.

## Entscheidungen des Benutzers

| # | Frage | Entscheidung |
|---|---|---|
| E1 | Ort der Verwaltung | **Neue Seite „Styles“ im Einstellungsdialog** (vertikale Seitenleiste), wie bei DOpus. |
| E2 | Bearbeiten in v1 | **Nur Haken pro Element** (an/aus) und „neu erfassen“. Werte direkt bearbeiten später. |
| E3 | Wirkung beim Anwenden | **Erst mal Startzustand:** Ein angewendeter Style wirkt wie eine Änderung durch den Benutzer und ist damit auch der Start für neue Fenster, Tabs und Splits. Später konfigurierbar (nur für dieses Fenster). |
| E4 | Reihenfolge | Die beiden Styles des Benutzers vor der Verwaltungsseite. |

## Grundidee: Ein Style besteht aus Elementen

- **Style:** stabile ID, Name, optional ein Kürzel, eine geordnete Liste von Elementen.
- **Element:** ein abgegrenzter Teil des Fensters, mit Schlüssel (z. B. `split`,
  `viewStyle.left`, `statusBar`), Haken („wird gesetzt“) und Wert (Text, vom Element-Typ
  serialisiert). Ein Element ohne Haken lässt der Style unverändert (DOpus-Prinzip).
- **Element-Typ** (`WhaleStyleElement`, eine Klasse pro Schlüssel):
  - `capture(window)` → Wert
  - `apply(window, value)`
  - `describe(value)` → kurzer Text für die Liste („Compact, versteckte Dateien“)
  - `isAvailable(window)`, z. B. „Stil pro Seite“ nur im globalen Modus
  - später optional `createEditor()` für E2
- **Register** (`WhaleStyleElements`): kennt alle Typen in fester Reihenfolge. Speicherung,
  Anwenden und Oberfläche laufen nur über das Register. Ein neues Element ist eine neue Klasse
  plus ein Eintrag.
- **Reihenfolge beim Anwenden:** fest im Register, nicht im Style. Erst das Layout (Split), dann
  die Views, dann die Ausstattung (Leisten, Panels, Statusleiste).
- **Unbekannte Elemente** (aus einer neueren Version) bleiben beim Laden erhalten und werden
  unverändert zurückgeschrieben.

## Speicherung

- Eigene Datei **`whalestylesrc`** (Konfiguration, nicht Zustand: Styles sind Einstellungen des
  Benutzers). Eine Formatversion steht in `[General] Version=1`.
- Vorschlag Format:

```
[General]
Version=1
Order=schlicht,bilder

[Style][schlicht]
Name=Schlicht
Shortcut=Ctrl+Alt+1

[Style][schlicht][Element][viewStyle.left]
Enabled=true
Value=<Text von DolphinView::ownStyleData()>

[Style][schlicht][Element][statusBar]
Enabled=true
Value=FullWidth
```

- Die Style-ID ist eine aus dem Namen gebildete, eindeutige Kennung. Sie bleibt beim Umbenennen
  gleich, denn Aktionsname und Kürzel hängen daran.

## Die Elemente für v1 (Schlicht und Bilder)

| Schlüssel | Inhalt | Erfassen / Anwenden über |
|---|---|---|
| `split` | geteilt oder einzeln (aktueller Tab) | `DolphinTabPage::splitViewEnabled()` / `setSplitViewEnabled()`, mit E3 `WhaleSplitViewStartup::rememberSplitView()` |
| `viewStyle.left`, `viewStyle.right` | ganzer eigener Stil der View: Modus, Vorschaubilder, Sortierung, versteckte Dateien, Spalten | `DolphinView::ownStyleData()` / `setOwnStyleData()`. Mit E3 zusätzlich in den Satz der Rolle speichern. Nur im globalen Modus verfügbar (siehe F1). `right` nur bei Split. |
| `zoom` | Zoomstufe pro Modus (Icons, Compact, Details) | `ViewModeSettings` (heute global pro Modus, nicht pro View) |
| `menuBar` | Menüleiste an/aus | Aktion `KStandardAction::ShowMenubar` |
| `toolBars` | jede Toolbar des Fensters an/aus, nach Objektname (Hauptleiste und die Leisten aus dem Multi-Toolbar-Feature) | `KMainWindow::toolBars()` |
| `fileViewToolBars` | File View Toolbars an/aus | `WhaleToolbarSettings::setFileViewToolBarsVisible()` |
| `statusBar` | Statusleiste: klein, volle Breite, aus | `GeneralSettings::showStatusBar()` |
| `panels` | Panels an/aus: Orte, Ordner, Information, Terminal | Dock-Widgets `placesDock`, `foldersDock`, `infoDock`, `terminalDock` |

Vorschaubilder (`show_preview`) gehören zum Stil der View (`ViewProperties::previewsShown`) und
brauchen kein eigenes Element.

## Schritte

### ST1: Fundament (Modell, Register, Speicherung)

- `src/whale/styles/`: `WhaleStyle` (Daten), `WhaleStyleElement` (Schnittstelle),
  `WhaleStyleElements` (Register), `WhaleStyleStore` (Laden/Speichern von `whalestylesrc`,
  Version, Reihenfolge, unbekannte Elemente).
- Bildung der ID, Umbenennen, Löschen, Reihenfolge.
- Unit-Tests: Hin und zurück, unbekanntes Element überlebt, kaputte Datei, Versionsfeld.

### ST2: Die Elemente für Schlicht und Bilder

- Die Element-Typen aus der Tabelle, jeweils mit `capture`/`apply`/`describe`/`isAvailable`.
- Unit-Tests pro Element am `DolphinMainWindow` (offscreen): erfassen → ändern → anwenden →
  wie vorher.

### ST3: Benutzen (Anwenden, Erfassen, Aktionen)

- `WhaleStyleManager` pro Fenster: wendet Styles an und erfasst den aktuellen Zustand.
- Eine Aktion pro Style (`whale_style_<id>`) mit dem Kürzel des Styles. Menü „Ansicht → Styles“
  mit allen Styles und „Aktuellen Zustand als Style speichern…“ (fragt nach dem Namen).
- E3: Anwenden schreibt den Startzustand (Satz der Rolle, gemerkter Split; die Ausstattung wird
  ohnehin als Einstellung oder Fensterzustand gespeichert).
- Dev MCP: `get_styles`, `apply_style`, `save_style`, dazu `styles` in `get_ui_state`.
- E2E-Abnahme: „Schlicht“ und „Bilder“ anlegen, hin- und herschalten, Neustart.

### ST4: Verwaltung (Settings-Seite „Styles“)

- Neue Seite im Einstellungsdialog (`addPage`, neben Interface, View, Context Menu, …):
  - Liste der Styles (Reihenfolge per Ziehen oder Knöpfen)
  - Neu aus aktuellem Fenster, Umbenennen, Löschen
  - Kürzel (`KKeySequenceWidget`)
  - Elemente des gewählten Styles mit Haken und Beschreibung (E2)
  - „Neu erfassen“ und „Auf Fenster anwenden“
- Die Seite erfasst über das Fenster, aus dem der Dialog geöffnet wurde. Vorbild ist
  `FoldersTabsSettingsPage`, die den Ort des aktiven Fensters liest.
- Gespeichert wird mit „Anwenden/OK“ des Dialogs; „Auf Fenster anwenden“ wirkt sofort.

### Danach (nach Bedarf, je ein kleiner Schritt)

- Weitere Elemente: Plugin-View pro Seite (Treemap), Lock, Teilungsverhältnis, Panel-Größen und
  -Positionen, Filterleiste, Ordner pro Seite, Tabs/Tab-Gruppen, Zoom pro View
- Werte direkt bearbeiten (E2, `createEditor()`)
- E3 konfigurierbar: „nur für dieses Fenster“
- Start-Style für neue Fenster, Import/Export, mitgelieferte Beispiele
- VS5 (Stil pro View in der Sitzung) mit denselben Bausteinen

## Offene Fragen (am 2026-10-22 bestätigt)

Der Benutzer hat alle Vorschläge bestätigt. Er rechnet damit, dass sich im Lauf der Zeit noch
viel ändert; der Plan ist ein Startpunkt.

| # | Frage | Entscheidung |
|---|---|---|
| F1 | „Stil pro Seite“ im Ordnermodus (ein Stil pro Ordner) | In v1 **nicht verfügbar**: ausgegraut mit Hinweis. Den Stil des aktuellen Ordners zu überschreiben, widerspräche dem Ordnermodus. |
| F2 | Kürzel und „Configure Shortcuts“ | Das Kürzel gehört zum Style und wird der Aktion gesetzt. Ändert man es in „Configure Shortcuts“, wird es in den Style zurückgeschrieben. Prüfen, ob KXmlGui dynamische Aktionen dort sauber führt (ggf. Spike). |
| F3 | Ist ein Style „aktiv“? | v1 merkt sich keinen aktiven Style. Nach dem Anwenden ist alles wieder normal veränderbar. Später ggf. Anzeige „verändert“. |
| F4 | Mehrere Fenster | Anwenden wirkt auf das Fenster der Aktion. E3 macht es zum Start neuer Fenster. Andere offene Fenster bleiben, wie sie sind. |
| F5 | Zurück aus „Schlicht“ ohne sichtbare Bedienelemente | Kürzel. Dazu bleibt der Weg zurück zu versteckten Toolbars ohne Menüleiste (Multi-Toolbar-Feature). Prüfen, dass er auch erreichbar ist, wenn Menüleiste **und** alle Toolbars aus sind. |
| F6 | Zoom ist global pro Modus | v1 setzt den globalen Zoom des Modus. Das wirkt auf alle Views dieses Modus; für Schlicht/Bilder passt das. Zoom pro View kommt später. |

## F2-Versuch (2026-10-22)

Kleines Testprogramm gegen KXmlGui 6.24 (offscreen, eigene XDG-Verzeichnisse), drei Aktionen in
der `actionCollection()` des Fensters: eine vor `setupGUI()` erzeugt, zwei danach (eine davon mit
`KActionCollection::setDefaultShortcut()`).

- **Anzeige:** Alle drei stehen in „Configure Shortcuts“ (`KShortcutsEditor` liest die Sammlung
  beim Öffnen).
- **Speichern:** `KShortcutsEditor::save()` schreibt alle drei als `<ActionProperties>` in die
  lokale `ui.rc` des Fensters.
- **Neustart:** KXmlGui wendet die gespeicherten Kürzel nur auf Aktionen an, die es bei
  `setupGUI()` schon gibt. Die später erzeugten Aktionen bekommen nichts.
- **„Standard“:** `allDefault()` setzt Aktionen ohne Standardkürzel auf leer, die anderen auf ihr
  Standardkürzel.
- **Signale:** `QAction::changed` kommt bei jeder Änderung. Dolphin öffnet den Dialog selbst
  (`DolphinMainWindow::slotKeyBindings()`), deshalb kommt `KXMLGUIFactory::shortcutsSaved` nicht;
  der Haken ist `KShortcutsDialog::saved`.
- **Vorbild upstream:** `ServiceMenuShortcutManager` (Kürzel für Service-Menüs) lässt KXmlGui
  speichern und räumt veraltete `ActionProperties` selbst auf.

Folgerung für ST3: **`whalestylesrc` ist die einzige Quelle.** Die Aktionen `whale_style_<id>`
entstehen nach `setupGUI()` und bekommen das Kürzel des Styles (ohne Standardkürzel). In
`slotKeyBindings()` hängt ein Haken an `KShortcutsDialog::saved`, der die Kürzel der
Style-Aktionen in die Styles zurückschreibt. Einträge `whale_style_*` in den `ActionProperties`
werden entfernt, wie es `ServiceMenuShortcutManager::cleanupStaleShortcuts()` macht.

## Risiken

- **Fensterzustand:** Toolbars und Docks gehören teils KXmlGui (`saveState`/`restoreState`,
  Autosave). Anwenden muss mit dessen Autosave zusammenspielen. Sonst überschreibt der
  gespeicherte Fensterzustand den Style beim nächsten Start. Mit E3 sollte genau der angewendete
  Zustand gespeichert werden.
- **Upstream-Diff:** Alles Neue liegt unter `src/whale/styles/`. In Dolphin-Dateien nur Haken
  (Menü, Einstellungsdialog `addPage`, Erzeugen des Managers im Fenster).
- **Versionen:** Das Format ist versioniert. Unbekannte Elemente bleiben erhalten, damit ältere
  und neuere Whale-Versionen dieselbe Datei teilen können.
