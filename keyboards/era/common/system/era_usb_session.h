// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* The local USB session as ERA reads it: one owner for the facts every ERA
   consumer derives from the bus. **Sleep is a consumer of this file, not its
   subject** -- the configure-state remap and the frame sampler live here, and
   the lighting-sleep predicate is one of several things built on them, so a
   fact about the bus belongs here rather than beside whichever consumer asked
   for it first. The mechanism, and why the frame read is guarded, are at the
   top of the .c. */

/* Sample once per keyboard pass. */
void era_usb_session_task(void);

/* QMK's configure state with ERA's unconfigured-SUSPEND-to-INIT remap applied.
   One copy: this replaces the two verbatim ones that lived in
   era_split_keyboard.c and era_split_authority_reducer.c, where a change to one
   left the sleep decision and the authority decision disagreeing about what
   state the board was in. */
uint8_t era_usb_session_configure_state(void);

/* Sample the frame counter now and report how long ago it last moved.
   Returns false where this build has no sampler, and then writes nothing.

   The caller decides what unavailable means for its own question, and the two
   current callers need OPPOSITE answers: the authority reducer treats it as
   fresh, because dropping host_open on a board that cannot sample would churn
   the wire role; the sleep decision treats it as not-lost, because sleeping a
   board that cannot sample would darken it at power-on. That is why this
   returns an age and an availability rather than a boolean -- a merged boolean
   necessarily inverts one of them, silently.

   Sample HERE, adjacent to your own evaluation, rather than reading a value
   another pass left behind. A pass stalled by a flash erase is exactly the case
   era_flash_slice.c exists for, and a stale shadow read there produces a false
   not-fresh, a closed host_open, a stepped usb_epoch, and role churn on the
   wire. */
bool era_usb_session_sample_frame_age(uint32_t *age_ms);

/* The frame-loss arm of the sleep decision: true when this board has been
   configured by a host at least once since power-on and no frame has arrived
   for ERA_USB_SESSION_SOF_STALE_MS. Reads the last sample; it does not take
   one. False on any port that has never enumerated, so a charger is not a
   sleeping host.

   The split layer ORs this into its own sleep predicate; a non-split board does
   not need to call it, because this unit applies the result itself. */
bool era_usb_session_frames_lost(void);

/* Firmware-initiated USB re-enumeration. The VIA Apply toggle-as-action is the
   one caller (`split/era_split_via_link.c`): VIA does not re-GET, so the bus
   has to drop and come back for the page to read the consumed 0. The bounce is
   not a host unplug and not a sleep. Call `note` immediately before
   `restart_usb_driver()`; both consumers of this session — the authority
   reducer's host-open and the frame-loss arm — ask `hold` for the window the
   host needs to re-enumerate. A real unplug that outlasts the hold closes as
   before. */
void era_usb_session_note_firmware_reattach(void);
bool era_usb_session_firmware_reattach_hold(void);
