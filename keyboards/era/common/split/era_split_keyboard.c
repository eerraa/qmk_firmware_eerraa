// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_keyboard.h"

#include "../system/era_common_features.h"
#include "../system/era_common_via.h"
#include "../system/era_via_system.h"
#include "../system/era_usb_session.h"
#include "atomic_util.h"
#include "era_split_authority_reducer.h"
#include "era_split_board.h"
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
#    include "era_host_peer_storage.h"
#endif
#include "era_host_peer_transaction.h"
#include "era_split_link.h"
#include "era_split_restart_agreement.h"
#include "era_split_rgb_sleep_policy.h"
#include "era_split_scheduler_events.h"
#include "era_split_sync_policy.h"
#include "era_split_tap_activity.h"
#include "era_split_transport_scheduler.h"
#include "era_split_usb_identity.h"

#if defined(MCU_RP)
#    include "hardware/structs/timer.h" /* timer_hw->timerawl, the resolver gate's raw-microsecond clock */
#endif
#include "era_split_via_link.h"
#include "era_split_via_sync.h"
#include "sync_timer.h"
#include "timer.h"
#include "usb_device_state.h"
#if defined(PROTOCOL_CHIBIOS)
#    include "usb_main.h"
#endif
#ifdef RGB_MATRIX_ENABLE
#    include "rgb_matrix.h"
#endif
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
#    include "diagnostics/era_split_wire_diagnostics.h"
#endif
#ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
#    include "diagnostics/era_via_macro_diagnostics.h"
#endif
#ifdef ERA_SPLIT_QWIN_COUNT_ONLY_ENABLE
#    include "diagnostics/era_split_qwin_diagnostics.h"
#endif
#ifdef ERA_PASS_PHASE_DIAGNOSTICS_ENABLE
#    include "../system/era_pass_phase_diagnostics.h"
#endif
#define ERA_SPLIT_USB_REMOTE_WAKEUP_ENABLED 2U

static bool    era_split_keyboard_usb_configure_state_valid;
static uint8_t era_split_keyboard_usb_configure_state;
/* The render gate's one owner. `lighting_sleep` is the decision this unit last
   wrote, `owner_is_wire` the owner it was resolved under, and
   `wire_lighting_sleep` the fact a HOST-PEER HOST supplied for a PEER to
   render. */
static bool era_split_keyboard_lighting_sleep_valid;
static bool era_split_keyboard_lighting_sleep;
static bool era_split_keyboard_lighting_sleep_owner_valid;
static bool era_split_keyboard_lighting_sleep_owner_is_wire;
static bool era_split_keyboard_wire_lighting_sleep_valid;
static bool era_split_keyboard_wire_lighting_sleep;
/* The sleep decision's rising-edge stamp, for the breaker hunt: the brk latch
   named a NONE frame with clean flush-time state, and the only mid-operation
   writer that can raise suspend is the resolve above this — so the question
   one operation answers is whether the decision pulsed true near `brkms`.
   Edge-only, diagnostics-read, no behavior. */
static uint16_t era_split_keyboard_lighting_sleep_true_count;
static uint32_t era_split_keyboard_lighting_sleep_true_ms;

static bool era_split_keyboard_local_sleep_state(void) {
    // Three independent reasons, one RGB decision: the USB stack's explicit
    // suspend, loss of USB frames, and the board's configured input-idle
    // timeout. Gating this on
    // authority host-open made sleep impossible to sustain, because the reducer
    // deliberately closes host-open 500 ms into any sustained suspend, and that
    // remains the wrong conjunct - what is added here is a disjunct with its own
    // evidence, which is the repair direction the device measurement pointed at.
    //
    // The frame arm is what closes ghost-power RGB sleep. This comment claimed
    // the unconfigured SUSPEND-to-INIT remap covered that case until
    // 2026-08-11, and the claim was inverted: the remap is what turns SUSPEND
    // into INIT there, so the first term is false in exactly the state a host
    // death behind a powered hub leaves behind. The remap stays - the authority
    // reducer needs it, and the second term no longer depends on it.
    //
    // This answers "what does MY USB session say", and nothing more. It used to
    // be the whole decision, on the premise that "on a PEER half both terms are
    // false and stay false: it never enumerates". That premise was false for the
    // half this pair actually produces - see the ownership block below - and the
    // repair is an owner rather than a third term here.
    bool     explicit_suspend = usb_device_state_get_configure_state() == USB_DEVICE_STATE_SUSPEND;
    uint16_t timeout_sec      = era_split_board_rgb_sleep_timeout_seconds();
    return era_split_rgb_sleep_policy_local_requested(explicit_suspend,
                                                       era_usb_session_frames_lost(),
                                                       timeout_sec,
                                                       last_matrix_activity_elapsed());
}

