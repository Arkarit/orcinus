# Whale Dev-MCP – Entwicklungs- und UI-Test-Automation

Stand: 2026-10-05, Commit `2584fa41e` auf `feature/view-plugins`. Technische Referenz (englisch):
`src/whale/devmcp/README.md`.

## Zweck und Leitfrage

Internen Whale-Zustand maschinenlesbar und gezielt steuerbar machen, damit Claude Whale-Änderungen
unter laufendem Whale selbst prüfen kann. Kein Remote Control, keine API, nicht ABI-stabil.
Leitfrage für jedes Tool: „Geht das schon zuverlässig über Shell/Tests?“ Wenn ja, gehört es nicht hinein.

## Untersuchung und Architekturentscheidung

Vorhanden waren:

- `KDBusService::Multiple`: Jede Instanz hat ihren eigenen Session-Bus-Namen
  `io.github.arkarit.whale-<pid>`.
- `appiumtests/` (Selenium über AT-SPI): nicht installiert, sieht nur den Barrierefreiheitsbaum
  (keine View-IDs, Focus-Proxys, versteckte Dolphin-Kinder). Die kitemviews-Barrierefreiheit ist
  zudem in `filenamehighlightingviewtest` abgestürzt. Deshalb nicht wiederverwendet.
- Keine MCP-Infrastruktur, kein Python-Paket `mcp`; `python3-dbus` ist installiert.

Gewählt: **Variante 3**, internes Diagnose-Interface plus dünner MCP-Adapter.

| Variante | Bewertung |
|---|---|
| 1 separater MCP-Server + eigener IPC-Kanal | Eigener Kanal wäre unnötig; D-Bus ist schon da. Entspricht praktisch Variante 3. |
| 2 MCP-Server im Whale-Prozess | Whale müsste JSON-RPC/stdio oder einen Socket bedienen: MCP-Code im Produkt, mehr Sicherheitsfläche. Verworfen. |
| **3 Diagnose-Objekt auf D-Bus + Adapter** | Whale kennt kein MCP; lokal, sitzungsgebunden, kein Port; Zustände in-process deterministisch abfragbar; Screenshot per `QWidget::grab()` auch unter Wayland ohne Portal. |

## Aktivierung

- Build: `WHALE_ENABLE_DEV_MCP` (Standard OFF), `build-whale` ist damit konfiguriert. Ohne Option
  wird `src/whale/devmcp` nicht gebaut. Geprüft durch `whale_dev_mcp_default_off` (Option,
  Hooks, Binary) und einmal durch einen frischen Default-Build: 0 Symbole, keine Tests registriert.
- Laufzeit: `WHALE_DEV_MCP=1`. Ohne den Schalter entsteht nichts und nichts wird registriert
  (im E2E-Test mit einer zweiten Instanz geprüft).
- Dolphin-Dateien: eine Option (`CMakeLists.txt`), ein Define (`config-dolphin.h.cmake`), ein
  `#if`-geschützter Hook in `main.cpp`.

## Tools

`get_ui_state`, `get_focus_state`, `set_view`, `activate_pane`, `set_split_view`, `set_location`,
`set_filter_text`, `clear_filter`, `set_hidden_files`, `set_sort`, `trigger_action` (nur
`focus_other_view`, `go_up`, `go_back`, `go_forward`, `edit_location`, `select_all`,
`invert_selection`, `rename`), `wait_for_state`, `screenshot`; seit M4 zusätzlich `set_selection`
und `activate_items` (beide über die Bridge-API, die auch Plugin-Views nutzen) sowie
`{"$contains": x}` in `wait_for_state`.

Bewusste Erweiterungen gegenüber Elfies Liste: `set_split_view` (die M3-Liste braucht die
Split-View deterministisch) und `screenshot` (unter Wayland per Shell nicht zuverlässig: Spectacle
braucht das aktive Fenster).

Nicht enthalten: Dateioperationen, Shell, Prozesse, Plugin-Management, Reflection, Netzwerk,
Makros. Delete/Trash/Cut/Copy/Paste bleiben gesperrt, bis Auswahl und Command-Routing (M4/M5) sauber
sind.

## Fokus

Die Rolle bezieht sich auf das Fokus-Widget des **Fensters** (`window->focusWidget()`), nicht auf
`QApplication::focusWidget()`: Das wäre null, sobald eine andere Anwendung aktiv ist. Zusätzlich
`keyboardFocus` (Fenster aktiv und Widget hat wirklich den Tastaturfokus), `windowActive`,
Focus-Proxy pro Pane, `hiddenDolphinViewHasFocus`, Anomalien und ein Aktivierungszähler
(`activations`), mit dem sich beweisen lässt, dass eine Aktion die andere Split-Hälfte nicht aktiviert.

## Befunde beim Bau

