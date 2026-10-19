# CX: Aufklapp-Pfeile in der Kompaktansicht, ganzen Zweig auf-/zuklappen (2026-10-19)

Vorarbeit für Lock-File-Panels (Branch `lock-file-panels`, noch leer): Eine gemeinsame
Mehrfachauswahl über mehrere Ordner lässt sich in jeder View darstellen, die Zweige aufklappen
kann (Details, Kompakt) oder flach ist. Icons und die Kissenansicht zeigen später nur das
aktuelle Element. Umgesetzt direkt auf `master`.

## Entscheidungen des Benutzers

| # | Frage | Entscheidung |
|---|---|---|
| C1 | Standard „Ordner: Aufklappbar“ in Kompakt | an (wie Details) |
| C2 | Tasten | `+` aufklappen, `-` zuklappen, `*` ganzen Zweig aufklappen; in Kompakt **und** Details; nicht während der Tipp-Suche |
| C3 | Schutz bei riesigen Bäumen | Abbruch nach 10 000 hinzugekommenen Elementen, Hinweis in der View; Zuklappen bricht ebenfalls ab |
| C4 | Ordner-Panel | gilt dort auch (gleicher Controller), mit demselben Schutz |

Zusätzlich: **Alt+Linksklick auf den Pfeil** klappt den ganzen Zweig auf bzw. zu. Nur auf dem
Pfeil; Alt+Klick auf den Namen bleibt wie bisher.

## CX1: Kompaktansicht aufklappbar

- Neue Einstellung `CompactMode/ExpandableFolders` (Standard an), Checkbox „Ordner: Aufklappbar“
  auf der Kompakt-Seite der Ansichtseinstellungen.
- Freigeschaltet in `DolphinItemListView::itemLayoutSupportsItemExpanding()`,
  `DolphinView::expandToUrl()` und `DolphinView::itemsExpandable()`.
- `KStandardItemListWidget`: Breite pro Element (Größenhinweis) und Textposition rechnen Pfeil
  und Einrückung mit, wie in Details (eine Ebene = Elementhöhe). Die Hervorhebung bedeckt Pfeil
  und Einrückung nicht.
- ←/→ wechseln in Kompakt weiter die Spalte. ↑ geht zum vorigen Element, nicht zum Elternordner
  (Dolphins Sprung zum Elternordner gilt nur in Details).
- Zwischen Details und Kompakt bleiben aufgeklappte Ordner erhalten (beide fordern dieselben
  Rollen an); Icons klappt wie bisher alles zu.
- Ein Zweig darf am Spaltenende umbrechen; durch die Einrückung bleibt das lesbar
  (Screenshot geprüft).

## CX2: Ganzen Zweig auf-/zuklappen

- `KItemModelBase::setExpandedRecursively()` (virtuell, Standard = `setExpanded()`), umgesetzt
  in `KFileItemModel`:
  - Aufklappen: Der Ordner wird Wurzel einer rekursiven Aufklappung. In `slotCompleted()`
    klappt `continueRecursiveExpansion()` alle geladenen Ordner unter einer Wurzel auf, Ebene für
    Ebene, asynchron. **Keine Links**: ein Link auf einen Ordner darüber würde nie enden.
  - Grenze: `setRecursiveExpansionLimit()` (Standard 10 000) hinzugekommene Elemente, dann Stopp
    und `recursiveExpansionLimitReached()`; `DolphinView` zeigt dazu eine Info-Meldung.
  - Zuklappen: klappt zu, beendet laufende Aufklappungen darunter und vergisst
    `previouslyExpandedChildren`. Normales Zuklappen merkt sich die aufgeklappten Kinder weiter
    (Dolphin-Verhalten).
- `KItemListController`: Alt+Klick auf den Pfeil und `*` rufen `setExpandedRecursively()`.
  `+`/`-`/`*` wirken auf das aktuelle und die ausgewählten Elemente; ist keines aufklappbar
  (z. B. eine Datei), gehen die Tasten an die Tipp-Suche.

## Tests

`src/whale/tests/whalecompactexpansiontest.cpp` (offscreen, mit `DolphinItemListView` wie in
Whale; `WHALE_TEST_SCREENSHOT_DIR` speichert Screenshots): Einstellung, Einrückung und
Hervorhebung, Pfeiltasten in Kompakt, `+`/`-`, Tipp-Suche, `*`, Alt+Klick auf/zu und Vergessen,
normaler Klick wie Dolphin, Grenze, keine Rekursion in Links.

Gesamtsuite offscreen ohne `dolphinmainwindowtest`: 72/72 grün.

## Offen

- Wayland mit echter Eingabe (Alt+Klick, Tasten), nur nach Freigabe.
- Übersetzungen der neuen Texte erst im Sammel-Durchlauf vor dem Release.

## Nachtrag 2026-10-19: keine Baumlinien in Kompakt

Wunsch des Benutzers nach dem ersten Test: In Kompakt nur die Pfeile, keine Striche/T-Stücke.
`KStandardItemListWidget::drawSiblingsInformation()` zeichnet in Kompakt nur noch den Pfeil
(`PE_IndicatorBranch` mit `State_Children`/`State_Open`, ohne `State_Item`/`State_Sibling`).
Details unverändert. Geprüft mit dem Stil `windows` (Breeze offscreen zeichnet keine Linien).
