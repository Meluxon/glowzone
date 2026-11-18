# Security model

Glowzone controls two kinds of lights in two different ways.

| Lights | How | Root? |
|---|---|---|
| Fan LEDs | ITE 8297 USB controller, HID feature reports | no – udev `uaccess` |
| Case lights | the BIOS (GameZone WMI `SetLED`) through `acpi_call` | a small KAuth helper |

## Fans

The controller is opened as your user. The udev rule (`70-glowzone.rules`) tags
only that one USB device `uaccess`, so whoever owns the active local session can
use it – and so can any program running as that user. That is the same trust
OpenRGB's rules give; it only reaches the fan LEDs.

## Case lights: the root helper

A small helper, started by D-Bus only when called, authorised by polkit through
KAuth. It offers three actions – read the case lights, set them, and read the
BIOS fan layout – and on every call it:

1. checks the PC model itself (DMI) instead of trusting the app;
2. checks the request: integers only, a light index, mode and brightness that
   the model profile allows, each light at most once;
3. only then loads `acpi_call` if needed and asks the BIOS whether the lights
   are fitted.

It builds every ACPI call itself and only ever calls four GameZone methods:
`0x0A` (fan layout), `0x0E` (lights fitted?), `0x0F` (GetLED) and `0x10`
(SetLED). `modprobe` is run with a fixed path, a fixed argument and an empty
environment.

How that is checked:

- unit tests run the helper's logic against a fake `acpi_call`, including
  requests with the wrong types, out-of-range values and repeated lights;
- a fuzzer feeds it random requests for every supported model and stops at the
  first ACPI call outside what that model allows (see
  [development.md](development.md)).

KAuth's helper serves one call at a time and doesn't queue overlapping ones, so
Glowzone's own calls – window and command line – take turns through a lock
file, and each call has a time limit.

## Permissions

All three actions are allowed without a password for the active local session
(the same trust as for the fans) and denied otherwise. A program in your
session could therefore keep changing the case lights; each change is a BIOS
call that can stall the PC for a moment, but it gives no further access.

To require your password instead, set `Policy=auth_self` and
`Persistence=session` in `data/io.github.meluxon.glowzone.ledhelper.actions`
and rebuild.

## Running in the background

With *Keep Running in Background* on, Glowzone stays in the system tray after
the window is closed, so fans following the CPU temperature keep changing
colour. That process is the normal program, as your user: it reads the CPU
temperature from hwmon (readable by everyone) and talks to the fan controller
like the window does. It doesn't use the root helper – the case lights are
only read and set while a window is open (a change made just before closing
it is still sent). *Start at Login* adds
`~/.config/autostart/io.github.meluxon.Glowzone.desktop`, which starts the
same program with `--background`; nothing system-wide is installed for it.

## Model lock

Glowzone only runs on models it has a profile for – vendor, product version and
board name must all match (`src/model/modelprofile.cpp`). The helper repeats
that check on its own. There is no override.

## Reporting a problem

Open an issue at <https://github.com/Meluxon/glowzone/issues>. For something you
would rather not post publicly, use GitHub's private vulnerability reporting
on the repository (Security → Report a vulnerability), if it is enabled.
