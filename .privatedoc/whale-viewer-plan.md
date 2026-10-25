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

Offen, wird ausprobiert (Benutzer, 2026-10-24: „müssen wir ausprobieren“): Verhalten der
Auswahl beim Blättern durch eine Mehrfachauswahl (V-E3).
- A (Vorschlag): Die Auswahl bleibt, nur das aktuelle Item (Fokusrahmen) wandert mit.
- B: Die Auswahl wandert auf das angezeigte Bild (wie ohne Mehrfachauswahl).
V1 baut beide, umschaltbar über eine versteckte Einstellung in whalerc; nach dem Ausprobieren
entscheidet der Benutzer, die andere Variante wird entfernt. Ohne Mehrfachauswahl wandert die
Auswahl immer mit.

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
- Varianten A/B der Auswahl bei Mehrfachauswahl, versteckt umschaltbar (siehe oben).
- Einstellungsseite mit MIME-Liste; Tests (Liste, Kopplung, Einstellungen) und Dev-MCP-Tools.

Schritte (2026-10-25):
- V1a: `src/whale/viewer/`: Einstellungen in whalerc `[Viewer]` (`MimeTypes`, Vorgabe `image/*`;
  `WrapAround`; versteckt `MultiSelection` = `KeepSelection` (A) / `MoveSelection` (B)),
  `WhaleViewerList` (anzeigbar = lokale Datei, MIME passt zur Liste und Qt kann es dekodieren;
  Liste aus dem Ordner bzw. der Auswahl in View-Reihenfolge; Blättern mit/ohne Wrap-around),
  `DolphinView::showViewerItem()` (aktuelles Item, ggf. Auswahl, Scrollen). Tests.
- V1b: Viewer-Fenster und Aktivierungs-Haken; die Rückfrage „%1 Elemente öffnen?“ ab 6 Items
  (`DolphinView::slotItemsActivated()`) zählt Items, die der Viewer zeigt, nicht mit. Mausrad,
  Tasten, Rand-Klick, Dateiname, Ladefehler, Mauszeiger. Offscreen-Tests.
- V1c: Einstellungsseite „Viewer“ (MIME-Liste, Wrap-around), Tests.
- V1d: Dev-MCP-Tools, e2e, beide Builds, volle Suite.
- Nicht lokale URLs (smb, sftp …) öffnen in V1 wie bisher mit ihrem Programm.

Stand V1b (2026-10-25): `WhaleViewer` (`src/whale/viewer/whaleviewer.*`), Haken in
`DolphinViewContainer::slotItemActivated()`/`slotItemsActivated()` nach dem Skript-Starter.
Laden mit `QImageReader` (EXIF-Drehung) im Hintergrund; Bildwechsel innerhalb von 40 ms
werden zusammengefasst, nur das letzte Bild wird geladen, und erst dann folgt die View
(beim Schließen sofort). Einpassen in Geräte-Pixeln, nie über 100 %. Rand-Klick = linkes/rechtes
Fünftel. Tasten: ←/↑/Bild↑/Rücktaste zurück, →/↓/Bild↓ vor, Pos1/Ende, ESC. Hinweis „First
file“/„Last file“ am Ende ohne Wrap-around. Strg+Mausrad ist für V2 reserviert (tut nichts).
Für V2 merken: Qt begrenzt `QImageReader` auf 256 MB je Bild (ca. 64 MP), größere Bilder gelten
derzeit als Ladefehler.

Stand V1c (2026-10-25): Seite „Viewer“ nach „Styles“ (`WhaleViewerSettingsPage`): Baum „Images“
(an/aus für alle) mit jedem Bildtyp, den Qt dekodieren kann (Beschreibung + MIME-Name). Alle an =
`image/*` (später dekodierbare Typen sind dabei), sonst die Namen der eingeschalteten. Muster
anderer Gruppen (z. B. `video/*` für V3) bleiben erhalten. Dazu „Continue with the first file after
the last one“ (Wrap-around). Der A/B-Schalter bleibt versteckt; „Defaults“ lässt ihn in Ruhe.
Icon der Seite: flacher Monitor mit Landschaftsbild im Breeze-Stil (Codex, `src/whale/viewer/whale-viewer.svg`,
als Ressource; die Retro-„Glotze“ war drollig, passte aber schlechter zu den anderen Icons).

