#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Orcinus contributors
# SPDX-License-Identifier: GPL-2.0-or-later

# Checks an installed release build of Orcinus as a new user (release plan RL6). Runs in a
# container or a test machine where Orcinus and Dolphin are installed; uses only the installed
# programs, no build directory and no development automation.
#
#   dbus-run-session -- ci/release-test.sh
#
# Needs: orcinus and dolphin in PATH, dbus-send, xdg-mime (xdg-utils), the offscreen Qt platform,
# the de_DE.UTF-8 locale and installed translations (container images often strip both).
# Works in a fresh temporary home directory, so it never touches the calling user's settings.
#
# Checks:
#   1. Orcinus starts with a folder, shows its file and quits cleanly (D-Bus: isUrlOpen,
#      isItemVisibleInAnyView, quit of org.kde.dolphin.MainWindow).
#   2. Dolphin's configuration, mimeapps.list and the default file manager are unchanged, and
#      Orcinus created its own configuration but no Dolphin-named file.
#   3. Dolphin itself still starts, shows the folder and quits.
#   4. Translations: under de_DE.UTF-8, Orcinus' own strings and strings from Dolphin's catalog are
#      German.

set -euo pipefail

[ -n "${DBUS_SESSION_BUS_ADDRESS:-}" ] || exec dbus-run-session -- "$0" "$@"

for tool in orcinus dolphin dbus-send xdg-mime; do
    command -v "${tool}" > /dev/null || { echo "release-test.sh: ${tool} is missing" >&2; exit 2; }
done

HOME="$(mktemp -d /tmp/orcinus-release-test.XXXXXX)"
XDG_RUNTIME_DIR="${HOME}/.runtime"
export HOME XDG_RUNTIME_DIR
unset XDG_CONFIG_HOME XDG_DATA_HOME XDG_STATE_HOME XDG_CACHE_HOME
mkdir -m 700 "${XDG_RUNTIME_DIR}"
export QT_QPA_PLATFORM=offscreen
export LANG=C.UTF-8 LC_ALL=C.UTF-8
unset LANGUAGE
logs="${HOME}/logs"
mkdir -p "${logs}"

failures=0
pass() { echo "PASS: $*"; }
fail() {
    echo "FAIL: $*"
    failures=$((failures + 1))
}

# Calls a method of org.kde.dolphin.MainWindow and prints the literal reply.
call() {
    local service="$1" path="$2" method="$3"
    shift 3
    dbus-send --session --print-reply=literal --reply-timeout=5000 --dest="${service}" "${path}" \
        "org.kde.dolphin.MainWindow.${method}" "$@" 2> /dev/null | tr -d ' \n'
}

# Starts a file manager on the test folder, waits until it shows the test file, quits it through
# D-Bus and checks that it exits cleanly. KDBusService registers "<app ID>-<pid>".
#   run_file_manager NAME PROGRAM SERVICE_PREFIX WINDOW_PATH
run_file_manager() {
    local name="$1" program="$2" prefix="$3" path="$4"
    local folder="file://${HOME}/testdir" item="file://${HOME}/testdir/release-test.txt"
    "${program}" "${HOME}/testdir" > "${logs}/${name}.log" 2>&1 &
    local pid=$! service="${prefix}-$!" shown=0 status=0 i
    for i in $(seq 1 60); do
        if [ "$(call "${service}" "${path}" isUrlOpen "string:${folder}")" = booleantrue ] \
            && [ "$(call "${service}" "${path}" isItemVisibleInAnyView "string:${item}")" = booleantrue ]; then
            shown=1
            break
        fi
        kill -0 "${pid}" 2> /dev/null || break
        sleep 0.5
    done
    if [ "${shown}" = 1 ]; then
        pass "${name} shows the test folder and its file"
    else
        fail "${name} does not show the test folder within 30 s"
    fi
    call "${service}" "${path}" quit > /dev/null || true
    for i in $(seq 1 40); do
        kill -0 "${pid}" 2> /dev/null || break
        sleep 0.25
    done
    if kill -0 "${pid}" 2> /dev/null; then
        kill -9 "${pid}"
        fail "${name} does not quit within 10 s"
    fi
    wait "${pid}" || status=$?
    if [ "${status}" = 0 ]; then
        pass "${name} quits with exit code 0"
    else
        fail "${name} exits with code ${status} (log: ${logs}/${name}.log)"
        tail -20 "${logs}/${name}.log"
    fi
}