static void era_split_keyboard_mark_authority_sample_due(void) {
    era_split_transport_scheduler_mark_maintenance_due(ERA_SPLIT_SCHEDULER_MAINT_DUE_AUTHORITY_SAMPLE);
}

static bool era_split_keyboard_try_remote_wakeup(void) {
#if defined(PROTOCOL_CHIBIOS)
    if (USB_DRIVER.state == USB_SUSPENDED && (USB_DRIVER.status & ERA_SPLIT_USB_REMOTE_WAKEUP_ENABLED)) {
        usbWakeupHost(&USB_DRIVER);
        return true;
    }
#endif
    return false;
}

/* Composed-matrix edge, reached from matrix_post_scan() on every scan whose
   composed rows changed - local rows always, projected PEER rows once the
   projection runs. Remote wake must ride this edge and not the action
   pipeline: the reducer closes host-open a grace window into any sustained
   suspend, is_keyboard_master() is that flag, should_process_keypress()
   consumes the answer, and action_exec() stops - so a wake requester behind
   process_record() is unreachable for the rest of the suspend. The predicate
   above reads local session facts only (bus suspended, host granted remote
   wake); authority facts must not gate it, and the grant check is what keeps
   a never-enumerated half (PEER, ghost power) from signaling resume into a
   dead bus. */
void era_split_keyboard_note_input_edge(void) {
    (void)era_split_keyboard_try_remote_wakeup();
}

/* --- The lighting render gate has exactly one owner ----------------------- *
 *
 * Which owner is a property of the RELATION, not of the USB stack:
 *
 *   own USB session -- LOCAL_NO_LINK, both DUAL-HOST roles, the HOST-PEER HOST
 *   the wire        -- the HOST-PEER PEER, whose pair shares one USB session
 *                      and that session is the HOST's
 *
 * The non-owner does not write the gate at all. That is the whole rule, and it
 * is canonical in era_authority_contract.md.
 *
 * What it replaces is two writers and no owner. The local predicate ran on
 * every half in every relation and the wire apply overwrote it afterwards, on
 * the premise -- written into both this unit and era_usb_session.c -- that a
 * PEER "never enumerates", so its own detectors could not fire. **A half
 * demoted from DUAL-HOST to PEER is a PEER that HAS enumerated**, and both
 * detectors fire on it: the bus goes idle so the LLD reports SUSPEND with
 * USB_DRIVER.configuration still non-zero (no INIT remap), and
 * era_usb_session_host_seen is a latch that never comes down. Device symptom,
 * operator-reported 2026-08-13: pull the RIGHT half's cable out of a DUAL-HOST
 * pair and its keys keep working while its lighting goes dark, and stays dark
 * until a key is pressed on the LEFT half -- because the HOST's RGB-state
 * section is latest-state and edge-armed, so it crosses once at relation open
 * and the HOST has no reason to send it again, and the only idle-time producer
 * that rebuilds the HOST's response plan is its own local key edge.
 *
 * Ownership transfer is an edge with a rule per direction, and both directions
 * were broken by the same missing owner:
 *
 *   local -> wire (demotion into a PEER): the incoming owner has said nothing
 *     yet, so this half holds NO locally-decided value and resolves LIT. It is
 *     lit rather than dark because dark-until-told is the failure above, and
 *     because the rotation drops the responder's RGB sent shadow, so the HOST's
 *     answer is due on the relation's first response and arrives within a poll.
 *
 *   wire -> local (promotion, or becoming DUAL-HOST): the wire's last word is
 *     dropped and this half's own session decides from the next pass. Keeping
 *     the HOST's last sleep bit across a promotion is the mirror image of the
 *     same defect.
 *
 * A board's status report may still punch the gate to force a frame visible
 * (the core1 launch-failure report). That is a render override and not a second
 * owner of the decision: it re-asserts itself for as long as it runs, and this
 * resolver writes only on an edge of its own value, so the two do not fight.
 * What the HOST publishes to its PEER is the resolved decision below and never
 * the raw gate, so a status report on the HOST cannot flash the PEER. */