Stand V1d (2026-10-25): Dev MCP: `viewer` in `get_ui_state` (Einstellungen, offen, Datei, Liste,
geladen/Fehler, Hinweistext, Vollbild), `viewer_input` (Taste, Mausrad, Klick links/Mitte/rechts als
echte Ereignisse ans Viewer-Fenster), `set_viewer_options` (auch der versteckte A/B-Schalter),
`screenshot` mit `target: viewer`. Geöffnet wird über `activate_items`. e2e
`test_v1_acceptance.py` (A–G: Öffnen, Blättern, Kopplung, Listenende/Wrap-around, Ladefehler/ESC,
Mehrfachauswahl A und B, keine Rückfrage bei 7 Bildern, MIME-Liste leer → Programm öffnet).
Nach ESC hat die View wieder echten Tastaturfokus.
Befund nebenbei: In der doppelten Stunde nach der Zeitumstellung (25.10.2026, 02:00–03:00 MEZ)
schlagen `viewpropertiestest` und `dolphinmainwindowtest` fehl: `ViewProperties` vergleicht lokale
Zeitstempel, die KConfig ohne Offset speichert (Dolphin-Erbe, unabhängig vom Viewer; mit `TZ=UTC`
grün).

### V2 – Qualität und Tempo

- Vorladen, Farbprofile, HiDPI, hochwertige Skalierung; Zoom/Pan (V-E5); animierte GIF/WebP.

Entscheidungen (Benutzer, 2026-10-25):
- V-E12 Zoom-Tasten: zusätzlich zu Strg+Mausrad `+`/`-`, `0` = einpassen, `1` = 100 %.
- V-E13 Zoom über 100 %: Pixel scharf (nearest) als Vorgabe, in den Einstellungen auf glatt
  umstellbar.
- V-E14 Farbprofil: Claude entscheidet. Bilder mit eingebettetem Profil werden nach sRGB
  umgerechnet; KWin behandelt Fenster ohne eigenes Profil als sRGB und rechnet selbst auf den
  Bildschirm um. Ein echtes Bildschirmprofil im Viewer erst, falls es nötig wird.
- V-E15 Neue Einstellung „Immer hochskalieren“ (Vorgabe aus): kleine Bilder werden bis an den
  Bildschirmrand vergrößert, immer glatt (das Einpassen soll das Bild zeigen, nicht seine Pixel;
  scharf/glatt nach V-E13 gilt nur für den Zoom).

Schritte:
- V2a: Skalieren im Worker-Thread statt im GUI-Thread; Cache mit Nachbarn (±1, in
  Blätterrichtung +2) und Speichergrenze; `QImageReader`-Grenze anheben (große Panoramen).
- V2b: Farbprofile nach sRGB (V-E14); Qualität des Herunterskalierens prüfen; „Immer
  hochskalieren“ (V-E15) mit Einstellungsseite.
- V2c: Zoom/Pan (V-E5, V-E12, V-E13): Strg+Rad zur Mausposition, mittlere Maustaste verschiebt,
  Tasten; gezeichnet wird der sichtbare Ausschnitt aus dem vollen Bild; beim Wechsel einpassen.
  Einstellung „scharf/glatt“.
- V2d: Animierte GIF/WebP: Animation in Schleife, hält beim Blättern an.
- V2e: Dev MCP (Zoom, Cache, Animation), `test_v2_acceptance.py`, beide Builds, volle Suite.

