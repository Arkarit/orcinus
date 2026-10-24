# EL: Namen der übergeordneten Ordner in den Einrückungsstreifen (2026-10-24)

Anlass: Trotz Einfärbung (ET) werden riesige aufgeklappte Ordner über viele Spalten
unübersichtlich. Idee des Benutzers (Mockup): die Namen der übergeordneten Ordner vertikal,
zurückhaltend in die Einrückungsstreifen schreiben, wenn Platz ist.

## Entscheidungen des Benutzers

| # | Frage | Entscheidung |
|---|---|---|
| EL-E1 | Position | Jeder zusammenhängende Lauf von Zeilen gleicher Tiefe (≥ 1) in einer Spalte bekommt mittig die Namen **aller** seiner Vorfahren nebeneinander auf gleicher Höhe (wie ein Breadcrumb quer gelesen). Äußere Namen richten sich nach dem tiefsten Lauf, nicht nach ihrem eigenen Abschnitt; sie wiederholen sich daher pro Lauf. |
| EL-E2 | Zu lang | Ein Name wird nur gezeichnet, wenn er vollständig in die Höhe seines Laufs passt (vorerst kein Kürzen; ausprobieren). |
| EL-E3 | Standard | an |
| EL-E4 | Deckkraft | Häkchen plus Slider für die Deckkraft der Schrift (10–100 %, Standard 40 %, zuerst 35 %), weil Vorlieben verschieden sind |
| EL-E5 | Klick auf Streifen | (Einklappen/zur Zeile springen) später ausprobieren |

## Stand (2026-10-24)

EL1 25b41def8 (Einstellungen, `expandedParentIndex()`, Malen, Tests), EL2 mit dem folgenden
Commit (Häkchen + Slider, Vorschau mit Namen, Tab-Tests, Doku-Gliederung). Danach auf Wunsch:
Standard-Deckkraft 40 %, Schrift 92 % statt 85 % der Item-Schrift (≈ 1 px größer).

## Gestaltung

- Der Name des Vorfahren der Tiefe j steht im Streifen `j·h … (j+1)·h` (unter dessen eigenem
  Pfeil), gedreht von unten nach oben, in Textfarbe mit der eingestellten Deckkraft, Schrift
  92 % der Item-Schrift. So überlappt er nie die Pfeile von Unterordnern im Lauf. RTL gespiegelt.
- Unabhängig von den Farben: funktioniert auch mit Farben „Off“.
- Gemalt vom Hintergrund-Item der Einfärbung (`KItemListExpansionTintItem`), also hinter den
  Einträgen; nur Compact (horizontales Scrollen).
- Vorfahren: neue virtuelle `KItemModelBase::expandedParentIndex()` (Standard: rückwärts suchen),
  `KFileItemModel` antwortet über `ItemData::parent` und den URL-Index.

## Schritte

- EL1: Einstellungen (`ShowNames`, `NameOpacity` in `[ExpansionTint]`), `expandedParentIndex()`,
  Malen in der View, Tests (Pixel: Namen in den Streifen, aus, zu kurze Läufe, Deckkraft).
- EL2: UI in „Expanded Folders“ (Häkchen + Slider, live), Vorschau mit Namen, Tab-Tests,
  Doku-Gliederung.
