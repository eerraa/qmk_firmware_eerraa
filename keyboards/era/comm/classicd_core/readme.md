# CLASSIC.D_CORE

* Keyboard Maintainer: [ERA](https://github.com/eerraa)
* Hardware supported: CLASSIC.D_CORE
* Hardware availability: CLASSIC.D_CORE

Make example for this keyboard (after setting up your build environment):

    make era/comm/classicd_core:default

Flashing example for this keyboard:

    make era/comm/classicd_core:default:flash

See the [build environment setup](https://docs.qmk.fm/#/getting_started_build_tools) and the [make instructions](https://docs.qmk.fm/#/getting_started_make_guide) for more information. Brand new to QMK? Start with our [Complete Newbs Guide](https://docs.qmk.fm/#/newbs).

## Bootloader

Enter the bootloader in 3 ways:

* **Bootmagic reset**: Hold down the key at ESC(0,0) in the matrix (usually the top left key or Escape) and plug in the keyboard
* **Physical reset button**: Briefly short the `RESET` and `GND` pads on the SWD header twice, or short the `BOOT` header and plug in keyboard
* **Keycode in layout**: Press the key mapped to `QK_BOOT` if it is available

## Firmware Guide

What this firmware adds over stock QMK — TAPDANCE in VIA, SOCD,
Anti-Ghosting, DEBOUNCE, TAPPING, NKRO and EEPROM CLEAN — is written
for keyboard owners in [the one-piece guide](../../../common/docs/user/readme.txt), with the keycodes
its TAPDANCE fields accept in
[via_keycodes.txt](../../../common/docs/user/via_keycodes.txt).
