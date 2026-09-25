# Whale: Produkt-Rename – Änderungs- und Testplan

Stand: 2026-09-25, Basis `1914f03` (Upstream-Dolphin 26.11.70-dev).
Status: **Plan, noch nicht umgesetzt.**

**Ziel:** Whale lässt sich systemweit parallel zum installierten Dolphin installieren, ohne Kollision bei
Dateien, IDs, D-Bus, Konfiguration oder Laufzeitverhalten. Whale ist als eigener Standard-Dateimanager
wählbar, und Dolphin bleibt unverändert nutzbar.

**Nicht-Ziele:**
- Kein interner Rename: Klassen, Dateinamen im Source, Targets (`dolphin`, `dolphinprivate`, …),
  Namespaces und der Logging-Identifier `DolphinDebug` bleiben.
- Copyright, Lizenzen, Autoren-Credits und Upstream-Hinweise bleiben unverändert.

Alle Aussagen wurden am Source, am Install-Manifest von `../build-dolphin` und an der
Systeminstallation (`dpkg -L dolphin`, Dolphin 25.12.3) geprüft.

---

## 1. Zu treffende Entscheidungen (mit Empfehlung)

| # | Frage | Empfehlung | Alternative |
|---|---|---|---|
| E1 | App-ID (Desktop-Datei, D-Bus, Wayland-app_id, Icon, AppStream) | **`io.github.arkarit.whale`**: eigene Reverse-Domain, weil `org.kde.*` KDE-Projekten vorbehalten ist | `org.kde.whale` (ein kleinerer Diff, aber formal falsch) |
| E2 | Executable / Component-Name | **`whale`** (→ `whalerc`, `~/.local/share/whale`, `whalestaterc`) | – |
| E3 | VCS-Plugins (Git-Emblems usw.) | **Die System-`libdolphinvcs.so.6` und den Plugin-Namespace `dolphin/vcs` weiterverwenden.** Dann funktioniert das installierte `fileviewgitplugin.so` sofort (Begründung in 3.3). | Eigene `libwhalevcs` + `whale/vcs`; das Git-Plugin aus dolphin-plugins müsste dann selbst gebaut werden |
| E4 | KPart, KCMs, mitgelieferte Kontextmenü-Plugins, servicemenuinstaller, KNewStuff-Datei, Handbuch, Entwickler-Header | **In Phase 1 nicht installieren.** Dolphins Paket stellt sie bereits systemweit bereit. | Unter Whale-Namen installieren (später, wenn Whale ohne Dolphin laufen soll) |
| E5 | Übersetzungen | **Domain `whale`**; `po/*/dolphin.po` per eigener CMake-Regel als `whale.mo` installieren; `po/` selbst bleibt unverändert (keine Merge-Konflikte mit KDE-l10n) | Domain `dolphin` behalten und Dolphins Kataloge mitnutzen (keine Datei-Kollision, aber versionsabhängig und nicht eigenständig) |
| E6 | `org.freedesktop.FileManager1` („Im Ordner anzeigen“ aus Browsern usw.) | **Standardmäßig nicht beanspruchen** (CMake-Option `WHALE_PROVIDE_FILEMANAGER1=OFF`); ein Opt-in erfolgt pro Benutzer, wenn Whale Standard werden soll (Kapitel 6) | Immer beanspruchen (dann gewinnt, wer zuerst startet) |
| E7 | Ansichtseinstellungen pro Ordner (xattr `kde.fm.viewproperties#1` bzw. `.directory`-Gruppe `[Dolphin]`) | **Phase 1: mit Dolphin teilen.** Es ist dieselbe Datenstruktur, und das Teilen wirkt eher als Feature. | Eigener xattr-Key, dann aber eigene Kompatibilitätsbehandlung |
| E8 | Telemetrie (KUserFeedback, Produkt-ID `org.kde.dolphin`) | **Für Whale deaktivieren** (`-DCMAKE_DISABLE_FIND_PACKAGE_KF6UserFeedback=ON` fest in CMake) | – |
| E9 | Globaler Shortcut Meta+E | **Nicht in der Desktop-Datei registrieren**; der Benutzer vergibt ihn selbst | – |
| E10 | Import bestehender Dolphin-Einstellungen | **Nein, Whale startet mit frischer Konfiguration.** Ein manueller Import per `cp dolphinrc whalerc` bleibt möglich. | Einmaliger automatischer Import beim ersten Start |
| E11 | Install-Prefix | Zum Testen `…/kde/install-whale`, für den Alltag `/usr/local` | `/opt/whale` |

Der Plan unten geht von den Empfehlungen aus.

---

## 2. Kollisions-Inventar: installierte Dateien