# Checksums of everything Dolphin owns in the home directory, and of the MIME defaults.
dolphin_state() {
    (cd "${HOME}" && find .config .local/share .local/state -path '*dolphin*' -type f 2> /dev/null \
        | sort | xargs -r sha256sum)
    (cd "${HOME}" && sha256sum .config/mimeapps.list)
}

# --- Setup: an existing Dolphin user whose default file manager is Dolphin
mkdir -p "${HOME}/testdir" "${HOME}/.config" "${HOME}/.local/share" "${HOME}/.local/state"
echo "release test" > "${HOME}/testdir/release-test.txt"
printf '[General]\nReleaseTestMarker=unchanged\n' > "${HOME}/.config/dolphinrc"
printf '[Default Applications]\ninode/directory=org.kde.dolphin.desktop\n' > "${HOME}/.config/mimeapps.list"
default_before="$(xdg-mime query default inode/directory 2> /dev/null || true)"
state_before="$(dolphin_state)"
touch "${HOME}/.setup-done"

echo "=== Orcinus $(orcinus --version 2> /dev/null | sed 's/^orcinus //'), $(dolphin --version 2> /dev/null)"

# --- 1. First start of Orcinus
run_file_manager orcinus orcinus io.github.arkarit.orcinus /orcinus/Orcinus_1

# --- 2. Coexistence with Dolphin
if [ "$(dolphin_state)" = "${state_before}" ]; then
    pass "Dolphin's configuration and mimeapps.list are unchanged"
else
    fail "Dolphin's configuration or mimeapps.list changed"
    diff <(echo "${state_before}") <(dolphin_state) || true
fi
default_after="$(xdg-mime query default inode/directory 2> /dev/null || true)"
if [ "${default_after}" = "${default_before}" ]; then
    pass "default file manager unchanged (${default_after:-none})"
else
    fail "default file manager changed from '${default_before}' to '${default_after}'"
fi
own_files="$(cd "${HOME}" && find .config .local/state .local/share -name 'orcinus*' 2> /dev/null | sort | tr '\n' ' ')"
if [ -n "${own_files}" ]; then
    pass "Orcinus created its own configuration (${own_files% })"
else
    fail "Orcinus created no configuration of its own"
fi
dolphin_named="$(cd "${HOME}" && find .config .local/share .local/state -path '*dolphin*' -newer .setup-done 2> /dev/null || true)"
if [ -z "${dolphin_named}" ]; then
    pass "Orcinus created no Dolphin-named files"
else
    fail "Orcinus created Dolphin-named files: ${dolphin_named}"
fi

# --- 3. Dolphin still works
run_file_manager dolphin dolphin org.kde.dolphin /dolphin/Dolphin_1

# --- 4. Translations (minimized container images strip /usr/share/locale; the drivers undo that)
mo_file="$(dirname "$(command -v orcinus)")/../share/locale/de/LC_MESSAGES/orcinus.mo"
if [ -f "${mo_file}" ]; then
    pass "German catalog installed"
else
    fail "German catalog missing: ${mo_file} (installation without translations?)"
fi
# gettext ignores LANGUAGE under the C locale, so the check needs a German locale.
german_locale="$(locale -a 2> /dev/null | grep -i -m1 '^de_DE\.utf-\?8$' || true)"
if [ -z "${german_locale}" ]; then
    fail "no German locale (de_DE.UTF-8) in this environment, translations not checked"
fi
help_de="$(LC_ALL="${german_locale:-C.UTF-8}" LANGUAGE=de orcinus --help 2> /dev/null || true)"
if grep -qF "Orcinus wird mit geteilter Ansicht gestartet." <<< "${help_de}"; then
    pass "German translation of Orcinus' own strings"
else
    fail "Orcinus' own strings are not German with LANGUAGE=de"
fi
if grep -qF "Zu öffnendes Dokument" <<< "${help_de}"; then
    pass "German translation of strings from Dolphin's catalog"
else
    fail "strings from Dolphin's catalog are not German with LANGUAGE=de"
fi

echo "=== ${failures} failure(s)"
[ "${failures}" = 0 ]
