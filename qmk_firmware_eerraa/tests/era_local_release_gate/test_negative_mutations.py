#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Root-run Linux fixture controls: rebuild real QMK, then restore exact source.

Run only in the era_build.py-synchronized locked test snapshot with no concurrent build. Each
mutation must reach gtest runtime; a compiler failure is never a negative pass.
The final build/test uses the restored production source, including an isolated
HP closure case so failure cannot be attributed to another test's cleanup.
"""
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'quantum' / 'keyboard.c'
TARGET = 'test:era_local_release_gate'
CLAUSE = b'if (local_key && !key_pressed) key_action_open = (matrix_action_down[row] & col_mask) != 0;'


def run(filter_name):
    env = dict(os.environ, GTEST_FILTER=filter_name)
    result = subprocess.run(['make', TARGET], cwd=ROOT, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    print(result.stdout, end='', flush=True)
    return result


def main():
    if os.name != 'posix':
        raise SystemExit('Run only in the root-owned locked Linux snapshot synchronized by era_build.py.')
    original = SOURCE.read_bytes()
    assert original.count(CLAUSE) == 1
    prefix = 'EraLocalReleaseGate.'
    controls = [
        ('closed-release-disabled',
         b'if (local_key && !key_pressed) key_action_open = process_keypress && (matrix_action_down[row] & col_mask) != 0;',
         'MomentaryLayerReleaseAfterSuspendGrace'),
        ('isolated-HP-local-release-disabled',
         b'if (local_key && !key_pressed) key_action_open = process_keypress && (matrix_action_down[row] & col_mask) != 0;',
         'RemoteClosureWaitsForWholeGroupAdmissionAtReopen'),
        ('unadmitted-release-enabled',
         b'if (local_key && !key_pressed) key_action_open = true;',
         'FreshClosedDownIsConsumedAcrossReopen'),
    ]
    try:
        for label, replacement, case in controls:
            SOURCE.write_bytes(original.replace(CLAUSE, replacement, 1))
            result = run(prefix + case)
            assert result.returncode != 0, label + ': mutation unexpectedly passed'
            assert '[ RUN      ] ' + prefix + case in result.stdout, label + ': did not reach runtime'
            assert '[  FAILED  ] ' + prefix + case in result.stdout, label + ': wrong failure boundary'
            print('PASS compiled runtime-negative ' + label, flush=True)
    finally:
        SOURCE.write_bytes(original)
    isolated = run(prefix + 'RemoteClosureWaitsForWholeGroupAdmissionAtReopen')
    assert isolated.returncode == 0, 'restored isolated HP closure failed'
    positive = run(prefix + '*')
    assert positive.returncode == 0, 'restored production suite failed'
    assert SOURCE.read_bytes() == original
    print('PASS three compiled negatives, isolated HP closure and restored full production suite', flush=True)


if __name__ == '__main__':
    main()