Quelle: `../build-dolphin/install_manifest.txt` (46 Dateien + 326 Übersetzungs-/Handbuchdateien),
abgeglichen mit `dpkg -L dolphin`. „K“ bedeutet: kollidiert mit einer Datei des System-Dolphin
bzw. mit einem Pfad, den der System-Dolphin zur Laufzeit liest.

| Installierte Datei (heute) | K | Aktion für Whale |
|---|---|---|
| `bin/dolphin` | K | → `bin/whale` (`OUTPUT_NAME whale`, Target bleibt `dolphin`) |
| `lib/libdolphinprivate.so.26.11.70` + `.so.6` | **K, kritisch** (3.2) | → `libwhaleprivate.so.*` (`OUTPUT_NAME whaleprivate`) |
| `lib/libdolphinvcs.so.*`, `include/Dolphin/*`, `include/dolphin*_export.h`, `lib/cmake/DolphinVcs/*` | K | **nicht installieren**; zur Laufzeit wird die System-`libdolphinvcs.so.6` genutzt (E3) |
| `plugins/kf6/parts/dolphinpart.so`, `share/dolphin/dolphinpartactions.desktop` | K | nicht installieren (E4) |
| `plugins/dolphin/kcms/kcm_dolphin{general,viewmodes}.so` | K | nicht installieren (E4) |
| `plugins/kf6/kfileitemaction/{movetonewfolder,hidefile,setfoldericon}itemaction.so` | K | nicht installieren (E4). Unter anderem Namen installiert, erschienen sie in **allen** KIO-Menüs doppelt, auch in Dolphin. |
| `bin/servicemenuinstaller`, `share/knsrcfiles/servicemenu.knsrc` | K | nicht installieren (E4); Whales „Service-Menüs herunterladen“ nutzt die Datei des Systems |
| `share/applications/org.kde.dolphin.desktop` (+ kglobalaccel-Symlink) | K | → `io.github.arkarit.whale.desktop`, ohne kglobalaccel-Symlink |
| `share/metainfo/org.kde.dolphin.appdata.xml` | K | → `io.github.arkarit.whale.metainfo.xml` (neue, schlanke Datei) |
| `share/icons/hicolor/scalable/apps/org.kde.dolphin.svg` | K | → `io.github.arkarit.whale.svg` (zunächst eine Kopie, später ein eigenes Icon) |
| `share/dbus-1/services/org.kde.dolphin.FileManager1.service` | K | Standard: nicht installieren (E6). Bei Opt-in: `io.github.arkarit.whale.FileManager1.service` |
| `lib/systemd/user/plasma-dolphin.service` | K | Standard: nicht installieren. Bei Opt-in: `plasma-whale.service` |
| `share/dbus-1/interfaces/org.freedesktop.FileManager1.xml` | (gleicher Inhalt, von Dolphin nicht installiert) | nicht installieren (nur für Entwickler) |
| `share/config.kcfg/dolphin_*.kcfg` (7) | K | nicht installieren (wird nur von KConfig-Werkzeugen gebraucht) |
| `share/kconf_update/dolphin_*.upd/.py`, `lib/kconf_update_bin/dolphin_*` | K | nicht installieren: Die Migrationen zielen auf `dolphinrc`, und eine frische `whalerc` braucht keine |
| `share/qlogging-categories6/dolphin.categories` | K | → `whale.categories`, Kategorie `org.kde.whale` |
| `share/zsh/site-functions/_dolphin` | K | → `_whale` mit `#compdef whale` |
| `share/locale/*/LC_MESSAGES/dolphin.mo` (93), `dolphin_servicemenuinstaller.mo` (56) | K | → `whale.mo` (E5); `servicemenuinstaller`-Kataloge entfallen |
| `share/doc/HTML/*/dolphin/*` (Handbuch) | K | nicht installieren; die `help:/dolphin/…`-Links öffnen weiterhin das installierte Dolphin-Handbuch |

**Nach Phase 1 installiert Whale nur noch:**
- das Executable `whale` und `libwhaleprivate.so.*`
- die Desktop-Datei, die Metainfo-Datei und das Icon
- die Datei für die Logging-Kategorie und die zsh-Completion
- die Übersetzungen `whale.mo`
- optional (FileManager1-Opt-in): die D-Bus-Service-Datei und die systemd-Unit

---

## 3. Laufzeit-Kollisionen (keine Datei-Konflikte, aber gleiche Namen)

### 3.1 Übersicht

