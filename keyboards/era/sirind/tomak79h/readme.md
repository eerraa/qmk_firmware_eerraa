# Tomak79H, Hotswap ver

Ergonomics Split Keyboard powered by RP2040.

* Keyboard Maintainer: [ERA](https://github.com/eerraa)
* Hardware supported: SIRIND Tomak79
* Hardware availability: [Syryan](https://srind.mysoho.com/)

Make example for this keyboard (after setting up your build environment):

    make era/sirind/tomak79h:default

Flashing example for this keyboard:

    make era/sirind/tomak79h:default:flash

## Split Port Safety

The inter-half USB-C connector is not a USB data port. On affected Tomak79H
PCBs, the D+/RX line is used as a single-wire half-duplex serial line and the
D-/TX line is left as an input by firmware. This avoids driving the GP0/TX line
against the other half when a normal USB-C to USB-C cable connects D- to D-.
Firmware cannot fully protect against plugging this inter-half port into a PC
or other USB host.

The two halves talk over that single wire at 460800 baud. Both halves run one
identical firmware image and are flashed together; a pair left on two different
versions is not a supported configuration.

Which half owns the USB session is decided by QMK's bounded USB-active
detection at boot, not by sampling a VBUS pin. The firmware refuses a VBUS-pin
role selection at build time, because raw VBUS sampling can race host
enumeration and drop USB during master selection.

See the [build environment setup](https://docs.qmk.fm/#/getting_started_build_tools) and the [make instructions](https://docs.qmk.fm/#/getting_started_make_guide) for more information. Brand new to QMK? Start with our [Complete Newbs Guide](https://docs.qmk.fm/#/newbs).

## Bootloader

Enter the bootloader in 3 ways:

* **Bootmagic reset**: Hold down the key in that half's own top-left position
  and plug in that half. Each half uses its own position, and the position is
  what counts rather than whatever legend sits there.
* **Physical reset**: Short the 'RESET' and 'GND' holes twice within one second, or plug in the keyboard with the 'BOOT' and 'GND' holes shorted.
* **Keycode in layout**: Press the key mapped to `QK_BOOT` if it is available.

## Firmware Guide

What this firmware adds over stock QMK — TAPDANCE in VIA, SOCD,
KKUK, DEBOUNCE, TAPPING, NKRO and EEPROM CLEAN — is written
for keyboard owners in [the split guide](../../common/docs/user/readme_split.txt), with the keycodes
its TAPDANCE fields accept in
[via_keycodes.txt](../../common/docs/user/via_keycodes.txt).