static bool era_split_keyboard_note_lighting_sleep(bool sleep) {
    bool changed = false;

    ATOMIC_BLOCK_RESTORESTATE {
        changed = !era_split_keyboard_lighting_sleep_valid ||
                  era_split_keyboard_lighting_sleep != sleep;
        era_split_keyboard_lighting_sleep_valid = true;
        era_split_keyboard_lighting_sleep = sleep;
    }

    if (changed && sleep) {
        era_split_keyboard_lighting_sleep_true_count++;
        era_split_keyboard_lighting_sleep_true_ms = timer_read32();
    }

#ifdef RGB_MATRIX_ENABLE
    /* Reconcile the physical gate every time, not only on a logical edge. QMK's
       wake path is allowed to clear this flag, and board presentation code must
       not become a second sleep owner. The current facts above are authority;
       this cached bool exists only for wire edge arming and diagnostics. */
    if (rgb_matrix_get_suspend_state() != sleep) {
        rgb_matrix_set_suspend_state(sleep);
    }
#endif
    if (changed) {
        era_split_transport_scheduler_mark_host_peer_rgb_state_due();
    }
    return changed;
}

static bool era_split_keyboard_resolve_lighting_sleep(void) {
    bool owner_is_wire = era_split_transport_scheduler_lighting_sleep_owner_is_wire();

    if (!era_split_keyboard_lighting_sleep_owner_valid ||
        era_split_keyboard_lighting_sleep_owner_is_wire != owner_is_wire) {
        era_split_keyboard_lighting_sleep_owner_valid   = true;
        era_split_keyboard_lighting_sleep_owner_is_wire = owner_is_wire;
        /* Both transfer directions drop the outgoing owner's answer. Dropping
           the wire fact is what makes a promotion re-evaluate; the local
           predicate needs no drop because it is recomputed below from state it
           does not cache. */
        ATOMIC_BLOCK_RESTORESTATE {
            era_split_keyboard_wire_lighting_sleep_valid = false;
            era_split_keyboard_wire_lighting_sleep       = false;
        }
    }

    bool sleep;
    if (owner_is_wire) {
        bool wire_valid = false;
        bool wire_sleep = false;
        ATOMIC_BLOCK_RESTORESTATE {
            wire_valid = era_split_keyboard_wire_lighting_sleep_valid;
            wire_sleep = era_split_keyboard_wire_lighting_sleep;
        }
        sleep = wire_valid && wire_sleep;
    } else {
        sleep = era_split_keyboard_local_sleep_state();
    }

    return era_split_keyboard_note_lighting_sleep(sleep);
}

/* The wire's publication into the owner, from the HOST-PEER response apply.
   It stores rather than writes: a fact that arrives while this half is not
   wire-owned -- a result drained one pass after a promotion -- must not reach
   the gate, and the resolver is the one place that knows. */
bool era_split_keyboard_note_wire_lighting_sleep(bool sleep) {
    bool changed = false;
    ATOMIC_BLOCK_RESTORESTATE {
        changed = !era_split_keyboard_wire_lighting_sleep_valid ||
                  era_split_keyboard_wire_lighting_sleep != sleep;
        era_split_keyboard_wire_lighting_sleep_valid = true;
        era_split_keyboard_wire_lighting_sleep       = sleep;
    }
    return changed;
}