| Bereich | Heute (Quelle) | Folge ohne Änderung | Änderung |
|---|---|---|---|
| KAboutData-Component | `main.cpp:95–96` `"dolphin"` | leitet `applicationName`, KDBusService-Namen, `AppDataLocation`, `…staterc` und KCrash ab | `"whale"` + `setOrganizationDomain("arkarit.github.io")` + `setDesktopFileName("io.github.arkarit.whale")` |
| Anzeigename | `main.cpp:97` `i18n("Dolphin")` | Fenstertitel und About zeigen „Dolphin“ | `i18n("Whale")`; die Copyright-Zeile `:101` bleibt |
| Homepage / Bugs | `main.cpp:102`; KCrash/DrKonqi meldet an bugs.kde.org, Produkt „dolphin“ | **Whale-Abstürze landen bei KDE** | Homepage → GitHub; `setBugAddress("https://github.com/Arkarit/whale/issues")` |
| Single-Instance-Suche | `global.cpp:156` Präfix `org.kde.dolphin-` | **Whale übergibt seine URLs an ein laufendes Dolphin** (und umgekehrt) | Präfix aus der App-ID ableiten |
| D-Bus-Objektpfad | `global.cpp:146, 163` `/dolphin/Dolphin_1` | Zugriff auf den eigenen Whale-Pfad (`/whale/…`) scheitert | Pfad aus `QCoreApplication::applicationName()` bilden (im Test mit `qdbus` verifizieren) |
| Eigene Service-Namen | `main.cpp:250`, `dbusinterface.cpp:35, 47` `org.kde.dolphin-%1` | falscher „bevorzugter“ Dienst | aus der App-ID ableiten |
| Neues Fenster starten | `global.cpp:44` `"dolphin --new-window"` | **„In neuem Fenster öffnen“ startet Dolphin** | Executable-Name `whale` |
| Tab ablösen | `dolphintabwidget.cpp:454–455` `"dolphin"`, `setDesktopName("org.kde.dolphin")` | **Ein abgelöster Tab wird zu einem Dolphin-Fenster** | `whale` + App-ID |
| Konfigurationsdatei | 11× `<kcfgfile name="dolphinrc"/>` (Liste 5.3) sowie `viewpropertiesdialog.cpp:212` und `settings/interface/folderstabssettingspage.cpp:217` | **Whale und Dolphin überschreiben gegenseitig ihre Einstellungen** | `whalerc` |
| KXMLGUI-Component | `dolphinmainwindow.cpp:147` `setComponentName("dolphin", …)`, `dolphin.qrc` Prefix `/kxmlgui5/dolphin` | **Toolbar- und Shortcut-Anpassungen landen in `~/.local/share/kxmlgui5/dolphin/`, also bei Dolphin** | Component `"whale"`, qrc-Prefix `/kxmlgui5/whale`, `setupGUI(…, "dolphinui.rc")` mit explizitem Dateinamen (die rc-Dateien selbst werden nicht umbenannt) |
| Session-Management | `main.cpp:239`, `dolphinmainwindow.cpp:760` `setSessionConfig("dolphin","dolphin")` | gemeinsame Session-Datei → falsche Fenster beim Login-Restore | `"whale"` |
| Fenster-/App-Icon | `main.cpp:76`, `settings/dolphinsettingsdialog.cpp:47` `org.kde.dolphin` | Whale zeigt das Dolphin-Icon | App-ID |
| Wayland app_id / X11 WM_CLASS | abgeleitet aus desktopFileName / applicationName | Taskleiste gruppiert Whale unter Dolphin | ergibt sich aus E1/E2; in der Desktop-Datei `StartupWMClass=whale` |
| Telemetrie | `userfeedback/dolphinfeedbackprovider.cpp:28` `org.kde.dolphin` | Whale sendet als Dolphin an KDE | deaktivieren (E8) |
| FileManager1 im GUI-Modus | `main.cpp:236` `DBusInterface` registriert immer (QueueService) | Wer zuerst läuft, bekommt „Im Ordner anzeigen“-Aufrufe | nur mit `WHALE_PROVIDE_FILEMANAGER1` (E6) |
| Übersetzungsdomain | `main.cpp:51`, `src/CMakeLists.txt:14` `TRANSLATION_DOMAIN "dolphin"` | lädt Dolphins (ältere) Kataloge | `whale` (E5) |
| Hinweistext root/sudo | `main.cpp:63` `"dolphin --sudo"` | irreführender Hinweis | `whale --sudo` |

### 3.2 Kritisch: gleichnamige private Bibliothek

- Das System hat `/usr/lib/x86_64-linux-gnu/libdolphinprivate.so.6` → **25.12.3**. Whale baut
  `libdolphinprivate.so.6` → **26.11.70**, mit derselben SONAME und einer anderen ABI.
- `/usr/bin/dolphin` hat **keinen RUNPATH**, und `/etc/ld.so.conf.d` listet `/usr/local/lib/x86_64-linux-gnu`
  **vor** `/usr/lib/x86_64-linux-gnu`.
