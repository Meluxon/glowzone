# Glowzone

Lighting control for supported Lenovo desktops on Linux: the fan LEDs and the
case lights (front logo, internal light) – what Lenovo Vantage does on Windows.

## Supported models

| Model | DMI vendor / product version / board | Fans | Case lights |
|---|---|---|---|
| Legion T5 26IOB6 | `LENOVO` / `Legion T5 26IOB6` / `3733` | CPU, rear, front ×2, top ×2 | front logo, internal light |

Other models need their own profile – see *Adding a model*.

> **Vibecoded.** This project was written with an AI assistant (Claude) and
> reviewed by me, for my own machine. It works for me; read the code before you
> trust it with yours. Bug reports and captures from other models are welcome.

**Status:** 0.5 – window, command-line tool, profiles and a BIOS fan-detection check.

## What it controls

| Light | How | Root? |
|---|---|---|
| CPU, rear, front ×2, top ×2 fan LEDs | ITE 8297 USB controller (`17EF:C955`), HID feature reports | no – udev `uaccess` |
| Case lights (front logo, internal light) | EC, through GameZone WMI `SetLED` via `acpi_call` | small KAuth helper |

Glowzone **only runs on supported models**. It checks DMI vendor, product
version and board name against the profiles above and refuses to do anything
otherwise; the root helper repeats that check on its own and also
asks the BIOS whether the lights exist before touching them. There is no
`--force`: header wiring differs between models, so a new model needs its own
profile (see *Adding a model*).

## Install

### Arch Linux (recommended)

```sh
cd packaging/arch
makepkg -si
```

For the case lights also install `acpi_call-dkms` (or
`acpi_call` / `acpi_call-lts` to match your kernel). Glowzone loads the module
itself when it needs it – nothing is set up at boot.

### Other distributions

Needs Qt ≥ 6.7, KDE Frameworks 6 (KAuth with the polkit backend, KCoreAddons,
KI18n, KWidgetsAddons ≥ 6.0), extra-cmake-modules, hidapi (hidraw), polkit and a C++20 compiler.

```sh
./scripts/install.sh
```

It builds, runs the tests, installs into `/usr` with sudo, keeps a copy of the
install manifest in `/usr/share/glowzone/` and reloads udev and D-Bus.

## The window

Run `glowzone` without arguments, or start *Glowzone* from the application menu.
The layout follows System Settings and Discover:

* **Sidebar** – the overview: every fan and case light with a dot in its current
  colour (rainbow for colour-cycling modes, hollow when off) and its mode.
  Below them, your profiles.
* **Page** – the settings of the selected fan or light: mode, colour, speed,
  brightness. Rows the mode doesn't use are hidden (no colour for Rainbow wave,
  no speed for Static). "Not set yet" means Glowzone hasn't set that fan.
* **Double fans** (front, top) have **LEDs: Set separately**. Checked,
  the sidebar entry opens into one entry per LED (e.g. *Front* / *Rear*) and the
  page shows both. Unchecking it copies the first LED’s settings to the other.
* **Profiles** – click one to apply it (the ✓ marks the one applied last).
  Right-click to rename or delete. *Save Current Lighting…* creates a new one.
  When you change something after applying a profile, it reads
  "Name · changed" and a message at the bottom offers **Save** or
  **Save as New…** – or click the profile again to go back to it.
* **Messages** (unsaved changes, warnings, errors) appear at the bottom of the
  page, below the settings, so no control ever moves under your cursor.
* **Turn All Off** (top right) switches everything off but keeps colours and
  levels, so switching a mode back on restores them.
* Selection and hover colours come from your colour scheme.
* **☰ → About Glowzone** shows the detected computer, whether the fan controller
  and case lights are available, and *Copy to Clipboard* for bug reports.
* Changes apply immediately. The window size, the Linked/Split choices and the
  last profile are kept in `~/.config/glowzone/glowzone.conf`.

## BIOS fan detection

The BIOS reports a "fan layout" (GameZone `IsSupportLightingFeature`) that it
derives from the fans' speed signals – a fan that isn't spinning at that moment
counts as missing. After a cold start this occasionally drops a fan, and Lenovo
Vantage then resets your lighting. Glowzone only *reports* it: the window shows
a warning naming the fan(s), the sidebar marks them "not detected", and
`glowzone status` prints the result. Nothing is ever changed because of it –
profiles stay as they are, and the full profile is still sent to every LED.

## Profiles

A profile stores every fan LED, the Linked / Split state of the double fans
and the case lights. Each profile is one JSON file in
`~/.config/glowzone/profiles/`, easy to back up or copy to another machine of
the same model. Names are case-insensitive.

```sh
glowzone profile save "Night"     # the current lighting
glowzone profile list             # * marks the profile applied last
glowzone profile apply night
glowzone profile rename night "Late night"
glowzone profile delete "late night"
```

`glowzone profile apply <name>` works well as a Plasma global shortcut
(System Settings → Keyboard → Shortcuts → Add New → Command or Script).

## Command line

