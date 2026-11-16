# SPDX-FileCopyrightText: 2026 Meluxon
# SPDX-License-Identifier: GPL-2.0-or-later

cmake_minimum_required(VERSION 3.22)
project(glowzone VERSION 0.9.0 LANGUAGES CXX)

set(QT_MIN_VERSION "6.7.0")   # 6.7: QStandardPaths::StateLocation
set(KF_MIN_VERSION "6.0.0")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)          # also provides the "uninstall" target
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMAddTests)
include(FeatureSummary)

find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS Core DBus Gui Widgets)
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS Auth CoreAddons DBusAddons I18n StatusNotifierItem WidgetsAddons)
find_package(PkgConfig REQUIRED)
pkg_check_modules(HIDAPI REQUIRED IMPORTED_TARGET hidapi-hidraw)

add_definitions(-DTRANSLATION_DOMAIN=\"glowzone\")

# Reverse-DNS identifiers, from the GitHub home (github.com/Meluxon/glowzone).
# They name installed files and the helper's D-Bus service – keep them stable.
set(GLOWZONE_APP_ID "io.github.meluxon.Glowzone")
set(GLOWZONE_HELPER_ID "io.github.meluxon.glowzone.ledhelper")

configure_file(config-glowzone.h.in ${CMAKE_CURRENT_BINARY_DIR}/config-glowzone.h)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_subdirectory(src)
add_subdirectory(data)

ki18n_install(po)

option(GLOWZONE_FUZZ "Build the fuzzer for the root helper (needs clang)" OFF)
if(GLOWZONE_FUZZ)
    add_subdirectory(fuzz)
endif()

if(BUILD_TESTING)
    find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS Test)
    add_subdirectory(autotests)
endif()

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