- Folge bei einer Whale-Installation nach `/usr/local`: **Nach `ldconfig` lädt der System-Dolphin Whales
  Bibliothek und stürzt ab oder verhält sich undefiniert.** Deshalb ist `OUTPUT_NAME whaleprivate`
  Pflicht und nicht nur Kosmetik.

### 3.3 VCS-Plugins: warum die System-`libdolphinvcs` (E3)

- `fileviewgitplugin.so` (aus dolphin-plugins, installiert in `…/qt6/plugins/dolphin/vcs/`) linkt gegen
  `libdolphinvcs.so.6` (System). `KPluginFactory::instantiatePlugin<KVersionControlPlugin>()` prüft per
  `qobject_cast`, und damit gegen die Metaobjekt-Instanz **einer** Bibliothek. Hätte Whale eine eigene,
  umbenannte VCS-Bibliothek, würde jedes Systemplugin abgelehnt.
- Die ABI ist kompatibel: `kversioncontrolplugin.h` ist seit 2023 unverändert (letzte Änderung
  `38c34ee`), die `.cpp` hat seitdem nur einen trivialen clang-tidy-Commit.
- **Umsetzung:**
  - Das Target `dolphinvcs` wird weiter gebaut (SONAME `libdolphinvcs.so.6`), aber nicht installiert.
  - Im Build-Verzeichnis findet der Loader die eigene Kopie (RUNPATH), installiert die des Systems.
  - Der Namespace `dolphin/vcs` in `versioncontrolobserver.cpp:298` und
    `contextmenusettingspage.cpp:318` bleibt.
  - Die Aktivierung liegt getrennt in `whalerc [VersionControl]`.
- **Abhängigkeit:** Whale braucht dann das Paket `dolphin` bzw. die Bibliothek `libdolphinvcs6` zur
  Laufzeit. Das passt zur Vorgabe „Dolphin bleibt installiert“ und muss dokumentiert werden.

### 3.4 Absichtlich geteilt (keine Änderung)

Diese Ressourcen sind KDE-weit gemeinsam, und das ist so gewollt:
- `kdeglobals`, `kiorc`
- `kservicemenurc`: Das Aktivieren eines Service-Menüs in Whale wirkt auch in Dolphin und allen
  KIO-Programmen. Das ist bei Dolphin genauso.
- KIO-Service-Menüs, `kfile/bookmarks.xml`, Orte (`user-places.xbel`), Papierkorb
- Baloo, der Thumbnail-Cache, Vorlagen für „Neu erstellen“
- Ansichtseinstellungen pro Ordner per xattr bzw. `.directory [Dolphin]` (E7). Die globalen
  Ansichtseinstellungen liegen dagegen automatisch getrennt in `~/.local/share/whale/view_properties`.
- das interne Drag-MIME `application/x-dolphin-blacklist-drop`
- der D-Bus-Interface-Name `org.kde.dolphin.MainWindow` (`dolphinmainwindow.h:75`): Er ist kein
  Busname, also gibt es keine Kollision. Er bleibt, damit die generierten Klassen gleich bleiben.
- das Notification-Event „Trash: emptied“: Dolphin installiert kein `.notifyrc`, das Verhalten bleibt
  also gleich.
- `help:/dolphin/…`-Links und `X-DocPath` zeigen weiterhin auf das installierte Dolphin-Handbuch.

---

## 4. Branding-Konstanten zentral

Um den Diff klein und Upstream-Merges einfach zu halten, werden alle neuen Namen **einmal** im
Top-Level-`CMakeLists.txt` definiert:

```cmake
set(WHALE_EXECUTABLE   "whale")
set(WHALE_COMPONENT    "whale")
set(WHALE_ORG_DOMAIN   "arkarit.github.io")
set(WHALE_APP_ID       "io.github.arkarit.whale")
set(WHALE_DISPLAY_NAME "Whale")
set(WHALE_BUG_URL      "https://github.com/Arkarit/whale/issues")
set(WHALE_HOMEPAGE     "https://github.com/Arkarit/whale")
option(WHALE_PROVIDE_FILEMANAGER1 "Register org.freedesktop.FileManager1 and install its service file" OFF)
```

Sie gelangen über `src/config-dolphin.h.cmake` als `#define WHALE_*` in den Code. Die Desktop-,
Metainfo-, Service- und Completion-Dateien werden per `configure_file` aus `*.in`-Vorlagen erzeugt.

---

## 5. Konkrete Änderungsliste (in Commit-Schritten)

### Schritt 1 – Branding-Konstanten
- `CMakeLists.txt`: Variablen und Option (Kapitel 4); KUserFeedback fest deaktivieren (E8).
- `src/config-dolphin.h.cmake`: `WHALE_*`-Defines, `WHALE_PROVIDE_FILEMANAGER1`.

