#!/usr/bin/env python3
# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later
"""Shim. The commit check lives in hooks/era_pretooluse.py."""

import runpy
from pathlib import Path

runpy.run_path(
    str(Path(__file__).resolve().parents[2] / "hooks" / "era_pretooluse.py"),
    run_name="__main__",
)
