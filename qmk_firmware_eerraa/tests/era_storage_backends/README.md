# Physical receipt and native EEPROM boundaries

```sh
python3 -m unittest discover -s tests/era_storage_backends -v
```

The fixture compiles complete production function bodies, not rewritten algorithms.
RP2040 flash SDK and XIP access are modeled: absent erase and torn/no-effect program
must fail readback. Native AVR uses the production public EEPROM header, nested
receipt owner and macro SET/RESET callers over an avr-libc byte-store model. All
eight write/update APIs reject no-effect programming; tests cover equal retry,
nested failure, failed payload followed by CLOSE, failed RESET invalidation,
failed RESET payload, and complete retry. The actual default initializer is tested
at byte cuts and at failed validity invalidation, with default providers stubbed.

These do not emulate EEPROM analog behavior, silicon timing, XIP cache coherence
or physical power loss. Native EEPROM and QMK wear-leveling retain their single
store recovery limits; they do not acquire the ERA NOR engine's dual-bank guarantee.