/* Diagnostics view of the sleep decision's rising edges: how many times the
   resolved decision went true since boot, and the local ms of the last rise.
   Read cold at the console print; pairs with the shim led line's `brk`. */
uint16_t era_split_keyboard_lighting_sleep_true_diag(uint32_t *last_ms) {
    if (last_ms != NULL) {
        *last_ms = era_split_keyboard_lighting_sleep_true_ms;
    }
    return era_split_keyboard_lighting_sleep_true_count;
}

/* What the HOST-PEER HOST publishes to its PEER: the resolved decision, never
   rgb_matrix_get_suspend_state(). Reported as "not sleeping" before the first
   resolve, which is the same default the transfer rule uses. */
bool era_split_keyboard_lighting_sleep_state(void) {
    bool sleep = false;
    ATOMIC_BLOCK_RESTORESTATE {
        sleep = era_split_keyboard_lighting_sleep_valid && era_split_keyboard_lighting_sleep;
    }
    return sleep;
}

/* keyboard_pre_init_kb, reached from keyboard_setup() at quantum/main.c:41 -
   before protocol_pre_init() starts USB and long before keyboard_init() runs
   split_pre_init(). Both initializers here need only facts that already exist
   at that instant: the hand pin, and the USB device state that
   usb_device_state_init() published in protocol_setup(). */
/* **The agreed restart's bindings**, all declared by
   split/era_split_restart_agreement.h and defined here so that the service
   names no user and no user names another: what each act *is*, what each act
   *does*, and whether the initiator may arm it now. This is the one unit that
   knows both users -- it includes the link unit and the VIA system already --
   so the link switch's param bound is written here as the link unit's own
   constant, and the service reads a number without learning whose it is. They
   live side by side on purpose: adding an act means one row and one case in
   the same screen of text, and the properties' reasons stay with the type they
   belong to, in the service header.
   The table is unconditional even where a user is compiled out (the clean's
   dispatch below is behind ERA_EEPROM_CLEAN_ENABLE), because the wire may
   carry any act and the validators read every row. */
const era_split_restart_act_rules_t era_split_restart_act_rules[ERA_SPLIT_RESTART_ACT_MAX + 1] = {
    [ERA_SPLIT_RESTART_ACT_NONE]         = {.requires_confirmation = false, .yields_to_storage = false, .resets = false, .param_max = 0},
    [ERA_SPLIT_RESTART_ACT_LINK_SPEED]   = {.requires_confirmation = true, .yields_to_storage = true, .resets = false, .param_max = ERA_SPLIT_LINK_LEVEL_LOW},
    [ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN] = {.requires_confirmation = true, .yields_to_storage = true, .resets = true, .param_max = 0},
};

/* The checked act dispatch. LINK_SPEED runs here at T_commit. A serviced CLEAN
   runs here during its deadline-free PREPARE phase, and a standalone CLEAN
   immediately before reset. The bool is what keeps a failed CLEAN write from
   becoming a deadline or reset. */
bool era_split_restart_prepare_local(era_split_restart_act_t act, uint8_t param) {
    switch (act) {
        case ERA_SPLIT_RESTART_ACT_LINK_SPEED: {
            bool agreed = era_split_restart_agreement_commit_agreed();
            bool owner  = era_split_link_commit_stores();
            if (era_split_link_commit_persists(param, agreed, owner)) {
                era_split_link_store_level(param, agreed);
            }
            (void)era_split_transport_scheduler_apply_link_level(param);
#ifdef VIA_ENABLE
            /* Bounce only after this half's owner Apply actually committed.
               A request that expired, was refused, or was inert must leave
               Enable on. */
            if (owner) {
                era_split_via_link_schedule_reattach();
            }
#endif
            return true;
        }
#ifdef ERA_EEPROM_CLEAN_ENABLE
        case ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN:
            return era_via_system_eeprom_invalidate();
#endif
        default:
            return false;
    }
}

