#!/bin/sh
# SPDX-FileCopyrightText: 2026 Meluxon
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Build and install Glowzone with CMake into /usr.
# On Arch Linux, use the package instead (cd packaging/arch && makepkg -si) –
# pacman then tracks every file. This script is for other distributions.
#
# Run as your normal user; it asks for sudo only to install.

set -eu
cd "$(dirname "$0")/.."

manifest_copy=/usr/share/glowzone/install_manifest.txt

if [ "$(id -u)" -eq 0 ]; then
    echo "Run this as your normal user, not root (sudo is used where needed)." >&2
    exit 1
fi

if command -v pacman >/dev/null 2>&1 && [ "${1:-}" != "--no-package" ]; then
    echo "Arch Linux detected. Install Glowzone as a package instead:"
    echo "    cd packaging/arch && makepkg -si"
    echo "(To install with CMake anyway: $0 --no-package)"
    exit 1
fi

# /usr only: the D-Bus activation file must point at the KAuth helper
# directory of the system's KDE Frameworks, and polkit/D-Bus read /usr/share.
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure

sudo cmake --install build
# Keep the list of installed files so scripts/uninstall.sh works without the build directory.
sudo install -Dm644 build/install_manifest.txt "$manifest_copy"

echo "Reloading udev rules and the D-Bus configuration..."
sudo udevadm control --reload
sudo udevadm trigger --subsystem-match=hidraw
if command -v busctl >/dev/null 2>&1; then
    sudo busctl call org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus ReloadConfig
else
    sudo dbus-send --system --type=method_call --dest=org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus.ReloadConfig
fi

# Refresh the icon theme cache if this system keeps one (GTK apps and some menus read it).
if [ -e /usr/share/icons/hicolor/icon-theme.cache ] && command -v gtk-update-icon-cache >/dev/null 2>&1; then
    sudo gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor
fi

echo
echo "Glowzone is installed. Try: glowzone status"
if [ ! -e /proc/acpi/call ] && ! modinfo acpi_call >/dev/null 2>&1; then
    echo "Note: the case lights need the acpi_call kernel module."
fi
