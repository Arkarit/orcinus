# UX-002 – Implementierungsplan: File Name Highlighting

Stand: 2026-10-02 · Revision 3 · Status: final, implementierungsbereit; nichts implementiert

Grundlage: UX-002 im `whale-ux-diary.md`, die weiterentwickelten Anforderungen (Textstil statt nur
Farbe, vollständige Infrastruktur, Config-UI an endgültiger Stelle) und die Entscheidungen E1–E6
sowie O2–O4 und die Executable-Korrektur vom 2026-10-02 (Abschnitt 0). Alle Aussagen zum bestehenden Verhalten wurden im Whale-Quellcode
sowie in den Quellen von KIO 6.24.0 und KCoreAddons 6.24.0 (installierte Versionen) und in der
installierten shared-mime-info-Datenbank geprüft.

Leitlinie: **einfache Oberfläche über einer erweiterbaren Engine** – mit vorhandenen
Dolphin/KDE-Mechanismen, ohne allgemeines CSS-System. Eine Regel hat genau eine Bedingungsart,
optional mit mehreren Mustern derselben Art; keine booleschen Ausdrücke, keine Selektoren, keine
Skriptsprache.

---

## 0. Getroffene Entscheidungen

| | Entscheidung |
|---|---|
| E1 | Kaskade pro Attribut: Regeln von oben nach unten; für jedes Attribut gewinnt die erste passende aktive Regel, die es explizit setzt. Ungesetzte Attribute blockieren nichts. |
| E2 | Bold und Italic tri-state: Unchanged / On / Off. |
| E3 | Keine konkreten Default-Farben jetzt. Infrastruktur inklusive Preview und Kontrastbehandlung wird vorbereitet; Farben werden später visuell in Whale beurteilt. |
| E4 | Windows-Executables werden über eine zentrale, erweiterbare Endungsliste erkannt; V1 nur `.exe`. Keine pauschale Aufnahme von `.bat`, `.cmd`, `.msi` usw. **Korrigiert in Revision 3:** Diese Erkennung gilt auf jedem Dateisystem, nicht nur als Nicht-POSIX-Fallback (siehe unten). |
| E5 | Nur der Dateiname wird hervorgehoben. Größe, Datum, Typ usw. übernehmen die Regelfarbe **nicht**. Dolphin-Verhalten ohne Highlighting bleibt unverändert. |
| E6 | Tab-Reihenfolge: `General | Content Display | Highlighting | Icons | Compact | Details`. |
| — | Die Konfiguration ist eine **geordnete, erweiterbare Liste von Regel-Objekten**. Keine fest verdrahteten Schlüssel wie `ExecutableColor`. Die UI bildet die Liste direkt ab. |
| — | Default-Liste: Executable files, Symbolic links, Media files – normale, editierbare Regeln. „Defaults“ erzeugt sie neu. |
| — | MIME-Regeln gehören in V1, weil „Media files“ eine Default-Regel ist. |
| O2 | Farbquelle einer Regel: wahlweise eine `KColorScheme`-Rolle oder eine explizite Farbe. Die Default-Regeln verwenden möglichst Rollen; welche, wird bei der visuellen Erprobung festgelegt. |
| O3 | Bei eingeschaltetem Highlighting bestimmt die Regelliste Bold/Italic. Die Default-Regel „Symbolic links → Italic On“ ersetzt Dolphins eingebautes Link-Kursiv. Ausgeschaltet: exakt Dolphin. Regel deaktiviert/gelöscht: kein automatisches Kursiv. `Italic Off` unterdrückt Kursiv explizit. |
| O4 | `image/*` bedeutet die kanonische MIME-Hauptgruppe `image`, ohne transitive Suche über Vorfahren. Exakte Typen verwenden `inherits()`. |
| O5 | **Offen:** Die Kontrastkorrektur wird zentral gekapselt implementiert; die Schwelle wird bei der visuellen Erprobung festgelegt. |
| Korrektur | `Executable = bekannter Windows-Executable-Typ ODER POSIX-executable`. Die Endungsprüfung (`.exe`) gilt immer; der X-Bit-Zweig nur auf Dateisystemen mit aussagekräftigen POSIX-Bits. |

---

## 1. Vorhandene Codepfade und ihr heutiges Verhalten

### 1.1 Zeichnen der Textfarbe

`src/kitemviews/kstandarditemlistwidget.cpp`

- `paint()` ruft `triggerCacheRefreshing()` und setzt den Stift mit `textColor(*widget)` für den
  Namen (einzige Verwendung von `textColor()`), danach `m_additionalInfoTextColor` für alle
  weiteren Spalten.
- `textColor()` (nicht virtuell, Zeile ~718):
  - **nicht selektiert:** versteckt → `m_additionalInfoTextColor`; sonst, falls gültig,
    `m_customTextColor`;
  - **selektiert:** immer Palette (`QPalette::Text`, nur bei `isPressed()` `HighlightedText`),
    Farbgruppe Active/Inactive je nach Fokus.
- `setTextColor()` speichert `m_customTextColor`, ruft `updateAdditionalInfoTextColor()`.
- `updateAdditionalInfoTextColor()` (Zeile ~1576): Basis `c1` ist die eigene Farbe, falls gültig,
  sonst Palette-Text; Ergebnis = 70 % `c1` + 30 % `Base`. Ausnahme: selektiert + Fokus + volle
  Zeilenhervorhebung → Palette-Textfarbe.

**Folge für E5:** `setTextColor()` färbt immer Name **und** weitere Spalten (Letztere
abgeschwächt) und steuert zugleich die Abblendung versteckter Dateien. Für reine
Namenshervorhebung ist ein getrennter Mechanismus nötig (6.1).

### 1.2 VCS-Farben

`src/views/dolphinfileitemlistwidget.cpp`, `refreshCache()`:

- Nur wenn die Rolle `"version"` existiert, wird eine Tönung 50/50 mit Palette-Text gemischt und
  per `setTextColor()` gesetzt; dazu kommt ein Overlay-Emblem.
- Für `NormalVersion` und `UnversionedVersion` bleibt die Tönung gleich der Textfarbe; es wird
  trotzdem eine **gültige** eigene Farbe (= Textfarbe) gesetzt. Ohne VCS-Rolle wird eine
  ungültige Farbe gesetzt (= Palette).
- VCS tönt damit heute auch die weiteren Spalten. Das bleibt so (Dolphin-Verhalten).

### 1.3 Font und Symlink-Kursiv

Kursiv für Symlinks existiert an **zwei** Stellen, die konsistent bleiben müssen:

- **Widget (Zeichnen):** `KFileItemListWidget::customizedFont()` setzt `italic` aus der Rolle
  `"isLink"`. `triggerCacheRefreshing()` speichert das Ergebnis in `m_customizedFont` und
  `m_customizedFontMetrics`. Diese Metriken steuern Eliding, Umbruch und Textpositionen.
- **Informant (Größenberechnung):** `KStandardItemListWidgetInformant` berechnet Item-Größen ohne
  Widget, direkt aus Modelldaten. Über `itemIsLink(index, view)` und `customizedFontForLinks()`
  verwendet er für Links den kursiven Font in
  - `preferredRoleColumnWidth()` (automatische Spaltenbreite der Detailansicht),
  - `calculateIconsLayoutItemSizeHints()` (Zeilenumbruch in der Symbolansicht),
  - `calculateCompactLayoutItemSizeHints()` (Spaltenbreite in der Kompaktansicht).

