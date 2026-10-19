// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

void era_split_communication_core_init(void);
bool era_split_communication_core_start(void);
bool era_split_communication_core_request_quiesce(void);
void era_split_communication_core_wake(void);

/* R7: declare core1 dead and hardware-reset it so the next start() runs the
   full launch handshake against the bootrom wait loop instead of a SEV wake
   against a corpse. Called by the owner layer when the revoke wait times out
   consecutively — the one caller entitled to the judgment, because a live
   core1 answers a revoke at the top of every loop pass. */
void era_split_communication_core_declare_dead(void);

/* R7: true once the per-boot launch-attempt cap latched. The scheduler reads
   it to stop wanting a wire role, which is what lets the half converge to
   LOCAL_NO_LINK instead of replanning against a core that will not come up. */
bool era_split_communication_core_launch_capped(void);

/* One aligned-word proof that core1 completed another lifecycle-loop pass.
 * A selected service must return before the word advances; an idle pass
 * advances it immediately before WFE. Unlike the standing-exchange count,
 * this therefore covers queue, storage, standing and idle progress. The
 * initiator silence watch reads it to distinguish a responsive core from one
 * that has actually stopped. */
uint32_t era_split_communication_core_progress_count(void);

/* R7 fix (2026-08-06): the owner layer's service report, which is what moves
   the give-up streak. `serviced` clears it — the owner observed core1 in
   service, either ready freshly published on a full transfer or a live
   serviced lease on the ensure/serial-reset fast paths (a late ready that
   already counted must not leave a residue that accumulates toward a false
   cap); `service_timeout` counts the ready wait expiring into the same
   streak the handshake failures use, which is what makes the cap reachable
   when a relaunched core1 keeps dying after a successful handshake (the
   kill leg's observed shape). Callers: era_split_communication_core_owner.c
   and the scheduler's serial-reset fast path. */
void era_split_communication_core_note_core1_serviced(void);
void era_split_communication_core_note_core1_service_timeout(void);

/* The boot-time core1 hardware halt does not live here. Its precondition is
   "core0 is about to restart", so it runs from early_hardware_init_pre() in
   system/era_boot_core1_halt.c, before crt0 copies the image this unit
   executes from. See era_invariants.md. The R7 declare-dead reset above is
   the one other sanctioned PSM touch: its precondition is the pico-SDK's own
   launch precondition — core1 must be reset before the handshake — and it
   runs only against a core already judged dead. */

/* Quiesce core1, reset both SPSC rings and their high-water marks, and step the
   queue generation. It was called ..._diagnostics_flush_queue and declared in
   the diagnostics header until 2026-08-11, and it is neither: both callers are
   release control paths -- the scheduler's recovery cancellation and its owner
   route -- so a release build reached a function whose name said it would not.
   It lives here because quiescing core1 is what it does first. */
bool era_split_communication_core_queue_reset(void);
