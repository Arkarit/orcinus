# Information density (ID1–ID5)

Branch `information-density`, started 2026-11-12.

## Problem

Orcinus shows fewer items per screen than Directory Opus. Measured at 16 px icons with the system
font Noto Sans 10pt: an Orcinus row (Compact, Details) is 22 px high, a Directory Opus row 18 px.

The row height comes from `DolphinItemListView::updateGridSize()`
(`src/views/dolphinitemlistview.cpp`):

```
itemHeight = padding * 2 + qMax(iconSize, textLines * fontMetrics.lineSpacing());
```

- `padding` is hard coded to 2 px (top and bottom → 4 px per row). It also feeds the item width,
  the gap between icon and text (`3 * padding`), the selection rectangle and the Icons layout
  (`padding * 3 + iconSize + lineSpacing`).
- `lineSpacing()` of Noto Sans 10pt is 18 px (ascent 14, descent 4) for a cap height of 9 px.
- Compact adds a fixed gap of 8 px between columns (`horizontalMargin = 8`).

## Decisions

- **Per view mode** (Icons, Compact, Details), stored like the icon size and the font in the
  `*ModeSettings` (kcfg).
- **Levels:** Normal (padding 2, today's look), Compact (padding 1), Very compact (padding 0),
  Custom.
- **Custom** additionally offers a line height in percent of the font's line spacing. Cutting the
  line height was rejected as a preset (descenders and accents may touch the selection or the next
  row); as an explicit custom setting the user decides. Range: padding 0–10 px, line height
  50–150 %.
- Upstream's Icons option "Spacing: Compact" (`IconsModeSettings::CompactSpacing`, gap *between*
  items) stays as it is: different thing (margin vs padding), and it keeps merges with upstream
  simple.
- Existing users keep Normal: the kcfg default is Normal. New users get Compact in all three
  modes: `main.cpp` writes it on the first start (the block that seeds the default profiles), so
  it is not tied to Dolphin's settings `Version`. "Defaults" in the settings dialog therefore
  means Normal (Dolphin's look).
- Not in this feature: window chrome (toolbar, location bar, filter bar, status bar). Separate
  topic after ID4.

## Steps

**ID1 – Model and layout.** kcfg entries `Density` (enum Normal/Compact/VeryCompact/Custom),
`CustomPadding` (int), `CustomLineHeight` (int, percent) in all three mode settings; accessors in
`ViewModeSettings`. `updateGridSize()` takes padding and line height from there instead of the
constant. First start writes Compact (see Decisions). Check every user of `option.padding` (widget layout, selection rect, preview size in
`KFileItemListView`, rubber band, expansion tints/labels in Compact and Details) for assumptions
of `padding > 0`. Unit tests in `dolphinitemlistviewtest`: row height per level, custom line
height, clamping. Tests isolate the shared settings (see the ED lesson about `~/.qttest`).

**ID2 – Settings UI.** In `ViewSettingsTab` per mode: combobox "Density:" with the four levels;
"Custom" shows two spin boxes (padding in px, line height in %). Live preview through the existing
apply path.

**ID3 – Compact columns.** Couple the Compact column gap to the padding (`4 * padding`, i.e. 8 px
at Normal as today). Icons: no further change (already derived from padding).

**ID4 – Profiles.** New profile element `density` (one value per mode, like `zoom`), registered
after `zoom`.

**ID5 – Documentation.** `docs/orcinus-vs-dolphin.md` (§4 Views, and §10 if the new-user default
differs), design notes entry, user docs.
