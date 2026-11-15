#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Orcinus contributors
# SPDX-License-Identifier: GPL-2.0-or-later

# Testing cheat: forgets that the AI disclaimer was accepted (AI5f), so the AI panel and the
# "AI Agents" settings page show it again. Works for every build (installed or development):
# they share orcinusrc.
#
# Orcinus keeps its settings in memory while it runs, so close it first.

set -euo pipefail

if pgrep -x orcinus > /dev/null; then
    echo "Orcinus is running. Close it first: it would not notice the change until restarted." >&2
    exit 1
fi

kwriteconfig6 --file orcinusrc --group AI --key DisclaimerAccepted --delete
echo "AI disclaimer reset: it shows again the next time the AI panel or its settings page opens."