### Schritt 2 – Identität und Single-Instance
- `src/main.cpp`:
  - `:51` Translation-Domain `"whale"`
  - `:63` Hinweistext
  - `:76` Icon
  - `:96–97` Component und Anzeigename
  - `:102` Homepage; neu: `setOrganizationDomain`, `setDesktopFileName`, `setBugAddress`
  - `:239` Session-Config
  - `:250` Service-Name
  - `:231–236`: `DBusInterface` nur `#if WHALE_PROVIDE_FILEMANAGER1`; ebenso im Daemon-Zweig
    (`:172–192`), wo der Daemon ohne FileManager1 keinen Zweck hat.
- `src/global.cpp`: `:44` Executable, `:146/163` Objektpfad, `:156` Service-Präfix.
- `src/dbusinterface.cpp:35, 47`: Service-Name.
- `src/dolphintabwidget.cpp:454–455`: Executable und Desktop-Name.
- `src/dolphinmainwindow.cpp`: `:147` Component; `:214` `setupGUI(..., "dolphinui.rc")` mit
  explizitem Dateinamen; `:760` Session-Config.
- `src/dolphin.qrc`: Prefix `/kxmlgui5/whale`.
- `src/settings/dolphinsettingsdialog.cpp:47`: Icon.
- `src/tests/dolphinmainwindowtest.cpp:139, 158, 164, 1315`: auf Domain, qrc-Pfad und
  Session-Name anpassen.

### Schritt 3 – Konfigurationsdatei `whalerc`
- `<kcfgfile name="dolphinrc"/>` → `whalerc` in diesen 11 Dateien:
  - `settings/`: `dolphin_compactmodesettings`, `_contentdisplaysettings`, `_contextmenusettings`,
    `_detailsmodesettings`, `_generalsettings`, `_iconsmodesettings`, `_versioncontrolsettings`
  - `panels/`: `folders/dolphin_folderspanelsettings`, `information/dolphin_informationpanelsettings`,
    `places/dolphin_placespanelsettings`
  - `search/dolphin_searchsettings`

  (`dolphin_directoryviewpropertysettings.kcfg` nutzt `arg="true"`, betrifft also die
  Ordner-Dateien und bleibt, siehe E7.)
- `src/settings/viewpropertiesdialog.cpp:212` und
  `src/settings/interface/folderstabssettingspage.cpp:217`: `"dolphinrc"` → `"whalerc"`,
  besser `KSharedConfig::openConfig()` (Standard = Component + `rc`).
- Die kconf_update-Helfer (`settings/dolphin_*update*.cpp`, `*.py`) bleiben im Source
  (Upstream-Merges), werden aber nicht installiert (Schritt 4).

### Schritt 4 – Installations-Layout (`src/CMakeLists.txt`, `CMakeLists.txt`, `src/itemactions/CMakeLists.txt`)
- `dolphin`: `set_target_properties(dolphin PROPERTIES OUTPUT_NAME ${WHALE_EXECUTABLE})`.
- `dolphinprivate`: `OUTPUT_NAME whaleprivate` (SONAME `libwhaleprivate.so.6`).
- `dolphinvcs`: bauen, **nicht** installieren (Header, Export-Header, CMake-Config ebenfalls nicht).
  Der Root-Block `CMakeLists.txt:179–200` entfällt.
- Nicht installieren:
  - `dolphinpart` + `dolphinpartactions.desktop`
  - KCMs (`:613–614`)
  - `add_subdirectory(itemactions)` → nur bauen oder ganz entfernen
  - `settings/contextmenu/servicemenuinstaller` + `servicemenu.knsrc` (`:617–620`)
  - kcfg-Dateien (`:632–639`)
  - kconf_update-Dateien und -Binaries (`:641–676`)
  - `doc/` bzw. `kdoctools_install`
  - `dolphin_export.h`

  **Technik:** Die `install()`-Aufrufe stehen hinter `if(NOT WHALE_BUILD)` bzw. einer Option
  `WHALE_INSTALL_DOLPHIN_EXTRAS=OFF`. So bleiben die Diffs zu Upstream klein, und die Targets werden
  weiter gebaut (die Tests brauchen `dolphinprivate`).
- `ecm_qt_declare_logging_category` (`src/CMakeLists.txt:187`): `CATEGORY_NAME org.kde.whale`;
  `ecm_qt_install_logging_categories` (`CMakeLists.txt:~221`): `FILE whale.categories`.
- zsh: `completions/dolphin.zsh` → per `configure_file` zu `_whale` mit `#compdef whale`.
- FileManager1 (`CMakeLists.txt:~204–219`) und `plasma-dolphin.service.in` nur
  `if(WHALE_PROVIDE_FILEMANAGER1)`, dann als `${WHALE_APP_ID}.FileManager1.service` +
  `plasma-whale.service` (neue Vorlage).

