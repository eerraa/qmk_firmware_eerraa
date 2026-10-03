// SPDX-License-Identifier: GPL-2.0-or-later
// Appended to the production launch/owner fixture by cancellation_unit().
#define ERA_SPLIT_COMMUNICATION_CORE_STATE_TIMEOUT_US 30000
#define ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_INVALID 255
#define ERA_SPLIT_SCHEDULER_DIRTY_SYNC_POLICY 2
#define ERA_SPLIT_SCHEDULER_DIRTY_MATRIX_READY 16
#define ERA_SPLIT_SCHEDULER_MAINT_DUE_AUTHORITY_SAMPLE 1
#define ERA_SPLIT_SCHEDULER_MAINT(source, expression) (expression)

static bool era_split_transport_scheduler_local_status_revalidation_due(void) { return true; }
/* MATRIX_READY_FUNCTIONS */

static struct {
    unsigned stop_count, stop_requested, stop_timeout;
    uint8_t launched, running;
    unsigned queue_high_water, queue_result_high_water, queue_generation, queue_flush_count;
} g_era_split_communication_core;
static unsigned g_era_split_communication_core_request_read, g_era_split_communication_core_request_write;
static unsigned g_era_split_communication_core_result_read, g_era_split_communication_core_result_write;
static void era_split_communication_core_init(void) {}
static bool era_split_communication_core_wait_state(volatile uint8_t *p, uint8_t value, unsigned timeout) {
    (void)timeout;
    *p = value;
    return true;
}
static void era_split_communication_core_note_stop_timeout(void) { assert(false); }
static void era_split_communication_core_initiator_queue_flushed(void) {}
static bool era_split_communication_core_initiator_pending(void) { return false; }
static bool era_split_communication_core_initiator_result_ready(void) { return false; }
/* CANCELLATION_FUNCTIONS */

// USB/session sampling and the mode planner supply these settled-policy facts.
// They deliberately do not infer policy from the owner that cancellation drops.
static bool policy_available, policy_initiator, policy_authority_changed, policy_mode_changed;
static bool era_split_transport_scheduler_update_mode(bool repair_wire) {
    bool authority_changed = policy_authority_changed;
    bool wire_role_changed = g_era_split_transport_scheduler.local_wire_available != policy_available ||
                             g_era_split_transport_scheduler.local_wire_initiator != policy_initiator;
    bool next_local_wire_available = policy_available;
    bool next_local_wire_initiator = policy_initiator;
    struct { bool mode_changed; } result = {policy_mode_changed};
    unsigned dirty_flags = ERA_SPLIT_SCHEDULER_DIRTY_MATRIX_READY;
    /* POLICY_REPAIR */
    g_era_split_transport_scheduler.local_wire_available = policy_available;
    g_era_split_transport_scheduler.local_wire_initiator = policy_initiator;
    return true;
}
static uint8_t era_split_transport_scheduler_read_pending_dirty_flags(void) { return dirty; }
static uint8_t era_split_transport_scheduler_consume_pending_dirty_flags(void) {
    uint8_t flags = dirty;
    dirty = 0;
    return flags;
}
static uint8_t era_split_transport_scheduler_consume_maintenance_due_flags(void) { return 0; }
static bool era_split_transport_scheduler_poll_core1_initiator(void) { return false; }
static bool era_split_transport_scheduler_refresh_time_due_tokens(uint32_t now) { (void)now; return false; }
static void era_split_transport_scheduler_note_sync_policy_edge(void) {}
static bool run_cancellation_housekeeping(void) {
    bool maintenance_performed = false, authority_changed = false;
    uint32_t now_ms = 0;
    /* HOUSEKEEPING_CANCELLATION */
    (void)maintenance_performed;
    (void)authority_changed;
    return true;
}

static void prepare_cancellation(bool physical_left, bool initiator) {
    fail_revoke_wait = false;
    reset_boot(physical_left);
    policy_available = true;
    policy_initiator = physical_left;
    policy_authority_changed = policy_mode_changed = false;
    memset(&g_era_split_communication_core, 0, sizeof g_era_split_communication_core);
    keyboard_init();
    // Both HOST orientations eventually assign the PEER an initiator lease.
    assert(era_split_transport_scheduler_reset_serial_for_transport_role(true, initiator, false));
    g_era_split_transport_scheduler.local_wire_initiator = initiator;
    g_era_split_transport_scheduler.route_due_flags = initiator ? ERA_SPLIT_SCHEDULER_ROUTE_DUE_ATTACH_STATUS : 0;
    policy_initiator = initiator;
    dirty = 0;
}