**Folge:** Bold (und Kursiv für Nicht-Links) ändert die Textbreite. Wird der Stil nur im Widget
gesetzt, stimmen die vorberechneten Größen nicht mehr: Namen würden in der Kompaktansicht
abgeschnitten, in der Symbolansicht falsch umbrochen. Farbe ist dagegen layoutneutral.

### 1.4 Hidden, Cut, Selection

- **Hidden:** Icon halbtransparent (`KIconEffect::semiTransparent`), Name in
  `m_additionalInfoTextColor`, also in einer abgeblendeten Variante der eigenen Farbe.
- **Cut:** betrifft nur das Icon (`KIconEffect::toDisabled`), nicht den Text.
- **Selection:** eigene Farbe wird ignoriert (1.1). Der Font ist selektionsunabhängig – wichtig,
  damit Bold beim Selektieren nicht umbricht.

### 1.5 Wann der Cache neu berechnet wird

- `refreshCache()` läuft in `triggerCacheRefreshing()`, wenn `m_dirtyContent` oder
  `m_dirtyLayout` gesetzt ist.
- Gesetzt werden die Flags durch `dataChanged()`, `styleOptionChanged()` (Palette, Font,
  Icongröße), `pressedChanged()`, das `PaletteChange`-Event des Widgets und weitere.
- **Palette-Wechsel:** `KItemListView::event(PaletteChange)` → `updatePalette()` →
  `setStyleOption()` → Widgets dirty → `refreshCache()` mit neuer Palette. Eine
  Kontrastkorrektur in `refreshCache()` folgt damit automatisch Breeze/Breeze Dark.
- **`chmod`:** `KFileItemModel::slotRefreshItems()` vergleicht nur die *angeforderten* Rollen.
  Ändern sich nur Rechte, ist `changedRoles` leer; `itemsChanged` wird trotzdem gesendet, und
  `KItemListWidget::setData()` übernimmt bei leerer Rollenmenge alle Daten → das Widget wird neu
  berechnet. **Aber:** `KStandardItemListView::itemSizeHintUpdateRequired()` liefert für eine
  leere Rollenmenge `false` → die Größen werden *nicht* neu berechnet. Für Farbe egal, für Bold
  nicht.
- **Regeländerung:** `KItemListView::setStyleOption()` kehrt bei unveränderter Option früh zurück
  und leert den Größencache nur bei echter Änderung. Es gibt keine öffentliche Methode, die
  Widgets und Größencache ohne Optionsänderung invalidiert.

### 1.6 Zugriff auf Dateieigenschaften

- Die Modellrolle `"type"` enthält nur `mimeComment()` (übersetzt) – ungeeignet.
- Das `KFileItem` ist aus Widget und Informant über
  `static_cast<KFileItemModel *>(view->model())->fileItem(index)` erreichbar (so bereits in
  `KFileItemListWidgetInformant::itemIsLink()`).
- `KItemListWidgetCreator<T>` erzeugt den Informant über `T::createInformant()`. Eine
  `DolphinFileItemListWidget::createInformant()` kann daher einen eigenen Dolphin-Informant
  liefern, ohne dass `kitemviews` davon weiß.
- `currentMimeType()` ist zunächst namensbasiert; der `KFileItemModelRolesUpdater` bestimmt den
  Typ für sichtbare Items später inhaltsbasiert und meldet u. a. `"iconName"`.

### 1.7 `KFileItem::isExecutable()` (KIO 6.24.0, `kfileitem.cpp:1869`)

- `false`, wenn Rechte unbekannt oder kein X-Bit gesetzt.
- Ist der **Datei-Eigentümer** der aktuelle Benutzer → `S_IXUSR`.
- Sonst: `KUser(uid).groups()` – geprüft wird, ob der **Eigentümer** (nicht der aktuelle Benutzer)
  Mitglied der Dateigruppe ist → `S_IXGRP`, sonst `S_IXOTH`.
- Pro Datei mit fremdem Eigentümer bedeutet das Passwd-/Group-Abfragen über NSS (bei LDAP/SSSD
  spürbar teuer, z. B. `/usr/bin` mit tausenden root-Dateien).
- Ordner werden nicht ausgeschlossen.

**Bewertung:** semantisch fragwürdig und für den Zeichenpfad zu teuer. Whale verwendet es
**nicht**, sondern wertet wie GNU `ls` jedes X-Bit: `permissions() & (S_IXUSR|S_IXGRP|S_IXOTH)`.

### 1.8 Mode-Informationen und Symlinks (KIO 6.24.0)

- `permissions()` liefert `UDS_ACCESS` (`& 07777`), `mode()` den Dateityp (`UDS_FILE_TYPE`).
- Lokaler `file`-Worker (`file_unix.cpp`, `createUDSEntry`): für Symlinks wird `UDS_LINK_DEST`
  gesetzt und anschließend `stat()` auf das Ziel ausgeführt → **Typ und Rechte des Ziels**;
  `isLink()` bleibt wahr.
- **Defekter Symlink:** `UDS_FILE_TYPE = S_IFMT - 1` (Sentinel) und `UDS_ACCESS = 0777`. Eine
  naive X-Bit-Prüfung würde jeden defekten Symlink als ausführbar werten. `isRegularFile()`
  (`S_ISREG` auf `m_fileMode`) ist dafür `false` und schließt ihn aus. Erkennung „defekter
  Symlink“: `isLink() && mode() == (S_IFMT - 1)`; derselbe Sentinel wird auch im lokalen
  Stat-Pfad von `KFileItemPrivate::init()` verwendet.
- Die API-Doku von `isRegularFile()` nennt „link“ als `false`; die Implementierung prüft aber den
  Zieltyp. Ein Symlink auf eine reguläre Datei ist damit regulär – per Test abzusichern.
- Andere KIO-Worker: Rechte sind vorhanden, wenn der Worker `UDS_ACCESS` liefert (SFTP ja);
  sonst `KFileItem::Unknown` → keine X-Hervorhebung.

### 1.9 Dateisysteme ohne POSIX-Execute-Semantik

`KFileSystemType::fileSystemType(path)` (KCoreAddons, `statfs` über Magic Numbers):

- liefert `Fat` (vfat, msdos), `Ntfs` (ntfs, ntfs3), `Exfat`, `Smb` (cifs/smbfs), `Nfs` (auch
  `fuse.sshfs`), `Fuse`;
- `fuseblk` (ntfs-3g, exfat-fuse) wird per udev auf den echten Typ aufgelöst → `Ntfs`/`Exfat`;
- kostet `statfs` (plus udev bei FUSE) → **muss gecacht werden**.

Auf diesem Rechner vorhanden: `/boot/efi` (vfat, `fmask=0022`), `/mnt/AI`, `/mnt/games` und zwei
Wechseldatenträger (ntfs3, teils `fmask=0022`) – dort meldet jede Datei X. Geeignet für die
praktische Prüfung.

### 1.10 Windows-Executables in shared-mime-info