### Schritt 5 – Desktop-Integration (neue Dateien, Originale bleiben für Upstream-Merges)
- `src/io.github.arkarit.whale.desktop.in`:
  - `Name=Whale`, `Exec=whale %u`, `Icon=io.github.arkarit.whale`, `Type=Application`
  - `Categories=Qt;KDE;System;FileTools;FileManager;`, `MimeType=inode/directory;`
  - `GenericName`/`Comment` inklusive Übersetzungen aus der Dolphin-Datei (ohne die `Name[xx]=Dolphin`-Zeilen)
  - `InitialPreference=9` (damit Dolphin Standard bleibt, bis der Benutzer umstellt)
  - `X-DBUS-ServiceName=io.github.arkarit.whale`, `StartupWMClass=whale`, `X-DocPath=dolphin/index.html`
  - **ohne** `X-KDE-Shortcuts`
- Install nach `KDE_INSTALL_APPDIR`; der kglobalaccel-Symlink (`src/CMakeLists.txt:629`) entfällt.
- `src/io.github.arkarit.whale.metainfo.xml`: `id`, `name`, `summary`, `metadata_license`,
  `project_license` (GPL-2.0-or-later wie Dolphin), `launchable`, `provides/binary whale`, URLs
  auf GitHub, Hinweis „based on KDE Dolphin“.
- Icon: `src/icons/io.github.arkarit.whale.svg` (zunächst eine Kopie von `org.kde.dolphin.svg`,
  Lizenz beibehalten und in `REUSE.toml` annotieren), Install nach hicolor/scalable/apps.
  `ecm_add_app_icon` (nur Windows/macOS) bleibt unverändert.

### Schritt 6 – Übersetzungen (E5)
- `src/CMakeLists.txt:14` `TRANSLATION_DOMAIN="whale"`.
- `ki18n_install(po)` ersetzen durch eine eigene Schleife: `po/<lang>/dolphin.po` → `msgfmt` →
  `share/locale/<lang>/LC_MESSAGES/whale.mo`. Die `servicemenuinstaller`-Kataloge entfallen.
- `po/` bleibt unverändert, damit die `GIT_SILENT`-Syncs von KDE weiter konfliktfrei mergen.
- Bekannte Grenze: Übersetzte Texte nennen weiterhin „Dolphin“, und neu geänderte Strings aus
  Schritt 7 sind zunächst unübersetzt.

### Schritt 7 – Sichtbare Produktnamen (optional, eigener Commit)
In 11 Dateien stehen 26 `i18n`-Aufrufe mit „Dolphin“; die meisten sollten `%1` mit
`QGuiApplication::applicationDisplayName()` verwenden (wie schon `dolphinmainwindow.cpp:646`).
Wichtige Stellen:
- `main.cpp:141–146`: `--help`-Texte
- `dolphinviewcontainer.cpp:104, 963`
- `dolphinmainwindow.cpp:1816`
- `settings/viewmodes/viewsettingstab.cpp:72`
- `settings/interface/folderstabssettingspage.cpp:110`
- `settings/interface/confirmationssettingspage.cpp:64–76`
- kcfg-Labels (nur intern)

**Nicht ändern:**
- `main.cpp:101` („© … The Dolphin Developers“)
- die Autorenliste
- die i18n-Kontextkommentare
- `servicemenuinstaller` (wird nicht installiert)

### Schritt 8 – Dokumentation
- `README.md`: Abschnitt „Whale – based on KDE Dolphin“, Installations- und Default-Hinweise
  (den Upstream-Text nicht löschen).
- `CLAUDE.md`: neue Namen, Laufzeitabhängigkeit von `libdolphinvcs.so.6`, Start mit `whale --new-window`.

**Nicht angefasst werden:** Klassennamen, Source-Dateinamen, CMake-Target-Namen, `po/`, `doc/`,
`LICENSES/`, SPDX-Header, `.gitlab-ci.yml`, `.kde-ci.yml`, `.flatpak-manifest.json` (Flatpak ist nicht
Teil von Phase 1; die ID dort bei Bedarf später ändern).

---

## 6. Whale als Standard-Dateimanager

1. **Ordner öffnen** (`xdg-open`, KIO, „Ordner öffnen“ in anderen Programmen):
   Systemeinstellungen → Standardanwendungen → Dateiverwaltung → **Whale**. Whale erscheint dort
   wegen `Categories=FileManager` + `MimeType=inode/directory`.
   CLI: `xdg-mime default io.github.arkarit.whale.desktop inode/directory`. Das schreibt
   `~/.config/mimeapps.list`, heute steht dort `org.kde.dolphin.desktop`.
   Zurück: `xdg-mime default org.kde.dolphin.desktop inode/directory`.