Stand V2a (2026-10-25): `WhaleViewerCache` (`src/whale/viewer/whaleviewercache.*`) dekodiert und
skaliert in einem eigenen, prozessweiten Pool mit 2 Threads (das aktuelle Bild mit Vorrang;
ESC wartet nicht auf große Bilder). Behalten werden das aktuelle Bild, eins zurück und zwei in
Blätterrichtung; der Rest wird vergessen, wartende Aufträge abgebrochen. Jeder Eintrag hat das
volle Bild (für andere Fenstergrößen, später Zoom) und das eingepasste; volle Bilder der Nachbarn
nur bis 512 MB, sonst nur das eingepasste. `QImageReader`-Grenze 2 GB. Ein vorgeladenes Bild
erscheint beim Blättern sofort; `loadFinished` und das Folgen der View weiter erst, wenn das
Blättern ruht. Beim Ändern der Fenstergröße wird das alte Bild gestreckt gezeigt, bis das neu
skalierte da ist. Tests `whaleviewercachetest`, `WhaleViewerTest::testPreloadsNeighbours`.

Stand V2b (2026-10-25): Beim Dekodieren werden Bilder mit eigenem Profil (`QImage::colorSpace()`
gültig und nicht sRGB) nach sRGB umgerechnet, andere Farbmodelle (CMYK, Grau) dabei nach
RGB32/ARGB32. Herunterskalieren mit `Qt::SmoothTransformation` (in Qt flächengemittelt) bleibt.
whalerc `[Viewer] Upscale` (Vorgabe aus), Seite „Viewer“: „Enlarge small images to fill the
screen“. Die Häkchen der Seite haben Objektnamen (`wrapAround`, `upscale`) für die Tests.

Stand V2c (2026-10-25): Zoom in Geräte-Pixeln je Bildpixel (`1` = 100 % = ein Bildpixel je
Bildschirmpixel, auch bei HiDPI). Strg+Mausrad (1,25 je Raste, Touchpad stufenlos) zur
Mausposition, `+`/`=`/`-` und `1` zur Fenstermitte, `0` einpassen. Nach unten bis zur eingepassten
Größe (dann wieder Einpassen-Modus), höchstens bis 100 %, wenn das Einpassen größer ist
(„Immer hochskalieren“); nach oben bis 3200 %. Kleiner als das Fenster: zentriert; größer: kein
schwarzer Rand, wo noch Bild ist. Mittlere Maustaste zieht (Hand-Cursor). Gezeichnet wird nur der
sichtbare Ausschnitt des vollen Bildes; fehlt es (Budget), holt `WhaleViewerCache::requestFull()`
es nach, bis dahin das eingepasste gestreckt. Über 100 % scharf, whalerc `[Viewer] SmoothZoom` /
Häkchen „Smooth the pixels when zoomed in beyond 100%“ glättet; unter 100 % immer glatt. Blättern
passt wieder ein. Das Budget wird auch beim Wechsel des aktuellen Bildes geprüft.

Stand V2d (2026-10-25): Der Cache merkt sich, ob eine Datei mehrere Frames hat
(`supportsAnimation()` und `imageCount() > 1`); gezeigt und vorgeladen wird zuerst der erste Frame.
Ist das aktuelle Bild fertig geladen, spielt ein `QMovie` es im GUI-Thread ab, immer wieder
(unabhängig von der Schleifenzahl der Datei); Blättern hält es an und verwirft es. Frames werden
eingepasst bzw. gezoomt wie das Bild. Frames werden nicht nach sRGB umgerechnet (GIF hat keine
Profile). Testdatei: eine 159-Byte-GIF mit drei Frames, eingebettet in `whaleviewertest`.

### V3 – Video

- libmpv, Bedienleiste, Video-Optionen (V-E7).

### V4 – Extras

- Optional Löschen (V-E6), evtl. Leertaste = zur Auswahl hinzufügen/entfernen,
  „Im Whale-Viewer öffnen“ im Kontextmenü auch für nicht automatisch zugeordnete Typen.

## Offen

- Plugin-Views (z. B. Treemap): Öffnen aus ihnen und Kopplung an ihre Auswahl, erst nach V1.
- Abstürze von Dekodern (z. B. RAW/HEIF) träfen den Whale-Prozess; bei Bedarf später isolieren.
