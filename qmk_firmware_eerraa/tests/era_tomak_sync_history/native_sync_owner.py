#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Focused legacy entry points execute the current native contract owners."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def run_owner(target, names):
    command = [str(ROOT / ".build/test" / (target + ".elf")),
               "--gtest_filter=" + ":".join(names)]
    result = subprocess.run(command, cwd=ROOT, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    print(result.stdout, end="")
    result.check_returncode()
    if f"[  PASSED  ] {len(names)} test" not in result.stdout:
        raise AssertionError("Focused native owner selection did not execute every named test")
