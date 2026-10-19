# Whale – Ideensammlung

Kurze Ideen für spätere Features. Noch nicht bewertet, geplant oder entschieden. Wird eine Idee
ernsthaft verfolgt, bekommt sie einen eigenen Eintrag im `whale-ux-diary.md`.

---

## 1. X-Bit beim Kopieren von NTFS entfernen

Auf NTFS, FAT und exFAT melden Dateien wegen der Mount-Optionen oft ein synthetisches X-Bit
(siehe UX-002). Beim Kopieren auf ein POSIX-Dateisystem wird es mitgenommen, sodass dort plötzlich
alle Dateien ausführbar sind. Whale könnte das X-Bit beim Kopieren von solchen Dateisystemen
entfernen, außer bei Dateien, die tatsächlich ausführbar sind.

## 2. Whitelist für ausführbare Dateien

Beim Öffnen einer ausführbaren Datei fragt der Dialog „What do you wish to do with this file?“
(„Open with …“, „Launch“, „Cancel“, „Remember for all executable files“). Ein zusätzlicher Button
wie „Add to Whitelist and Launch“ würde nur diese eine Datei künftig ohne Rückfrage starten, statt
die Rückfrage für alle ausführbaren Dateien abzuschalten.

Hinweis: Der Dialog stammt nicht aus Dolphin, sondern aus KIO
(`KIO::WidgetsOpenOrExecuteFileHandler`). Dolphin liest nur dessen Einstellung
(`kiorc`, Gruppe `Executable scripts`, z. B. in `dolphinviewcontainer.cpp`).

## 3. Sitzungswiederherstellung beim Start mit URL

Beobachtet am 2026-10-03: Ein Whale-Fenster in `~/Desktop/games` öffnen und wieder schließen,
danach `~/Desktop/output` öffnen. Im neuen Fenster erscheint zusätzlich der alte Tab `games`. Das
wirkt befremdlich.

Ursache (geerbtes Dolphin-Verhalten, kein Whale-Fehler): Steht „Ordner, Tabs und Fensterzustand vom
letzten Mal“ an (`RememberOpenedTabs`, Standard `true`) und ist die neue Instanz die einzige
laufende, stellt `src/main.cpp` (Block `if (app.isSessionRestored() || GeneralSettings::rememberOpenedTabs())`)
zuerst die zuletzt gespeicherte Sitzung wieder her (`~/.config/session/whale_whale_whale`). Erst
danach öffnet es die übergebenen URLs als zusätzliche Tabs. In Dolphin fällt das selten auf, wenn
ohnehin mehrere Fenster offen sind.

Mögliche Richtung, noch nicht bewertet: Beim Start **mit** URL nur diese öffnen, beim Start ohne URL
die Sitzung wie bisher wiederherstellen und die Plasma-Sitzungswiederherstellung
(`app.isSessionRestored()`) unverändert lassen. Vorher klären: Gibt es Upstream-Bugs oder
Diskussionen dazu? Was soll ein so geöffnetes Fenster beim Schließen speichern? Wäre das ein
Kandidat für einen Merge Request bei KDE?

## 4. Nach dem Schließen eines Viewers die zuletzt angesehene Datei selektieren

Wird eine Datei in einem Viewer geöffnet (z. B. Gwenview) und blättert man dort weiter, sollte Whale
nach dem Schließen des Viewers die Datei selektieren, die zuletzt im Viewer angezeigt wurde. Heute
bleibt die Selektion auf der ursprünglich geöffneten Datei.

Offene Fragen, noch nicht geprüft: Whale erfährt von sich aus nicht, welche Datei der Viewer zuletzt
gezeigt hat. Zu klären wäre, ob Gwenview das irgendwie mitteilt (z. B. über D-Bus) oder ob es einen
anderen Weg gibt. Zum Beispiel könnte der Viewer beim Beenden `org.freedesktop.FileManager1.ShowItems`
aufrufen; dann bräuchte es aber eine Änderung im Viewer selbst.

## 5. Docking verbessern

Das Andocken der Panels (Orte, Ordner, Information, Terminal) ist kaum bedienbar: Die Einraststellen
findet man beim Ziehen fast nicht.

