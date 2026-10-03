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
