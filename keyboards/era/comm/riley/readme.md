# Riley

ERA Riley is a non-split RP2040 keyboard with three WS2812 RGBLight LEDs.

* Keyboard Maintainer: [eerraa](https://github.com/eerraa)
* Hardware Supported: Riley RP2040 PCB
* Bootloader: RP2040 UF2

The three RGB LEDs normally participate in the selected RGBLight effect. Each
LED can instead follow Caps Lock, Scroll Lock, or Num Lock. When Indicator-Only
is off an inactive lock falls back to the RGB effect; when it is on an inactive
lock LED is dark. The independent GP25 Caps Lock LED remains a normal hardware
lock indicator.

Build examples:

    qmk compile -kb era/comm/riley -km default
    qmk compile -kb era/comm/riley -km via

For ERA evidence builds, use the repository's `era-build` workflow rather than
invoking QMK directly.
