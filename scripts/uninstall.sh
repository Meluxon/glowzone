#!/bin/sh
# SPDX-FileCopyrightText: 2026 Meluxon
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Full uninstall of Glowzone, however it was installed:
#   1. optionally turn all lighting off (glowzone reset)
#   2. optionally delete your own data (glowzone --purge-user-data)
#   3. remove the installed files – via pacman for the Arch package, or via
#      the saved CMake install manifest
#   4. reload udev and D-Bus, then check that nothing is left behind
#
# Run as your normal user (step 2 works on your home directory); sudo is used
# for step 3. Options: -y  answer yes to every question.

set -eu

manifest=/usr/share/glowzone/install_manifest.txt
assume_yes=no
[ "${1:-}" = "-y" ] && assume_yes=yes

if [ "$(id -u)" -eq 0 ]; then
    echo "Run this as your normal user, not root – otherwise step 2 would look in /root." >&2
    exit 1
fi

ask() {
    if [ "$assume_yes" = yes ]; then
        return 0
    fi
    printf '%s [y/N] ' "$1"
    read -r answer || answer=
    case "$answer" in
        y | Y | yes | Yes | t | T | tak | Tak) return 0 ;;
        *) return 1 ;;
    esac
}

# --- 1 + 2: things only the installed program can do -------------------------
if command -v glowzone >/dev/null 2>&1; then
    if ask "Turn all lighting off before uninstalling (glowzone reset)?"; then
        glowzone reset || echo "Reset did not fully succeed – continuing."
    fi
    if ask "Delete your Glowzone settings, profiles and saved state?"; then
        glowzone --purge-user-data
    fi
else
    echo "glowzone is not in PATH – skipping reset and user-data cleanup."
fi

# --- 3: remove installed files -------------------------------------------------
removed_by=none
if command -v pacman >/dev/null 2>&1 && pacman -Qq glowzone >/dev/null 2>&1; then
    echo "Removing the glowzone package with pacman..."
    sudo pacman -Rns glowzone
    removed_by=pacman
elif [ -f "$manifest" ]; then
    echo "Removing the files listed in $manifest..."
    while IFS= read -r path || [ -n "$path" ]; do  # last line has no newline
        [ -n "$path" ] || continue
        case "$path" in
            /usr/*) ;;
            *) echo "  skipping unexpected path: $path"; continue ;;
        esac
        if [ -e "$path" ] || [ -L "$path" ]; then
            sudo rm -f -- "$path"
            echo "  removed $path"
        fi
    done < "$manifest"
    sudo rm -f -- "$manifest"
    sudo rmdir --ignore-fail-on-non-empty /usr/share/glowzone 2>/dev/null || true
    removed_by=manifest

    echo "Reloading udev rules and the D-Bus configuration..."
    sudo udevadm control --reload
    if command -v busctl >/dev/null 2>&1; then
        sudo busctl call org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus ReloadConfig >/dev/null
    else
        sudo dbus-send --system --type=method_call --dest=org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus.ReloadConfig
    fi

    # Refresh the icon theme cache if this system keeps one (GTK apps and some menus read it).
    if [ -e /usr/share/icons/hicolor/icon-theme.cache ] && command -v gtk-update-icon-cache >/dev/null 2>&1; then
        sudo gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor
    fi
else
    echo "No glowzone package and no install manifest found."
fi

# --- 4: check for leftovers ----------------------------------------------------
echo "Checking for leftover files..."
leftovers=$(
    {
        for f in /usr/bin/glowzone \
            /usr/share/dbus-1/system.d/org.glowzone.ledhelper.conf \
            /usr/share/dbus-1/system-services/org.glowzone.ledhelper.service \
            /usr/share/polkit-1/actions/org.glowzone.ledhelper.policy \
            /usr/share/applications/org.glowzone.Glowzone.desktop \
            /usr/lib/udev/rules.d/70-glowzone.rules \
            /lib/udev/rules.d/70-glowzone.rules \
            /usr/share/glowzone \
            /usr/share/licenses/glowzone; do
            if [ -e "$f" ]; then echo "$f"; fi
        done
        find /usr/lib /usr/libexec -name glowzone_helper 2>/dev/null
        find /usr/share/locale -path '*/LC_MESSAGES/glowzone.mo' 2>/dev/null
        find /usr/share/icons -name 'org.glowzone.Glowzone.*' 2>/dev/null
    } | sort -u
)
if [ -n "$leftovers" ]; then
    echo "These Glowzone files are still installed:"
    echo "$leftovers" | sed 's/^/  /'
    exit 1
fi
echo "No system files left."

for d in "${XDG_CONFIG_HOME:-$HOME/.config}/glowzone" "${XDG_STATE_HOME:-$HOME/.local/state}/glowzone"; do
    if [ -e "$d" ]; then
        echo "Kept your data: $d"
    fi
done

if [ "$removed_by" != none ]; then
    echo "Glowzone is uninstalled."
fi
if [ -e /proc/acpi/call ]; then
    echo "The acpi_call module stays loaded until reboot (or: sudo rmmod acpi_call)."
fi