```sh
glowzone status
glowzone fan all rainbow -s 1 -b 2
glowzone fan top static -c ff3d9c
glowzone fan top static -c 0000ff --led rear    # one LED of a double fan
glowzone fan rear off
glowzone light logo on -b 1
glowzone light ambient breathe
glowzone light all off
glowzone reset                  # everything off
```

Double fans have named LEDs (`--led`, or `1` / `2`); `glowzone --help` lists
the fans, LEDs, lights and modes of your machine. On the Legion T5 26IOB6: front
fans `upper` / `lower`, top fans `front` / `rear`.

Fan modes: `off`, `static`, `sparkle`, `breathing`, `wave`, `specter`,
`rainbow`, `random` (all confirmed on the hardware).
Light modes: `off`, `on`, `blink-fast`, `blink-slow`, `breathe`.

The fan controller can't be read back, so `status` shows what Glowzone last
sent (`~/.local/state/glowzone/fans.json`). The case lights are read
from the EC.

## Uninstall (full)

```sh
./scripts/uninstall.sh          # asks before each step; -y answers yes to all
```

1. offers `glowzone reset`, because the lights otherwise keep their last setting;
2. offers `glowzone --purge-user-data` (`~/.config/glowzone` including profiles,
   and `~/.local/state/glowzone`);
3. removes the program – `pacman -Rns glowzone` for the Arch package, or every
   file in the saved install manifest otherwise;
4. reloads udev and D-Bus and checks that no Glowzone file is left.

With pacman alone (`sudo pacman -Rns glowzone`) steps 1 and 2 are up to you –
packages never touch home directories. The `acpi_call` module stays loaded
until reboot (`sudo rmmod acpi_call`).

## Installed files

| Path | Purpose |
|---|---|
| `/usr/bin/glowzone` | the program (window and CLI) |
| `/usr/share/applications/org.glowzone.Glowzone.desktop` | menu entry |
| `<KAuth helper dir>/glowzone_helper` (Arch: `/usr/lib/kf6/kauth/`) | root helper |
| `/usr/share/dbus-1/system.d/org.glowzone.ledhelper.conf` | D-Bus policy |
| `/usr/share/dbus-1/system-services/org.glowzone.ledhelper.service` | starts the helper on demand |
| `/usr/share/polkit-1/actions/org.glowzone.ledhelper.policy` | polkit actions |
| `/usr/lib/udev/rules.d/70-glowzone.rules` | fan controller access for the logged-in user |
| `/usr/share/locale/*/LC_MESSAGES/glowzone.mo` | translations |

## Security model

* The fan controller is opened as your user. The udev rule tags only that one
  USB device `uaccess`, so whoever owns the active local session can use it.
* The helper runs as root only when called (D-Bus activation) and offers two
  actions: read the two lights, and set them. It accepts nothing but a light
  index (1–2), a mode (1–5) and a brightness (1–4); it builds the ACPI call
  itself and only ever calls GameZone methods `0x0E`, `0x0F` and `0x10`.
* Both actions are allowed without a password for the active local session
  (the same trust as for the fans) and denied otherwise. To require your
  password instead, set `Policy=auth_self` and `Persistence=session` in
  `data/org.glowzone.ledhelper.actions` and rebuild.

## Translations

English is the source language. Glowzone follows the system / Plasma language.

| Language | Status |
|----------|--------|
| Polish   | Complete, by the author (mode names follow Lenovo Vantage) |
| German   | Complete; AI draft, partly reviewed by the author |
| French   | Complete; AI draft, **not yet reviewed** by a French speaker |
| Japanese | Complete; AI draft, **not yet reviewed** by a Japanese speaker |

Corrections from native speakers are very welcome – an issue or a pull
request against `po/<lang>/glowzone.po` is enough.

```sh
./scripts/update-translations.sh   # refresh po/glowzone.pot and merge into po/*/glowzone.po
```

Translate `po/<lang>/glowzone.po` with Lokalize or Poedit. A new language is
just a new `po/<lang>/glowzone.po` (start with `msginit -i po/glowzone.pot`).
The polkit texts live in `data/org.glowzone.ledhelper.actions` (`Name[xx]=`),
the menu entry texts in `data/org.glowzone.Glowzone.desktop` (`Comment[xx]=` etc.).

To try another language without changing your session:

```sh
LANGUAGE=de glowzone        # or fr, ja, pl – the locale itself must exist (locale -a)
```

## Adding a model

Capture Lenovo Vantage on Windows (USBPcap + Wireshark), map every fan to its
ITE 8297 header, check the GameZone WMI methods in the DSDT, and add a profile
to `src/model/modelprofile.cpp`. Please include the captures in the merge request.

## Development

```sh
cmake -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build
```

The tests check report bytes against Vantage captures and the helper's checks
against a fake `acpi_call`; they need no hardware.

The reverse-DNS IDs (`org.glowzone.*`) are placeholders until the project has a
public home; they are set in `CMakeLists.txt`.

## License

GPL-2.0-or-later. See `LICENSES/`.