2. **„Im Ordner anzeigen“** (Browser, Mailprogramme → D-Bus `org.freedesktop.FileManager1`):
   - Mit `WHALE_PROVIDE_FILEMANAGER1=ON` bauen.
   - Eine Benutzer-Override-Datei `~/.local/share/dbus-1/services/org.freedesktop.FileManager1.service`
     mit `Exec=<prefix>/bin/whale --daemon` anlegen. Das Benutzerverzeichnis hat Vorrang vor
     `/usr/share/dbus-1/services`.
   - Danach neu anmelden, weil ein laufender `dolphin --daemon` bzw. `plasma-dolphin.service` den
     Namen sonst weiter hält.
   - Zurück: Datei löschen und neu anmelden.
3. **Meta+E:** Systemeinstellungen → Kurzbefehle: bei Dolphin entfernen und Whale zuweisen.
4. **Dolphin bleibt** über das Startmenü, `dolphin` und `org.kde.dolphin.desktop` normal benutzbar.
   Konfiguration und Instanz sind getrennt.

---

## 7. Testplan

### 7.1 Build und Unit-Tests
- [ ] Frisch konfigurieren: separates Build-Verzeichnis `../build-whale` mit Prefix
      `…/kde/install-whale`. Den bestehenden KDevelop-Build `../build-dolphin` nicht anfassen,
      bis alles grün ist.
- [ ] `cmake --build ../build-whale && ctest --test-dir ../build-whale --output-on-failure`: alle Tests
      grün, insbesondere `dolphinmainwindowtest` (qrc-Pfad, Domain).
- [ ] `grep -rn 'org\.kde\.dolphin-\|"/dolphin/Dolphin_1"\|"dolphin --new-window"\|kcfgfile name="dolphinrc"' src`
      liefert 0 Treffer (Regressions-Guard, später als CTest-Skript).

### 7.2 Installations-Kollisionen (automatisch)
- [ ] `DESTDIR=/tmp/whale-stage cmake --install ../build-whale --prefix /usr/local`, danach für jede
      Datei: `dpkg -S <pfad>` → **keine** Treffer.
- [ ] Die Pfade im Staging mit `dpkg -L dolphin dolphin-plugins` vergleichen → keine Überschneidung
      (auch nicht in `/usr/local` gespiegelt: gleiche Pfade unterhalb von `share/`, weil XDG-Pfade
      prefixübergreifend gesucht werden).
- [ ] `grep -rl 'org.kde.dolphin' /tmp/whale-stage`: nur erwartete Treffer (`X-DocPath`, Handbuch-Links).
- [ ] `readelf -d whale | grep NEEDED`: `libwhaleprivate.so.6` und `libdolphinvcs.so.6`, keine
      `libdolphinprivate`.
- [ ] Nach echter Installation nach `/usr/local` + `ldconfig`:
      `ldd /usr/bin/dolphin | grep dolphinprivate` → weiterhin `/usr/lib/...25.12.3`.

### 7.3 Isolation zur Laufzeit
Vorher Prüfsummen der Dolphin-Dateien sichern:
`sha256sum ~/.config/dolphinrc ~/.local/state/dolphinstaterc ~/.local/share/kxmlgui5/dolphin/* 2>/dev/null > /tmp/dolphin-before.sha`

| # | Test | Erwartung |
|---|---|---|
| R1 | Dolphin läuft, dann `whale ~/Downloads` | Ein neues **Whale**-Fenster öffnet sich; Dolphin bekommt keinen Tab |
| R2 | Whale läuft, dann `dolphin ~` | Das geht an Dolphin, nicht an Whale |
| R3 | Whale läuft, dann ein zweites `whale ~/tmp` | Tab bzw. Fenster in der **laufenden Whale-Instanz** (Single-Instance funktioniert) |
| R4 | `qdbus \| grep -i 'whale\|dolphin'` | `io.github.arkarit.whale-<pid>` und `org.kde.dolphin-<pid>` getrennt; Objektpfad unter `/whale/…` |
| R5 | Whale: Tab ablösen, „In neuem Fenster öffnen“, Tab per Mittelklick | Es entstehen nur Whale-Fenster |
| R6 | Whale: Einstellung ändern, Toolbar anpassen, Shortcut ändern, Panel verschieben, schließen | `~/.config/whalerc`, `~/.local/state/whalestaterc` und `~/.local/share/kxmlgui5/whale/` ändern sich; `sha256sum -c /tmp/dolphin-before.sha` ist OK |
| R7 | Taskleiste / Fensterwechsler | Whale hat ein eigenes Icon und eine eigene Gruppe; das Anheften ist unabhängig |
| R8 | Info → Über Whale | Name „Whale“, Credits und Copyright von Dolphin unverändert |
| R9 | `LANGUAGE=de whale --new-window` | Deutsche Oberfläche |
| R10 | `kill -SEGV $(pidof whale)` | DrKonqi nennt Whale und bietet keine Meldung an bugs.kde.org/dolphin an |
| R11 | Git-Plugin in den Whale-Einstellungen aktivieren, ein Repo öffnen | Git-Embleme und -Kontextmenü in Whale; in Dolphin bleibt die Einstellung unverändert |
| R12 | Rechtsklick auf eine Datei | „In neuen Ordner verschieben“ usw. erscheint **einmal** (aus Dolphins Paket) |
| R13 | Info-Panel, Vorschaubilder, Terminal-Panel, Suche, Papierkorb, „Als Administrator“ | Funktionieren wie in Dolphin |
| R14 | Abmelden und Anmelden mit offenem Whale und Dolphin | Beide werden getrennt wiederhergestellt |
| R15 | Whale-Session-Datei | Liegt unter `~/.config/session/whale_*` und nicht unter `dolphin_*` |

