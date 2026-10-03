# Production EEPROM boundary tests

Run in the synchronized WSL tree:

```sh
make test:era_nvm test:era_nvm_qmk_driver test:era_wl_qmk_driver -j12
python3 -m unittest discover -s tests/era_storage_backends -v
python3 -m unittest discover -s tests/era_boot_sequence -v
```

The custom fixture links actual QMK keyboard/VIA/default owners, EEPROM update
helpers, ERA adapter and journal engine. Only physical NOR is modeled. It checks
normal remount, one factory commit, nested rollback, uncertain-tail equal retry,
macro upload/reset, local metadata, CLEAN and scoped writes around an open macro.
Factory injection visits each program, readback and erase failure and each torn
program boundary, then remounts and retries against the retained physical bytes.
`era_nvm` separately covers rotation, maintenance, replacement and replay faults.

The cost fixture has six layers of a 4x10 all-KC_NO default layout. Its initialization
counts begin after mounting the empty store; they exclude that mount's seed bank.
The assertions count physical mock calls, not device time or a TOMAK layout boot.
To form a historical counterexample, the fixture's prefix plus its two cost tests
can be linked to pre-change production sources; no engine implementation is copied.

`era_wl_qmk_driver` links production macro callers, EEPROM adapter, wear-leveling
and State Sync; only backing storage is mocked. Each packet write failure retains
all desired bytes, equal CLOSE repairs them, and mixed-domain recovery notifies
KEYMAP/CONFIG without prematurely closing MACRO. Cold mounts after every packet
program cut discard the desired cache and require an incomplete macro marker.
Consolidation tests cut every physical write and four erase boundaries, remount,
and retry. They explicitly demonstrate the single-bank limit: an erased old
image cannot be recovered, and an incomplete consolidated image mounts blank
on checksum failure. The partial-sector fixture models erased words, not silicon
erase ordering. AVR and RP2040 physical receipt
fixtures are described in `tests/era_storage_backends/README.md`.
