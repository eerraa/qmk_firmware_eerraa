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

if __name__ == '__main__':
    unittest.main(verbosity=2)
