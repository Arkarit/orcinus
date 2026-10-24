# ET: Aufgeklappte Ordner einfärben (2026-10-24)

Anlass: Das Aufklappen in Compact (CX) verwirrt, wenn ein Teilbaum in die nächste Spalte
umbricht. Oben in der Folgespalte sieht man nicht, zu welchem Ordner die Einträge gehören, und
wann ein Teilbaum endet, sieht man nur an kleinen Einrückungsunterschieden über Spalten hinweg.
Idee des Benutzers: Den Hintergrund aufgeklappter Teilbäume je nach Tiefe dezent einfärben, mit
zyklischen Farben. Mit ChatGPT und Gemini abgestimmt, Mockups von Codex (Pillow, Multiply auf
einem echten Screenshot): Mint/Rosé wirkte deutlich besser als Pfirsich/Lavendel, Grau
funktioniert, wird bei mehr Ebenen aber zu dunkel.

## Stand (2026-10-24)

ET1 381b965ec, ET2 83e31e5cc, ET3 ff62f7a17, ET4 mit diesem Commit. Offen: Pastellwerte im
dunklen Theme (Gelb wirkt oliv), Blick auf die Aufklapp-Animation im echten Fenster (die Flächen
stehen sofort an der Endposition, während die Einträge noch gleiten).

2026-10-25: „Pastel“ heißt jetzt „Pastel (Vivid)“ (Config-Name bleibt `Pastel`, Standard), neu
„Pastel (Subtle)“ (`PastelSubtle`) nach den Custom-Farben des Benutzers. Die Töne sind so gewählt,
dass sie mit 1/16 Anteil auf Weiß exakt #fffcf0, #fff5f8, #f5fffb, #fff8f3, #f9f5ff ergeben;
hellere Töne mit 10 % hätten im dunklen Theme stärker gewirkt als Vivid.

2026-10-25: Ein aufgeklappter Ordner, dem seine Kinder folgen, bekommt ab dem Ende seines Pfeils
die Farbe der Kinder (bei Compact-Standardgrößen 3 px vor deren Fläche); der Pfeil bleibt in der
Farbe der Elternebene. Vorschau im Dialog zeigt jetzt auch Pfeile.

Erkenntnis aus ET2: Die Item-Widgets haben `ItemStacksBehindParent`, was die View in `paint()`
malt, liegt über ihnen. Die Flächen malt deshalb `KItemListExpansionTintItem` (z = −1, ebenfalls
hinter der View).

## Befund im Quelltext

- Compact (horizontales Scrollen): Jedes Item-Widget ist nur so breit wie seine eigene
  Size-Hint (`KItemListViewLayouter::itemRect()`, `kitemlistviewlayouter.cpp:229–244`). Die
  Spalte ist so breit wie ihr breitester Eintrag (`maxItemHeight` in logischen Koordinaten,
  `:584`). Malt jedes Widget seine Fläche selbst, entstehen zerfranste Kanten. Die Fläche muss
  also die View malen: von der Einrückung des Items bis zum Spaltenende, über den Zeilenabstand
  hinweg lückenlos.
- Tiefe pro Item: Rolle `expandedParentsCount` (`kstandarditemlistwidget.cpp:1113`), die
  Einrückung ist `expandedParentsCount * widgetHeight` (+ Abstand).
- Hover nutzt `Text` mit Alpha 0.06, Selektion `Highlight` (`kitemlistwidget.cpp:648–668`); die
  Tönung muss sich davon abheben und darunter liegen.
- Vorbild für Einstellungen: `FileNameHighlightingSettings` (`src/views/`), eigene Config-Gruppe,
  `DolphinItemListView` lädt sie im Konstruktor und bei Settings-Änderungen neu
  (`dolphinitemlistview.cpp:28`, `:129`). UI: `FileNameHighlightingTab`
  (`src/settings/viewmodes/`).

## Entscheidungen des Benutzers

| # | Frage | Entscheidung |
|---|---|---|
| E1 | Farben | Zyklus von **2 bis 10 Farben**, Farbe für Tiefe d = `Farben[(d − 1) % n]` |
| E2 | Presets | **Aus**, **Druckerpapier** (Hellgrau / Hell-Mint), **Grau** (Hellgrau / dunkleres Hellgrau), **Pastell-5**; dazu **Benutzerdefiniert** |
| E3 | Wechsel | pro **Ebene**, nicht pro Zeile (auch beim Druckerpapier) |
| E4 | Dark Theme | Presets **themeabhängig** (in `Base` gemischt); benutzerdefinierte Farben **exakt wie gewählt** (WYSIWYG, Benutzy ist selbst schuld 🦄) |
| E5 | Standard | **Pastell-5** |
| E6 | Ansichten | zuerst **nur Compact**; Details-Baum später, falls gewünscht |
| E7 | Styles | **global**, nicht Teil der Styles |
| E8 | Ort der UI | Tab **Highlighting**, vertikal geteilt |

