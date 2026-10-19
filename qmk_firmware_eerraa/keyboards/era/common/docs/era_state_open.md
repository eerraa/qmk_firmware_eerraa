# ERA Open Items

Genre: state
Canonical for: decided but unimplemented product changes shared with H7S,
unmeasured hardware acceptance, and the entry condition of each

Only current unknowns live here. Delete an item when it closes. Requirements
and prohibitions stay in their contracts, and verification method stays in
`manuals/era_performance_gates.md`.

## 1. Decided, not yet implemented

None.

## 2. Hardware acceptance still open

| Item | Next verification |
| --- | --- |
| TOMAK TKL trial image | Tap Dance ownership on both halves and in DUAL-HOST (retro-tap cancel included), the first KKUK repeat about 80 ms after arming, one full reset on a changed `ERA_EEPROM_RESET_KEY` and none on a same-image reinstall, and QK_BOOT or a magic key fired from a TD action (core0 stack). |
| SOCD modes | With a key tester on a real board: each mode while both keys of a pair are held, a Tap Dance output against the opposing physical key, 6KRO and NKRO, and a pair split across the halves of a single-host split. |
| Maker USB identity | On one non-split board and on a split pair with each half as HOST: the image enumerates with its maker VID/PID and manufacturer string, the official JSON from the same build loads in usevia.app Design and connects, the ERA app recognises the new identity and still the legacy one on an older image, and settings stored by the older image survive the update. |
| Lighting sleep | On an RGBLight, an RGB Matrix, a backlight-effect and a plain-backlight board (7B75, N86, CLASSICD_A1, DIVINE cover them): each output sleeps on its own timeout, on USB suspend and on host loss, stays on with its switch off, wakes on input, and a VIA save during sleep keeps the user's lighting; lock indicators on a backlight rail go dark with backlight sleep. |
