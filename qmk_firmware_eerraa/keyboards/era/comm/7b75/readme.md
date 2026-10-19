# 7B75

* Keyboard Maintainer: [ERA](https://github.com/eerraa)
* Hardware supported: 7B75
* Hardware availability: 7B75

Make example for this keyboard (after setting up your build environment):

    qmk compile -kb era/comm/7b75 -km default

Flashing example for this keyboard:

    qmk flash -kb era/comm/7b75 -km default

See the [build environment setup](https://docs.qmk.fm/#/getting_started_build_tools) and the [make instructions](https://docs.qmk.fm/#/getting_started_make_guide) for more information. Brand new to QMK? Start with our [Complete Newbs Guide](https://docs.qmk.fm/#/newbs).

## Bootloader

Enter the bootloader in 3 ways:

* **Bootmagic reset**: Hold down the key at ESC(0,0) in the matrix (usually the top left key or Escape) and plug in the keyboard
* **Physical reset button**: Briefly short the `RESET` and `GND` pads on the SWD header twice, or short the `BOOT` header and plug in keyboard
* **Keycode in layout**: Press the key mapped to `QK_BOOT` if it is available
