# Development

## Building and tests

The requirements are listed in the [README](../README.md#install).

```sh
cmake -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build
```

The tests need no hardware: they check the fan reports against bytes captured
from Lenovo Vantage, the root helper's checks against a fake `acpi_call`, the
case-light queue against a fake helper, the model profiles, profiles and the
saved fan state.

`./scripts/install.sh` does the same and installs into `/usr` with sudo; it
keeps the install manifest in `/usr/share/glowzone/` for
`./scripts/uninstall.sh`.

## Fuzzing the root helper

```sh
cmake -B build-fuzz -DCMAKE_CXX_COMPILER=clang++ -DGLOWZONE_FUZZ=ON
cmake --build build-fuzz --target fuzz_setled
build-fuzz/bin/fuzz_setled -max_total_time=300
```

It feeds random requests to the helper for every supported model and stops at
the first ACPI call outside what that model allows.

## Source layout

| Directory | What |
|---|---|
| `src/model` | DMI and the model profiles – the only place a model's details live |
| `src/helper` | the root helper (`EcService` holds its checks) |
| `src/core` | fan reports, the fan controller, helper calls, the light and fan queues, profiles, saved state |
| `src/cli`, `src/gui` | the command line and the window |
| `autotests`, `fuzz` | tests and the fuzzer |

## Adding a model

Capture Lenovo Vantage on Windows (USBPcap + Wireshark), map every fan to its
ITE 8297 header and every mode Vantage offers, check the GameZone WMI methods
in the DSDT, and add a profile to `src/model/modelprofile.cpp` – fans, modes,
levels and the case lights with their colours – plus its USB ID in
`data/70-glowzone.rules`. The tests check that a profile is consistent and that
the udev rule covers it. Please include the captures in the pull request.

## Translations

English is the source language; Glowzone follows the system language.

| Language | Status |
|---|---|
| Polish | complete, by the author (mode names follow Lenovo Vantage) |
| German | complete; AI draft, partly reviewed by the author |
| French | complete; AI draft, **not yet reviewed** by a French speaker |
| Japanese | complete; AI draft, **not yet reviewed** by a Japanese speaker |

Corrections are very welcome – an issue or a pull request against
`po/<lang>/glowzone.po` is enough.

```sh
./scripts/update-translations.sh   # refresh po/glowzone.pot, merge into po/*/glowzone.po
LANGUAGE=de glowzone               # try a language without changing your session
```

A new language is a new `po/<lang>/glowzone.po` (`msginit -i po/glowzone.pot`).
The polkit texts are in `data/io.github.meluxon.glowzone.ledhelper.actions`,
the menu entry's in `data/io.github.meluxon.Glowzone.desktop`.

## Installed files

| Path | Purpose |
|---|---|
| `/usr/bin/glowzone` | the program (window and CLI) |
| `/usr/share/applications/io.github.meluxon.Glowzone.desktop` | menu entry |
| `/usr/share/icons/hicolor/{scalable,16x16,22x22}/apps/io.github.meluxon.Glowzone.svg` | icon |
| `<KAuth helper dir>/glowzone_helper` (Arch: `/usr/lib/kf6/kauth/`) | root helper |
| `/usr/share/dbus-1/system.d/io.github.meluxon.glowzone.ledhelper.conf` | D-Bus policy |
| `/usr/share/dbus-1/system-services/io.github.meluxon.glowzone.ledhelper.service` | starts the helper on demand |
| `/usr/share/polkit-1/actions/io.github.meluxon.glowzone.ledhelper.policy` | polkit actions |
| `/usr/lib/udev/rules.d/70-glowzone.rules` | fan controller access for the logged-in user |
| `/usr/share/locale/*/LC_MESSAGES/glowzone.mo` | translations |

Your own files: `~/.config/glowzone/` (settings, profiles) and
`~/.local/state/glowzone/` (the last fan state). `glowzone --purge-user-data`
removes both.

The reverse-DNS IDs (`io.github.meluxon.*`) are set in `CMakeLists.txt`.
