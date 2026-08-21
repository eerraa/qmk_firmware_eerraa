// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_communication_core_standing.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "hal.h"
#include "hardware/structs/timer.h"

#include "era_split_communication_core_internal.h"
#include "era_split_communication_core_owner.h"
#include "../era_host_peer_transaction.h"
#include "../era_split_transaction_backend.h"
#include "../era_split_transaction_engine.h"
#include "../era_split_wire_payload.h"
#include "../era_split_wire_router.h"


#ifndef ERA_SPLIT_COMMUNICATION_CORE_STANDING_READ_RETRIES
#    define ERA_SPLIT_COMMUNICATION_CORE_STANDING_READ_RETRIES 4
#endif

/* Both records use the responder snapshot's publication discipline verbatim:
 * an odd/even sequence around the write, and a reader that copies between two
 * equal even reads. It is device-proven, it costs one shared word each, and
 * reusing it means the three published records in this system have one
 * discipline rather than three. */
static era_split_communication_core_standing_plan_t g_era_split_communication_core_standing_plan;
static volatile uint32_t g_era_split_communication_core_standing_plan_seq __attribute__((aligned(4)));

static era_split_communication_core_standing_state_t g_era_split_communication_core_standing_state;
/* Two sequences, and they answer different questions.
 *
 * `state_seq` guards the read: it goes odd across the write, so a reader that
 * sees the same even value either side copied a consistent record. It moves on
 * every exchange, because every exchange updates the counters.
 *
 * `state_change_seq` is the wake: it moves only when a field core0 acts on has
 * actually changed -- the peer layer byte, or the stop flag. Splitting them is
 * not tidiness. With one sequence, `exchange_count++` made the record differ
 * every exchange, so core0's housekeeping woke once per poll and the standing
 * grant's whole claim -- that core0's cost does not scale with the poll rate --
 * was false by a factor of one. Device-measured 2026-08-01: the DUAL-HOST Left
 * ran 373 housekeeping passes per second against a 200 Hz authority-poll floor
 * at a 198.7 Hz poll, so about 173 of them were this. */
static volatile uint32_t g_era_split_communication_core_standing_state_seq __attribute__((aligned(4)));
static volatile uint32_t g_era_split_communication_core_standing_state_change_seq __attribute__((aligned(4)));

/* Core1-private. It is not published and core0 never reads it: the sent-state
 * shadow belongs to whoever confirms the send, and that is core1 now. It
 * resets on the relation identity, because the peer clears what it holds on
 * the same event and a surviving shadow would let an unchanged layer count as
 * already known. */
static struct {
    uint16_t relation_generation;
    uint32_t next_due_us;
    uint8_t  due_valid;
    uint8_t  sent_input_layer;
    uint8_t  sent_input_layer_valid;
    uint8_t  sent_storage_pending;
    uint8_t  sent_storage_pending_valid;
    uint8_t  sent_authority_valid;
    era_split_wire_authority_section_t sent_authority;
    uint8_t  sent_rgb_valid;
    era_host_peer_rgb_state_t sent_rgb;
    uint8_t  sent_activity_valid;
    era_split_wire_activity_section_t sent_activity;
    /* The visual-baseline shadow (Slice 14). An invalid shadow forces one
       send at relation open -- reason RELATION_OPEN -- whose all-zero
       baseline is a receiver-side no-op by arithmetic; afterwards the reason
       is RENDER_RESET, the ordinary diff that re-fires nothing unchanged. */
    uint8_t  sent_visual_valid;
    uint8_t  sent_visual[ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_BASELINE_BYTES];
    /* The restart-arm shadow, with no zero-baseline refinement: an invalid
       shadow crosses the idle body once per relation, five bytes in the
       yielding class, and that cross is load-bearing rather than waste. A
       confirmed arm survives a relation rotation while this shadow does not, so
       the arm re-crosses on the reopened relation and the responder
       re-validates its deadline; an unconfirmed one is retired by the rotation,
       and the idle body is what tells a responder holding a stale arm to
       disarm. Both depend on this section being able to cross with nothing to
       say. */
    uint8_t  sent_restart_act;
    uint8_t  sent_restart_param;
    uint32_t sent_restart_commit_ms;
    uint8_t  sent_restart_valid;
    /* Which period the current deadline was armed under. The two differ by an
       order of magnitude, so carrying one across the edge either fires a beat
       immediately on entering exclusivity or holds the cadence off for a
       liveness period on leaving it. Neither is harmful and both are
       confusing in a capture. */
    uint8_t  due_liveness;
    uint8_t  stopped;
    /* The plan this half stopped under. Without it the stop latch has no clear
       path on the ordinary recovery: core0 raises a pending SESSION_STATUS,
       which drops `enabled` and returns it, and the relation generation never
       moves because the relation never rotated -- so core1 would sit stopped
       on a healthy link forever. Clearing on a *plan* change is what makes
       "core0 republishes to restart it" the mechanism rather than the
       intention. */
    uint16_t stopped_plan_generation;
} g_era_split_communication_core_standing_private;