## Gestaltungsregeln

- Ein Item der Tiefe d (≥ 1) bekommt für jede Ebene k = 1…d eine Fläche ab der Einrückung von
  Ebene k bis zum Spaltenende in `Farben[(k − 1) % n]`. Tiefere Blöcke liegen damit in den
  äußeren, links bleibt ein Streifen der äußeren Farbe sichtbar (wie im Mockup).
- Tiefe 0 (auch die Zeile des aufgeklappten Ordners selbst) bleibt ungefärbt.
- Rechts vor der nächsten Spalte bleibt ein kleiner ungefärbter Rand, damit Blöcke benachbarter
  Spalten nicht ineinanderfließen.
- Kein Blau in den Presets (Verwechslung mit Selektion/Hover).
- Pastell-5, Reihenfolge so, dass Nachbarn (auch über den Zyklus) gut unterscheidbar sind:
  Gelb → Rosé → Mint → Pfirsich → Lavendel. Endgültige Werte nach Screenshots in ET2.
- Presets werden als Farbton + Stärke in `Base` gemischt (grob 6–12 %), die Grau-Presets als
  `Text`-Anteil in `Base`. So passen sie in hellen und dunklen Themes.

## Schritte

### ET1: Einstellungen

- `ExpansionTintSettings` (`src/views/expansiontintsettings.{h,cpp}`, `DOLPHIN_EXPORT`), nach dem
  Muster von `FileNameHighlightingSettings`: Gruppe `[ExpansionTint]`,
  `Preset=Off|PrinterPaper|Grey|Pastel5|Custom` (Standard `Pastel5`), `CustomColors=` Liste.
  Die eigenen Farben bleiben gespeichert, wenn ein Preset gewählt wird, und kommen bei
  „Benutzerdefiniert“ zurück. Länge auf 2–10 begrenzen, ungültige Einträge verwerfen.
- `colors(const QPalette &)` liefert die effektive Farbliste (Presets gemischt, Custom exakt,
  `Off` leer).
- Test: Laden/Speichern, Standard, Grenzen 2/10, kaputte Config, Mischung hell/dunkel.

### ET2: Malen in Compact

- Layouter: Abfrage der Spaltenausdehnung eines Items (Anfang/Ende der Spalte, Zeilenabstand),
  damit Flächen lückenlos sind. RTL berücksichtigen.
- `KItemListView`: generisch `setExpansionTintColors(QList<QColor>)`; malt die Flächen für
  sichtbare Items unter den Widgets, nur bei horizontalem Scrollen (Compact) und wenn die
  Liste nicht leer ist. Neu malen bei Scrollen, Auf-/Zuklappen, Layout- und Palettenwechsel.
- `DolphinItemListView`: lädt `ExpansionTintSettings` wie das Highlighting und reicht die
  Farben weiter.
- Tests (`src/whale/tests/`, offscreen): Pixelprüfung an Spaltenumbruch, Ende des Teilbaums,
  Tiefe 0 ungefärbt, Rand zwischen Spalten, `Off` malt nichts. Screenshots hell und dunkel aller
  Presets → Werte von Pastell-5 mit dem Benutzer festlegen.

### ET3: UI im Highlighting-Tab

- Tab vertikal teilen (`QSplitter` oder zwei Gruppen): oben die bestehenden Regeln, unten
  „Expanded folders“ / „Aufgeklappte Ordner einfärben“.
- Preset-Combo, darunter eine Reihe `KColorButton` mit + und − (2–10), eine kleine Vorschau
  (verschachtelte Blöcke in den effektiven Farben). Bei einem Preset zeigen die Knöpfe dessen
  Farben; ändert man eine Farbe, wechselt das Combo auf „Custom“.
- Apply/Defaults wie bei den Regeln, Live-Übernahme in offenen Fenstern.
- Test: `filenamehighlightingtabtest` erweitern oder eigener Tab-Test.

### ET4: Abschluss

- Eintrag in `docs/whale-vs-dolphin.md`.
- Build beider Konfigurationen (`../build-whale`, `../build-whale-system`), alle Tests.
- Lokalisierung erst im Sammelschritt vor dem Release.

## Später (nicht Teil von ET)

- Details-Ansicht im Baummodus (E6).
- Optional: Hinweis auf den Elternordner oben in einer fortgesetzten Spalte („llama.cpp ›“),
  falls die Tönung allein nicht reicht.