1. **Zwei gleichzeitig startende Whales blockieren sich bis zu 25 s** (D-Bus-Timeout): Beim Start
   fragt `Dolphin::dolphinGuiInstances()` alle Instanzen synchron ab („Tabs merken“). Das ist
   vorbestehendes Dolphin-Verhalten. Der E2E-Test startet die zweite Instanz deshalb erst später.
2. **Wayland:** Die zuletzt gestartete Instanz bekommt die Aktivierung. Die Instanz ohne
   Automation läuft im Test deshalb immer offscreen.
3. **Pixel hinken dem Zustand hinterher:** Screenshots direkt nach einer Änderung können noch
   Animationen zeigen (Filterleiste, Split-View). Der semantische Zustand ist sofort korrekt.

## M3-Abnahme über das Dev-MCP (2026-10-05)

`tests/test_m3_acceptance.py` fährt die M3-Liste ausschließlich über das MCP-Protokoll ab, mit 20
Schritten. In jedem Schritt gilt: keine Fokus-Anomalie und echter Tastaturfokus.

- offscreen (ctest `whale_dev_mcp_e2e`): grün, 0,6 s
- **echtes Plasma/Wayland** (`--platform ""`, Screenshots pro Schritt): grün. Die Screenshots zeigen
  Test-Liste, Filter, Split (Test-Liste links, Dolphin rechts) und Test-Liste auf beiden Seiten.

Manuelle Wayland-Abnahme von M3 ist damit durch diese automatisierte Abnahme ersetzt (Entscheidung
der Nutzerin/des Nutzers vom 2026-10-05, keine Zeit für manuelle Tests).

## Mögliche Erweiterungen (nicht gebaut)

- `set_selection` (URL-basiert) für M4, sobald die Auswahl-Spiegelung existiert
- ein „settled“-Signal (keine laufenden Animationen) für pixelgenaue Screenshots
- Tab-Adressierung
- Eingaben per simulierter Maus/Tastatur, falls Klick-Pfade (z. B. FocusIn → Aktivierung) direkt
  getestet werden sollen
- `trigger_action` für Datei-Actions erst nach M4/M5

## M4-Abnahme über das Dev-MCP (2026-10-05)

`tests/test_m4_acceptance.py` (Fälle A–F, 26 Schritte), offscreen (ctest `whale_dev_mcp_m4_e2e`) und auf
echtem Plasma/Wayland grün, mit Screenshots. Dateien öffnen mit einem Test-Öffner im Whale-Profil
(`mcp_test_support.install_test_opener`), nie mit einer echten Anwendung. Die M3-Abnahme läuft dabei
als Regression weiterhin grün.

Hinweis: `set_split_view(false)` schließt wie der Schließen-Knopf die **aktive** Hälfte.

## M5-Erweiterung (2026-10-05)

- `trigger_action`: zusätzlich `properties` sowie die Dateioperationen `copy`, `cut`, `paste`,
  `trash`, `delete`. Gesperrte Actions werden abgewiesen.
- **Dateioperationen** (inklusive `rename_dialog`) nur mit `WHALE_DEV_MCP_FILE_OPERATIONS=1`
  (`whale-dev-session.sh --file-operations` bzw. `start_whale(file_operations=True)`): eigenes `HOME`
  und Papierkorb im Profil, keine Lösch-Rückfrage. Die Zwischenablage erscheint nur dann im Zustand.
- Neue Tools: `get_action_state`, `open_context_menu`/`close_popup`, `request_item_info`,
  `rename_dialog`, `close_dialogs`. Zustand: `actions` (enabled + gate), `popupMenu`
  (`names`/`texts`/`entries`), `dialogs`, `shell.itemInfo`, `panes.N.selectionMode`, `clipboard`.
- `test_m5_acceptance.py` (A–G, 40 Schritte offscreen). Grenzen auf echtem Wayland: Kontextmenüs
  brauchen echte Eingabe; Zwischenablage-Schritte nur mit `--clipboard`.

## M6-Erweiterung (2026-10-05)

- Zustand: `pluginTopItem`, `storedViewStates`, `restore` (source/requested/shown/fallback).
- Tools: `scroll_to_item`, `quit` (schließt wie der Benutzer, Session wird gespeichert);
  `trigger_action` zusätzlich `new_tab`, `activate_next_tab`, `activate_prev_tab`.
- `test_m6_acceptance.py` (A–G, 36 Schritte) startet sich selbst unter `dbus-run-session`; grün offscreen
  und auf Wayland inkl. Neustarts.

## LK6: Lock der geteilten Ansicht (2026-10-20, Branch `lock-file-panels`)

Neu: Zustand `splitLock` in `get_ui_state` und Werkzeug `set_split_lock` (Setter, Klick auf den
Lock-Button). Abnahme `tests/test_lk_acceptance.py` (offscreen, 20 Schritte). Details in
`.privatedoc/whale-split-lock-plan.md`.

