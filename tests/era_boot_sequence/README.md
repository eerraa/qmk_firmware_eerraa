# ERA boot lifecycle tests

Run from the synchronized WSL tree:

```sh
python3 -m unittest discover -s tests/era_boot_sequence -v
```

The fixture extracts production functions instead of keeping a second boot
implementation. The launch test connects QMK `keyboard_init`/`quantum_init`,
Bootmagic, ERA matrix scan/post-scan, scheduler transport/dispatch and the actual
owner transfer/admission functions. The reducer's peer-unknown role, local DMA
sampling, USB, PIO launch and cross-core READY/release publication are simulated.
It checks both halves, storage enabled/disabled, post-init Low startup, request
admission after teardown, responder ownership, epoch/revoke rejection, readiness
repair and policy retry after a failed first launch.

The cancellation fixture connects the production housekeeping cancellation,
dirty consumption and policy dispatch to the real queue reset, quiesce and
owner transfer. It retains the production policy repair condition/body while
simulating authority/planner outputs and Core1 acknowledgments. It covers a
Left no-USB Bootmagic readiness arm followed by post-init discovery, both
physical sides as PEER initiator or HOST responder, unchanged-role dirty facts,
failed cancellation/retry, a new responder policy and a closed-wire policy.
Local DMA readiness and request enqueue are simulated; this is not a complete
two-board wire or USB-enumeration test.

The settings fixture executes both class hooks in VIA and default builds, plus
the production sync-policy loader. EEPROM bytes and common feature hardware
initialization are simulated. It checks one runtime load before the user hook,
saved policy restoration, and replacement by defaults on the later eeconfig
reset path. It does not test physical NVM durability.

The suspend fixture runs the real ChibiOS pre-task loop, ERA suspend hook,
common active task and RP2040 deadline implementation. Time and USB state are
simulated. It covers suspend before housekeeping, active/suspend/resume sharing
one deadline, timestamp zero, wrap and clock-free service after disarm.

Four negative controls compile isolated in-memory source mutations and require
the corresponding runtime assertion to fail: implicit ownership grant, missing
default settings load, missing suspend service and missing cancellation policy
repair. Compilation failure does not
count as a counterexample. No production source is mutated by the tests.

Official firmware builds validate target integration. Physical reset timing,
USB enumeration, wire baud/latency, LED output and performance remain device gates.

The matrix-startup fixture executes the production PIO sampler initialization,
raw-row admission/decode and Bootmagic callers. Register, clock, GPIO and DMA
operations are simulated; debounce is a direct raw-row bridge. Zero and partial
first-frame timeouts must not produce input or request EEPROM reset/bootloader.
A later complete frame restores admission, a valid held Bootmagic key still
works, and a failed/reentered initialization cannot reuse stale readiness.
The timeout negative control must compile and reproduce the false Bootmagic
request. This does not measure DMA startup or electrical settle margins.
