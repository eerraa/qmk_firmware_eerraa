# ODESSEY60S

Non-split RP2040 firmware for the NEWONE ODESSEY60S.

* Keyboard Maintainer: eerraa
* Hardware Supported: ODESSEY60S PCB
* Hardware Availability: Not available

Make example for this keyboard after setting up your build environment:

    make era/newone/odessey60s:default

Flashing example for this keyboard:

    make era/newone/odessey60s:default:flash

See the [build environment setup](https://docs.qmk.fm/getting_started_build_tools) and the [make instructions](https://docs.qmk.fm/build-compile-instructions) for more information.

## Firmware Guide

What this firmware adds over stock QMK — TAPDANCE in VIA, SOCD,
Anti-Ghosting, DEBOUNCE, TAPPING, NKRO and EEPROM CLEAN — is written
for keyboard owners in [the one-piece guide](../../common/docs/user/readme.txt), with the keycodes
its TAPDANCE fields accept in
[via_keycodes.txt](../../common/docs/user/via_keycodes.txt).
