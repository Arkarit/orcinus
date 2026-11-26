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

Done (17): first start, file view toolbars, location bar second row, menu bar element, bar density, split own style, linked views (also used for the profile "Locked Cushion Map"), treemap, columns view, expansion tint Compact/Details, information density, flat view, profile "Images Only", viewer, name highlighting, filter bar.

Still missing (`<TODO-Bild …>` in the text): the dev MCP screenshot only grabs the main window (or the viewer), not popup menus or dialogs, and the AI panels need a configured agent:

- `orcinus-vs-dolphin.png` (Einführung): needs Dolphin with default settings next to Orcinus
- `orcinus-view-modes.png` (menu), `orcinus-profiles-menu.png` (menu)
- `orcinus-profiles-settings.png`, `orcinus-viewer-settings.png`, `orcinus-name-highlighting-rule.png` (dialogs)
- `orcinus-ai-panel.png`, `orcinus-ai-canvas.png` (need an agent and a conversation)

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