Für `*.exe` gibt es mehrere MIME-Typen (`application/x-msdownload` mit Alias
`application/x-ms-dos-executable`, `application/vnd.microsoft.portable-executable`,
`application/x-dosexec`, `application/x-ms-ne-executable`). **Alle teilen ihre Globs mit `*.dll`,
`*.cpl`, `*.drv`, `*.scr`**, teils auch `*.sys`, `*.efi`, `*.ocx`, `*.lib`. Geprüft mit
`kmimetypefinder`: `t.exe`, `T.EXE` und `t.dll` ergeben alle `application/x-msdownload`.

**Folge für E4:** Eine MIME-basierte Erkennung würde Bibliotheken und Treiber als „Executable
file“ markieren. Die Windows-Executable-Erkennung arbeitet deshalb mit einer **Endungsliste**
(zunächst nur `exe`, ohne Groß-/Kleinschreibung), nicht mit MIME-Typen. Sie gilt unabhängig vom
Dateisystem (2.3).

### 1.11 MIME-Gruppen

- shared-mime-info hat eigene Typen, die von `image/*`, `audio/*` oder `video/*` *erben*, aber in
  einer anderen Hauptgruppe liegen (z. B. `application/x-quicktime-media-link` erbt von
  `video/quicktime`).
- Für eine Gruppenregel `image/*` wird daher nur die **Hauptgruppe des kanonischen MIME-Namens**
  geprüft, nicht die Vorfahren. Exakte Muster (`text/plain`) nutzen dagegen
  `QMimeType::inherits()`, damit Spezialisierungen eingeschlossen sind.

### 1.12 Settings-Architektur

- `ViewSettingsPage` (`settings/viewmodes/viewsettingspage.cpp`) erzeugt ein `QTabWidget` mit den
  Tabs General, Content Display, Icons, Compact, Details. Jeder Tab ist ein `SettingsPageBase`
  (`applySettings()`, `restoreDefaults()`, Signal `changed()`).
- `ViewSettingsPage::restoreDefaults()` setzt **nur den aktuell sichtbaren Tab** zurück. Der
  „Defaults“-Button des Dialogs existiert bereits → kein eigener Defaults-Button im Tab.
- Übernahme: `DolphinSettingsDialog::settingsChanged` → `DolphinMainWindow::refreshViews()` →
  `DolphinView::readSettings()` → `DolphinItemListView::readSettings()`.
- Präzedenzfall für strukturierte Einstellungen außerhalb von KConfigXT: `PreviewSettings/Plugins`
  wird direkt per `KConfigGroup(KSharedConfig::openConfig(), "PreviewSettings")` gelesen
  (`dolphinitemlistview.cpp`, `previewssettingspage.cpp`). `openConfig()` ist in Whale `whalerc`.
- Bereits gelinkt: `KF6::GuiAddons` (`KColorUtils::contrastRatio`, `shade`, `mix`),
  `KF6::WidgetsAddons` (`KColorButton`, `KMimeTypeChooserDialog`), `KF6::ColorScheme`.
- `KColorScheme(QPalette::ColorGroup, ColorSet, KSharedConfigPtr)` liefert über
  `foreground(ForegroundRole)` die Rollen `NormalText`, `InactiveText`, `ActiveText`,
  `LinkText`, `VisitedText`, `NegativeText`, `NeutralText`, `PositiveText`. Die Farben stammen aus
  der Farbschema-Konfiguration (Default: die der Anwendung), **nicht** aus der `QPalette` des
  Widgets. Für Tests ist die Konfiguration über den dritten Parameter injizierbar.

### 1.13 Korrekturen gegenüber UX-002

| UX-002 | Tatsächlicher Befund |
|---|---|
| `isExecutable()` prüft vermutlich „irgendein X-Bit“ | prüft eigentümer-/gruppenabhängig mit teuren `KUser`-Lookups; nicht verwenden (1.7) |
| Neue Farbquelle in `refreshCache()` genügt, „ohne den generischen Zeichenpfad zu verändern“ | gilt nur für Farbe; Bold/Kursiv brauchen den Informant (1.3); reine Namensfarbe braucht eine kleine kitemviews-Erweiterung (1.1) |
| Versteckt: „bleibt abgeblendet … zu prüfen, ob abgeblendete Regelfarbe besser wäre“ | heute automatisch über `setTextColor()`; mit getrennter Namensfarbe muss die Abblendung explizit nachgebildet werden (3.2) |
| Selektion: „Markierungsfarbe gewinnt“ | genauer: Palette-Textfarbe, `HighlightedText` nur beim Drücken (1.1) |
| `chmod +x` wird ohne Neuladen sichtbar | für Farbe ja; Größen werden bei leerer Rollenmenge nicht neu berechnet (1.5) |
| VCS gewinnt vor Typfarbe | VCS setzt auch für Normal/Unversioned eine gültige Farbe; Vorrang darf nur für echte Statusfarben gelten (1.2) |
| FAT/NTFS-Erkennung „falls mit vertretbarem Aufwand möglich“ | möglich über `KFileSystemType`, inkl. ntfs-3g (1.9) |
| Typ-Regeln mit `application/x-executable` für Windows-Programme | MIME-Typen von `.exe` schließen `.dll` usw. ein; Endungsliste nötig (1.10) |
| — | defekte Symlinks melden `0777`; ohne `isRegularFile()` wären sie „ausführbar“ (1.8) |

---

## 2. Architektur und Datenmodell

### 2.1 Datenmodell

```
FileNameColor
    Source      source      // Unset, SchemeRole, Custom
    KColorScheme::ForegroundRole role   // nur bei SchemeRole
    QColor      custom      // nur bei Custom

FileNameTextStyle
    FileNameColor         color     // Unset = keine Namensfarbe aus dieser Regel
    std::optional<bool>   bold      // ungesetzt = Unchanged, true = On, false = Off
    std::optional<bool>   italic    // ungesetzt = Unchanged, true = On, false = Off

FileNameHighlightRule
    QString      name          // Anzeigename, z. B. "Media files"; leer → automatische Beschreibung
    bool         enabled
    Condition    condition     // V1: Executable, Symlink, MimeType
                               // später: NamePattern, BrokenSymlink, Setuid, Setgid, Sticky,
                               //         OtherWritable, Fifo, Socket, Device
    QStringList  patterns      // nur für musterbasierte Bedingungen; ODER-verknüpft
                               // MimeType:    "image/*", "audio/*", "video/*", "text/x-c++src"
                               // NamePattern: "*.cpp", "Makefile"
    FileNameTextStyle style
```

- **Eine Bedingungsart pro Regel, beliebig viele Muster derselben Art (ODER).** Damit ist
  „Media files = `image/*` ODER `audio/*` ODER `video/*`“ eine einzelne Regel. Verschiedene
  Bedingungsarten werden nicht kombiniert; es gibt kein UND und keine Negation.