Erster Befund, noch nicht analysiert: Die Panels sind gewöhnliche `QDockWidget`s
(`DolphinDockWidget` in `src/dolphindockwidget.*`). `DolphinMainWindow` setzt keine eigenen
`QMainWindow::setDockOptions`. Das Einrastverhalten ist also das Standardverhalten von Qt.
Denkbar wären passendere Dock-Optionen (z. B. `AnimatedDocks`, `AllowNestedDocks`,
`AllowTabbedDocks`, `GroupedDragging`), sichtbare Drop-Indikatoren oder eine andere
Docking-Bibliothek (etwa KDDockWidgets oder Qt Advanced Docking System). Vorher klären, wie stark
das von Style und Plattform (Wayland/X11) abhängt.

Vorschlag (von Desktop-Elfie, gefällt dem Nutzer sehr): Das schwer treffbare Ziehen muss gar nicht
der einzige Weg sein. Im Kontextmenü eines Panels gäbe es zusätzlich **„Panel positionieren →
Links | Rechts“** (ggf. auch Oben/Unten). Seit Qt 6.9 gibt es dafür
`QDockWidget::setDockLocation(Qt::DockWidgetArea)` mit `dockLocation()` und
`dockLocationChanged()`. Geprüft: Die installierten Qt-6.10.2-Header enthalten die Funktion.
Ältere Qt-Versionen bieten `QMainWindow::addDockWidget(area, dock)` an. Zu beachten:
- Dolphin verlangt nur Qt 6.4 (`QT_MIN_VERSION` in `CMakeLists.txt`). Für `setDockLocation()`
  braucht es also eine Versionsprüfung oder den Weg über `addDockWidget()`.
- Panels haben bereits ein Kontextmenü mit „Panels sperren“ (`lock_panels`,
  `DolphinMainWindow`). Das Positionieren gehört dorthin, sollte bei gesperrten Panels aber
  ebenfalls funktionieren oder klar deaktiviert sein.
- Ein guter Kandidat für einen Merge Request bei KDE, weil die Änderung klein und unabhängig ist.

## 6. Information-Panel: Höhe von Vorschau und Infotext einstellbar

Im Information-Panel (F11) nimmt die Vorschau oft so viel Höhe ein, dass für die Metadaten nur ein
kleiner Bereich mit einer nutzlosen Scrollleiste bleibt (Screenshot vom 2026-10-04: Vorschau eines
Shell-Skripts, darunter Typ, Größe, Datum usw. mit Scrollbalken). Die Aufteilung zwischen Vorschau
und Infotext sollte einstellbar sein.

Erster Befund, noch nicht analysiert (`src/panels/information/informationpanelcontent.cpp`):
- Vorschau, Name, Metadaten-Bereich (`QScrollArea m_metaDataArea`) usw. liegen in einem einfachen
  vertikalen Layout (`:134–140`), ohne Splitter.
- Die Vorschau wird quadratisch so groß wie die Panelbreite (`m_preview->setSizeHint(QSize(maxWidth, maxWidth))`,
  `:495`) und mindestens `KIconLoader::SizeEnormous` hoch (`:66`). Bei einem breiten Panel frisst sie
  deshalb die Höhe. Das Video-Widget folgt derselben Größe (`:498`).

Mögliche Richtungen: ein `QSplitter` zwischen Vorschau und Metadaten (Position in
`dolphin_informationpanelsettings.kcfg` speichern), eine einstellbare maximale Vorschauhöhe, oder
eine automatische Begrenzung, damit die Metadaten ohne Scrollleiste passen, solange das Panel hoch
genug ist. Ein Splitter wäre der direkteste Weg und ebenfalls ein Kandidat für einen Merge Request
bei KDE.

## 7. Ordner-Richtlinien (Path Policies / Folder Policies)

Pro Verzeichnis (ggf. samt Unterverzeichnissen oder per Muster) Regeln festlegen wie:
- keine Thumbnails
- keine Hover-Vorschau
- keine Metadatenanalyse
- keine Inhaltsindizierung
- keine rekursive Größenberechnung
- vielleicht später: keine Versionskontroll-Overlays, keine Netzwerkzugriffe, keine Plugins

Anlass (2026-10-06): Schaut man mit Dolphin/Whale in `~/.cache/thumbnails/normal`, findet eine
wundersame Dateivermehrung statt: Jede Vorschau erzeugt eine Vorschau erzeugt eine Vorschau …

Erster Befund, noch nicht analysiert:
- Ein Teil existiert schon: Die ordnerbezogenen View-Properties (`views/viewproperties.cpp`) speichern
  `previewsShown` pro Ordner. Das gilt aber nur für genau diesen Ordner, nicht vererbt, und ist
  eine Ansichtseinstellung, keine Richtlinie.
