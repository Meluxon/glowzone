# SPDX-FileCopyrightText: 2026 Meluxon
# SPDX-License-Identifier: GPL-2.0-or-later

# Where udev looks for vendor rules: ask udev itself (udev.pc), else the usual place.
pkg_get_variable(_udevdir udev udevdir)
if(_udevdir)
    set(_udev_rules_default "${_udevdir}/rules.d")
else()
    set(_udev_rules_default "lib/udev/rules.d")
endif()
set(GLOWZONE_UDEV_RULES_DIR "${_udev_rules_default}" CACHE PATH "udev rules directory")

# Lets the logged-in user open the fan LED controller (hidraw) without root.
install(FILES 70-glowzone.rules DESTINATION ${GLOWZONE_UDEV_RULES_DIR})

# Menu entry for the window
install(FILES ${GLOWZONE_APP_ID}.desktop DESTINATION ${KDE_INSTALL_APPDIR})

# App icon, installed under the app ID (the name the desktop file and the window ask for).
# The 16 and 22 px versions are hand-tuned to the pixel grid; larger sizes scale the SVG.
install(FILES icons/glowzone.svg DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/scalable/apps RENAME ${GLOWZONE_APP_ID}.svg)
install(FILES icons/glowzone-22.svg DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/22x22/apps RENAME ${GLOWZONE_APP_ID}.svg)
install(FILES icons/glowzone-16.svg DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/16x16/apps RENAME ${GLOWZONE_APP_ID}.svg)