### 7.4 Standard-Dateimanager
| # | Test | Erwartung |
|---|---|---|
| D1 | `xdg-mime default io.github.arkarit.whale.desktop inode/directory`; `xdg-open ~` | Whale öffnet sich |
| D2 | Aus einem anderen Programm (z. B. Kate: „Ordner öffnen“) | Whale |
| D3 | Dolphin aus dem Startmenü starten | Dolphin startet normal |
| D4 | Ohne FileManager1-Opt-in: Firefox → Downloads → „Im Ordner anzeigen“ | Dolphin (unverändert) |
| D5 | Mit Opt-in + Override-Datei + neuer Anmeldung: dasselbe; zusätzlich `dbus-send --session --dest=org.freedesktop.FileManager1 --type=method_call /org/freedesktop/FileManager1 org.freedesktop.FileManager1.ShowItems array:string:"file://$HOME/.bashrc" string:""` | Whale öffnet sich mit markierter Datei |
| D6 | Zurückstellen (Kapitel 6) | Dolphin ist wieder Standard |

### 7.5 Deinstallation und Integrität
- [ ] `xargs rm < ../build-whale/install_manifest.txt`; danach startet Dolphin normal.
- [ ] `dpkg -V dolphin` liefert vor und nach Installation und Deinstallation keine Abweichungen.
- [ ] `sha256sum -c /tmp/dolphin-before.sha` ist OK.

---

## 8. Risiken und Upstream-Merge-Aufwand

| Risiko | Bewertung / Gegenmaßnahme |
|---|---|
| Upstream ändert Stellen aus Schritt 2 und 3 | Nur ca. 15 Zeilen in ca. 12 Dateien, und die Konstanten sind zentral. Konflikte sind klein und offensichtlich. |
| Upstream fügt neue `.kcfg` mit `dolphinrc` hinzu | Der Regressions-Guard (7.1) schlägt an |
| Upstream fügt neue `install()`-Ziele hinzu | Der Staging-Kollisionstest (7.2) schlägt an; nach jedem Upstream-Merge ausführen |
| System-Dolphin wird aktualisiert, `libdolphinvcs` ändert die ABI | Unwahrscheinlich (seit 2023 stabil); sonst E3-Alternative (eigene `whalevcs` + eigenes Git-Plugin) |
| Whale ohne installierten Dolphin | In Phase 1 nicht unterstützt (VCS-Lib, Kontextmenü-Plugins, KNS, Handbuch). Phase 2: die E4-Teile unter Whale-Namen installieren. |
| Die `.directory`-Gruppe `[Dolphin]` bzw. der xattr werden geteilt | Gewollt (E7). Sollte Whales Ansichtslogik abweichen, wird ein eigener Key nötig. |
| `help:/dolphin`-Links | Funktionieren nur, solange das Dolphin-Handbuch installiert ist (hier der Fall) |

---

## 9. Offene Punkte zur Bestätigung

1. Die App-ID **`io.github.arkarit.whale`** (E1) – oder eine andere Domain?
2. Weiterverwendung der **System-VCS-Bibliothek** (E3) und damit die Laufzeitabhängigkeit von Dolphin
   in Phase 1: OK?
3. Übersetzungen per eigener `msgfmt`-Regel (E5) – oder zunächst nur Englisch und Deutsch?
4. Schritt 7 (sichtbare „Dolphin“-Texte) gleich mitmachen oder später?
5. Ein eigenes Icon: Gibt es schon ein Motiv, oder zunächst die Kopie des Dolphin-Icons?
