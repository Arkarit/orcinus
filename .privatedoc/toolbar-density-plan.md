# Toolbar density (TD0–TD6)

Branch `toolbar-density`, from `information-density` (experimental), started 2026-11-12.

## Problem

The bars around the file view take much more height than in Directory Opus (about 2/3 of
Orcinus' height there). Measured offscreen with Breeze, 22 px toolbar icons and Noto Sans:

| Bar | Height | Made of |
|---|---|---|
| Main toolbar (Profiles, menu bar) | 46 px | tool button 34 (icon 22 + 2 × `PM_ButtonMargin` 6) + 2 × `PM_ToolBarItemMargin` 6 |
| File view toolbar with location bar | 48 px | the location bar is 36 px high |
| Filter bar | ≈ 32 px | line edit 31, combo box 32 |
| Status bar | 30 px | own fixed heights (`DolphinStatusBar`, computed once) |

Smaller icons alone hardly help (46 → 43): line edits (31 px) and combo boxes (32 px) then
set the height.

## Decisions

- **One global setting** "Toolbar density" (Normal, Compact, Very compact) for all bars:
  window toolbars, file view toolbars with their location bars, filter bar, status bar.
  No Custom level for now.
- **The rows stay as they are.** Merging the main toolbar and the file view toolbar into one
  row (as Directory Opus does) is not wanted; whoever does not want the file view toolbar
  hides it.
- Normal is Dolphin's look and the kcfg default; new users get Compact (as with the item
  density, written on the first start).
- Experimental: on its own branch until the look holds up on the maintainer's desktop.

## Approach (prototype, uncommitted at the time of writing)

A `QProxyStyle` (`WhaleDensityStyle`, `src/whale/toolbars/`) wraps the application style.
`QWidget::setStyle()` does not reach child widgets, so a style per bar would have to be set on
every button and field; the proxy instead affects only widgets inside a `QToolBar`, the
`FilterBar` or the `DolphinStatusBar` (walk up the parents to the window). It lowers
`PM_ToolBarItemMargin` and the heights from `sizeFromContents()` (tool buttons, line edits,
combo boxes, push buttons).

Prototype measurements (main toolbar + file view toolbar):

| Level of the prototype | Top bars | Notes |
|---|---|---|
| 0 (today) | 46 + 48 = 94 px | |
| 1 (margin / 2, heights − 4) | 36 + 38 = 74 px | filter field loses its frame |
| 2 (margin 1, heights − 8) | 31 + 30 = 61 px | location bar loses its frame too; status bar space info smeared |

**Breeze limit.** Breeze draws a line edit flat (without frame) when its rect is lower than
2 × `LineEdit_FrameWidth` (6) + the font height, i.e. below 31 px with Noto Sans 10pt.
KUrlNavigator's frame follows the same rule.

## Steps

**TD0 – Measure on the desktop.** The prototype with `ORCINUS_TOOLBAR_DENSITY=1|2` on the
maintainer's Wayland desktop (real Breeze, real scale); screenshots of all bars, compared with
Directory Opus. Decide the shrink values from that.

Result (2026-11-12, Wayland, scale 1, the dev profile of a new user): identical to the offscreen
measurements, 94 / 74 / 62 px for the two top bars. The same defects: the filter field flat at
levels 1 and 2, the location bar flat at level 2, the status bar's space info smeared at 1 and 2,
the status bar itself unchanged at 30 px. Item rows 20 px (item density Compact) as expected.

**TD1 – Setting and style.** `GeneralSettings` entry `ToolbarDensity` (enum, default Normal),
new users Compact in `main.cpp`. `WhaleDensityStyle` installed after `KStyleManager::initStyle()`
and re-installed when the application style changes at runtime (the style menu replaces it).
Changing the setting re-polishes the bars (no restart). Compact: smaller tool button padding
and toolbar margins; line edits and combo boxes keep their height. Unit tests: metrics inside
and outside the bars, the main window's toolbars get lower, dialogs do not change.

**TD2 – Low line edits (Very compact).** Lower line edits, combo boxes and the location bar
while keeping Breeze's frame: when drawing their frame inside a bar, hand Breeze an option with
the font metrics of a smaller font, so that its "too small for a frame" rule does not apply.
Contained in the proxy; if it does not look right, Very compact keeps the fields at 31 px.

**TD3 – Status bar and filter bar.** `DolphinStatusBar` computes its heights once in the
constructor; recompute on density changes (Dolphin code, keep the change small and marked).
Find the cause of the smeared space info at lower heights. The filter bar follows the field
rules of TD1/TD2.

**TD4 – Settings UI.** A combo box "Toolbar density:" on the settings page where it fits
(Interface/General), with a tooltip.

**TD5 – Profiles.** Profile element `toolbarDensity` (after `density`); the shipped profiles do
not set it.

**TD6 – Documentation.** `docs/orcinus-vs-dolphin.md` (new section, §5, §10), changelog, design
notes.

## Risks

- Proxy styles around Breeze: Breeze caches per-widget data (animations, frame shadows); a
  wrapping proxy must forward everything not changed. Runtime style changes replace the proxy.
- Other styles (Fusion, Oxygen, Kvantum) have other metrics; the shrink must be relative to the
  base style's values and clamped.
- Hit targets get smaller; acceptable for the audience (power users), Normal stays available.
