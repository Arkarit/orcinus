#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Orcinus contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# OR7 of the Orcinus rename (.privatedoc/orcinus-rename-plan.md): moves one user's Whale
# configuration over to Orcinus. Run it once, with Whale and Orcinus closed. Not shipped (N8).
#
# Without arguments it only prints what it would do. With --apply it copies; the Whale files stay
# untouched, so going back to Whale keeps working. Files that already exist for Orcinus are never
# overwritten. The only file changed in place is mimeapps.list, after a backup next to it.
#
# Usage: orcinus-migrate-user.sh [--apply]

set -euo pipefail

apply=0
case "${1:-}" in
    --apply) apply=1 ;;
    "") ;;
    *) echo "Usage: $0 [--apply]" >&2; exit 2 ;;
esac

config="${XDG_CONFIG_HOME:-$HOME/.config}"
data="${XDG_DATA_HOME:-$HOME/.local/share}"
state="${XDG_STATE_HOME:-$HOME/.local/state}"

if pgrep -x whale > /dev/null || pgrep -x orcinus > /dev/null; then
    echo "Whale or Orcinus is running. Close it first, it would overwrite the copies on exit." >&2
    exit 1
fi

run() {
    echo "  $*"
    if [[ ${apply} -eq 1 ]]; then
        "$@"
    fi
}

# Copies a file or a directory, unless the target exists already.
copy() {
    local from="$1" to="$2"
    if [[ ! -e "${from}" ]]; then
        echo "- ${from}: not there, skipped"
    elif [[ -e "${to}" ]]; then
        echo "- ${to}: exists already, left alone"
    else
        echo "- copy ${from} -> ${to}"
        run cp -a "${from}" "${to}"
        copied+=("${to}")
    fi
}

copied=()
[[ ${apply} -eq 1 ]] || echo "Dry run, nothing is changed. Run with --apply to migrate."
echo
echo "Configuration and state:"
copy "${config}/whalerc" "${config}/orcinusrc"
copy "${config}/whalestylesrc" "${config}/orcinusstylesrc"
copy "${state}/whalestaterc" "${state}/orcinusstaterc"
copy "${state}/whaleviewpreferencesrc" "${state}/orcinusviewpreferencesrc"
copy "${data}/whale" "${data}/orcinus"
copy "${data}/kxmlgui5/whale" "${data}/kxmlgui5/orcinus"

# The treemap plugin is called orcinus_treemap now (OR3); styles and view preferences store its ID.
echo
echo "Plugin IDs:"
for file in "${copied[@]}"; do
    case "${file}" in
        "${config}/orcinusstylesrc" | "${state}/orcinusviewpreferencesrc")
            if [[ ${apply} -eq 0 ]] || grep -q 'whale_treemap' "${file}"; then
                echo "- whale_treemap -> orcinus_treemap in ${file}"
                run sed -i 's/\bwhale_treemap\b/orcinus_treemap/g' "${file}"
            fi
            ;;
    esac
done

echo
echo "Default file manager:"
for list in "${config}/mimeapps.list" "${data}/applications/mimeapps.list"; do
    if [[ -f "${list}" ]] && grep -q 'io\.github\.arkarit\.whale\.desktop' "${list}"; then
        echo "- ${list}:"
        grep -n 'io\.github\.arkarit\.whale\.desktop' "${list}" | sed 's/^/    /'
        run cp -a "${list}" "${list}.before-orcinus"
        run sed -i 's/io\.github\.arkarit\.whale\.desktop/io.github.arkarit.orcinus.desktop/g' "${list}"
    fi
done

# Plasma rewrites these files itself while it runs, so they are only reported.
echo
echo "To change by hand (Plasma panels and shortcuts):"
found=0
for file in "${config}/plasma-org.kde.plasma.desktop-appletsrc" "${config}/kglobalshortcutsrc" "${config}/khotkeysrc"; do
    if [[ -f "${file}" ]] && grep -q 'io\.github\.arkarit\.whale' "${file}"; then
        grep -n 'io\.github\.arkarit\.whale' "${file}" | sed "s|^|    ${file}:|"
        found=1
    fi
done
[[ ${found} -eq 1 ]] || echo "    nothing found"

echo
echo "Not migrated: the session files of Whale (${config}/session/whale_*), its cache"
echo "(${XDG_CACHE_HOME:-$HOME/.cache}/whale) and any other whale* file; delete them once Orcinus works."
