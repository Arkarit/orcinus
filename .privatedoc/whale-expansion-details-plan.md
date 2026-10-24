# ED: Einfärbung und Ordnernamen in der Details-Ansicht (2026-10-25)

Anlass: ET (Einfärbung) und EL (Ordnernamen) funktionieren in Compact so gut, dass die
Details-Ansicht dagegen „abstinkt“. Details hat nur eine Hauptspalte, daher leicht andere Regeln.

## Entscheidungen des Benutzers

| # | Frage | Entscheidung |
|---|---|---|
| ED-E1 | Einfärbung | wie Compact (Ebenen ab der Einrückung, Ordnerzeile ab dem Pfeilende), aber nur in der **Name-Spalte** |
| ED-E2 | Namen | jeder aufgeklappte Ordner, dessen Inhalt sichtbar ist, im Streifen unter seinem Pfeil; gedreht (von unten nach oben), Ende des Namens oben („rechtsbündig“), **klebt** am oberen Rand des sichtbaren Teils seines Blocks (View-Oberkante oder direkt unter der Ordnerzeile); nur wenn er vollständig in den sichtbaren Teil des Blocks passt |
| ED-E3 | Einstellungen | **zwei Sätze**, je einer für Compact und Details (Farben, Namen an/aus, Deckkraft); im Dialog ein Umschalter Compact/Details, die Vorschau folgt den Regeln der gewählten Ansicht |
| ED-E4 | Abwechselnde Zeilenhintergründe | die Streifen laufen durch die Farben: auf jeder zweiten Zeile ist die Tönung um denselben kleinen Betrag verschoben, den sonst die Streifen ausmachen (Wirkung ausprobieren) |
| ED-E5 | Infotext | gekürzt: „Colors the contents of expanded folders by depth and shows the folder names next to them where there is room.“ |

Annahme (nicht ausdrücklich besprochen): Details bekommt dieselben Standardwerte wie Compact
(Pastel (Vivid), Namen an, 40 %).

## Befund im Quelltext

- Details: `KStandardItemListWidget` legt die Name-Spalte von `leftPadding()` bis
  `leftPadding() + columnWidth(erste Rolle)`; die Spaltenbreiten und Ränder kommen vom Header
  (`KItemListView::updateWidgetColumnWidths`, `m_headerWidget->columnWidth()`,
  `leftPadding()`). Die Einrückung beginnt bei `leftPadding()` nur mit „ganze Zeile
  hervorheben“ (`updateExpansionArea()`).
- Abwechselnde Hintergründe: `KItemListWidget::paint()` füllt bei ungeraden Zeilen die ganze
  Zeile mit `AlternateBase` (ohne Fokus leicht aufgehellt/abgedunkelt), entschieden in
  `KItemListView::updateAlternateBackgroundForWidget()` (auch pro Gruppe). Diese Fläche würde
  die Tönung (die hinter den Widgets liegt) überdecken.
- Einstellungen: `ExpansionTintSettings` in `[ExpansionTint]`, geladen von
  `DolphinItemListView` (nur Compact malt heute).

## Gestaltung

- Einstellungen: `[ExpansionTint]` bleibt Compact (bestehende Konfigurationen), Details in
  `[ExpansionTintDetails]`; `load()`/`save()` bekommen die Ansicht als Parameter.
  `DolphinItemListView` nimmt den Satz der aktuellen Ansicht (Icons: nichts), auch beim
  Wechsel der Ansicht.
- Streifen bei aktiver Tönung in Details: Die Widgets malen ihren abwechselnden Hintergrund
  nicht mehr; das Hintergrund-Item malt ihn für die ganze Zeile (gleiche Regel inkl. Gruppen
  und Fokus-Nuance) und in den getönten Flächen die Tönung plus die Differenz
  `AlternateBase − Base`.
- Namen in Details: Blöcke aus den Vorfahren des ersten sichtbaren Items und allen
  aufgeklappten Items im sichtbaren Bereich; Ende eines Blocks nur im sichtbaren Bereich
  suchen (keine langen Scans). Oberkante = max(Blockanfang, View-Oberkante).

## Schritte

- ED1: Einstellungen pro Ansicht, Auswahl in `DolphinItemListView`, Tests.
- ED2: Einfärbung in Details (Name-Spalte, Ordnerzeile, Streifen durch die Farben), Tests,
  Screenshots.
- ED3: klebende Namen in Details, Tests (oben kleben, unter der Ordnerzeile, passt nicht),
  Screenshots.
- ED4: Umschalter Compact/Details im Dialog, Details-Vorschau, kürzerer Infotext, Tab-Tests,
  Doku-Gliederung; beide Builds, volle Suite.
