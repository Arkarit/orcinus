# SPDX-FileCopyrightText: 2026 Orcinus contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

# The back channel of the AI agents (AI6): a relay between the agent's stdio and the panel.
# No Qt. See .privatedoc/ai-support-plan.md.

add_executable(orcinus-mcp orcinus-mcp.cpp)
install(TARGETS orcinus-mcp DESTINATION ${WHALE_SANDBOX_DIR})
