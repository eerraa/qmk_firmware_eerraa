# LINK SPEED production-boundary tests

Run `make test:era_split_link_lifecycle` in the officially synchronized WSL
build tree. Run `python3 tests/era_split_link_lifecycle/test_source_contracts.py`
from the repository to check the adjacent hardware-dispatch wiring.

The C fixture includes production VIA LINK handling, LINK lifecycle, restart
agreement and the EEPROM adapter, links the real NVM engine, and includes the
scheduler's **same cold transition implementation**, not a copied apply model.
The prepare callback forwards to the real LINK act or agreed report commit. Hardware
owner quiesce/READY, snapshot publication, peer facts and the NOR device are
fault-injection boundaries. The whole scheduler planner, PIO registers, USB
stack and physical IRQ timing are not executed by this fixture.

Two endpoint snapshots hold independent singleton RAM and NVM engine states.
Each NOR has independent bytes and mutation counters. Endpoint selection does
not synchronize clocks or copy storage. The test explicitly selects each
endpoint's local time; the shared clock may have a different offset and may
change at a role flip. NOR latency advances only that selected endpoint. This
proves software ordering under an independently progressing peer, not measured
simultaneity on two RP2040s.

Fault cases include program-prefix cuts, final record-seal readback failure,
and final bank-activation readback failure. The last case injects a full
journal cursor to enter the production rotation path without filling the
fixture with irrelevant records. Reboot uses the production mount/parser.
An unacknowledged final physical commit can legitimately recover the new
complete record; a later RAM-identical replacement must repair the ambiguity
before claiming durable success.

The tests keep wire ids, level values and the four-byte stored layout. Local
success is runtime READY/publications plus durable replacement, not a pair-wide
commit receipt. Lost final messages, pathological Core0 stalls, PIO/IRQ timing,
held-key behavior and real USB session continuity require the device gates in
`keyboards/era/common/docs/manuals/era_performance_gates.md`.

Recovery presentation uses the real LINK_RECOVERED handshake with independent
endpoint report state. Coverage includes DUAL-HOST and both HOST orientations,
different serviced edges, a 200-ms-late first display or cold dispatch, duplicate
arms, clock/role change, local timer wrap through deadline zero, hidden-report
expiry, service loss before and after acceptance, lost echo/disarm, time-anchor
and storage gates, and searched fallback without a new rate raise. The report
is one agreed epoch, never a pair of first-call timers. The settled initiator
alone requests it, from its local search or the discovery answer. A regression
executes the real Right-listener search, Left-HOST role handoff, unchanged-rate
agreement and report without injecting an idle between acts. Physical LED edge skew
and rendering while a higher-priority indication or sleep owns the LEDs are
not host-test measurements.

Explicit local Apply coverage separates selection, configured divider, saved
level, pending intent and checked completion. Pending owns no RGB frame;
no-change and checked local success share one green pulse. Busy, competing
peer intent, expiry and runtime/NVM failure have failure receipts. A later
failed health check corrects the local receipt; success never replays green.
Both HOST orientations and DUAL-HOST directions bypass HID quiet without
bypassing time-anchor, storage drain or wire confirmation. Slow/faulted flash
and rotation are inspected before return to reject premature success.
Repeated clicks and dropdown changes cannot rewrite a pending receipt's
target. Automatic/peer acts stay silent. The production
presenter timing is exercised across wrap and hidden expiration, and the VIA
labels reject short reports and write commands. The adjacent source checks pin
all six JSON label definitions and the family's colour/priority dispatch;
actual GUI refresh and LED visibility remain device/app checks.

Listener-search coverage exercises the production counter consumer and cold
transition, not a copied scan algorithm. One complete observation window
replaces the early/fallback split; accepted frames veto it. Silence, a single
boot break, burst noise before the deadline, counter/time wrap, role exit
without an intervening task, rate changes and serial faults are covered. A
late task may evaluate one window but never replay missed windows.

The periodic-probe model sweeps initial/target rates, probe phases and fast or
backed-off completion intervals with an independently clocked cold task. It
checks convergence without extra rate transitions, writes or presentation;
it is not a PIO/IRQ, CPU utilization or electrical-noise measurement. The
source checks keep listener work off the scan path and pin the bounded faster
no-peer sender cadence and unchanged parked RX waits. Run
`python3 tests/era_split_link_lifecycle/test_discovery_cadence.py` in the WSL
tree: it compiles the actual sender period, due, reset and completion functions
against controlled time, exercising the initial miss threshold, success,
failed/unsent requests, timer wrap, stale revalidation and the steady rate cap.
