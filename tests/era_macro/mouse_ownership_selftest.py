#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Require the real QMK ownership tests to reject removed direction restoration.

Run after make test:era_macro. Only isolated source and object copies are used;
compile/link failures never count as successful negative runtime controls.
"""
from pathlib import Path
import hashlib
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TARGET = ROOT / '.build/test_obj/era_macro'
CASE = 'EraMacro.MouseOppositePhysicalDirectionSurvivesMacroCancel'


def main():
    flags = shlex.split((TARGET / 'cflags.txt').read_text())
    objects = list(dict.fromkeys(shlex.split((TARGET / 'obj.txt').read_text())))
    link_flags = shlex.split((TARGET / 'ldflags.txt').read_text())
    selected = [obj for obj in objects if obj.endswith('quantum/action_util.o')]
    assert len(selected) == 1, selected
    source = ROOT / 'quantum/action_util.c'
    original = source.read_text()
    needle = 'mousekey_restore_direction((uint8_t)(QK_MOUSE_CURSOR_UP + opposite));'
    assert original.count(needle) == 1
    paths = [source, *(ROOT / obj for obj in objects)]
    digest = lambda: [hashlib.sha256(path.read_bytes()).hexdigest() for path in paths]
    before = digest()
    with tempfile.TemporaryDirectory(prefix='era-mouse-owner-control-') as directory:
        out = Path(directory)
        for mutant in (False, True):
            src, obj, exe = out / 'action_util.c', out / 'action_util.o', out / 'control'
            src.write_text(original.replace(needle, '(void)opposite;') if mutant else original)
            subprocess.run(['gcc', *flags, '-iquote', str(source.parent), '-c', str(src), '-o', str(obj)],
                           cwd=ROOT, check=True, capture_output=True)
            subprocess.run(['gcc', *(str(obj) if value == selected[0] else value for value in objects),
                            '-o', str(exe), *link_flags], cwd=ROOT, check=True, capture_output=True)
            run = subprocess.run([str(exe), '--gtest_filter=EraMacro.Mouse*'], cwd=ROOT, capture_output=True, text=True, errors='replace')
            if mutant:
                assert run.returncode != 0 and '[  FAILED  ] ' + CASE in run.stdout, run.stdout + run.stderr
                print('PASS compiled restoration-removal runtime negative', flush=True)
            else:
                assert run.returncode == 0, run.stdout + run.stderr
                print('PASS production opposed mouse ownership', flush=True)
    assert before == digest(), 'Source or native objects changed'


if __name__ == '__main__':
    main()
