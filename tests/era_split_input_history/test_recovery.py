#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual standing/responder/input/codec and QMK projection; simulated wire/OS.

The HOST reset's receiver gate/owner and matrix-flush effects are injected.
This is not a complete scheduler, USB, PIO or physical-timing test.
"""
import re
import unittest
import test_production as production
from test_production import ROOT, HERE, run_c


def function(path, name):
    text = (ROOT / path).read_text(encoding='utf-8')
    text = re.sub(r'__attribute__\(\(always_inline\)\)\s*', '', text)
    match = re.search(r'^[ \t]*(?:(?:static|inline)\s+)*\w+[ \t*]+' + name + r'\([^;{}]*\)\s*\{', text, re.M)
    if not match:
        raise AssertionError(name)
    depth = 1
    for end in range(match.end(), len(text)):
        depth += (text[end] == '{') - (text[end] == '}')
        if not depth:
            return text[match.start():end + 1] + '\n'
    raise AssertionError(name)


def unit():
    c = (HERE / 'recovery.c').read_text(encoding='utf-8')
    # Keep the established geometry/codec helpers, including real restart validators.
    c = c.replace('/* INPUT_OWNERS */', '#define main input_owner_tests\n' +
                  production.InputProduction().owners_unit('input_history.c') + '\n#undef main\n')
    engine = 'keyboards/era/common/system/era_rp2040_matrix_core.c'
    source = (ROOT / engine).read_text(encoding='utf-8')
    matrix = '#include "keyboards/era/common/system/era_matrix_engine.h"\n'
    matrix += source[source.index('typedef struct {'):source.index('#ifdef MATRIX_MASKED')]
    for name in ['next_seq8', 'rows_equal', 'rows_copy', 'rows_any', 'rows_clear',
                 'apply_peer_rows', 'clear_peer_rows', 'accept_peer_snapshot',
                 'peer_cache_dirty', 'copy_peer_matrix_if_needed', 'sync_peer_projection',
                 'peer_closure_pending', 'request_peer_closure', 'flush_host_peer_relation']:
        matrix += function(engine, 'era_matrix_engine_' + name)
    c = c.replace('/* MATRIX_ENGINE */', matrix)
    for name in ['era_split_input_action_begin', 'era_split_input_action_end']:
        c += function('keyboards/era/common/split/era_split_input_qmk.c', name)
    c += function('keyboards/era/common/split/era_host_peer_response.c', 'era_host_peer_transaction_extract_sections')
    c += function('keyboards/era/common/split/era_split_transaction_engine.c', 'era_split_transaction_engine_response_kind_matches')
    return c


class Recovery(unittest.TestCase):
    def test_standing_to_action_convergence(self):
        run_c(unit(), ['-I' + str(ROOT / 'tests/era_split_standing_lifecycle/include')])

    def test_receiver_ready_before_first_advertisement(self):
        before, marker, after = unit().rpartition('int main(void) {')
        self.assertTrue(marker)
        c = before + 'int recovery_owner_tests(void) {' + after
        c += PUBLICATION_STUBS
        publisher = function('keyboards/era/common/split/scheduler/era_split_transport_scheduler_responder.c',
                             'era_split_transport_scheduler_publish_communication_core_responder_snapshot')
        c += publisher.replace('era_split_communication_core_publish_responder_snapshot(&snapshot)',
                               'observe_publication(&snapshot)')
        run_c(c + PUBLICATION_CASES, ['-I' + str(ROOT / 'tests/era_split_standing_lifecycle/include')])


PUBLICATION_STUBS = r'''
#include "keyboards/era/common/split/era_split_mode_planner.h"
#define ERA_HOST_PEER_STORAGE_V1_ENABLE
static struct {
    bool initialized, responder_snapshot_publish_due, next_scheduler_deadline_valid;
    uint16_t core1_initiator_relation_generation;
    era_split_mode_t mode;
} g_era_split_transport_scheduler;
static bool exclusive, admitted = true;
uint16_t era_split_communication_core_owner_epoch(void) { return host_owner; }
static bool era_split_transport_scheduler_communication_core_responder_owned(void) { return true; }
static bool era_split_scheduler_session_build_local_status(bool primary, era_split_wire_session_status_t *status) {
    (void)primary; status->accepted_host_open = true; return true;
}
static bool era_host_peer_storage_route_exclusive(void) { return exclusive; }
static bool era_split_scheduler_session_host_peer_host_matrix_admitted(void) { return admitted; }
static void era_split_transport_scheduler_snapshot_response_plan(era_split_communication_core_responder_snapshot_t *s) { (void)s; }
static bool observe_publication(const era_split_communication_core_responder_snapshot_t *snapshot) {
    // Core1 may observe the new advertisement as soon as this call publishes.
    assert((receiver_gate(snapshot->owner_epoch, snapshot->relation_generation) != 0) ==
           (snapshot->host_matrix_admitted != 0));
    return era_split_communication_core_publish_responder_snapshot(snapshot);
}
'''

PUBLICATION_CASES = r'''
int main(void) {
    clear_owners(); test_core = 0;
    era_split_input_receiver_gate(false, 0, 0);
    g_era_split_transport_scheduler.initialized = true;
    g_era_split_transport_scheduler.mode = ERA_SPLIT_MODE_HOST_PEER_HOST;
    g_era_split_transport_scheduler.core1_initiator_relation_generation = 9;
    // First FENCE must work before the next scan has ever run the gate update.
    assert(era_split_transport_scheduler_publish_communication_core_responder_snapshot());
    assert(input() && source.phase == SOURCE_BIND);
    // A new owner is ready before publication too, including a refused publish.
    test_core = 0; host_owner = 8;
    g_era_split_transport_scheduler.core1_initiator_relation_generation = host_relation = 10;
    g_era_split_communication_core_responder_snapshot_claim_generation = 1;
    assert(!era_split_transport_scheduler_publish_communication_core_responder_snapshot());
    assert(g_era_split_transport_scheduler.responder_snapshot_publish_due);
    assert(!receiver_gate(7,9));
    uint32_t gate = receiver.gate_seq;
    g_era_split_communication_core_responder_snapshot_claim_generation = 0;
    assert(era_split_transport_scheduler_publish_communication_core_responder_snapshot());
    assert(receiver.gate_seq == gate); // retry is not another incarnation
    assert(era_split_transport_scheduler_publish_communication_core_responder_snapshot());
    assert(receiver.gate_seq == gate); // unchanged publications are inert
    exclusive = true;
    assert(era_split_transport_scheduler_publish_communication_core_responder_snapshot());
    assert(!receiver_gate(8,10));
    exclusive = false;
    assert(era_split_transport_scheduler_publish_communication_core_responder_snapshot());
    assert(receiver_gate(8,10));
    admitted = false;
    assert(era_split_transport_scheduler_publish_communication_core_responder_snapshot());
    assert(!receiver_gate(8,10));
    admitted = true; g_era_split_transport_scheduler.mode = ERA_SPLIT_MODE_DUAL_HOST_RIGHT;
    assert(era_split_transport_scheduler_publish_communication_core_responder_snapshot());
    assert(!receiver_gate(8,10));
    puts("PASS receiver publication ordering: first FENCE before scan, owner recovery, failed publish retry, storage/authority/DH closure");
    return 0;
}
'''


if __name__ == '__main__':
    unittest.main(verbosity=2)
