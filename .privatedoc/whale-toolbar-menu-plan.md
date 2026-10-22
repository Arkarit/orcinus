# TM: Menüleiste als Element der Werkzeugleiste (2026-10-22)

Wunsch des Benutzers: Die komplette Menüleiste soll sich als eigenes Element mit fester Breite in
die Haupt-Werkzeugleiste setzen lassen. Wird die Breite im verfügbaren Platz unterschritten,
schaltet das Element automatisch auf ein Burger-Menü um.

## Befund im Quelltext

- Die Menüleiste baut KXmlGui aus `dolphinui.rc` (`menuBar()`); sie kann jederzeit neu aufgebaut
  werden (Kurzbefehle, Werkzeugleisten einrichten, Plugins).
- `KHamburgerMenu` (`hamburger_menu`, `DolphinMainWindow::setupActions`/`updateHamburgerMenu`):
  sichtbar nur, solange die klassische Menüleiste versteckt ist (`setMenuBar`). Einträge, die schon
  in einer Werkzeugleiste sichtbar sind, lässt es weg (`hideActionsOf(toolBar())`). Bei versteckter
  Menüleiste hängt es einen Abschnitt mit den übrigen Menüs an.
- `QToolBar` verkleinert Elemente nicht, sondern schiebt sie in das Überlaufmenü „»“. Das
  Umschalten auf den Burger muss deshalb selbst gerechnet werden.
- Fenster-Werkzeugleisten nehmen jede Aktion der Sammlung des Fensters; die File View Toolbars nur
  `WhaleViewCommandScope::allowedActions()` (dort kommt das Element nicht hinein).
- Plasma kann die Menüleiste als globales Menü exportieren; die eingebettete Leiste muss im Fenster
  bleiben (`QMenuBar::setNativeMenuBar(false)`).

## Entscheidungen des Benutzers

| # | Frage | Entscheidung |
|---|---|---|
| E1 | Klassische Menüleiste, wenn das Element in einer Werkzeugleiste steht | **Unabhängig.** Beides bleibt getrennt; Strg+M wirkt nur auf die klassische Leiste. |
| E2 | Burger bei Platzmangel | **Das Element selbst wird zum Burger-Knopf.** Ein zusätzlicher `hamburger_menu`-Knopf in derselben Leiste wäre doppelt. |
| E3 | Breite | **Alles oder Burger:** entweder alle Menütitel nebeneinander, oder nur der Burger. Kein schrittweises Auslagern. |

## Offene Fragen (Vorschläge)

| # | Frage | Vorschlag |
|---|---|---|
| F1 | Was zeigt der Burger des Elements? | **Die Menüs der Menüleiste als Untermenüs** (Datei, Bearbeiten, Ansicht, …): genau die zusammengeklappte Menüleiste, unabhängig von E1. Das kuratierte Burger-Menü von Dolphin bleibt beim Knopf `hamburger_menu`. |
| F2 | Der Knopf `hamburger_menu`, wenn das Element in derselben Leiste steht | **Ausblenden**, solange das Element dort steht (beide würden sonst je nach Platz gleichzeitig erscheinen). |
| F3 | Standardmäßig in der Werkzeugleiste? | **Nein**, nur über „Werkzeugleisten einrichten“. Der Standard von Whale bleibt wie bisher. |
| F4 | Styles | Das Element `toolBars` merkt nur die Sichtbarkeit der Leisten, nicht ihren Inhalt; nichts zu tun. |

## Schritte

### TM1: Element „Menüleiste“ (`whale_menu_bar`)

- Neues `QWidgetAction` in `src/whale/menubar/` (Text „Menu Bar“, Icon `application-menu`), im
  Fenster vor `setupGUI()` angelegt, damit es in „Werkzeugleisten einrichten“ erscheint.
- `createWidget()`: ein Widget mit einer eigenen `QMenuBar` (`setNativeMenuBar(false)`), die die
  Menü-Aktionen der echten Menüleiste enthält (dieselben `QMenu`-Objekte). Ein Event-Filter auf der
  echten Menüleiste (`ActionAdded`/`ActionRemoved`/`ActionChanged`) hält sie gleich.
- Feste Breite: `sizeHint()` = Breite aller Titel; `QSizePolicy::Fixed`.
- Mehrere Leisten oder Fenster: je Widget eine eigene Leiste, gleiche Menüs.
- Tests: Element in die Leiste, gleiche Menütitel wie die echte Leiste, Nachführen nach Neuaufbau,
  unabhängig von Strg+M (E1), nicht in den File View Toolbars.

### TM2: Umschalten auf den Burger

- Das Widget hat zwei Zustände: Menüleiste oder Burger-Knopf (`QToolButton`, `InstantPopup`, Menü
  nach F1, beim Öffnen gefüllt).
- Rechnung bei jeder Größenänderung der Leiste (Event-Filter, verzögert gebündelt): verfügbare
  Breite = Breite der Leiste − Breiten der übrigen Elemente (dehnbare wie Spacer und Ortsleiste mit
  ihrer Mindestbreite) − Ränder und Abstände. Reicht sie für die volle Breite, Menüleiste, sonst
  Burger. Etwas Puffer gegen Flackern an der Grenze.
- Senkrechte Leisten (links/rechts angedockt): immer Burger.
- F2: `hamburger_menu` in derselben Leiste ausblenden, solange das Element dort steht.
- Tests: breites Fenster → Menüleiste, schmal → Burger, wieder breit → Menüleiste; Burger-Menü
  enthält die Menüs; kein „»“-Überlauf für das Element.

### TM3: Dev MCP und Abnahme

- Dev MCP: Zustand des Elements (`menuBarItem`: vorhanden, Modus `menus`/`burger`) im
  UI-Zustand, ohne neues Werkzeug (Breite über `set_window_size`, Leiste über
  `configure_toolbars`).
- Abnahme offscreen: Element hinzufügen, Fenster verkleinern/vergrößern, Screenshots prüfen.

Reihenfolge: TM1 → TM2 → TM3, je ein Commit direkt auf `master`.

## Stand

- **TM1** (2026-10-22): `WhaleMenuBarAction` (`src/whale/menubar/`, `whale_menu_bar`, „Menu Bar“,
  Icon `application-menu`), angelegt vor `setupGUI()`. Widget `WhaleToolBarMenu` mit eigener
  `QMenuBar` (nicht nativ), dieselben Menü-Aktionen, nachgeführt über Event-Filter
  (gebündelt). Breite fest (`QSizePolicy::Fixed`). Alt+Buchstabe öffnet das Menü auch, wenn beide
  Leisten sichtbar sind (offscreen geprüft). Test `whalemenubartest`.
