# Persistence failure boundaries

Run `python3 -m unittest discover -s tests/era_persistence_failures -v` with GCC.
Generated sources and binaries stay in temporary directories. The tests execute
production NVM/WL engines, EEPROM adapters, custom VIA dispatch and setting owners.
Boot, reset and the large Tap Dance/KKUK units use complete production function
extraction. MCU reset, clocks, peripheral actions and physical storage are modeled;
the boot fixture supplies default payloads while production code owns their order.

The cases cover CLEAN failure/retry/shutdown/cold mount, reset-key boot refusal,
custom synchronous failure responses, equal-value retry, equal macro payloads,
and semantic notification unions. Negative mutations must compile and then fail
the same runtime assertions. The NOR harness also retains program, erase,
readback, rotation, open-macro exclusion and rollback fault sweeps.

Feature cases follow a failed NKRO save through GET, RGB Sleep, MAGIC keys and
the actual QMK report bodies, and run both Velocikey board owners against wear leveling.
Macro cases exercise live-session proof and cold remount on production WL/native
paths. Semantic cases distinguish sparse writes, later SET debt and repair-only
no-ops. CLEAN retry checks include duplicate requests and timer wrap; full-default
tests reject a foreign scope without weakening nested default-owner coverage.

VIA-only boot cases inject NOR outer commit and final-valid-magic WL append
failures through the real initializers, adapters and `quantum_init` caller.
They distinguish raw desired magic from ready state, preserve valid QMK/ERA
settings during retries, check cold mount after successful retry and valid boot's
zero-write behavior,
reject foreign standalone VIA scopes, and retain full defaults' outer gate.
Negative controls separately remove the receipt, retry classification and
scope guards; each must compile before its runtime counterexample counts.
Failed torn WL log headers have no cold old-or-new guarantee in this fixture;
the tests preserve the backend's existing storage-format boundary.

`callers.py OUTPUT_DIRECTORY` builds a standalone production VIA fixture; its
`callers.exe bridge` mode accepts hexadecimal RAW HID reports and a `STATE`
request on stdin for an external app test. Runtime CONFIG and public MACRO revisions are separate
from durable KEYMAP completion. MACRO regressions exercise staging, no-op writes,
failed CLOSE, unrelated failure/repair of the public marker and negative controls
that remove the actual production visibility notifications. No fixture changes storage geometry or wire IDs.
