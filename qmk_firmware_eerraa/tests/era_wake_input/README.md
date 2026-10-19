# Captured wake input

`make test:era_wake_input test:era_wake_input_swap` executes the real QMK matrix
ingress, tapping FIFO, dynamic ERA Tap Dance, shared reports and ChibiOS wake
clear. Only persistent configuration and host hardware are simulated. Macros
are disabled so Tap Dance alone crosses the actual suspend capture/reconcile
path. The second target also executes the original hand-swap press mapping.

Already executed ordinary output remains cleared and its captured up remains
suppressed. Ordinary momentary layer state survives that output clear: MO,
held LT, LM and runtime-disabled OSL release their original layer; held TT
performs its original inverse operation, including a previously enabled layer.
A still-queued accepted down executes after wake; its matching up
must retire whichever output that down actually supplies. A fresh wake down or
rejected/retired invalid dance owns no ordinary release. Device suspend timing
and endpoint behavior are outside these host assertions.

Post-wake ordinary dispatch retains QMK's bit-state and live-remap meaning.
Space Cadet and Grave Escape exercise modules that produce output and return
false before the action handler. Repeated wake clears prior ordinary dispatch
qualification, and overflow/retirement cannot manufacture it. Bounded FIFO
inspection and marking occur only at wake transitions; normal ticks do neither.

Each accepted ordinary layer action retains a two-byte raw-position release
identity. It preserves QMK bit-state rather than adding physical reference
counts, and ignores injected TD output. Wake clear retires old LM modifiers;
an LM dispatched after that clear retains its own modifier release. Explicit
layer replacement retires the layer release while preserving still-live LM
modifier release. Original remapping and scoped TD layer preservation are
tested separately. The identity and captured-up bookkeeping compile out when
`NO_USB_STARTUP_CHECK` removes stock wake capture; shared wake reprojection,
TD epoch protection and split authority-closed local release remain enabled.

`python3 tests/era_wake_input/protocol_order_selftest.py` extracts the current
production ChibiOS `protocol_pre_task`, deferred USB event handlers and HAL
`_usb_wakeup`, links them to the actual native QMK objects, and schedules host
and remote wake at the HAL seam. Ten cases run with zero and nonzero wake
delay. A queued wake clear must occur before the first resumed QMK pass settles
LT/MT/TD records. Its isolated removal must compile/link and fail a named runtime
assertion. Endpoint implementation, complete ChibiOS IRQ/thread integration and
device timing remain outside this host fixture.

After both native targets, run `python3 tests/era_wake_input/wake_release_selftest.py`
under the host build lock. It recompiles eighteen isolated fix-removal mutations,
requires named runtime failures, and verifies product sources/native objects
remain byte-identical. Compile or link errors never count as negative passes.