- Vorschauen entstehen an mehreren Stellen mit eigenen `KIO::PreviewJob`s: im
  `KFileItemModelRolesUpdater` (Icons und Hover-Sequenz) und im Information-Panel
  (`panels/information/informationpanelcontent.cpp:183`, `ScaledAndCached`). Eine Richtlinie müsste
  alle diese Stellen erreichen.
- Noch prüfen: Welcher Weg erzeugt die Vermehrung konkret (Ansicht, Hover, Information-Panel, andere
  Thumbnail-Größe)? Schließt KIO das Thumbnail-Verzeichnis selbst schon irgendwo aus?
  Für den Thumbnail-Ordner wäre eine eingebaute Standardregel sinnvoll, unabhängig von
  Nutzereinstellungen; das wäre auch ein Kandidat für einen Merge Request bei KDE (oder bei KIO).

## 8. Alt+Klick auf den Ausklapppfeil klappt die ganze Hierarchie ein oder aus

Ein Klick auf den Ausklapppfeil (Details-Ansicht, Ordner-Panel) mit gedrückter Alt-Taste klappt
nicht nur den Ordner selbst, sondern die komplette Hierarchie darunter aus bzw. wieder ein.

Erster Befund, noch nicht analysiert:
- Der Klick auf den Pfeil landet in `KItemListController::onRelease()`
  (`src/kitemviews/kitemlistcontroller.cpp:1928–1931`, `isAboveExpansionToggle()`), das nur
  `m_model->setExpanded(index, !expanded)` für diesen einen Eintrag aufruft. Die Modifier werden an
  dieser Stelle bisher nicht ausgewertet.
- Rekursives Ausklappen ist asynchron: `KFileItemModel::setExpanded()` stößt nur das Laden des
  Ordners an, die Unterordner sind erst danach bekannt. Es bräuchte also einen Mechanismus, der
  nachgeladene Unterordner weiter ausklappt (ähnlich `expandParentDirectories()` bzw.
  `m_urlsToExpand`).
- Noch klären: Begrenzung bei sehr tiefen oder großen Bäumen (Abbruch, Tiefenlimit?), Verhalten bei
  Netzwerk-Ordnern, und ob Alt+Klick mit dem Fenster-Verschieben des Fenstermanagers kollidiert
  (KWin nutzt dafür standardmäßig Meta, andere Umgebungen teils Alt).

## 9. Modus, der Symlinks auf Ordner stets auflöst

Öffnet man einen Symlink auf einen Ordner, zeigt Whale wie Dolphin den Pfad über den Symlink an
(`ordner-1/ordner-symlink/…`), nicht den echten Zielpfad. Ein optionaler Modus würde beim Öffnen
stets zum aufgelösten Ziel navigieren.

Bereits für Dolphin vorgeschlagen (2025-05-23):
<https://discuss.kde.org/t/dolphin-option-to-follow-symlinked-folders-when-opened/34671>
- Anlass dort: Der angezeigte Pfad ist verwirrend, Geschwisterordner des Ziels sind nicht
  erreichbar, und Dolphin soll sich wie das Terminal (`cd -P`) verhalten.
- Gegenstimmen: Der Symlink-Pfad ist ein gültiger Pfad, automatisches Auflösen würde andere Nutzer
  überraschen. Deshalb höchstens als Option.
- Laut `meven` im Thread wäre ein Patch annehmbar, hat für die Maintainer aber niedrige Priorität.
  Ein Bugreport oder Merge Request ist dort nicht verlinkt.
- Genannte Behelfe: Kontextmenü „Ziel anzeigen“, „Zu Orte hinzufügen“, `.desktop`-Datei mit
  `Type=Link`, Bind-Mounts. Der Vorschlagende wünscht sich das Verhalten von „Ziel anzeigen“ im
  selben Fenster und beim normalen Öffnen.

Erster Befund, noch nicht analysiert:
- „Ziel anzeigen“ gibt es schon: `DolphinMainWindow::showTarget()` (`src/dolphinmainwindow.cpp:610`)
  löst `linkDest()` des selektierten Eintrags auf und prüft das Ziel per `KIO::stat`. Die Logik
  ließe sich vermutlich wiederverwenden.
- Noch klären: An welcher Stelle auflösen (nur beim Aktivieren eines Symlink-Eintrags oder bei jeder
  URL-Änderung, also auch bei getippten Pfaden, Orten, Lesezeichen und aufgeklappten Bäumen)?
  Verhalten von „Zurück“/„Nach oben“, Symlink-Ketten, relative und defekte Links, nicht lokale URLs.
  Kandidat für einen Merge Request bei KDE.
