#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Orcinus contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Minimal stand-in for KDE's extractrc: prints the translatable texts of .rc (<text>),
.kcfg (<label>, <whatsthis>, <tooltip>) and .ui (<string>) files as i18n calls for xgettext."""
import sys, re, xml.etree.ElementTree as ET
out = []
def esc(s): return s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n')
for f in sys.argv[1:]:
    try: root = ET.parse(f).getroot()
    except Exception: continue
    for el in root.iter():
        tag = el.tag.split('}')[-1]
        if tag in ('text', 'label', 'whatsthis', 'tooltip', 'string', 'title') and el.text and el.text.strip():
            if f.endswith('.ui') and el.attrib.get('notr') == 'true': continue
            ctx = el.attrib.get('context') or el.attrib.get('comment')
            out.append(f'// i18n: file: {f}\n' + (f'i18nc("{esc(ctx)}","{esc(el.text)}");' if ctx else f'i18n("{esc(el.text)}");'))
print('\n'.join(out))
