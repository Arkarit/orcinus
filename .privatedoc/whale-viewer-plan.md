# V: Eingebauter Viewer (2026-10-24)

Anlass: Der Benutzer arbeitet seit Jahren mit Directory Opus und hat dort per Skript die Lücke
geschlossen, dass die Auswahl im Lister dem Viewer folgt. Typischer Einsatz: das beste Foto in
voller Qualität finden; nach dem Schließen (ESC) ist es in Whale ausgewählt.

## Vorbild Directory Opus (recherchiert)

- Opus 13 hat „Link both ways“ (Auswahl im Lister folgt dem Lister-linked Viewer), aber der
  Viewer muss bei jedem Öffnen per „Link to Lister“ verknüpft werden (Forenklage); Opus 12
  brauchte das Skript „Viewer Select“. Whale koppelt **immer**, ohne Schalter.
- Übernommen: Mausrad mit Akkumulieren (schnelles Drehen springt mehrere Bilder, ohne die
  Zwischenbilder zu laden), bei Auswahl nur durch die Auswahl blättern, Rand-Klick (linke/rechte
  ~20 %) blättert, Wrap-around als Option, Zoom beim Bildwechsel zurücksetzen, Zoom zur
  Mausposition (Opus 13.23).
- Nicht übernommen: frei belegbare Maustasten, Diashow, Zwischenablage-Bereich, „markierte
  Bilder“ als eigenes Konzept (bei uns ist die Whale-Auswahl die Markierung).

## Befund im Quelltext

- Öffnen läuft über `DolphinViewContainer::slotItemActivated()` (ein Item) und
  `slotItemsActivated()` (mehrere, z. B. Enter bei Mehrfachauswahl), `dolphinviewcontainer.cpp`
  ~824/~905. Dort sitzt schon `WhaleScriptLauncher::launch()`; der Viewer kommt daneben.
  Ein Klick auf ein Bild ersetzt die Auswahl vorher durch dieses eine; Mehrfachauswahl kommt
  praktisch nur über `slotItemsActivated()` an.
- Auswahl setzen: `DolphinViewBridge::setSelection()` (`src/whale/host/dolphinviewbridge.h`).
- Vorhanden: Qt 6.10 inkl. Multimedia, kimageformats (HEIF, AVIF, JXL, RAW, PSD, XCF …),
  libmpv 0.41 (+ libmpvqt).
- `KUrlMimeData::exportUrlsToPortal()` blockiert bei Portal-Ausfällen (siehe Flaky-e2e-Befund);
  der Viewer erzeugt keine Drag-Daten, betrifft ihn also nicht.

## Entscheidungen des Benutzers

| # | Frage | Entscheidung |
|---|---|---|
| V-E1 | Öffnen | normales Öffnen der View (Einfach-/Doppelklick nach KDE-Einstellung, Enter) |
| V-E2 | Was wird durchgeblättert | nur Dateien, die der Viewer anzeigen kann |
| V-E3 | Mehrfachauswahl | ist beim Öffnen mehr als ein anzeigbares Item ausgewählt, nur durch diese blättern |
| V-E4 | Umfang | nur der Ordner des geöffneten Bildes (bei Mehrfachauswahl: die Ordner der ausgewählten), keine aufgeklappten Unterordner |
| V-E5 | Zoom | Strg+Mausrad (zur Mausposition), Verschieben mit mittlerer Maustaste; beim Bildwechsel wieder einpassen |
| V-E6 | Löschen | standardmäßig aus, höchstens optional (Papierkorb/Bestätigung) |
| V-E7 | Video | Autoplay, Ton, Schleife konfigurierbar |
| V-E8 | Kopplung | jeder Wechsel setzt in der View, aus der geöffnet wurde, aktuelles Item und Auswahl; immer, ohne Schalter |
| V-E9 | Wrap-around | standardmäßig aus, konfigurierbar |
| V-E10 | Klick ins Bild | tut nichts (nur Rand-Klick blättert); ESC schließt |
| V-E11 | Ladefehler | kurz Dateiname + „kann nicht angezeigt werden“, im Bild stehenbleiben |

Vorschlag, noch nicht ausdrücklich bestätigt: Bei Mehrfachauswahl (V-E3) bleibt die Auswahl
beim Blättern bestehen, nur das aktuelle Item (Fokusrahmen) wandert mit; ohne Mehrfachauswahl
wandert die Auswahl selbst mit. Vor V1 mit dem Benutzer klären.

## Gestaltung

- Randloses Vollbildfenster im Whale-Prozess auf dem Monitor des Whale-Fensters; keine
  sichtbaren Bedienelemente, Mauszeiger blendet nach kurzer Ruhe aus.
- Mausrad: nächstes/vorheriges, akkumulierend; Tasten: Pfeile, Bild↑/↓, Pos1/Ende, ESC.
- Dateiname unten kurz einblenden (~1 s, ausblenden), auch beim Ende der Liste ohne Wrap-around
  ein kurzer Hinweis.
- Bildqualität: volle Auflösung dekodieren, hochwertig herunterskalieren, nie über 100 %
  vergrößern (außer per Zoom), EXIF-Drehung (`QImageReader::setAutoTransform`), Farbprofil,
  HiDPI. Dekodieren im Hintergrund-Thread, Nachbarn vorladen.
- Video: libmpv (bessere Formate, Hardware-Dekodierung); Bedienleiste nur bei Mausbewegung,
  blendet aus. Mausrad wechselt immer die Datei, Pfeiltasten spulen. libmpv als optionale
  Build-Abhängigkeit; ohne sie nur Bilder.
- Einstellungen: neue Seite „Viewer“: MIME-Liste mit an/aus (Vorgabe `image/*`, `video/*`),
  Wrap-around, Video-Optionen, (V4) Löschen.
- Dev MCP: `viewer_open`, `viewer_next`/`viewer_previous`, `viewer_close`, Zustand in
  `get_ui_state` (offen, Datei, Index, Liste), damit alles offscreen testbar ist.

## Etappen

### V1 – Bilder, Kern

- Viewer-Fenster, Öffnen über die Aktivierungs-Haken, Liste nach V-E2/E3/E4 in der Reihenfolge
  der View (Sortierung, Filter, versteckte Dateien).
- Mausrad (akkumulierend), Tasten, Rand-Klick, Dateiname einblenden, ESC, Ladefehler (V-E11).
- Kopplung (V-E8) inkl. Scrollen der View zum Item; Wrap-around-Einstellung (V-E9).
- Einstellungsseite mit MIME-Liste; Tests (Liste, Kopplung, Einstellungen) und Dev-MCP-Tools.

### V2 – Qualität und Tempo

- Vorladen, Farbprofile, HiDPI, hochwertige Skalierung; Zoom/Pan (V-E5); animierte GIF/WebP.

### V3 – Video

- libmpv, Bedienleiste, Video-Optionen (V-E7).

### V4 – Extras

- Optional Löschen (V-E6), evtl. Leertaste = zur Auswahl hinzufügen/entfernen,
  „Im Whale-Viewer öffnen“ im Kontextmenü auch für nicht automatisch zugeordnete Typen.

## Offen

- Plugin-Views (z. B. Treemap): Öffnen aus ihnen und Kopplung an ihre Auswahl, erst nach V1.
- Abstürze von Dekodern (z. B. RAW/HEIF) träfen den Whale-Prozess; bei Bedarf später isolieren.
