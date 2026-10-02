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