/* Field compares rather than memcmp, on both records, for the same reason the
 * responder's own comparators are written out: every field here is byte-sized
 * today, so a memcmp happens to work, and it would keep happening to work right
 * up until someone widens one and the padding starts deciding whether a section
 * is news. */
static bool era_split_communication_core_standing_visual_equal(const era_host_peer_visual_snapshot_t *lhs, const era_host_peer_visual_snapshot_t *rhs) {
    if (lhs->reason != rhs->reason) {
        return false;
    }
    for (uint8_t index = 0; index < ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_BASELINE_BYTES; index++) {
        if (lhs->pressed_baseline[index] != rhs->pressed_baseline[index]) {
            return false;
        }
    }
    return true;
}

static bool era_split_communication_core_standing_baseline_equal(const uint8_t *lhs, const uint8_t *rhs) {
    for (uint8_t index = 0; index < ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_BASELINE_BYTES; index++) {
        if (lhs[index] != rhs[index]) {
            return false;
        }
    }
    return true;
}

static bool era_split_communication_core_standing_rgb_equal(const era_host_peer_rgb_state_t *lhs, const era_host_peer_rgb_state_t *rhs) {
    return lhs->enabled == rhs->enabled && lhs->sleep == rhs->sleep && lhs->mode == rhs->mode &&
           lhs->hue == rhs->hue && lhs->sat == rhs->sat && lhs->val == rhs->val &&
           lhs->speed == rhs->speed && lhs->flags == rhs->flags;
}

/* No return value, deliberately. The shape was inherited from the storage
   unit's twin, which really can refuse -- a NULL sequence, or one at
   UINT32_MAX-1 -- and whose callers act on the refusal. Neither guard belongs
   here. All four callers pass the address of a file-scope global, and the
   overflow refusal would be a defect rather than a safety net on this
   sequence: it advances on every exchange, 1000/s at the DUAL-HOST poll, so
   the ceiling is reachable in weeks of uptime and refusing at it would stop
   core1 publishing to core0 for the rest of the boot. A plain wrap is benign
   for an odd/even protocol whose reader only compares two samples for
   equality. So all four call sites discarded a value that could only be true. */
static void era_split_communication_core_standing_seq_write_begin(volatile uint32_t *seq, uint32_t *odd) {
    uint32_t next = *seq + 1U;
    if ((next & 1U) == 0) {
        next++;
    }
    *seq = next;
    __DMB();
    *odd = next;
}

static void era_split_communication_core_standing_seq_write_end(volatile uint32_t *seq, uint32_t odd) {
    __DMB();
    *seq = odd + 1U;
    __DMB();
}

/* The generation is excluded from the comparison for the same reason the
 * responder snapshot excludes its own: a field that changes on every publish
 * makes every publish differ, the early-out never fires, and "on change"
 * quietly becomes "on pass". That is the exact failure the time anchor caused
 * on the responder path, measured at 911 publishes per second. */
bool era_split_communication_core_standing_plan_differs(const era_split_communication_core_standing_plan_t *plan) {
    if (plan == NULL) {
        return false;
    }
    era_split_communication_core_standing_plan_t candidate = *plan;
    candidate.plan_generation                              = g_era_split_communication_core_standing_plan.plan_generation;
    return memcmp(&candidate, &g_era_split_communication_core_standing_plan, sizeof(candidate)) != 0;
}

bool era_split_communication_core_publish_standing_plan(const era_split_communication_core_standing_plan_t *plan) {
    if (plan == NULL || get_core_num() != 0) {
        return false;
    }

    /* The equality early-out is the mechanism, not an optimisation: without it
     * a caller on a scan-bound path publishes every pass and the grant costs
     * what it was built to remove. */
    if (!era_split_communication_core_standing_plan_differs(plan)) {
        return false;
    }

    era_split_communication_core_standing_plan_t published = *plan;
    uint16_t generation = (uint16_t)(g_era_split_communication_core_standing_plan.plan_generation + 1U);
    if (generation == 0) {
        generation = 1;
    }
    published.plan_generation = generation;

    uint32_t odd = 0;
    era_split_communication_core_standing_seq_write_begin(&g_era_split_communication_core_standing_plan_seq, &odd);
    g_era_split_communication_core_standing_plan = published;
    era_split_communication_core_standing_seq_write_end(&g_era_split_communication_core_standing_plan_seq, odd);

    /* Tell core1 the plan moved. Without this the plan is correct and complete
     * in memory and nobody says so, and core1 finds it when its own period
     * alarm fires -- which makes the "a differing body outranks the cadence"
     * branch in service_once() unreachable early, because reaching it at all
     * requires being awake. A layer edge then waited a full period instead of
     * the scan it takes to publish.
     *
     * Free where it matters: the equality early-out above means this runs only
     * when the plan actually changed, so an idle keyboard never executes it.
     * And it cannot be lost -- SEV sets the event register, so a core1 that has
     * not reached WFE yet returns from the next one immediately. */
    __SEV();
    return true;
}