int main(void) {
    // A no-USB Left's first Bootmagic scan publishes readiness before the
    // explicit post-init launch. The dirty arm survives keyboard_init(), then
    // the first ordinary matrix scan can submit discovery before housekeeping.
    // Local DMA readiness and request enqueue are simulated; the transport
    // ordering, readiness arm, Bootmagic and post-init/owner functions are real.
    simulate_boot_matrix_ready = true;
    fixture_matrix_ready = false;
    reset_boot(true);
    policy_available = policy_initiator = true;
    keyboard_init();
    assert(early_starts == 0 && starts == 1 && requests == 0);
    assert(dirty == ERA_SPLIT_SCHEDULER_DIRTY_MATRIX_READY);
    matrix_scan();
    assert(requests == 1 && g_era_split_transport_scheduler.core1_initiator_async_pending);
    assert(run_cancellation_housekeeping());
    bool cancellation_restored_current_initiator = era_split_communication_core_owner_core1_role_is_live(ERA_SPLIT_TRANSACTION_BACKEND_ROLE_INITIATOR);
    assert(cancellation_restored_current_initiator);
    simulate_boot_matrix_ready = false;

    const unsigned non_role_dirty[] = {ERA_SPLIT_SCHEDULER_DIRTY_MATRIX_READY, ERA_SPLIT_SCHEDULER_DIRTY_SYNC_POLICY};
    for (unsigned side = 0; side < 2; side++) {
        for (unsigned d = 0; d < sizeof(non_role_dirty) / sizeof(non_role_dirty[0]); d++) {
            prepare_cancellation(side == 0, true);
            dirty = non_role_dirty[d];
            g_era_split_transport_scheduler.core1_initiator_async_pending = true;
            unsigned flushes = g_era_split_communication_core.queue_flush_count;
            assert(run_cancellation_housekeeping());
            assert(g_era_split_communication_core.queue_flush_count == flushes + 1);
            assert(!g_era_split_transport_scheduler.core1_initiator_async_pending);
            bool cancellation_restored_current_initiator = era_split_communication_core_owner_core1_role_is_live(ERA_SPLIT_TRANSACTION_BACKEND_ROLE_INITIATOR);
            assert(cancellation_restored_current_initiator);
            assert(era_split_communication_core_owner_ensure_core1());
            unsigned sent = requests;
            matrix_scan();
            assert(requests == sent + 1);
        }
        // A HOST responder has no generic initiator request to cancel.
        prepare_cancellation(side == 0, false);
        dirty = ERA_SPLIT_SCHEDULER_DIRTY_MATRIX_READY;
        assert(run_cancellation_housekeeping());
        assert(g_era_split_communication_core.queue_flush_count == 0);
        assert(era_split_communication_core_owner_core1_role_is_live(ERA_SPLIT_TRANSACTION_BACKEND_ROLE_RESPONDER));
        assert(!era_split_communication_core_owner_ensure_core1());
    }

    // No dirty fact: leave an in-flight request and its lease alone.
    prepare_cancellation(true, true);
    g_era_split_transport_scheduler.core1_initiator_async_pending = true;
    assert(run_cancellation_housekeeping());
    assert(g_era_split_transport_scheduler.core1_initiator_async_pending);
    assert(g_era_split_communication_core.queue_flush_count == 0);

    // Failed revoke keeps the original dirty fact and retries through policy;
    // request admission must not revive the revoked owner in between.
    dirty = ERA_SPLIT_SCHEDULER_DIRTY_MATRIX_READY;
    fail_revoke_wait = true;
    assert(!run_cancellation_housekeeping());
    assert(dirty == ERA_SPLIT_SCHEDULER_DIRTY_MATRIX_READY);
    assert(g_era_split_transport_scheduler.core1_initiator_async_pending);
    assert(!era_split_communication_core_owner_ensure_core1());
    fail_revoke_wait = false;
    assert(run_cancellation_housekeeping());
    assert(era_split_communication_core_owner_ensure_core1());

    // Cancellation repairs the newly chosen policy, never the old role.
    prepare_cancellation(true, true);
    dirty = ERA_SPLIT_SCHEDULER_DIRTY_SYNC_POLICY;
    g_era_split_transport_scheduler.core1_initiator_async_pending = true;
    policy_initiator = false;
    policy_mode_changed = true;
    assert(run_cancellation_housekeeping());
    assert(era_split_communication_core_owner_core1_role_is_live(ERA_SPLIT_TRANSACTION_BACKEND_ROLE_RESPONDER));
    assert(!era_split_communication_core_owner_ensure_core1());

    // A policy that closes the wire leaves NONE; the repair arm is not an
    // unconditional initiator grant after quiesce.
    prepare_cancellation(true, true);
    dirty = ERA_SPLIT_SCHEDULER_DIRTY_SYNC_POLICY;
    g_era_split_transport_scheduler.core1_initiator_async_pending = true;
    policy_available = policy_initiator = false;
    assert(run_cancellation_housekeeping());
    assert(era_split_communication_core_owner_word_owner(g_era_split_communication_core_owner.lease_word) == ERA_SPLIT_COMMUNICATION_CORE_BACKEND_OWNER_NONE);
    assert(!era_split_communication_core_owner_ensure_core1());
}