bool era_split_restart_arm_ready(era_split_restart_act_t act) {
    if (act != ERA_SPLIT_RESTART_ACT_LINK_SPEED && act != ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN) {
        return true;
    }
    /* PREPARE itself carries no deadline. Before either act emits T_commit,
       though, the initiator must have applied one time-anchor or the deadline
       is in the wrong time domain and the two halves miss. The responder is
       the shared-clock source and needs no adoption. */
    return sync_timer_is_time_source() || era_host_peer_transaction_time_anchor_adopted();
}

#ifdef ERA_EEPROM_CLEAN_ENABLE
/* The strong half of system/era_via_system.h's hand-off. With a serviced
   relation CLEAN is a bilateral prepared restart: both checked boot-predicate
   writes and storage quarantine precede the first deadline. Without a serviced
   relation the service retains the ordinary checked local CLEAN. The hand-off
   remains unconditional because a refused second request must do nothing, not
   fall through to an unrelated local reset. */
bool era_via_system_eeprom_clean_handed_off(void) {
    (void)era_split_restart_agreement_request(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN, 0);
    return true;
}
#endif

void era_split_keyboard_pre_init(void) {
    era_split_usb_identity_init();
    /* The reducer is the single derivation of local USB authority. Initializing
       it here is what removes the "before the reducer exists" window that the
       boot master latch used to cache an answer for. */
    era_split_authority_reducer_init();
}

/* keyboard_post_init_kb, the last step of keyboard_init(). Storage reads its
   seven domains, then the wire opens - once, by name, and after the EEPROM has
   settled. Before this the scheduler has only planned. */
void era_split_keyboard_post_init(void) {
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
    era_host_peer_storage_init();
#endif
    (void)era_split_transport_scheduler_start_communication_core();
}

void era_split_keyboard_reload_features_from_eeprom(void) {
    era_split_sync_policy_reload_from_eeprom();
    era_common_features_reload_from_eeprom();
}

/* SPLIT_TRANSPORT-style provision: the common split layer supplies this QMK
   hook for every ERA split board, the same way era_split_transport.c supplies
   the transport hooks. No ERA board defines its own, and one that started to
   would fail the link rather than silently lose the producer. It must return
   layer_state_set_user(state) or keymap-level hooks stop running. */
layer_state_t layer_state_set_kb(layer_state_t state) {
    era_split_transport_scheduler_note_local_layer_change();
    return layer_state_set_user(state);
}

