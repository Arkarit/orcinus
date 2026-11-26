# Handbook draft (German) – overview

Working notes for the raw chapters in this folder. Not part of the handbook itself.

## Chapter order (part 2, after Dolphin's chapters)

| # | File | Title | Status |
|---|------|-------|--------|
| 1 | `Einfuehrung.md` | Einführung in Orcinus (part 1, before Dolphin's chapters) | user's text, switched to "Du" |
| 2 | `GenerelleUnterschiede.md` | Dolphin und Orcinus zusammen | user's start, completed |
| 3 | `Werkzeugleisten.md` | Werkzeugleisten | draft |
| 4 | `GeteilteAnsicht.md` | Geteilte Ansicht (incl. linked views) | draft |
| 5 | `Ansichten.md` | Ansichten (Columns, plugins, expanding, tints, density, flat) | draft |
| 6 | `CushionTreemap.md` | Die Cushion Treemap | draft |
| 7 | `Profile.md` | Profile | draft |
| 8 | `Betrachter.md` | Der eingebaute Betrachter | draft |
| 9 | `Dateinamen-Hervorhebung.md` | Dateinamen hervorheben | draft |
| 10 | `KI-Unterstuetzung.md` | KI-Unterstützung | draft |
| 11 | `DieKleinigkeiten.md` | Die Kleinigkeiten (filter, recent folders, Shift+double-click, scripts, dragging, symlinks, exec bit) | user's start, completed |

The English versions (`Introduction.md`, `TheLittleThings.md`) are not updated yet; they follow once the German text is approved.

Sources: `docs/orcinus-vs-dolphin.md`, `docs/ai-agents.md`, `~/Desktop/orcinus-features.txt`, and checks in the source code.

## Screenshots (`images/`)

Taken with the dev build (2026-11-26) in a fresh profile: Breeze light, no kdeglobals of the user, Orcinus' defaults for new users (profile Compact, densities Compact), **English UI** (the handbook's base language is English; German screenshots would come with the translation).
The demo home contains only generated files (landscape pictures, empty/allocated dummy files). It lived in the scratchpad (`/tmp/...`), which is why "Home" in the location bar is not `/home/<user>`.
The "Devices" and "Removable Devices" sections of Places were hidden so the host's drives do not appear.

Done: first start, file view toolbars, location bar second row, menu bar element, bar density, split own style, linked views (also used for the profile "Locked Cushion Map"), treemap, columns view, expansion tint Compact/Details, information density, flat view, profile "Images Only", viewer, name highlighting, filter bar; on 2026-11-27 also view modes menu, profiles menu, settings pages Profiles and Viewer, the name highlighting rule editor and Dolphin vs. Orcinus (stock Dolphin 25.12.3 in a fresh profile, left; Orcinus with the defaults for new users, right).

How menus and dialogs were taken (2026-11-27): an invisible `kwin_wayland --virtual --xwayland` on a private D-Bus daemon, Orcinus started there with `QT_QPA_PLATFORM=xcb` (Wayland popups need a real input serial), clicks through XTest on that Xwayland display (ctypes, libXtst), screen captured through KWin's `ScreenShot2.CaptureWorkspace` (`KWIN_SCREENSHOT_NO_PERMISSION_CHECKS=1`), then cropped to the window's client area. Nothing touches the desktop.

AI panel and canvas (2026-11-27): local agent "Qwen3.8 27B (local)" (LM Studio, OpenCode) in the AI Workbench profile; the panel shows the agent's plan in plan mode, the canvas the before/after comparison it made after the switch to build mode.

All screenshots done (25).

## UI terms without a German translation yet

Several Orcinus strings are not translated in `po-orcinus/de/orcinus.po` yet. The draft uses these German names; the translation batch before the release should use the same ones (or the text is adapted):

| English UI | Term in the draft |
|---|---|
| Bar density / Normal / Compact / Very compact | Leistendichte / Normal / Kompakt / Sehr kompakt |
| Density / Padding / Line height (view modes) | Dichte / Abstand / Zeilenhöhe |
| Recent Folders / Clear List | Zuletzt besuchte Ordner / Liste leeren |
| Relative Path | Relativer Pfad |
| Startup profile | Startprofil |
| Save Profile "…" | Profil „…“ speichern |
| Cushion Treemap | Cushion Treemap (kept) |
| AI Assistant / AI Canvas / AI Support | KI-Assistent / KI-Leinwand / KI-Unterstützung |
| AI Workbench (profile) | KI-Werkbank |
| Keep on Canvas / Add Example Tool | Auf der Leinwand behalten / Beispielwerkzeug hinzufügen |
| Saved Conversations / Memory / System Prompt | Gespeicherte Gespräche / Gedächtnis / Systemprompt |
| Talk with agents in | Mit Agenten sprechen in |
| Administrator rights | Administratorrechte |
| Configure Orcinus… | Orcinus einrichten … |

## Open points for the review

- Tone: "Du" capitalized throughout (as the user's "Ihr" was). Lowercase "du" is more common in current handbooks; easy to switch.
- `GenerelleUnterschiede.md`: the filter bar is shown in new windows, but on the very first start the profile *Compact* hides it (checked in the dev build). Mentioned in the text; intended?
- The Treemap chapter is its own chapter; it could also become a section of "Ansichten".
- Chapter "KI-Unterstützung" refers to `docs/ai-agents.md` for the setup; in the handbook that should become a chapter or a link to the website.
