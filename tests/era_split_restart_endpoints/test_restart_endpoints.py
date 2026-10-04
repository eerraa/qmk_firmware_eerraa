#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Two real agreement owners through real standing/responder publication.

Only the clock, hardware/transaction transport and final act are simulated.
The scheduler restart apply branch and complete response-shadow commit caller
are copied verbatim, keeping its eligibility and receipt APIs in this fixture.
"""
from pathlib import Path
import re
import shutil
import signal
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
SPLIT = 'keyboards/era/common/split/'
CORE = SPLIT + 'communication_core/'


def clean(text):
    return re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)


def block(text, start):
    opening = text.index('{', start)
    depth = 1
    for end in range(opening + 1, len(text)):
        depth += (text[end] == '{') - (text[end] == '}')
        if not depth:
            return text[start:end + 1]
    raise RuntimeError('Unclosed production block')


def function(text, name):
    text = clean(text)
    match = re.search(r'^\s*(?:static\s+)?[A-Za-z_]\w*[\s*]+' + name + r'\([^;{}]*\)\s*\{', text, re.M)
    if not match:
        raise RuntimeError('Missing production function ' + name)
    return block(text, match.start())


def change(root, path, before, after):
    file = root / path
    source = file.read_text(encoding='utf-8')
    if source.count(before) != 1:
        raise RuntimeError('Mutation seam moved: ' + before)
    file.write_text(source.replace(before, after), encoding='utf-8')


def run(mutation=None, case=None):
    with tempfile.TemporaryDirectory(prefix='era-restart-endpoints-') as directory:
        dest = Path(directory)
        # Scratch snapshot only; immutable root runner input is never modified.
        shutil.copytree(ROOT / 'keyboards/era/common', dest / 'keyboards/era/common')
        for name, text in {
            'matrix.h': '#pragma once\n#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n#define MATRIX_ROWS 12\n#define MATRIX_ROWS_PER_HAND 6\n#define MATRIX_COLS 11\ntypedef uint16_t matrix_row_t;\n',
            'qmk_stub.h': '#pragma once\n#include <stdint.h>\n#include <stdbool.h>\nvoid soft_reset_keyboard(void);\n',
            'timer.h': '#pragma once\n#include <stdint.h>\nuint32_t timer_read32(void);\nuint32_t timer_elapsed32(uint32_t);\n',
            'sync_timer.h': '#pragma once\n#include <stdint.h>\nuint32_t sync_timer_read32(void);\n',
            'atomic_util.h': '#pragma once\n#define ATOMIC_BLOCK_RESTORESTATE for (int atomic_once=1; atomic_once; atomic_once=0)\n',
        }.items():
            (dest / name).write_text(text, encoding='utf-8')
        if mutation == 'retirement':
            # Remove timeout retirement and the same-tuple successor fence
            # which would otherwise also catch a retained timed-out tuple.
            change(dest, SPLIT + 'era_split_restart_agreement.c',
                   'era_split_restart_agreement_retire_arm();\n            g_era_split_restart_agreement.request_live = false;',
                   'g_era_split_restart_agreement.arm_live = false;\n            g_era_split_restart_agreement.request_live = false;')
        elif mutation == 'snapshot_fence':
            change(dest, CORE + 'era_split_communication_core_responder_service.c',
                   'if (restart_fenced) {', 'if (false && restart_fenced) {')
        elif mutation == 'equal_refresh':
            change(dest, CORE + 'era_split_communication_core_responder.c',
                   'if (!g_era_split_communication_core_responder_refresh_due &&\n        g_era_split_communication_core.responder_snapshot_valid &&',
                   'if (g_era_split_communication_core.responder_snapshot_valid &&')
        elif mutation == 'authority_reassert':
            change(dest, SPLIT + 'scheduler/era_split_transport_scheduler_responder.c',
                   'era_host_peer_transaction_force_responder_authority_response();', '(void)0;')
        elif mutation == 'confirmed_finality':
            source = (dest / (SPLIT + 'era_split_restart_agreement.c')).read_text(encoding='utf-8')
            start = source.index('    if (agreed) {', source.index('static void era_split_restart_agreement_arm_commit('))
            change(dest, SPLIT + 'era_split_restart_agreement.c', block(source, start), '')
        if mutation in ('retirement', 'same_tuple'):
            source = (dest / (SPLIT + 'era_split_restart_agreement.c')).read_text(encoding='utf-8')
            start = source.index('    if (target_act == g_era_split_restart_agreement.arm_act &&')
            change(dest, SPLIT + 'era_split_restart_agreement.c', block(source, start), '')
        header = (dest / (SPLIT + 'era_split_restart_agreement.h')).read_text(encoding='utf-8')
        declarations = re.findall(r'\b(?:bool|void|era_split_restart_result_t)\s+era_split_restart_\w+\s*\([^;]*?\);', clean(header))
        names = sorted(set(re.search(r'\b(era_split_restart_\w+)\s*\(', decl).group(1) for decl in declarations) -
                       {'era_split_restart_prepare_local', 'era_split_restart_arm_ready'})
        includes = [dest, ROOT / 'tmk_core/protocol', ROOT / 'tests/era_split_standing_lifecycle/include']
        flags = ['cc', '-std=gnu11', '-Wall', '-Wextra', '-ffunction-sections', '-fdata-sections',
                 *['-I' + str(path) for path in includes], '-DQMK_KEYBOARD_H="qmk_stub.h"', '-DSPLIT_KEYBOARD', '-include', 'stddef.h']
        if case == 'role_flip':
            flags.append('-DERA_RESTART_ROLE_FLIP_PROBE')
        elif case == 'clean':
            flags.append('-DERA_RESTART_CLEAN_PROBE')
        objects = []
        proto = ''
        for endpoint in (0, 1):
            output = dest / ('endpoint' + str(endpoint) + '.o')
            renamed = ['-D' + name + '=e' + str(endpoint) + '_' + name for name in names]
            compiled = subprocess.run([*flags, '-DERA_SPLIT_RESTART_AGREEMENT_TEST', *renamed, '-c',
                                       str(dest / (SPLIT + 'era_split_restart_agreement.c')), '-o', str(output)], capture_output=True, text=True)
            if compiled.returncode:
                raise AssertionError(compiled.stderr)
            objects.append(output)
            for decl in declarations:
                name = re.search(r'\b(era_split_restart_\w+)\s*\(', decl).group(1)
                if name in names:
                    proto += decl.replace(name, 'e' + str(endpoint) + '_' + name) + '\n'
        source = (dest / (SPLIT + 'era_split_keyboard.c')).read_text(encoding='utf-8')
        start = source.index('const era_split_restart_act_rules_t era_split_restart_act_rules[')
        table = '#define ERA_SPLIT_LINK_LEVEL_LOW 2\n' + source[start:source.index('\n};', start) + 3]
        scheduler = clean((dest / (SPLIT + 'scheduler/era_split_transport_scheduler_responder.c')).read_text(encoding='utf-8'))
        start = scheduler.index('if (result->peer_restart_valid &&')
        apply = block(scheduler, start).replace('era_split_restart_agreement_note_peer_arm(', 'e1_era_split_restart_agreement_note_peer_arm(')
        commit = function(scheduler, 'era_split_transport_scheduler_commit_communication_core_host_response')
        response = (dest / (SPLIT + 'era_host_peer_response.c')).read_text(encoding='utf-8')
        codec = '\n'.join(function(response, name) for name in [
            'era_host_peer_transaction_encode_rgb_state_body', 'era_host_peer_transaction_decode_rgb_state_body',
            'era_host_peer_transaction_extract_sections'])
        unit = (HERE / 'restart_endpoints.c').read_text(encoding='utf-8')
        unit = unit.replace('/* ENDPOINT_DECLARATIONS */', proto + table)
        unit = unit.replace('/* PRODUCTION_CODEC */', codec)
        unit = unit.replace('/* PRODUCTION_RESTART_APPLY */', apply)
        unit = unit.replace('/* PRODUCTION_RESPONSE_COMMIT */', commit)
        file = dest / 'fixture.c'
        file.write_text(unit, encoding='utf-8')
        output = dest / 'fixture'
        compiled = subprocess.run([*flags, str(file), *map(str, objects), '-Wl,--gc-sections', '-o', str(output)], capture_output=True, text=True)
        if compiled.returncode:
            raise AssertionError(compiled.stderr)
        return subprocess.run([str(output)], capture_output=True, text=True)


class RestartEndpoints(unittest.TestCase):
    def negative(self, mutation, context, assertion, case=None):
        result = run(mutation, case)  # Compilation errors raise; never count as detection.
        self.assertEqual(result.returncode, -signal.SIGABRT, result.stdout + result.stderr)
        self.assertIn(context + ': Assertion', result.stderr)
        self.assertIn(assertion, result.stderr)

    def test_real_pipeline(self):
        result = run()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_role_flip_receipt_preserves_confirmed_arm(self):
        result = run(case='role_flip')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_clean_actual_receipt_pipeline(self):
        result = run(case='clean')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stdout.count('PASS CLEAN '), 2, result.stdout)

    def test_removed_clean_snapshot_fence_is_detected(self):
        result = run('snapshot_fence', case='clean')
        self.assertEqual(result.returncode, -signal.SIGABRT, result.stdout + result.stderr)
        self.assertIn("clean_coalesced_commit_and_idle: Assertion `arm()==0' failed.", result.stderr)

    def test_removed_retirement_is_detected(self):
        self.negative('retirement', 'start_lost_echo', 'arm()==0')

    def test_removed_snapshot_fence_is_detected(self):
        self.negative('snapshot_fence', 'coalesced_idle_and_cached_prearm', 'arm()==0')

    def test_removed_equal_body_refresh_is_detected(self):
        self.negative('equal_refresh', 'fresh', 'peer_authority_valid')

    def test_removed_authority_shadow_reassert_is_detected(self):
        self.negative('authority_reassert', 'finish_retry', 'standing_state_seq()!=before')

    def test_removed_same_tuple_successor_fence_is_detected(self):
        self.negative('same_tuple', 'completed_same_tuple_with_delayed_peer_task', 'arm()==0')

    def test_removed_confirmed_finality_cleanup_is_detected(self):
        self.negative('confirmed_finality', 'role_flip_receipt_preserves_confirmed_arm',
                      'act==ERA_SPLIT_RESTART_ACT_LINK_SPEED && param==1 && carried==first', 'role_flip')


if __name__ == '__main__':
    unittest.main(verbosity=2)
