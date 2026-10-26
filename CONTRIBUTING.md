<!--
SPDX-FileCopyrightText: 2026 Orcinus contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Contributing to Orcinus

Orcinus is an unofficial fork of KDE Dolphin. Contributions are welcome; this
page explains where things belong and how changes are made.

## Where does my report belong?

- **Orcinus:** anything that only happens in Orcinus, including its own features
  (file view toolbars, styles, the built-in viewer, view plugins such as the
  treemap, linked split views, expansion tints) and its packaging.
  Use the [Orcinus issue tracker](https://github.com/Arkarit/orcinus/issues).
- **Dolphin:** problems that also happen in an unmodified Dolphin of the same
  version. Please report them to KDE on the
  [KDE Bugtracking System](https://bugs.kde.org/enter_bug.cgi?format=guided&product=dolphin).
  An Orcinus issue that links the KDE report is still welcome.
- **KDE Frameworks, Qt, Plasma:** problems in file dialogs, KIO workers,
  thumbnails, Plasma integration and similar belong to the respective KDE or Qt
  project.

If you are not sure, open an Orcinus issue; we sort it out together.

## Making changes

- For anything bigger than a small fix, open an issue first so we can agree on
  the approach. Orcinus follows Dolphin's
  [development philosophy](README.md#development-philosophy): features must not
  clutter menus, context menus or toolbars for people who do not need them.
- Keep changes focused. Fixes that also apply to Dolphin should stay small and
  self-contained, so they can be proposed upstream as well.
- Code follows KDE's style: format with the repository's `.clang-format`
  (`git clang-format`), user-visible strings use KI18n (`i18nc` with a context).
- Every new file needs SPDX headers (`SPDX-FileCopyrightText`,
  `SPDX-License-Identifier`); the repository is checked with
  [REUSE](https://reuse.software/).
- Commit messages, code comments and documentation are in English.
- Internal names still use the working title "Whale" (`src/whale/`, `Whale*`
  classes, `WHALE_*` CMake variables). Keep them; everything installed or shown
  to users says Orcinus.

## Building and testing

```bash
cmake -S . -B ../build-orcinus -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_TESTING=ON \
    -DCMAKE_INSTALL_PREFIX="$PWD/../install-orcinus"
cmake --build ../build-orcinus
ctest --test-dir ../build-orcinus --output-on-failure
```

The tests run on the offscreen Qt platform and do not open windows. Please run
the test suite before submitting a change and add tests for new behavior.

## Translations

Orcinus reuses Dolphin's translations for everything it shares with Dolphin.
Only the strings that exist in Orcinus alone (about 300) are translated here, in
`po-orcinus/<language>/orcinus.po`; at build time they are merged in front of
Dolphin's catalog of the same language.

- The catalogs for German, French, Spanish, Italian, Brazilian Portuguese,
  Polish, Russian, Simplified Chinese and Japanese were translated by an AI
  model. German has been reviewed by a native speaker; the others have not, so
  corrections are very welcome.
- To add a language, copy `po-orcinus/orcinus.pot` to
  `po-orcinus/<language>/orcinus.po` (the language code as in `po/`) and
  translate it with any PO editor, e.g. Lokalize.
- Send translations as pull requests. Once the repository is public, Orcinus
  will also be translatable on Weblate.
- After changing user-visible strings, run `po-orcinus/update-catalogs.sh`; it
  regenerates the template and merges it into every catalog.
- Use the same terms as Dolphin's catalog of your language (`po/<language>/dolphin.po`).