void era_split_communication_core_clear_standing(void) {
    if (get_core_num() != 0) {
        return;
    }
    uint32_t odd = 0;
    era_split_communication_core_standing_seq_write_begin(&g_era_split_communication_core_standing_plan_seq, &odd);
    memset(&g_era_split_communication_core_standing_plan, 0, sizeof(g_era_split_communication_core_standing_plan));
    era_split_communication_core_standing_seq_write_end(&g_era_split_communication_core_standing_plan_seq, odd);

    /* The reported peer values clear with the plan, and the reason is the
       edge-driven wake rather than tidiness. Core0 drops its peer layer and
       forgets its peer session on this same rotation, so a surviving
       `peer_input_layer_valid` or `peer_authority_valid` would let the first
       exchange of the reopened relation carry an unchanged value, raise no
       change, and leave core0 holding nothing until the peer happened to move. That is the reopened-peer-stranded-at-zero failure the
       sent-state shadows rotate to avoid, arriving through the receive side
       instead.

       `peer_storage_news_valid` clears for the same reason and lands the
       same way: the storage lane's own relation-open audit arms a whole-family
       `SYNC_STATUS` summary with ROUND_VERIFY_ALL on this rotation and forgets
       the peer's last claim with it, so a re-delivered news value costs one
       redundant classification at worst and a lost one never. This used to
       say the audit "declares every domain in hand" -- that in-hand set
       retired at D2 along with the domain identity that was its only reason to
       exist, and the audit's whole-family sweep is what carries the property
       now.

       **The counters deliberately survive.** They are free-running, and the
       DUAL-HOST era block differences them against a baseline with unsigned
       arithmetic (`current - base`). Zeroing them mid-era makes that
       subtraction wrap and reports `rt` as an enormous number -- a fabricated
       reading on the one counter whose pass value is zero. */
    era_split_communication_core_standing_state_t cleared = {0};
    cleared.exchange_count   = g_era_split_communication_core_standing_state.exchange_count;
    cleared.tx_section_count = g_era_split_communication_core_standing_state.tx_section_count;
    cleared.rx_section_count = g_era_split_communication_core_standing_state.rx_section_count;
    era_split_communication_core_standing_seq_write_begin(&g_era_split_communication_core_standing_state_seq, &odd);
    g_era_split_communication_core_standing_state = cleared;
    era_split_communication_core_standing_seq_write_end(&g_era_split_communication_core_standing_state_seq, odd);
}

uint32_t era_split_communication_core_standing_state_seq(void) {
    __DMB();
    return g_era_split_communication_core_standing_state_change_seq;
}

uint32_t era_split_communication_core_standing_exchange_count(void) {
    __DMB();
    return g_era_split_communication_core_standing_state.exchange_count;
}

bool era_split_communication_core_read_standing_state(era_split_communication_core_standing_state_t *state) {
    if (state == NULL) {
        return false;
    }
    for (uint8_t retry = 0; retry < ERA_SPLIT_COMMUNICATION_CORE_STANDING_READ_RETRIES; retry++) {
        uint32_t first = g_era_split_communication_core_standing_state_seq;
        if ((first & 1U) != 0) {
            continue;
        }
        __DMB();
        *state = g_era_split_communication_core_standing_state;
        __DMB();
        if (first == g_era_split_communication_core_standing_state_seq) {
            return true;
        }
    }
    return false;
}

/* ---- core1 ---- */

static bool era_split_communication_core_read_standing_plan(era_split_communication_core_standing_plan_t *plan) {
    for (uint8_t retry = 0; retry < ERA_SPLIT_COMMUNICATION_CORE_STANDING_READ_RETRIES; retry++) {
        uint32_t first = g_era_split_communication_core_standing_plan_seq;
        if ((first & 1U) != 0) {
            continue;
        }
        __DMB();
        *plan = g_era_split_communication_core_standing_plan;
        __DMB();
        if (first == g_era_split_communication_core_standing_plan_seq) {
            return true;
        }
    }
    return false;
}

/* `notify` is what wakes core0, and the caller passes it only when a field
 * core0 acts on has changed. The write itself always happens -- the counters
 * are diagnostics and are read under the guard sequence at print time, not
 * through the wake. */
static void era_split_communication_core_standing_publish_state(const era_split_communication_core_standing_state_t *state, bool notify) {
    uint32_t odd = 0;
    era_split_communication_core_standing_seq_write_begin(&g_era_split_communication_core_standing_state_seq, &odd);
    g_era_split_communication_core_standing_state = *state;
    era_split_communication_core_standing_seq_write_end(&g_era_split_communication_core_standing_state_seq, odd);
    if (notify) {
        g_era_split_communication_core_standing_state_change_seq++;
        __DMB();
        __SEV();
    }
}

/* D1's cross-lane fold retired with the second carrier (D3). It kept this
 * record's cache honest against a value the push lane had delivered behind its
 * back; the push lane's answer carries no section now, so this record has one
 * writer and one reader, both below. */