- Bedingungen ohne Muster (Executable, Symlink) ignorieren `patterns`.
- `name` ist ein frei editierbarer Anzeigename. Ist er leer, erzeugt die UI eine Beschreibung aus
  Bedingung und Mustern („MIME type: image/*, audio/*“).
- `Condition` wird in der Konfiguration als stabiler Text gespeichert (`Executable`, `Symlink`,
  `MimeType`, …), nicht als Zahl.
- **Farbquelle (O2):** Die Kaskade behandelt die Farbe als *ein* Attribut, gleich welcher Quelle.
  Die Regel speichert die Quelle, nicht das Ergebnis. Eine Rolle wird erst beim Zeichnen
  aufgelöst und folgt damit jedem Farbschema, auch selbst erstellten.
- Die Rolle `NormalText` ist erlaubt: In einer höheren Regel unterdrückt sie bewusst Farben
  tieferer Regeln (Gegenstück zu `Italic Off`).

### 2.2 Matcher

`FileNameHighlighter` – unveränderlich nach dem Erzeugen, eine Instanz pro geladenem Regelsatz:

- `FileNameTextStyle styleFor(const KFileItem &item, Attributes wanted = All) const`
  - `wanted` begrenzt die Auswertung, z. B. auf `Font` (Bold/Italic) für den Informant. Regeln,
    die keines der gewünschten Attribute setzen, werden übersprungen und lösen damit auch keine
    MIME-Bestimmung aus.
- `bool isActive() const` – Feature eingeschaltet (auch bei leerer Regelliste: dann gilt
  „keine Hervorhebung, kein Dolphin-Link-Kursiv“, siehe 3.1)
- `bool affectsFont() const` – eine aktive Regel setzt Bold oder Italic
- `bool fontDependsOnMimeType() const` – eine aktive MIME-Regel setzt Bold oder Italic
- vorkompiliert: MIME-Muster aufgeteilt in Gruppen (`image`) und exakte Typen; später
  Namensmuster als `QRegularExpression::fromWildcard()`

**Kaskade (E1):** Regeln werden von oben nach unten ausgewertet. Für jedes Attribut gewinnt die
erste passende aktive Regel, die es setzt. Sobald alle gewünschten Attribute belegt sind, wird
abgebrochen.

Beispiel mit den Default-Regeln: Ein Symlink auf ein ausführbares Skript erhält Bold aus
„Executable files“ und Italic aus „Symbolic links“.

### 2.3 Bedingungen in V1

**Symlink:** `item.isLink()` – unabhängig vom Zieltyp, wie Dolphins bisheriges Kursiv.

**MimeType:** Treffer, wenn eines der Muster passt:

- Gruppe `major/*` → Hauptgruppe des kanonischen MIME-Namens == `major` (1.11);
- exakter Typ → `mime.inherits(pattern)` (deckt Aliase und Spezialisierungen ab).

Unbekannte exakte Typen sind beim Laden kein Fehler, sie passen nur nie. Der Editor warnt (5.3).

**Executable** – semantisch „Executable file“, nicht „X-Bit“:

`Executable = bekannter Windows-Executable-Typ ODER POSIX-executable`

```
isExecutableFile(item):
    if !item.isRegularFile():          return false     // Ordner, defekte Symlinks, FIFOs, Geräte
    if isWindowsExecutable(item):      return true      // immer, auf jedem Dateisystem
    return isPosixExecutable(item)

isWindowsExecutable(item):                              // zentrale Erweiterungsstelle (E4)
    return windowsExecutableSuffixes.contains(item.suffix().toLower())   // V1: { "exe" }

isPosixExecutable(item):
    if permissions() == KFileItem::Unknown:  return false
    if !posixExecBitsMeaningful(item):       return false   // FAT/NTFS/exFAT: X-Bits synthetisch
    return permissions() & 0111                             // jedes X-Bit, wie GNU ls (1.7)
```

Die Windows-Prüfung kommt zuerst, weil sie billiger ist (keine Dateisystemabfrage) und auf jedem
Dateisystem gilt. Eine `foo.exe` ist auf ext4, NTFS, FAT und exFAT gleichermaßen „Executable“;
unter Linux kann sie z. B. über Wine gestartet werden.

`posixExecBitsMeaningful(item)`:

- nicht lokal → `true` (Worker-Rechte werden geglaubt; siehe 8.4);
- lokal → `KFileSystemType::fileSystemType(Elternordner)` ∉ {`Fat`, `Ntfs`, `Exfat`};
- Cache `QHash<QString /*Elternordner*/, bool>` im Highlighter; geleert beim Erzeugen eines neuen
  Highlighters und beim Verzeichniswechsel der View;
- die Dateisystemabfrage ist eine injizierbare Funktion (`std::function`), damit Tests FAT/NTFS
  simulieren können.

`isWindowsExecutable(item)`:

- eine Funktion, eine Endungsliste an genau einer Stelle;
- V1: `{ "exe" }`, Vergleich ohne Groß-/Kleinschreibung (`KFileItem::suffix()`; Verhalten bei
  Mehrfachendungen wie `foo.tar.exe` per Test absichern);
- bewusst keine MIME-Prüfung (1.10), daher kein `.dll`, `.sys`, `.cpl` usw.;
- keine Skript-/Installer-Typen (`.bat`, `.cmd`, `.msi`); deren Aufnahme ist eine spätere, eigene
  Entscheidung (8.3).

Erwartetes Verhalten:

| Datei | Dateisystem | Executable |
|---|---|---|
| `script.sh` mit X-Bit | ext4 | ja |
| `script.sh` ohne X-Bit | ext4 | nein |
| `foo.exe` ohne X-Bit | ext4 | ja |
| `foo.exe` | NTFS, FAT, exFAT | ja |
| `FOO.EXE` | beliebig | ja |
| beliebige Datei mit synthetischem X-Bit | NTFS, FAT, exFAT | nein |
| `foo.dll` (mit oder ohne X-Bit) | NTFS | nein |
| `foo.dll` mit X-Bit | ext4 | ja (POSIX-Zweig, wie `ls`) |
| Verzeichnis `bar.exe/` | beliebig | nein |

### 2.4 Caching und Performance

- **Flags** (Executable, Symlink): Bitoperationen und ein Endungsvergleich; Dateisystemtyp pro
  Ordner gecacht und nur abgefragt, wenn eine Datei X-Bits hat und keine Windows-Executable ist.
- **Farbschema-Rollen:** aufgelöste Rollenfarben pro `(Rolle, Farbgruppe)` cachen; Cache bei
  Palette-Wechsel verwerfen (3.3).
- **MIME:** `currentMimeType()` nur, wenn eine noch offene Regel eine MIME-Bedingung hat (durch
  `wanted` und die Kaskade begrenzt). Ergebnis pro kanonischem MIME-Namen cachen:
  `QHash<QString, quint64 /*Bitmaske passender Regeln*/>`. `inherits()` wird so nur einmal pro
  Typ berechnet.
- **Wo gerechnet wird:**
  - im Widget nur für sichtbare Items und nur bei `refreshCache()`;
  - im Informant für alle Items, deren Größe berechnet wird – aber nur mit `wanted = Font` und nur,
    wenn `affectsFont()`. Mit den Default-Regeln (Bold für Executable, Italic für Symlink, Farbe
    für Media) wertet der Informant nur Flags aus, nie MIME.
- Der Informant erzeugt `QFontMetrics` heute pro Aufruf; es gibt höchstens vier Fontvarianten
  (normal/bold × normal/kursiv) → Metriken pro Variante wiederverwenden.

---

## 3. Styling- und Prioritätslogik

### 3.1 Font (layoutrelevant, selektionsunabhängig)

```
font = styleOption().font                         // Label-Font-Einstellung
if highlighter aktiv:
    style = highlighter.styleFor(item, Font)
    if style.bold:   font.setBold(*style.bold)
    if style.italic: font.setItalic(*style.italic)
else:
    if isLink: font.setItalic(true)               // Dolphin-Verhalten, unverändert
```

**Symlink-Kursiv bei eingeschaltetem Highlighting:** Die Regelliste ist dann die *einzige* Quelle
des Namensstils. Dolphins eingebautes Link-Kursiv wird durch die Default-Regel „Symbolic links →
Italic On“ ersetzt. So bewirkt das Deaktivieren oder Löschen dieser Regel sichtbar, was man
erwartet. Bei ausgeschaltetem Highlighting bleibt Dolphins Darstellung exakt erhalten.

Identische Logik in Widget (`customizedFont()`) und Informant (`itemFont()`, 6.1).

### 3.2 Farbe – nur der Dateiname (E5)

Neu in `KStandardItemListWidget` (6.1): eine **Namensfarbe** (`setNameTextColor()`), die
ausschließlich den Namen betrifft und `m_additionalInfoTextColor` nicht verändert.
`setTextColor()` behält seine bisherige Bedeutung.

Namensfarbe in `textColor()`, nicht selektiert:

```
if hidden:
    return nameColor gültig ? abgeblendete Namensfarbe : m_additionalInfoTextColor   // bisher
if nameColor gültig: return nameColor
if customTextColor gültig: return customTextColor                                     // bisher
```

- Die abgeblendete Namensfarbe nutzt dieselbe Mischung wie `updateAdditionalInfoTextColor()`
  (70 % Farbe, 30 % `Base`) und wird bei Palette-Wechsel mit neu berechnet.
- Ist keine Namensfarbe gesetzt, sind Ergebnis und Codepfad identisch mit heute.

Entscheidung in `DolphinFileItemListWidget::refreshCache()`, absteigende Priorität:

1. **Selektiert** → Palette (bestehendes `textColor()`, unverändert).
2. **VCS-Status mit Aussage** (alles außer `NormalVersion`, `UnversionedVersion`) → bestehende
   VCS-Tönung über `setTextColor()`; Namensfarbe ungültig. Statusinformationen bleiben sichtbar,
   die weiteren Spalten verhalten sich wie in Dolphin.
3. **Regelfarbe** → `setNameTextColor(kontrastkorrigiert)`; `setTextColor()` wie bisher (für
   VCS Normal/Unversioned die Textfarbe, sonst ungültig). Weitere Spalten bleiben unverändert.
4. sonst → Namensfarbe ungültig; alles wie bisher.

Unverändert danach:

- **Hidden:** Name in abgeblendeter Variante seiner Farbe, weitere Spalten wie bisher.
- **Cut:** nur Icon.
- Font-Attribute einer Regel gelten auch, wenn VCS die Farbe gewinnt oder das Item selektiert ist.

### 3.3 Farbauflösung und Kontrastkorrektur

Eine zentrale Funktion `resolveNameColor(FileNameColor, const QPalette &)` übernimmt beides:

1. **Quelle auflösen:**
   - `SchemeRole` → `KColorScheme(QPalette::Active, KColorScheme::View, config).foreground(role)`;
     `View` ist die Farbmenge für Dateilisten. `config` ist injizierbar (Tests); im Programm die
     Farbschema-Konfiguration der Anwendung.
   - `Custom` → die gespeicherte Farbe.
   - `Unset` → keine Namensfarbe.
2. **Kontrast sichern:** `KColorUtils::contrastRatio(color, palette.base())`; unter der Schwelle
   wird die Farbe mit `KColorUtils::shade()` schrittweise aufgehellt bzw. abgedunkelt.
   - Die Schwelle ist **eine** benannte Konstante an genau dieser Stelle; ihr Wert wird bei der
     visuellen Erprobung festgelegt (O5).
   - Sie gilt für beide Quellen. Scheme-Rollen sind für ihr Schema gedacht und bestehen die
     Prüfung normalerweise unverändert; die Korrektur ist dort nur eine Absicherung.

- Ergebnis-Cache: `(Quelle, Base-Farbe) → korrigierte Farbe`.
- Aufgerufen in `refreshCache()`, also automatisch nach jedem Palette-Wechsel neu.
- Dieselbe Funktion nutzt die Preview der Config-UI.

### 3.4 Feature aus

Ist der Highlighter nicht aktiv (Default), wirkt kein neuer Codepfad: `refreshCache()` läuft den
bisherigen VCS-Zweig, die Namensfarbe bleibt ungültig, `customizedFont()` und `itemFont()`
liefern das bisherige Link-Verhalten.

---

## 4. Config-Speicherung

In `whalerc` über `KSharedConfig::openConfig()` (Präzedenzfall `PreviewSettings`). Die
Regelliste ist eine geordnete Folge verschachtelter KConfig-Gruppen; die Reihenfolge ist die
Priorität der Kaskade.

```ini
[FileNameHighlighting]
Enabled=false
RuleCount=3

[FileNameHighlighting][Rule0]
Name=Executable files
Enabled=true
Condition=Executable
Bold=true
ColorRole=<vorläufig, siehe 4.1>

[FileNameHighlighting][Rule1]
Name=Symbolic links
Enabled=true
Condition=Symlink
Italic=true

[FileNameHighlighting][Rule2]
Name=Media files
Enabled=true
Condition=MimeType
Patterns=image/*,audio/*,video/*
ColorRole=<vorläufig, siehe 4.1>
```

Eine benutzerdefinierte Farbe wird stattdessen so gespeichert:

```ini
Color=39,174,96
```

- Keine fest verdrahteten Kategorie-Schlüssel; jede Regel hat dieselbe Struktur.
- **Farbe:** `ColorRole=<Rollenname>` (stabile Namen wie `PositiveText`, `NeutralText`,
  `LinkText` …, nicht die Enum-Zahl) **oder** `Color=<r,g,b>` (KConfig-`QColor`-Format). Beim
  Speichern wird immer nur einer der beiden Schlüssel geschrieben. Stehen beim Laden beide da,
  gewinnt `ColorRole`. Ein unbekannter Rollenname ergibt „keine Farbe“.
- Fehlt ein Stil-Schlüssel (`ColorRole`/`Color`, `Bold`, `Italic`), ist das Attribut
  **ungesetzt**. `Bold=false` bedeutet ausdrücklich „Off“.
- `Patterns` ist eine KConfig-Stringliste.
- **Gruppe fehlt komplett** → Default-Liste, Feature aus. **`RuleCount=0`** → bewusst leere Liste.
- Beim Speichern werden alle `RuleN`-Untergruppen gelöscht und neu geschrieben (stabile
  Reihenfolge, keine Altlasten).
- Fehlende `RuleN`-Gruppen und unbekannte `Condition`-Werte (z. B. aus einer neueren
  Whale-Version) werden beim Laden übersprungen, nicht als Fehler behandelt.
- `FileNameHighlightingSettings` (load, save, `defaultRules()`) ist getrennt vom Matcher, damit der
  Matcher ohne KConfig testbar bleibt.
- KConfigXT wird nicht verwendet, weil es Listen strukturierter Einträge nicht sinnvoll abbildet.
  `Enabled` liegt in derselben Gruppe, damit das Feature nur einen Speicherort hat.

### 4.1 Default-Liste

`defaultRules()` ist die einzige Quelle der Vorbelegung und wird verwendet, wenn keine
Konfiguration existiert und wenn der Benutzer „Defaults“ wählt:

| # | Name | Bedingung | Stil V1 |
|---|---|---|---|
| 1 | Executable files | Executable | Bold On; Farbe: Scheme-Rolle, Auswahl offen |
| 2 | Symbolic links | Symlink | Italic On (ersetzt Dolphins Link-Kursiv, O3) |
| 3 | Media files | MimeType `image/*`, `audio/*`, `video/*` | Farbe: Scheme-Rolle, Auswahl offen |

- Alle drei sind normale Listeneinträge ohne Sonderbehandlung.
- Die Default-Regeln verwenden Scheme-Rollen, keine festen Farben (O2). Bis zur visuellen
  Erprobung stehen vorläufige Rollen als benannte Konstanten an einer Stelle in `defaultRules()`
  und sind im Code als vorläufig markiert. Ob „Executable files“ zusätzlich zu Bold überhaupt
  eine Farbe erhält, wird dabei mit entschieden.
- Namen der Default-Regeln werden beim Erzeugen übersetzt (`i18nc`) und dann als normaler Text
  gespeichert. Bis zum ersten Speichern folgen sie der UI-Sprache; danach nicht mehr (8.7).

---

## 5. Aufbau der Config-UI

### 5.1 Tab

`Configure Whale → View → Highlighting`, eingefügt zwischen „Content Display“ und „Icons“ (E6).

```
[x] Highlight file names

 ┌───────────────────────────────────────────────┐   [ Add…      ]
 │ [x] Executable files          program         │   [ Edit…     ]
 │ [x] Symbolic links            link            │   [ Remove    ]
 │ [x] Media files               photo.jpg       │   [ Move Up   ]
 │                                               │   [ Move Down ]
 └───────────────────────────────────────────────┘
 Rules higher in the list take precedence.
```

- Die Liste zeigt nur: Enabled-Checkbox, Name (bzw. automatische Beschreibung), Preview.
- Preview: ein Beispielname passend zur Bedingung, gezeichnet im Stil **dieser Regel**; die
  Farbe wird über `resolveNameColor()` (3.3) mit aktueller Palette und aktuellem Farbschema
  aufgelöst. Bei einem Wechsel des Farbschemas bei geöffnetem Dialog wird die Liste neu
  gezeichnet (`PaletteChange`).
- Inline editierbar ist nur die Enabled-Checkbox; Doppelklick oder „Edit…“ öffnet den Editor.
- Ist das Hauptkästchen aus, sind Liste und Buttons deaktiviert.
- „Defaults“ über den bestehenden Dialog-Button: Hauptkästchen aus, Liste = `defaultRules()`.
- Model: kleines `QAbstractTableModel` über `QList<FileNameHighlightRule>`; Add, Remove, Move
  Up/Down ändern direkt diese Liste.

### 5.2 Progressive Darstellung

Wer mit den Default-Regeln zufrieden ist, sieht nur ein Kästchen und drei verständlich benannte
Zeilen. Bedingungsarten, Muster und Stilattribute erscheinen erst im Editor.

### 5.3 Regel-Editor (Dialog)

```
Name:        [ Media files                         ]
Condition:   [ MIME type                         v ]
MIME types:  [ image/*; audio/*; video/*  ] [Choose…]      (nur bei MIME type)

Text color:  ( ) Unchanged
             (•) From color scheme  [ Positive text         v ]
             ( ) Custom             [ KColorButton ]
Bold:        [ Unchanged | On | Off ]
Italic:      [ Unchanged | On | Off ]

Preview:     photo.jpg
```

- Die Condition-Auswahl enthält nur implementierte Bedingungen; in V1: Executable files,
  Symbolic links, MIME type.
- MIME-Feld: durch `;` getrennte Liste; Gruppen (`image/*`) werden von Hand eingegeben, exakte
  Typen alternativ über `KMimeTypeChooserDialog` (KWidgetsAddons) ergänzt. Unbekannte Typen und
  Gruppen werden markiert; „OK“ ist bei leerer Liste gesperrt.
- **Farbe (O2):** drei Radiobuttons. „From color scheme“ zeigt eine Combobox der Rollen mit
  übersetzten Namen (Normal, Inactive, Active, Link, Visited, Negative, Neutral, Positive text),
  jeweils mit einem Farbfeld in der aktuell aufgelösten Farbe. „Custom“ aktiviert den
  `KColorButton`. Ein kurzer Hinweis erklärt, dass Scheme-Farben sich dem Farbschema anpassen.
  Beim Wechsel von „From color scheme“ auf „Custom“ wird der `KColorButton` mit der aktuell
  aufgelösten Rollenfarbe vorbelegt.
- Bold/Italic als Combobox mit drei Zuständen statt tri-state Checkbox – eindeutiger beschriftet.
- Live-Preview über `resolveNameColor()` (3.3) mit der aktuellen Palette – dieselbe Funktion wie
  im Dateiview, damit Preview und Dateiliste nicht voneinander abweichen.

Begründung für Dialog statt editierbarer Tabellenzellen:

- tri-state Attribute, optionale Farbe und bedingungsabhängige Musterfelder sind in Zellen schwer
  verständlich und schlecht per Tastatur bedienbar;
- Validierung braucht Platz für Rückmeldung;
- die Tabelle bleibt eine lesbare Übersicht (5.2).

---

## 6. Änderungen nach Dateien und Klassen

### 6.1 Generisch, `kitemviews` (upstream-fähig, minimal)

| Datei | Änderung |
|---|---|
| `kitemviews/kstandarditemlistwidget.{h,cpp}` | **Informant:** neuer virtueller Hook `QFont itemFont(int index, const KItemListView *view, const QFont &baseFont) const`; Default = bisherige Link-Logik (`itemIsLink` → `customizedFontForLinks`). Die drei Aufrufstellen (1.3) nutzen ihn; Metriken pro Fontvariante wiederverwenden. |
| `kitemviews/kstandarditemlistwidget.{h,cpp}` | **Widget:** `setNameTextColor()` / Member für Namensfarbe und abgeblendete Variante; `textColor()` gemäß 3.2; Neuberechnung der abgeblendeten Variante in `styleOptionChanged()`. Ohne gesetzte Namensfarbe unverändert. |
| `kitemviews/kitemlistview.{h,cpp}` | geschützte Methode, z. B. `invalidateItemAppearance()`: Widgets dirty, `m_sizeHintResolver->clearCache()`, Relayout (1.5). |

Alle drei Änderungen sind ohne gesetzte Namensfarbe, ohne überschriebenen Hook und ohne Aufruf
der neuen Methode verhaltensneutral.

### 6.2 Dolphin-Schicht, `dolphinprivate` (upstream-fähig)

| Datei | Änderung |
|---|---|
| **neu** `views/filenamehighlighter.{h,cpp}` | `FileNameColor`, `FileNameTextStyle`, `FileNameHighlightRule`, `FileNameHighlighter` (Bedingungen, Kaskade, Executable-Semantik, Caches), `resolveNameColor()` (Rollenauflösung und Kontrastkorrektur) |
| **neu** `views/filenamehighlightingsettings.{h,cpp}` | Laden/Speichern der Regelliste aus `[FileNameHighlighting]`, `defaultRules()` |
| `views/dolphinfileitemlistwidget.{h,cpp}` | Farbentscheidung in `refreshCache()` (3.2); `customizedFont()` überschreiben (3.1); `static createInformant()` liefert neuen `DolphinFileItemListWidgetInformant` mit `itemFont()`-Override |
| `views/dolphinitemlistview.{h,cpp}` | hält `std::shared_ptr<const FileNameHighlighter>`; `readSettings()` lädt und ruft `invalidateItemAppearance()`; Override von `itemSizeHintUpdateRequired()`: bei `affectsFont()` auch für leere Rollenmenge (chmod) `true`, bei `fontDependsOnMimeType()` zusätzlich für `"iconName"`; Getter für Widget und Informant; Leeren des Dateisystem-Caches beim Verzeichniswechsel |
| `src/CMakeLists.txt` | neue Dateien in `dolphinprivate` |

### 6.3 Settings-UI, `dolphinstatic`

| Datei | Änderung |
|---|---|
| **neu** `settings/viewmodes/filenamehighlightingtab.{h,cpp}` | Tab (`SettingsPageBase`), Regelliste, Model, Buttons, Preview-Delegate |
| **neu** `settings/viewmodes/filenamehighlightingruledialog.{h,cpp}` | Regel-Editor |
| `settings/viewmodes/viewsettingspage.cpp` | Tab nach „Content Display“ einfügen, `m_tabs` ergänzen |
| `src/CMakeLists.txt` | neue Dateien in `dolphinstatic` |

`kcm_dolphinviewmodes` bleibt unverändert (wird nicht installiert).

### 6.4 Whale-spezifisch

Der Funktionsumfang ist im Kern vollständig upstream-fähig. Whale-spezifisch sind nur:

- die Produktentscheidung, das Feature anzubieten, und die konkreten Default-Farben;
- der Konfigurationsdateiname (`whalerc`), der bereits generisch über die App-Identität entsteht;
- Diary und Dokumentation.

Commit-Trennung für eine spätere Upstream-Einreichung: (1) kitemviews-Hooks, (2) Matcher +
Settings + Rendering, (3) UI, (4) Whale-Defaults.

---

## 7. Teststrategie

### 7.1 Neu: `filenamehighlightertest` (Unit, ohne GUI)

Mit `QTemporaryDir` und echten Dateien/`chmod`:

- Feature aus → kein Stil
- Feature an, leere Liste → kein Stil, kein Link-Kursiv
- Executable – vollständige Matrix aus 2.3, mit injizierter Dateisystemfunktion:
  - simuliertes POSIX-Dateisystem: `script.sh` mit X-Bit → ja; ohne X-Bit → nein;
    `foo.exe` **ohne** X-Bit → ja; `FOO.EXE` → ja; `foo.dll` ohne X-Bit → nein;
    `foo.dll` mit X-Bit → ja (POSIX-Zweig)
  - simuliertes NTFS, FAT und exFAT: `foo.exe` → ja; `script.sh`, `foo.dll`, `run.bat`,
    `setup.msi` mit synthetischem X-Bit → nein
  - Verzeichnis mit X-Bit, auch mit Namen `bar.exe` → nein
  - Dateisystemfunktion wird für `.exe` nicht aufgerufen (Kosten)
- Symlink auf ausführbare Datei → Executable **und** Symlink
- defekter Symlink → Symlink, **nicht** Executable (auch bei Namen `x.exe`)
- MIME: `image/png`, `audio/*`-Datei, `video/mp4` → Media; `application/x-quicktime-media-link`
  → kein Treffer (nur Hauptgruppe, 1.11); exakter Typ mit Vererbung (`text/x-c++src` gegen
  `text/plain`)
- Kaskade: Farbe aus Regel 3, Bold aus Regel 1, Italic aus Regel 2; „Off“ in einer höheren Regel
  blockiert „On“ einer tieferen
- deaktivierte Regeln werden übersprungen
- `wanted = Font` löst keine MIME-Bestimmung aus, wenn keine MIME-Regel Fontattribute setzt
- Kaskade mit Farbquellen: Rolle aus Regel 1 blockiert Custom-Farbe aus Regel 2 und umgekehrt;
  `NormalText` in einer höheren Regel unterdrückt tiefere Farben
- `resolveNameColor()` mit injizierter Farbschema-Konfiguration (temporäre Datei mit hellem und
  dunklem Schema): dieselbe Rolle ergibt je Schema die Schemafarbe; Custom bleibt gleich
- Kontrastkorrektur (Schwelle aus der zentralen Konstante, Tests ohne festen Zahlenwert
  formuliert): dunkle Farbe auf dunklem Base wird aufgehellt, auf hellem Base nicht verändert

### 7.2 Neu: `filenamehighlightingsettingstest`

- Round-Trip speichern/laden (temporäre KConfig-Datei), inklusive Reihenfolge, `Patterns` und
  Name
- fehlende Gruppe → `defaultRules()` mit drei Regeln, Feature aus; `RuleCount=0` → leer
- ungesetzte Attribute bleiben ungesetzt; `Bold=false` bleibt „Off“
- `ColorRole` und `Color` im Round-Trip; nur einer der beiden Schlüssel wird geschrieben; bei
  beiden gewinnt `ColorRole`; unbekannter Rollenname → keine Farbe
- `defaultRules()` verwendet Scheme-Rollen, keine Custom-Farben
- unbekannte Condition und fehlende `RuleN`-Gruppe werden übersprungen
- erneutes Speichern entfernt überzählige alte `RuleN`-Gruppen

### 7.3 Neu bzw. Erweiterung: Widget- und View-Integration (mit `TestDir`)

- Feature aus → Font, Namensfarbe und Zusatzspaltenfarbe identisch zum Baseline-Verhalten,
  Symlinks kursiv
- Feature an, Symlink-Regel deaktiviert → Symlinks nicht kursiv
- Regelfarbe → Name gefärbt, **Zusatzspaltenfarbe unverändert** (E5)
- Selection → Palette-Farbe, Bold bleibt
- Hidden → Name in abgeblendeter Regelfarbe, Zusatzspalten wie bisher
- VCS: Rolle `"version"` manuell setzen → Statusfarbe gewinnt, Bold der Regel bleibt;
  Normal/Unversioned → Regelfarbe
- Palette-Wechsel (helle → dunkle Palette per `setPalette`) → korrigierte und abgeblendete Farbe
  neu berechnet
- `chmod +x` / `chmod -x` → nach `itemsChanged` (`QSignalSpy`) neuer Stil **und** neue Größe
- Icons/Compact/Details: Größe eines Bold-Items ≥ Größe des gleichen Namens ohne Bold
- Regeländerung über `readSettings()` → sichtbare Widgets und Größen werden neu berechnet

Für den Zugriff auf `textColor()`/`customizedFont()` wird eine schmale Test-Naht benötigt
(Test-Unterklasse oder `friend`), keine duplizierte Logik.

### 7.4 Neu: Settings-Tab

- Model: Add/Remove/Move Up/Down ändern die Reihenfolge; `applySettings()` schreibt sie
  unverändert
- `restoreDefaults()` erzeugt die drei Default-Regeln, Feature aus
- Regel-Editor: Wechsel Scheme → Custom belegt den Farbwähler mit der aufgelösten Rollenfarbe vor;
  „Unchanged“ ergibt eine ungesetzte Farbe

### 7.5 Performance

- Benchmark `styleFor()` über 50 000 Items: Default-Regeln, nur Flags, viele MIME-Muster
- Laden und Scrollen eines großen Verzeichnisses mit den Default-Regeln, besonders in der
  Kompaktansicht (Breiten für alle Items)

### 7.6 Manuell

- Breeze ↔ Breeze Dark und ein benutzerdefiniertes Schema zur Laufzeit; dabei Scheme-Rollen der
  Default-Regeln (O1) und Kontrastschwelle (O5) festlegen; prüfen, dass Rollenfarben ohne
  Neustart folgen (8.10)
- `/boot/efi` (vfat), `/mnt/AI` (ntfs3) mit `.exe`- und anderen Dateien, SFTP-Verzeichnis
- Icon-, Compact-, Details-View; Split View; Rechts-nach-links
- Git-Repository mit geänderten, neuen und ignorierten Dateien (VCS-Priorität)

---

## 8. Offene technische Fragen und Risiken

1. **Spät bestimmte MIME-Typen und Bold.** Setzt eine MIME-Regel Fontattribute, kann eine
   nachträgliche inhaltsbasierte MIME-Bestimmung die Breite ändern. Lösung: bei
   `fontDependsOnMimeType()` auch für `"iconName"` die Größen neu berechnen. Das ist teurer,
   weil beim ersten Laden jedes sichtbare Item ein Icon bekommt; betrifft die Default-Regeln
   aber nicht (Media setzt nur Farbe). Kosten messen.
2. **`isRegularFile()` bei Symlinks**: Doku und Implementierung widersprechen sich (1.8); der Test
   entscheidet.
3. **Weitere Windows-Typen:** `.com` (in shared-mime-info nicht als eigener Glob vorhanden),
   `.bat`, `.cmd`, `.msi` bewusst nicht in V1; Aufnahme nur nach eigener Entscheidung. Da die
   Endungsprüfung auf jedem Dateisystem gilt, wirkt jede Erweiterung auch auf ext4.
4. **SMB/CIFS, sshfs, KIO-Worker mit Pseudo-Rechten** (SMB-Worker, MTP): Rechte können pauschal
   `0755` sein. Vorschlag: zunächst glauben und praktisch beobachten; `Smb` ggf. später der
   Nicht-POSIX-Liste hinzufügen.
5. **Kontrastschwelle** (O5): z. B. 4.5 (WCAG AA für Text) ist sicher, kann gewählte Farben
   aber deutlich verändern; 3.0 lässt mehr Farbe zu. Wird bei der Erprobung festgelegt.
6. **Breite der Reiterleiste** mit sechs Tabs und Übersetzungen.
7. **Übersetzung gespeicherter Default-Namen** (4.1): nach dem ersten Speichern sprachfest.
8. **Upstream-Akzeptanz** der kitemviews-Erweiterungen (Font-Hook, Namensfarbe) – klein, aber
   API-Erweiterungen.
9. **Dateisystem-Cache** veraltet, wenn an einem Pfad während der Laufzeit ein anderes
   Dateisystem eingehängt wird; Leeren beim Verzeichniswechsel ist ein bewusster Kompromiss.
10. **Scheme-Rollen und Palette können auseinanderlaufen.** `KColorScheme` liest die
    Farbschema-Konfiguration, die View zeichnet mit ihrer `QPalette` (1.12). Zwei Folgen:
    - Nach einem Schemawechsel zur Laufzeit muss die Konfiguration neu gelesen sein, bevor
      Rollen aufgelöst werden. Der `PaletteChange` löst die Neuauflösung aus; ob die
      geteilte Konfiguration dann schon aktuell ist, wird in Schritt 4 geprüft. Falls nicht:
      Konfiguration beim Verwerfen des Rollen-Caches explizit neu einlesen.
    - Eine Palette, die nicht aus einem KDE-Schema stammt (z. B. außerhalb von Plasma), kann
      zu Rollenfarben ohne Bezug zur Palette führen. Die Kontrastkorrektur gegen
      `palette.base()` sichert die Lesbarkeit trotzdem.

### 8.1 Noch offene Entscheidungen

Beide werden bei der visuellen Erprobung (Schritt 6) entschieden; sie blockieren die Umsetzung
nicht, weil Rolle und Schwelle jeweils an genau einer Stelle stehen:

- **O1 – Scheme-Rollen der Default-Regeln** für „Executable files“ (ob überhaupt eine Farbe
  zusätzlich zu Bold) und „Media files“.
- **O5 – Kontrastschwelle** (8.5).

---

## 9. Reihenfolge kleiner Schritte

Jeder Schritt baut, ist getestet und einzeln committbar. **Schritte 1–6 ergeben den ersten
vollständigen, nutzbaren Stand (V1).**

1. **Matcher-Kern** – `FileNameHighlighter` mit Executable (`.exe` immer, X-Bit nur auf
   POSIX-Dateisystemen, injizierbare Dateisystemabfrage), Symlink, MimeType mit mehreren
   Mustern, Kaskade, `wanted`-Begrenzung, MIME-Cache; `FileNameColor` und `resolveNameColor()`
   mit injizierbarer Farbschema-Konfiguration und zentraler Kontrastkonstante;
   `filenamehighlightertest`. Kein Rendering, keine UI.
2. **Speicherung** – `FileNameHighlightingSettings` mit Regelliste, `ColorRole`/`Color` und
   `defaultRules()` (vorläufige Rollen); `filenamehighlightingsettingstest`.
3. **kitemviews-Hooks** – `itemFont()` im Informant, Namensfarbe im Widget,
   `invalidateItemAppearance()`; bestehende Tests müssen unverändert grün sein, neue Tests für
   Namensfarbe und Hidden.
4. **Rendering-Integration** – `DolphinFileItemListWidget` (+ Informant), `DolphinItemListView`
   (Laden, Invalidierung, `itemSizeHintUpdateRequired()`); Integrationstests. Ab hier per
   Konfigurationsdatei nutzbar.
5. **Tab „Highlighting“ + Regel-Editor** mit Executable files, Symbolic links und MIME type;
   Farbquelle Scheme-Rolle/Custom; Preview; Tab-Tests.
6. **Erprobung + Diary** – manuelle Tests (7.6), Benchmark (7.5), Festlegen der Scheme-Rollen
   (O1) und der Kontrastschwelle (O5), Addendum zu UX-002 mit Entscheidungen und den
   Korrekturen aus 1.13.

Danach:

7. **NamePattern und BrokenSymlink** als weitere Bedingungen (Datenmodell und Speicherung sind
   bereits vorbereitet; nur Matcher, Editor-Auswahl und Tests).
8. **Weitere Unix-Flags** (setuid, setgid, sticky, für andere beschreibbar, FIFO, Socket, Gerät)
   und gegebenenfalls weitere Nicht-POSIX-Executable-Typen nach eigener Entscheidung.
