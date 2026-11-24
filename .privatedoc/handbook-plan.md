# Orcinus Handbook (HB)

Branch `docbook` (local), started 2026-11-24.

## Goal

An Orcinus handbook in KHelpCenter (`help:/orcinus`, F1) and as HTML on the website. It contains
Dolphin's handbook and writes Orcinus' differences and additions around it.

## Decisions

- **Dolphin's handbook stays DocBook and unchanged.** `doc/index.docbook` keeps coming from
  upstream through normal merges; KDE's translations of it (`po/<lang>/docs/dolphin/`, 19
  languages) stay usable. Converting it to AsciiDoc was rejected: every upstream change would
  have to be redone by hand, a conversion on every build is lossy (KDE entities, GUI markup),
  and the translations would be lost.
- **Orcinus' parts are written in AsciiDoc** (easier to write, also for non-developers) and
  converted to DocBook at build time with Asciidoctor.
- **A small DocBook frame** (`doc/orcinus/index.docbook.in`, KDE DocBook 4.5 dialect) assembles
  the book: Orcinus chapters plus Dolphin's chapters or sections, cut as text out of
  `doc/index.docbook` without touching that file (`<!-- orcinus:… -->` markers, replaced by
  `doc/orcinus/assemble-handbook.cmake`). Not XInclude, see HB1.
- Granularity: Orcinus text can go before, between and after Dolphin's chapters and `sect1`
  sections, and link into them. Text inside a Dolphin section cannot be changed without
  changing the upstream file; where Dolphin's text is wrong for Orcinus, a note in the Orcinus
  part says so, or that section is left out and replaced by an own one.

## Spike results (2026-11-24)

Spike files are not kept; the steps are described below.

A. **XInclude: works, with a preprocessing step.** (Replaced in HB1, see there.)
   - `meinproc6` does not process XInclude itself (the element is left alone, a validity error).
   - `xmllint --loaddtd --xinclude --nonet` with `XML_CATALOG_FILES` = KDocTools' catalog
     resolves it; KDE entities of the frame stay, those of the included part are expanded.
     `xml:base` attributes must be removed (not in the DTD).
   - `xpointer(/book/chapter[@id!='credits'])` takes several chapters at once; IDs and
     `xref`s across both parts work, the HTML chunks are generated (one per chapter/sect1),
     images are referenced relative (`default-ui.png`) and must be installed next to them.
   - Remaining validity warnings come from the expanded entities only (e.g.
     `<application>` inside `<guimenuitem>`); the original passes `meinproc6 --check` cleanly.
     The HTML is generated anyway.

B. **AsciiDoc → KDE DocBook 4.5: works.**
   - `asciidoctor -b docbook5` with `:doctype: book` (`==` → `chapter`, `===` → `section`),
     then a small XSLT (DocBook 5 → 4.5: drop the namespace, `xml:id` → `id`,
     `link xl:href` → `ulink`, drop `info`/PIs). `xsltproc` is not installed; `meinproc6
     --stylesheet x.xsl --stdout` runs it, so no extra tool is needed.
   - Asciidoctor's output is valid in KDE's DTD as it is: `menu:View[Profiles > Save]` →
     `menuchoice`/`guimenu`/`guisubmenu`/`guimenuitem`, `kbd:[Ctrl+Shift+P]` → `keycombo`,
     NOTE/TIP → `note`/`tip`, tables, images, `xref:dolphin-view[]` (takes Dolphin's title).
   - The frame takes Orcinus chapters from the generated file with XInclude as well, so the
     order of Orcinus and Dolphin chapters is set in one place.

C. **Assembled book: works.** Orcinus intro, Dolphin chapters, an Orcinus chapter in between, an
   own chapter holding one Dolphin `sect1`, and the rest of Dolphin's chapters: 13 HTML pages,
   cross references both ways, images by relative name.

### Findings that shape the build

- **Left-out Dolphin sections break Dolphin's own links into them** (`meinproc6 --check`:
  unknown ID). An own replacement section must take over the IDs that are linked to; the
  build runs the check and fails on unknown IDs.
- Orcinus' and Dolphin's images are installed into the same folder: avoid name clashes
  (prefix `orcinus-`).
- KDE's stylesheet adds "Send feedback to the KDE Docs Team" to every page; misleading for
  Orcinus. To check: a parameter or an own customization layer.
- The HTML references `help:/kdoctools6-common/…` (CSS, images): fine in KHelpCenter, the
  website needs its own copies (HB3).
- Build dependencies: Asciidoctor (Ruby, Ubuntu `asciidoctor`), `xmllint` (libxml2-utils),
  KDocTools. The handbook is optional: skipped when one is missing; CI images get them.

## Steps (after the spike)

- HB1 Build — **done 2026-11-24.**
  - XInclude was dropped: `xmllint` expands the KDE entities of the included part
    (`&dolphin;` → `<application>…`), and the expanded markup is invalid in some places
    (`guimenuitem`, `holder`, `action`), so `meinproc6 --check` fails (exit 1). The spike
    had taken these for warnings. Instead the frame has `<!-- orcinus:… -->` markers that
    `assemble-handbook.cmake` (`cmake -P`) replaces with text cut out of Dolphin's file and
    of the converted Orcinus parts; entities stay references, `--check` passes cleanly and
    fails on unknown IDs. `xmllint` is no longer an own dependency (meinproc6 uses it).
  - Chain in `doc/orcinus/CMakeLists.txt`: Asciidoctor (`--failure-level WARN`) →
    `db5-to-db45.xsl` via `meinproc6 --stylesheet` → assembly → `meinproc6 --check --cache`.
    Not `kdoctools_create_handbook`: it works on source-dir files only. Installed to
    `share/doc/HTML/en/orcinus` with Dolphin's images; target `orcinus-handbook-html` writes
    HTML pages to `<build>/doc/orcinus/html` (not in `all`).
  - Option `WHALE_BUILD_HANDBOOK` (ON); without KF6DocTools or asciidoctor it is skipped and
    `WHALE_HANDBOOK` falls back to `dolphin`. `X-DocPath` of the desktop file (F1, Help menu)
    and the `help:/dolphin/` links of inherited What's This texts point to
    `help:/${WHALE_HANDBOOK}/` (`Dolphin::withHandbookLinks()`, applied after translation,
    so the upstream strings and their translations stay unchanged).
  - Book now: Orcinus introduction (placeholder), Dolphin's chapters, own Credits chapter
    holding Dolphin's credits body. Authors/copyright: Orcinus + Dolphin's handbook's.
  - Open: packaging recipes (PPA, AUR, CI images) need asciidoctor as build dependency.
- HB2 Structure and first Orcinus chapters (from `docs/orcinus-vs-dolphin.md`).
- HB3 HTML for the website.
- HB4 Translations (po4a for the AsciiDoc parts; Dolphin's parts from `po/<lang>/docs/dolphin`).
