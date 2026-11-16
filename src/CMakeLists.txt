# SPDX-FileCopyrightText: 2026 Meluxon
# SPDX-License-Identifier: GPL-2.0-or-later

# --- Model detection: shared by the app and the root helper (no hidapi, no KAuth)
add_library(glowzone_model STATIC
    model/dmi.cpp
    model/modelprofile.cpp
    model/helperprotocol.cpp
)
target_include_directories(glowzone_model PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(glowzone_model PUBLIC Qt6::Core KF6::I18n)

# --- EC light logic run by the helper (kept separate so it can be unit-tested)
add_library(glowzone_ecservice STATIC
    helper/ecservice.cpp
    helper/procacpicall.cpp
)
target_link_libraries(glowzone_ecservice PUBLIC glowzone_model)

# --- User-side core: fan LEDs over USB, EC lights through the helper, saved state
add_library(glowzone_core STATIC
    core/ite8297.cpp
    core/fancontroller.cpp
    core/fanstate.cpp
    core/eclights.cpp
    core/messages.cpp
    core/settings.cpp
    core/profiles.cpp
    core/helpercall.cpp
    core/appliers.cpp
    core/cputemperature.cpp
    core/instance.cpp
)
target_link_libraries(glowzone_core PUBLIC
    glowzone_model
    Qt6::DBus
    Qt6::Gui
    KF6::AuthCore
    KF6::CoreAddons
    KF6::I18n
    PkgConfig::HIDAPI
)

# --- glowzone: the window without arguments, the CLI with them
add_executable(glowzone
    cli/main.cpp
    cli/commands.cpp
    gui/gui.cpp
    gui/app.cpp
    gui/mainwindow.cpp
    gui/sidebar.cpp
    gui/pages.cpp
    gui/forms.cpp
    gui/temperaturerange.cpp
    gui/lighting.cpp
    gui/aboutdialog.cpp
    gui/colourbutton.cpp
    gui/glowzone.qrc
)
target_link_libraries(glowzone PRIVATE glowzone_core Qt6::Widgets KF6::WidgetsAddons KF6::DBusAddons KF6::StatusNotifierItem)
install(TARGETS glowzone ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

# --- Root helper for the case lights (KAuth, D-Bus activated)
add_executable(glowzone_helper helper/ledhelper.cpp)
target_link_libraries(glowzone_helper PRIVATE glowzone_ecservice KF6::AuthCore)
install(TARGETS glowzone_helper DESTINATION ${KAUTH_HELPER_INSTALL_DIR})
kauth_install_helper_files(glowzone_helper ${GLOWZONE_HELPER_ID} root)
kauth_install_actions(${GLOWZONE_HELPER_ID} ${PROJECT_SOURCE_DIR}/data/${GLOWZONE_HELPER_ID}.actions)
