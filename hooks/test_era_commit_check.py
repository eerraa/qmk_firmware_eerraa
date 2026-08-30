#!/usr/bin/env python3
# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later
"""Conformance for the host-neutral pre-commit check."""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import era_commit_check as gate

REPO = Path(__file__).resolve().parent.parent
PRE_COMMIT = REPO / "hooks" / "pre-commit"
COMMIT_CHECK = REPO / "hooks" / "era_commit_check.py"


def check(name, cond):
    if not cond:
        raise SystemExit(f"FAIL: {name}")
    print(f"ok  {name}")


def test_wiring():
    proc = subprocess.run(
        ["git", "ls-files", "-s", "hooks/pre-commit"],
        cwd=str(REPO),
        capture_output=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )
    mode = (proc.stdout or "").split()[0] if proc.stdout.strip() else ""
    check("pre-commit mode 100755", mode == "100755")
    check(
        "pre-commit calls era_commit_check.py",
        "era_commit_check.py" in PRE_COMMIT.read_text(encoding="utf-8"),
    )
    settings = json.loads(
        (REPO / ".claude" / "settings.json").read_text(encoding="utf-8")
    )
    check("claude settings has no hooks key", "hooks" not in settings)
    for rel, label in (
        (".codex", ".codex/"),
        (".grok", ".grok/"),
        (".claude/hooks", ".claude/hooks/"),
        ("hooks/era_pretooluse.py", "hooks/era_pretooluse.py"),
    ):
        check(f"bypass absent: {label}", not (REPO / rel).exists())


def test_units():
    staged_err = "hooks/pre-commit:1: trailing whitespace.\n"
    unstaged_err = "notes.txt:1: trailing whitespace.\n"
    calls = []

    def fake_git(*args):
        calls.append(args)
        if args[:3] == ("diff", "--cached", "--check"):
            return 2, staged_err
        if args[:2] == ("diff", "--check"):
            return 2, unstaged_err
        return 0, ""

    original = gate.git
    gate.git = fake_git
    try:
        problems, notes = [], []
        gate.check_whitespace(False, problems, notes)
        check("whitespace staged is a problem", any("staged" in p for p in problems))
        check(
            "whitespace unstaged is a note",
            any("unstaged" in n for n in notes),
        )
        check(
            "whitespace staged text present",
            any(staged_err.strip() in p for p in problems),
        )
        check(
            "whitespace unstaged text present",
            any(unstaged_err.strip() in n for n in notes),
        )
    finally:
        gate.git = original

    armed = (
        "AGENTS.md",
        ".claude/rules/x.md",
        "keyboards/era/a.c",
        "quantum/b.h",
    )
    idle = ("readme.md", "lib/x")
    for path in armed:
        check(f"doc_layer_armed {path}", gate.doc_layer_armed({path}))
    for path in idle:
        check(f"doc_layer_idle {path}", not gate.doc_layer_armed({path}))

    ledger = (
        "see quantum/matrix.c in the table\n"
        "quantum/foo.[ch] is one edit\n"
        "platforms/chibios/ holds the rest; bootloader.c is the file\n"
    )
    check(
        "ledger_records full path",
        gate.ledger_records("quantum/matrix.c", ledger),
    )
    check(
        "ledger_records stem.[ch]",
        gate.ledger_records("quantum/foo.c", ledger),
    )
    check(
        "ledger_records dir + basename",
        gate.ledger_records("platforms/chibios/bootloader.c", ledger),
    )
    check(
        "ledger_records misses a stranger",
        not gate.ledger_records("quantum/stranger.c", ledger),
    )


def _git(cwd, *args, check=True, input=None):
    return subprocess.run(
        ["git", *args],
        cwd=str(cwd),
        capture_output=True,
        encoding="utf-8",
        errors="replace",
        input=input,
        check=check,
    )


def test_end_to_end():
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        _git(root, "init")
        _git(root, "config", "user.email", "era-test@example.com")
        _git(root, "config", "user.name", "ERA test")
        hooks = root / "hooks"
        hooks.mkdir()
        shutil.copy2(PRE_COMMIT, hooks / "pre-commit")
        shutil.copy2(COMMIT_CHECK, hooks / "era_commit_check.py")
        os.chmod(hooks / "pre-commit", 0o755)
        _git(root, "config", "core.hooksPath", "hooks")

        dirty = root / "dirty.txt"
        dirty.write_text("hello \n", encoding="utf-8")
        _git(root, "add", "dirty.txt")
        refused = _git(
            root, "commit", "-m", "trailing space",
            check=False,
        )
        combined = (refused.stdout or "") + (refused.stderr or "")
        check("end-to-end refused commit rc", refused.returncode != 0)
        check("end-to-end stderr names whitespace", "whitespace" in combined)

        dirty.write_text("hello\n", encoding="utf-8")
        _git(root, "add", "dirty.txt")
        accepted = _git(root, "commit", "-m", "clean")
        check("end-to-end clean commit rc", accepted.returncode == 0)


def test_live_tree():
    check_proc = subprocess.run(
        [sys.executable, str(COMMIT_CHECK)],
        cwd=str(REPO),
        capture_output=True,
        encoding="utf-8",
        errors="replace",
    )
    ws = subprocess.run(
        ["git", "diff", "--cached", "--check"],
        cwd=str(REPO),
        capture_output=True,
        encoding="utf-8",
        errors="replace",
    )
    # git diff --check is 0 when clean, 2 when it prints errors.
    expected = 0 if ws.returncode == 0 else 1
    check(
        "live tree rc matches cached whitespace",
        check_proc.returncode == expected,
    )


if __name__ == "__main__":
    test_wiring()
    test_units()
    test_end_to_end()
    test_live_tree()
    print("all tests passed")