void era_split_keyboard_task(void) {
#ifdef ERA_PASS_PHASE_DIAGNOSTICS_ENABLE
    /* Closes the loop-tail segment: everything QMK's main loop ran between the
       end of keyboard_task() and this hook -- protocol_post_task, the raw-HID
       and console tasks, deferred exec, and housekeeping_task down to the split
       skeleton. This is the first ERA-owned point after keyboard_task() ends. */
    era_pass_phase_mark(ERA_PASS_PHASE_LOOP);
    /* The HK sub-span opens here, on the stamp the mark above just took. The
       four parts tile HK so its 153 us maximum gets an owner; the fourth is
       derived by the reader as HK minus these three. */
    era_pass_phase_hk_open();
#endif
#ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
    /* raw_hid_task() ran earlier in this main-loop pass. Polling the RAW IN
     * endpoint here separates response enqueue time from actual queue drain. */
    era_via_macro_diagnostics_task();
#endif
    era_common_features_task();
#ifdef ERA_PASS_PHASE_DIAGNOSTICS_ENABLE
    era_pass_phase_hk_mark(ERA_PASS_PHASE_HK_FEATURES);
#endif
    (void)era_split_transport_scheduler_task();
#ifdef ERA_PASS_PHASE_DIAGNOSTICS_ENABLE
    era_pass_phase_hk_mark(ERA_PASS_PHASE_HK_SCHED);
#endif
#ifdef VIA_ENABLE
    /* After the scheduler so a bounce cannot occupy T_commit. */
    era_split_via_link_task();
#endif
    /* Unconditional since 2026-08-11, where it used to run only on a pass the
       scheduler reported work for. The frame-loss arm of the local predicate
       goes true on its own timer with nothing on the wire to report - a dead
       host produces no scheduler work by definition - so hanging this off the
       scheduler's return value is precisely the case it would miss. The cost is
       one relation compare plus, when this half owns the decision, one
       configure-state read; the state change it publishes is unchanged, because
       note_lighting_sleep() still acts only on an edge.

       It also no longer carries the MCU_RP/PROTOCOL_CHIBIOS guard the sleep
       refresh used to. The ownership question is platform-independent, and the
       guard was on the caller while the predicate reached the gate ungated from
       the USB notify below - two paths to one flag under different conditions,
       which is the shape this whole block exists to remove.

       Once per millisecond rather than once per pass, and the gate is one raw
       counter read: every input the resolver folds changes at the bus rate or
       slower (configure state, the 1 kHz-sampled frame age, the wire-carried
       fact), so a scan-rate re-resolve re-derives the same answer tens of
       times per millisecond — the 2026-08-15 qwin bisect priced this chain's
       call plumbing as part of a 22-to-16 kHz scan regression. Edges do not
       wait on the tick: the USB notify below resolves directly on the event,
       and the millisecond bound is invisible against the frame arm's 300 ms
       threshold. On a platform without the raw counter the gate compiles to
       the old every-pass call. */
#if defined(MCU_RP)
    {
        static uint32_t resolve_last_us;
        uint32_t        now_us = timer_hw->timerawl;
        if ((uint32_t)(now_us - resolve_last_us) >= 1000) {
            resolve_last_us = now_us;
            (void)era_split_keyboard_resolve_lighting_sleep();
#ifdef ERA_SPLIT_QWIN_COUNT_ONLY_ENABLE
            era_split_qwin_diagnostics_tick_1ms();
#endif
        }
    }
#else
    (void)era_split_keyboard_resolve_lighting_sleep();
#endif
#ifdef ERA_PASS_PHASE_DIAGNOSTICS_ENABLE
    era_pass_phase_hk_mark(ERA_PASS_PHASE_HK_TICK);
#endif
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    era_split_wire_diagnostics_task();
#endif
#ifdef ERA_PASS_PHASE_DIAGNOSTICS_ENABLE
    era_pass_phase_mark(ERA_PASS_PHASE_HK);
#endif
}

void era_split_keyboard_notify_usb_device_state_change(uint8_t configure_state) {
    /* The event's own argument is discarded for the session unit's live read.
       They are the same value at this instant -- usb_device_state.c stores
       configure_state before it notifies -- and reading it here is what keeps
       one owner for the remap. */
    (void)configure_state;
    configure_state = era_usb_session_configure_state();

    bool changed = false;
    ATOMIC_BLOCK_RESTORESTATE {
        changed = !era_split_keyboard_usb_configure_state_valid ||
                  era_split_keyboard_usb_configure_state != configure_state;
        era_split_keyboard_usb_configure_state_valid = true;
        era_split_keyboard_usb_configure_state = configure_state;
    }
    /* Through the resolver, not straight at the gate: a USB event on a
       wire-owned half is news about a session this half is not rendering, and
       the resolver is where that is decided. */
    bool sleep_changed = era_split_keyboard_resolve_lighting_sleep();
    if (changed || sleep_changed) {
        era_split_keyboard_mark_authority_sample_due();
    }
}

void era_split_keyboard_suspend_wakeup_init(void) {
    era_split_keyboard_mark_authority_sample_due();
}

/* Slice 11.5 removed a call from the top of this hook -- the producer that
   opened the DUAL-HOST activity *cadence window* on every key event -- because
   an unconditionally polling relation has no cadence for a key press to open.
   FA-2 S2 put a per-record call back in the same recorded position, before
   every early return, because a key consumed by a feature is still local input
   activity; what it feeds is different in kind. It counts input and notes
   tap-hold settles for the judgment window, and it produces wire traffic only
   while a judgment window is open on one half or the other, so it revives none
   of the cadence machinery 11.5 retired and fresh defaults still move
   nothing. */