static bool era_split_communication_core_standing_due(uint32_t now_us) {
    if (!g_era_split_communication_core_standing_private.due_valid) {
        return true;
    }
    return (int32_t)(now_us - g_era_split_communication_core_standing_private.next_due_us) >= 0;
}

bool era_split_communication_core_standing_service_once(uint16_t owner_epoch) {
    era_split_communication_core_standing_plan_t plan;
    if (!era_split_communication_core_read_standing_plan(&plan)) {
        return false;
    }

    /* The grant's three identities, and core1 stops on any of them. Core0
     * moves the epoch on a wire-role change, bumps the generation on a
     * relation rotation, and clears the bit for storage exclusivity or a
     * pending status revalidation.
     *
     * **The bit is the odd one out and only it** (Slice 11.7): the other two
     * say this grant is not ours any more, and the bit says the cadence is
     * suspended. Liveness survives it. Clearing the bit used to hand the wire
     * back to core0 at the one moment core0 was about to vanish into a flash
     * write, and the peer's silence watch fired 100 ms into a 1499 ms apply. */
    if (plan.owner_epoch != owner_epoch || plan.relation_generation == 0 ||
        plan.plan_generation == 0 || plan.poll_period_ms == 0) {
        return false;
    }
    bool cadence_granted = plan.enabled != 0;
    /* Defensive, not a supported plan value: the single publisher either leaves
     * the record memset -- caught by the generation tests above -- or writes the
     * scheduler's constant, which a _Static_assert holds above zero. This term
     * is two instructions guarding the one field a half-formed plan could hand
     * over that the guards above would not catch, on the beat that keeps the
     * relation alive through core0's flash writes. */
    if (!cadence_granted && plan.liveness_period_ms == 0) {
        return false;
    }

    if (g_era_split_communication_core_standing_private.relation_generation != plan.relation_generation) {
        memset(&g_era_split_communication_core_standing_private, 0, sizeof(g_era_split_communication_core_standing_private));
        g_era_split_communication_core_standing_private.relation_generation = plan.relation_generation;
    }
    /* Stopped means stopped until core0 republishes. Core1 never retries a
     * failed standing exchange on its own: the failure hands the wire back to
     * core0, whose SESSION_STATUS revalidation outranks this route. Retrying
     * here would poll a doubtful relation at rate, which is exactly what route
     * priority exists to prevent.
     *
     * The clear is a plan change, not a timer and not a success. On the
     * ordinary recovery core0 raises a pending status, which drops `enabled`
     * and later returns it -- two publishes, so the generation moves twice and
     * this releases on the first. */
    if (g_era_split_communication_core_standing_private.stopped) {
        if (g_era_split_communication_core_standing_private.stopped_plan_generation == plan.plan_generation) {
            return false;
        }
        g_era_split_communication_core_standing_private.stopped                 = 0;
        g_era_split_communication_core_standing_private.stopped_plan_generation = 0;
        g_era_split_communication_core_standing_private.due_valid               = 0;
    }

    /* Latest-state and edge-armed, unchanged in rule and moved in owner: the
     * push is due while the published body differs from what this half last
     * confirmed on the wire. The shadow lives here now because core1 is what
     * confirms. Both sections follow the identical rule, which is why the
     * authority one is three lines rather than a mechanism. */
    /* Under liveness alone the frame carries nothing, and that is the contract
     * rather than an omission: storage exclusivity suppresses content, and this
     * exchange exists to prove the half is alive rather than to move state. The
     * sent-state shadows are therefore untouched, so every section stays due
     * and crosses on the first cadence poll after the episode. */
    uint8_t push_sections = 0;
    if (cadence_granted &&
        (plan.eligible_push_sections & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_INPUT_LAYER) != 0 &&
        (!g_era_split_communication_core_standing_private.sent_input_layer_valid ||
         g_era_split_communication_core_standing_private.sent_input_layer != plan.input_layer)) {
        push_sections |= ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_INPUT_LAYER;
    }
    if (cadence_granted &&
        (plan.eligible_push_sections & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_AUTHORITY) != 0 &&
        plan.authority_valid &&
        (!g_era_split_communication_core_standing_private.sent_authority_valid ||
         !era_split_wire_authority_equal(&g_era_split_communication_core_standing_private.sent_authority, &plan.authority))) {
        push_sections |= ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_AUTHORITY;
    }
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
    /* The storage-pending arm (2026-08-14 indicator redesign): one byte in
     * the never-deferring core, on the INPUT layer's exact discipline — an
     * invalid shadow forces the current value across once per relation,
     * zero included, because the receiver holds the value applied as the
     * lamp's mirror and it must be retired through the apply path rather
     * than stranded by absence. Compiled with the storage engine rather than
     * with the marker, so a split build without the sync feature keeps its
     * exact wire silence — the same shape the storage news section has on
     * the response side. */
    if (cadence_granted &&
        (plan.eligible_push_sections & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_STORAGE_PENDING) != 0 &&
        (!g_era_split_communication_core_standing_private.sent_storage_pending_valid ||
         g_era_split_communication_core_standing_private.sent_storage_pending != plan.storage_pending)) {
        push_sections |= ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_STORAGE_PENDING;
    }
#endif
    /* The restart arm, the yielding class's first claimant. Five bytes do not
     * fit beside the never-deferring core in either relation, so it yields
     * through the same projected-length check as everything below it -- and a
     * one-poll deferral is noise against a commit window of hundreds of
     * milliseconds. It claims ahead of ACTIVITY because an agreement that has
     * started is bounded by a deadline while a judgment refresh is not, and
     * because the frame it wants (arm plus AUTHORITY, 15 exactly) is the one
     * that carries the confirmation back. */
    if (cadence_granted &&
        (plan.eligible_push_sections & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_RESTART_ARM) != 0 &&
        (!g_era_split_communication_core_standing_private.sent_restart_valid ||
         g_era_split_communication_core_standing_private.sent_restart_act != plan.restart_act ||
         g_era_split_communication_core_standing_private.sent_restart_param != plan.restart_param ||
         g_era_split_communication_core_standing_private.sent_restart_commit_ms != plan.restart_commit_ms)) {
        if (era_split_wire_source_push_projected_len((uint8_t)(push_sections | ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_RESTART_ARM)) <=
            ERA_SPLIT_WIRE_COMPACT_MAX_PAYLOAD_LEN) {
            push_sections |= ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_RESTART_ARM;
        }
    }
    /* The ACTIVITY push arm (FA-2 S2), first of the yielding class: judgment
     * data with a live window behind it claims remaining budget ahead of the
     * RGB refresh below, and yields to the never-deferring sections through
     * the same projected-length check. The edge rule carries the
     * zero-baseline refinement -- an invalid shadow compares against the
     * all-zero body rather than forcing a send -- so a fresh relation on
     * fresh defaults crosses nothing, which is the silence property the
     * section was priced on. */
    if (cadence_granted &&
        (plan.eligible_push_sections & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_ACTIVITY) != 0 &&
        plan.activity_valid) {
        const era_split_wire_activity_section_t zero_baseline = {0};
        const era_split_wire_activity_section_t *last_confirmed =
            g_era_split_communication_core_standing_private.sent_activity_valid
                ? &g_era_split_communication_core_standing_private.sent_activity
                : &zero_baseline;
        if (!era_split_wire_activity_equal(last_confirmed, &plan.activity)) {
            if (era_split_wire_source_push_projected_len((uint8_t)(push_sections | ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_ACTIVITY)) <=
                ERA_SPLIT_WIRE_COMPACT_MAX_PAYLOAD_LEN) {
                push_sections |= ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_ACTIVITY;
            }
        }
    }
    /* The visual-baseline push arm (Slice 14), second of the yielding class:
     * it claims after ACTIVITY and ahead of RGB in the decided order, through
     * the same projected-length check. The edge rule is plain shadow-differs
     * -- an invalid shadow forces one RELATION_OPEN send whose all-zero
     * baseline is a receiver-side no-op by arithmetic. */
    if (cadence_granted &&
        (plan.eligible_push_sections & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_VISUAL) != 0 &&
        plan.visual_valid &&
        (!g_era_split_communication_core_standing_private.sent_visual_valid ||
         !era_split_communication_core_standing_baseline_equal(g_era_split_communication_core_standing_private.sent_visual,
                                                               plan.visual_baseline))) {
        if (era_split_wire_source_push_projected_len((uint8_t)(push_sections | ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_VISUAL)) <=
            ERA_SPLIT_WIRE_COMPACT_MAX_PAYLOAD_LEN) {
            push_sections |= ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_VISUAL;
        }
    }
    /* The RGB push arm (Slice 12), on the identical edge rule plus the one
     * thing the never-deferring arms do not need: a room check. RGB is a
     * multi-byte refresh and yields to the never-deferring sections, to a
     * claimed ACTIVITY, and to a claimed visual baseline by remaining budget
     * -- 3 + INPUT + AUTHORITY + RGB is 18 against 15 -- so a poll that
     * carries a due AUTHORITY defers RGB one poll. The shadow only advances
     * on a confirmed send, so the deferred section stays due and drains on
     * the first poll the earlier claims' edge-armed retirement leaves room. */
    if (cadence_granted &&
        (plan.eligible_push_sections & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_RGB_STATE) != 0 &&
        plan.rgb_valid &&
        (!g_era_split_communication_core_standing_private.sent_rgb_valid ||
         !era_split_communication_core_standing_rgb_equal(&g_era_split_communication_core_standing_private.sent_rgb, &plan.rgb))) {
        if (era_split_wire_source_push_projected_len((uint8_t)(push_sections | ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_RGB_STATE)) <=
            ERA_SPLIT_WIRE_COMPACT_MAX_PAYLOAD_LEN) {
            push_sections |= ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_RGB_STATE;
        }
    }

    /* One deadline, two periods, and the arming mode travels with it so an
     * exclusivity edge does not read as a missed or an early beat. */
    uint32_t period_ms = cadence_granted ? plan.poll_period_ms : plan.liveness_period_ms;
    if (g_era_split_communication_core_standing_private.due_liveness != (cadence_granted ? 0U : 1U)) {
        g_era_split_communication_core_standing_private.due_liveness = cadence_granted ? 0U : 1U;
        g_era_split_communication_core_standing_private.due_valid    = 0;
    }

    uint32_t now_us = timer_hw->timerawl;
    if (push_sections == 0 && !era_split_communication_core_standing_due(now_us)) {
        /* Nothing to do this pass, and the loop is about to park on WFE with
           no core0 event coming -- a period is this half's own deadline, not
           the peer's and not core0's. Arm the backend's core1 alarm so the
           park ends at the deadline instead of at the next unrelated wake. */
        era_split_transaction_backend_arm_core1_idle_wake(g_era_split_communication_core_standing_private.next_due_us);
        return false;
    }

    uint8_t payload[ERA_SPLIT_WIRE_MAX_PAYLOAD_LEN];
    uint8_t payload_len = 0;
    uint8_t tx_seq      = 0;
    if (!era_split_transaction_engine_prepare_control(push_sections != 0, &payload[0], &tx_seq)) {
        return false;
    }
    if (push_sections != 0) {
        /* Bodies append in ascending marker-bit order, the same rule both
           directions follow. */
        payload[0]  = (uint8_t)(payload[0] | ERA_SPLIT_WIRE_CONTROL_EXT);
        payload[1]  = ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH;
        payload[2]  = push_sections;
        payload_len = 3;
        if ((push_sections & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_INPUT_LAYER) != 0) {
            payload[payload_len++] = plan.input_layer;
        }
        if ((push_sections & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_AUTHORITY) != 0) {
            era_split_wire_encode_authority_body(&plan.authority, &payload[payload_len]);
            payload_len = (uint8_t)(payload_len + ERA_SPLIT_WIRE_AUTHORITY_BYTES);
        }
        if ((push_sections & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_RGB_STATE) != 0) {
            (void)era_host_peer_transaction_encode_rgb_state_body(&plan.rgb, &payload[payload_len]);
            payload_len = (uint8_t)(payload_len + ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RGB_STATE_BYTES);
        }
        /* Ascending marker order puts ACTIVITY (0x10) after RGB (0x08) on the
           wire; its claim ran first, which is the priority. The plan's, not
           the serializer's. */
        if ((push_sections & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_ACTIVITY) != 0) {
            era_split_wire_encode_activity_body(&plan.activity, &payload[payload_len]);
            payload_len = (uint8_t)(payload_len + ERA_SPLIT_WIRE_ACTIVITY_BYTES);
        }
        if ((push_sections & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_VISUAL) != 0) {
            payload[payload_len++] = g_era_split_communication_core_standing_private.sent_visual_valid
                                         ? ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_REASON_RENDER_RESET
                                         : ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_REASON_RELATION_OPEN;
            memcpy(&payload[payload_len], plan.visual_baseline, ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_BASELINE_BYTES);
            payload_len = (uint8_t)(payload_len + ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_BASELINE_BYTES);
        }
        if ((push_sections & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_STORAGE_PENDING) != 0) {
            payload[payload_len++] = (uint8_t)(plan.storage_pending & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_STORAGE_PENDING_FLAG_MASK);
        }
        if ((push_sections & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_RESTART_ARM) != 0) {
            payload[payload_len++] = (uint8_t)((plan.restart_param & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RESTART_PARAM_MASK) |
                                               ((uint8_t)(plan.restart_act << ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RESTART_ACT_SHIFT) &
                                                ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RESTART_ACT_MASK));
            era_split_wire_put32(&payload[payload_len], plan.restart_commit_ms);
            payload_len = (uint8_t)(payload_len + 4U);
        }
    } else {
        payload_len = 1;
    }

    era_split_wire_frame_t response;
    memset(&response, 0, sizeof(response));
    /* `failure` is written and not read here: the stop latch keys on the result
       code alone, and the engine has already folded the failure class into its
       own counters by the time this returns. It stays because the engine
       refuses a NULL one; `request_sent` did not have to, so it does not. */
    era_split_transaction_failure_t failure = ERA_SPLIT_TRANSACTION_FAILURE_NONE;
#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
    era_split_transaction_engine_timing_begin_route((uint8_t)ERA_SPLIT_ROUTE_HOST_PEER_HEARTBEAT,
                                                    push_sections != 0 ? (uint8_t)ERA_SPLIT_ROUTE_REASON_RUNTIME_SECTION_PUSH
                                                                       : (uint8_t)ERA_SPLIT_ROUTE_REASON_RUNTIME_RESPONSE_POLL);
#endif
    era_split_transaction_engine_result_t result =
        era_split_transaction_engine_transact_compact_owned(ERA_SPLIT_WIRE_DIRECTION_PRIMARY_TO_SECONDARY,
                                                            payload,
                                                            payload_len,
                                                            tx_seq,
                                                            ERA_SPLIT_WIRE_PAYLOAD_GRANT_ACK,
                                                            ERA_SPLIT_WIRE_PAYLOAD_HOST_PEER_HOST_SOURCE_RSP,
                                                            ERA_SPLIT_PEER_RESPONSE_WINDOW_MS,
                                                            owner_epoch,
                                                            &response,
                                                            NULL,
                                                            &failure);
#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
    era_split_transaction_engine_timing_end_route();
#endif

    era_split_communication_core_standing_state_t state = g_era_split_communication_core_standing_state;
    state.owner_epoch         = owner_epoch;
    state.relation_generation = plan.relation_generation;

    if (result != ERA_SPLIT_TRANSACTION_RESULT_OK) {
        g_era_split_communication_core_standing_private.stopped                 = 1;
        g_era_split_communication_core_standing_private.stopped_plan_generation = plan.plan_generation;
        bool notify                                                             = state.stopped == 0;
        state.stopped                                                           = 1;
        era_split_communication_core_standing_publish_state(&state, notify);
        return true;
    }

    /* The wake is edge-driven from here down. `stopped` falling is news, a new
       peer layer value is news, and an exchange that changed neither is not --
       that last case is the ordinary poll, and it must cost core0 nothing. */
    bool notify   = state.stopped != 0;
    state.stopped = 0;
    state.exchange_count++;
    g_era_split_communication_core_standing_private.due_valid   = 1;
    g_era_split_communication_core_standing_private.next_due_us = now_us + (uint32_t)period_ms * 1000U;
    /* `rt` counts runtime *sections*, never polls, so a frame carrying two
       sections counts two. That is what keeps the steady-state legs readable
       under a constant cadence. */
    if ((push_sections & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_INPUT_LAYER) != 0) {
        g_era_split_communication_core_standing_private.sent_input_layer       = plan.input_layer;
        g_era_split_communication_core_standing_private.sent_input_layer_valid = 1;
        state.tx_section_count++;
    }
    if ((push_sections & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_STORAGE_PENDING) != 0) {
        g_era_split_communication_core_standing_private.sent_storage_pending       = plan.storage_pending;
        g_era_split_communication_core_standing_private.sent_storage_pending_valid = 1;
        state.tx_section_count++;
    }
    if ((push_sections & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_AUTHORITY) != 0) {
        g_era_split_communication_core_standing_private.sent_authority       = plan.authority;
        g_era_split_communication_core_standing_private.sent_authority_valid = 1;
        state.tx_section_count++;
    }
    if ((push_sections & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_RGB_STATE) != 0) {
        g_era_split_communication_core_standing_private.sent_rgb       = plan.rgb;
        g_era_split_communication_core_standing_private.sent_rgb_valid = 1;
        state.tx_section_count++;
    }
    if ((push_sections & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_ACTIVITY) != 0) {
        g_era_split_communication_core_standing_private.sent_activity       = plan.activity;
        g_era_split_communication_core_standing_private.sent_activity_valid = 1;
        state.tx_section_count++;
    }
    if ((push_sections & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_RESTART_ARM) != 0) {
        g_era_split_communication_core_standing_private.sent_restart_act       = plan.restart_act;
        g_era_split_communication_core_standing_private.sent_restart_param     = plan.restart_param;
        g_era_split_communication_core_standing_private.sent_restart_commit_ms = plan.restart_commit_ms;
        g_era_split_communication_core_standing_private.sent_restart_valid     = 1;
        state.tx_section_count++;
    }
    if ((push_sections & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_VISUAL) != 0) {
        memcpy(g_era_split_communication_core_standing_private.sent_visual, plan.visual_baseline,
               ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_BASELINE_BYTES);
        g_era_split_communication_core_standing_private.sent_visual_valid = 1;
        state.tx_section_count++;
    }

    /* The accept-clip, core1's half. Core0 re-asks the same question against
     * its own live relation on apply, and both are required: the value shifts
     * which keycode the receiving half resolves, which is not a place to rely
     * on one check. */
    era_host_peer_transaction_result_t decoded;
    (void)era_host_peer_transaction_extract_sections(&response, &decoded);
    if (decoded.host_source_input_layer_valid &&
        (plan.eligible_rsp_sections & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_INPUT_LAYER) != 0 &&
        (!state.peer_input_layer_valid || state.peer_input_layer != decoded.host_source_input_layer)) {
        state.peer_input_layer       = decoded.host_source_input_layer;
        state.peer_input_layer_valid = 1;
        state.rx_section_count++;
        notify                       = true;
    }
    /* The edge that decides whether this lane works at all. An authority
       record equal to the last one delivered is the ordinary quiet poll and
       must cost core0 nothing; only a differing one is news. */
    if (decoded.host_source_authority_valid &&
        (plan.eligible_rsp_sections & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_AUTHORITY) != 0 &&
        (!state.peer_authority_valid || !era_split_wire_authority_equal(&state.peer_authority, &decoded.host_source_authority))) {
        state.peer_authority       = decoded.host_source_authority;
        state.peer_authority_valid = 1;
        state.rx_section_count++;
        notify                     = true;
    }
    /* The storage hint, on the identical edge (Slice 11.7). **The all-clear is
       observable here since D1**, and what stood in this comment claimed the
       opposite as a property: the responder omitted the section when it had
       nothing settled and the decoder refused a zero body, so a fall reached
       this cache as an *absence*, which a cache cannot represent. The edge then
       filtered nothing away and left core0 re-delivering a byte that could not
       come down -- device-measured on both halves of a DUAL-HOST pair,
       2026-08-09. The sender is truthful now (era_host_peer_responder.c), so
       zero arrives as a value and this arm carries the fall like any other
       transition.

       The edge stays and is load-bearing: the responder answers many polls from
       one published snapshot, so an unchanged section crosses on consecutive
       polls, and reporting every arrival would wake this half's core0 at the
       poll rate -- 1000/s in DUAL-HOST. What changed is that a truthful sender
       makes the filtering lossless rather than lossy. */
    if (decoded.host_source_storage_news_valid &&
        (plan.eligible_rsp_sections & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_STORAGE_NEWS) != 0 &&
        (!state.peer_storage_news_valid || state.peer_storage_news != decoded.host_source_storage_news)) {
        state.peer_storage_news       = decoded.host_source_storage_news;
        state.peer_storage_news_valid = 1;
        state.rx_section_count++;
        notify                              = true;
    }
    /* The remaining response sections (R2), each on the identical edge rule and
       each behind this relation's own eligibility. The edge is not an
       optimisation on any of them: the responder answers from one published
       snapshot until its own core0 drains the result and retires the sent-state,
       so an unchanged section can cross on many consecutive polls, and reporting
       every arrival would wake this half's core0 at the poll rate for a value
       that has not moved -- the failure the AUTHORITY section is commented
       against, arriving through four more doors. */
    if (decoded.host_source_lock_state_valid &&
        (plan.eligible_rsp_sections & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_LOCK_STATE) != 0 &&
        (!state.peer_lock_state_valid || state.peer_lock_state != decoded.host_source_lock_state)) {
        state.peer_lock_state       = decoded.host_source_lock_state;
        state.peer_lock_state_valid = 1;
        state.rx_section_count++;
        notify                      = true;
    }
    /* The one section whose apply is an event, so its edge also carries a
       sequence: core0 replays a pressed baseline only for a snapshot it has not
       already applied. Comparing reason *and* baseline is what makes the same
       bits with a different reason still count as news -- a RENDER_RESET
       followed by a TICK_GAP is the receiver being asked to re-fire, not a
       repeat. */
    if (decoded.host_source_visual_snapshot_valid &&
        (plan.eligible_rsp_sections & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_VISUAL_RESYNC) != 0 &&
        (!state.peer_visual_valid ||
         !era_split_communication_core_standing_visual_equal(&state.peer_visual_snapshot, &decoded.host_source_visual_snapshot))) {
        state.peer_visual_snapshot = decoded.host_source_visual_snapshot;
        state.peer_visual_valid    = 1;
        state.peer_visual_seq++;
        state.rx_section_count++;
        notify                     = true;
    }
    if (decoded.host_source_rgb_state_valid &&
        (plan.eligible_rsp_sections & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_RGB_STATE) != 0 &&
        (!state.peer_rgb_state_valid ||
         !era_split_communication_core_standing_rgb_equal(&state.peer_rgb_state, &decoded.host_source_rgb_state))) {
        state.peer_rgb_state       = decoded.host_source_rgb_state;
        state.peer_rgb_state_valid = 1;
        state.rx_section_count++;
        notify                     = true;
    }
    /* The anchor stores its own receive instant beside it, which is what turns a
       timestamp into a level (see the record's comment). The read is taken here
       rather than reused from the pass's `now_us` above, because that one was
       taken before the exchange and the exchange is the part with the latency
       worth counting. */
    if (decoded.host_source_time_anchor_valid &&
        (plan.eligible_rsp_sections & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_TIME_ANCHOR) != 0 &&
        (!state.peer_time_anchor_valid || state.peer_time_anchor_ms != decoded.host_source_time_anchor_ms)) {
        state.peer_time_anchor_ms    = decoded.host_source_time_anchor_ms;
        state.peer_time_anchor_rx_us = timer_hw->timerawl;
        state.peer_time_anchor_valid = 1;
        state.rx_section_count++;
        notify                       = true;
    }
    /* The responder's tap-hold activity (FA-2 S2), on the identical edge. Its
       apply is a cache overwrite and idempotent, so it shares the others'
       latched-valid discipline rather than needing a sequence. */
    if (decoded.host_source_activity_valid &&
        (plan.eligible_rsp_sections & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_ACTIVITY) != 0 &&
        (!state.peer_activity_valid ||
         !era_split_wire_activity_equal(&state.peer_activity, &decoded.host_source_activity))) {
        state.peer_activity       = decoded.host_source_activity;
        state.peer_activity_valid = 1;
        state.rx_section_count++;
        notify                    = true;
    }
    era_split_transaction_engine_commit_received_frame(&response);
    era_split_communication_core_standing_publish_state(&state, notify);
    return true;
}
