#!/bin/sh
# SPDX-FileCopyrightText: 2026 Meluxon
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Extract translatable strings into po/glowzone.pot and merge them into every
# po/<lang>/glowzone.po. Run from anywhere; needs GNU gettext.
# Translate with Lokalize or Poedit, then rebuild.

set -eu
cd "$(dirname "$0")/.."

# shellcheck disable=SC2046 # word splitting of the file list is intended
xgettext --from-code=UTF-8 --language=C++ --kde \
    --package-name=glowzone --msgid-bugs-address= \
    -ci18n \
    -ki18n:1 -ki18nc:1c,2 -ki18np:1,2 -ki18ncp:1c,2,3 \
    -kki18n:1 -kki18nc:1c,2 -kki18np:1,2 -kki18ncp:1c,2,3 \
    -kkli18n:1 -kkli18nc:1c,2 -kkli18np:1,2 -kkli18ncp:1c,2,3 \
    -kxi18n:1 -kxi18nc:1c,2 -kxi18np:1,2 -kxi18ncp:1c,2,3 \
    -kkxi18n:1 -kkxi18nc:1c,2 -kkxi18np:1,2 -kkxi18ncp:1c,2,3 \
    -kklxi18n:1 -kklxi18nc:1c,2 -kklxi18np:1,2 -kklxi18ncp:1c,2,3 \
    -o po/glowzone.pot \
    $(find src -name '*.cpp' -o -name '*.h' | sort)

for po in po/*/glowzone.po; do
    [ -e "$po" ] || continue
    msgmerge --quiet --update --backup=none "$po" po/glowzone.pot
    printf '%s: ' "$po"
    msgfmt --check --statistics -o /dev/null "$po"
done
