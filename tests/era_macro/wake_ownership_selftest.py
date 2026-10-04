#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Link the real wake integration with its ownership branch removed.

Compile success is required; only a runtime failure of the real wake case
counts as a detected regression. Source and native objects remain untouched.
"""
from pathlib import Path
import hashlib
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TARGET = ROOT / '.build/test_obj/era_macro'
CASE = 'EraMacro.RealWakeRetiresRegularInputAndPreservesScopedOutputAndRemainingDelay'

def main():
    subprocess.run(['make','test:era_macro','-j8'],cwd=ROOT,check=True,stdout=subprocess.DEVNULL)
    flags=shlex.split((TARGET/'cflags.txt').read_text())
    objects=list(dict.fromkeys(shlex.split((TARGET/'obj.txt').read_text())))
    linkflags=shlex.split((TARGET/'ldflags.txt').read_text())
    selected=[o for o in objects if o.endswith('platforms/chibios/suspend.o')]
    assert len(selected)==1,selected
    source=ROOT/'platforms/chibios/suspend.c'
    original=source.read_text()
    needle='#ifdef ACTION_OWNED_OUTPUTS'
    assert original.count(needle)==1
    paths=[source,*(ROOT/o for o in objects)]
    digest=lambda: [hashlib.sha256(p.read_bytes()).hexdigest() for p in paths]
    before=digest()
    with tempfile.TemporaryDirectory(prefix='era-wake-control-') as directory:
        out=Path(directory)
        for mutant in (False,True):
            c,obj,exe=out/'wake.c',out/'wake.o',out/'wake'
            c.write_text(original.replace(needle,'#if 0') if mutant else original)
            subprocess.run(['gcc',*flags,'-c',str(c),'-o',str(obj)],cwd=ROOT,check=True,capture_output=True)
            subprocess.run(['gcc',*(str(obj) if o==selected[0] else o for o in objects),
                            '-o',str(exe),*linkflags],cwd=ROOT,check=True,capture_output=True)
            run=subprocess.run([str(exe),'--gtest_filter='+CASE],cwd=ROOT,capture_output=True,text=True)
            if mutant:
                assert run.returncode!=0 and '[  FAILED  ] '+CASE in run.stdout,run.stdout+run.stderr
            else:
                assert run.returncode==0,run.stdout+run.stderr
            print('PASS', 'removed ownership branch detected at runtime' if mutant else 'production wake',flush=True)
    assert before==digest(),'Source or native objects changed'

if __name__=='__main__':
    main()
