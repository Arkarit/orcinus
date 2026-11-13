# SPDX-FileCopyrightText: 2026 Orcinus contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

# The sandbox the AI panel starts agents in (AI5d): no Qt, Linux namespaces only.
# See .privatedoc/ai-support-plan.md.

add_executable(orcinus-sandbox orcinus-sandbox.cpp)
find_package(Threads REQUIRED)
target_link_libraries(orcinus-sandbox PRIVATE Threads::Threads)
install(TARGETS orcinus-sandbox DESTINATION ${WHALE_SANDBOX_DIR})

# Ubuntu lets a program create user namespaces with capabilities only with an AppArmor profile
# that allows it. The profile for the installed helper, and one for the build tree to load by
# hand while developing (sudo apparmor_parser -r <build>/orcinus-sandbox.apparmor).
set(WHALE_SANDBOX_PROFILE_NAME orcinus-sandbox)
set(WHALE_SANDBOX_PROFILE_PATH "${CMAKE_INSTALL_PREFIX}/${WHALE_SANDBOX_DIR}/orcinus-sandbox")
configure_file(orcinus-sandbox.apparmor.in ${CMAKE_CURRENT_BINARY_DIR}/installed/orcinus-sandbox @ONLY)
set(WHALE_SANDBOX_PROFILE_NAME orcinus-sandbox-build)
set(WHALE_SANDBOX_PROFILE_PATH "$<TARGET_FILE:orcinus-sandbox>")
configure_file(orcinus-sandbox.apparmor.in ${CMAKE_CURRENT_BINARY_DIR}/orcinus-sandbox.apparmor.gen @ONLY)
file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/orcinus-sandbox.apparmor INPUT ${CMAKE_CURRENT_BINARY_DIR}/orcinus-sandbox.apparmor.gen)

option(WHALE_INSTALL_APPARMOR_PROFILE "Install the AppArmor profile of orcinus-sandbox (needed on Ubuntu for the AI sandbox)" OFF)
if(WHALE_INSTALL_APPARMOR_PROFILE)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/installed/orcinus-sandbox DESTINATION ${WHALE_APPARMOR_DIR})
endif()