bool era_split_keyboard_process_record(uint16_t keycode, keyrecord_t *record) {
    era_split_tap_activity_note_record(record->event.pressed, record->event.key, record->event.time, keycode, record->tap.count);
    /* Slice 14's visual producer, in the recorded before-every-early-return
       position for the tap note's reason: a key a feature consumes is still a
       local pressed-baseline change. Self-gated to the armed relation. */
    era_split_transport_scheduler_note_local_visual_change();
    if (record->event.pressed && era_split_keyboard_try_remote_wakeup()) {
        return false;
    }

    if (!era_common_features_process_record(keycode, record)) {
        return false;
    }
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    if (!era_split_wire_diagnostics_process_record(keycode, record)) {
        return false;
    }
#endif
#ifdef ERA_SPLIT_QWIN_COUNT_ONLY_ENABLE
    if (!era_split_qwin_diagnostics_process_record(keycode, record)) {
        return false;
    }
#endif
    return true;
}

/* The split half of the ERA VIA router. Its three callees are compiled only
   under VIA_ENABLE, and its own callers are each board's
   via_custom_value_command_kb(), which QMK only calls from via.c and which
   every board already guards on the same macro. The declaration in the header
   is deliberately left ungated, the treatment
   era_split_transport_scheduler.h's diagnostics exports take: a release-side
   caller then fails at link instead of silently pulling this back in. */
#ifdef VIA_ENABLE
static bool era_split_keyboard_handle_rgb_sleep_via_command(uint8_t *data) {
    if (data == NULL || data[1] != ERA_VIA_SYSTEM_CHANNEL) {
        return false;
    }
    uint8_t value_id = data[2];
    if (value_id != ERA_VIA_SYSTEM_RGB_SLEEP_PRESET_VALUE_ID && value_id != ERA_VIA_SYSTEM_RGB_SLEEP_EXACT_SECONDS_VALUE_ID) {
        return false;
    }

    uint16_t current = era_split_board_rgb_sleep_timeout_seconds();
    if (current == 0) {
        return false;
    }

    switch (data[0]) {
        case id_custom_get_value:
            if (value_id == ERA_VIA_SYSTEM_RGB_SLEEP_PRESET_VALUE_ID) {
                data[3] = era_split_rgb_sleep_policy_preset_minutes(current);
            } else {
                era_common_via_put_u16_be(&data[3], current);
            }
            return true;
        case id_custom_set_value: {
            uint16_t requested;
            if (value_id == ERA_VIA_SYSTEM_RGB_SLEEP_PRESET_VALUE_ID) {
                uint8_t minutes = data[3];
                if (!era_split_rgb_sleep_policy_preset_valid(minutes)) {
                    return false;
                }
                requested = (uint16_t)minutes * 60U;
            } else {
                requested = era_common_via_get_u16_be(&data[3]);
                if (requested == 0) {
                    return false;
                }
            }
            if (!era_split_board_set_rgb_sleep_timeout_seconds(requested)) {
                return false;
            }
            current = era_split_board_rgb_sleep_timeout_seconds();
            if (value_id == ERA_VIA_SYSTEM_RGB_SLEEP_PRESET_VALUE_ID) {
                data[3] = era_split_rgb_sleep_policy_preset_minutes(current);
            } else {
                era_common_via_put_u16_be(&data[3], current);
            }
            (void)era_split_keyboard_resolve_lighting_sleep();
            return true;
        }
        default:
            return false;
    }
}

bool era_split_keyboard_handle_via_command(uint8_t *data, uint8_t length) {
    if (era_common_via_handle_system_command(data, length)) {
        return true;
    }

    if (era_split_keyboard_handle_rgb_sleep_via_command(data)) {
        return true;
    }

    if (era_split_via_sync_handle_via_command(data, length)) {
        return true;
    }

    if (era_split_via_link_handle_via_command(data, length)) {
        return true;
    }

    return era_common_via_handle_feature_command(data, length);
}
#endif
