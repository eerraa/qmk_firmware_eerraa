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
    attr_proc = subprocess.run(
        ["git", "check-attr", "eol", "--", "hooks/pre-commit"],
        cwd=str(REPO), capture_output=True, encoding="utf-8",
        errors="replace", check=False,
    )
    check(
        "pre-commit checkout stays LF",
        attr_proc.returncode == 0
        and (attr_proc.stdout or "").strip().endswith(": lf"),
    )
    check(
        "pre-commit calls era_commit_check.py",
        "era_commit_check.py" in PRE_COMMIT.read_text(encoding="utf-8"),
    )
    settings = json.loads(
        (REPO / ".claude" / "settings.json").read_text(encoding="utf-8")
    )
    check("claude settings has no hooks key", "hooks" not in settings)
    tracked_proc = subprocess.run(
        ["git", "ls-files"], cwd=str(REPO), capture_output=True,
        encoding="utf-8", errors="replace", check=False,
    )
    check("tracked wiring scan succeeds", tracked_proc.returncode == 0)
    tracked = set((tracked_proc.stdout or "").splitlines())
    for rel in (
        ".claude/hooks/era_commit_gate.py",
        ".claude/hooks/era_graphify_read_guard.py",
        "hooks/era_pretooluse.py",
        "hooks/test_era_pretooluse.py",
    ):
        check(f"legacy host hook absent: {rel}", rel not in tracked)


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
        gate.DOC_REFS_REL,
    )
    idle = ("readme.md", "lib/x")
    for path in armed:
        check(f"doc_layer_armed {path}", gate.doc_layer_armed({path}))
    for path in idle:
        check(f"doc_layer_idle {path}", not gate.doc_layer_armed({path}))

    for path in ("quantum/a.c", "tmk_core/b.c", "builddefs/c.mk"):
        check(f"fork_layer_armed {path}", gate.fork_layer_armed({path}))
    check(
        "fork_layer_idle readme.md",
        not gate.fork_layer_armed({"readme.md"}),
    )

    conflicts = gate.checked_surface_conflicts(
        {"AGENTS.md", "tmk_core/a.c", "readme.md"}, set(), True, True
    )
    check(
        "doc check catches every unstaged tracked input",
        conflicts == {"AGENTS.md", "tmk_core/a.c", "readme.md"},
    )
    check(
        "index-pure fork check ignores working-tree differences",
        not gate.checked_surface_conflicts({"quantum/a.c"}, set(), True, False),
    )
    check(
        "doc surface catches py and json changes",
        gate.checked_surface_conflicts(
            {"keyboards/era/common/tools/new_tool.py", "board.json"},
            set(), False, True
        ) == {"keyboards/era/common/tools/new_tool.py", "board.json"},
    )
    check(
        "doc surface catches any untracked path",
        gate.checked_surface_conflicts(
            set(), {"scratch/new-target"}, False, True
        ) == {"scratch/new-target"},
    )

    original_name_only = gate.name_only
    try:
        gate.name_only = lambda *args: (
            {"AGENTS.md"}
            if args == ("ls-files", "--others", "--exclude-standard")
            else set()
        )
        surface_problems = []
        check(
            "untracked checked-surface file refuses validation",
            not gate.check_checked_surface_matches_index(
                False, True, surface_problems
            ),
        )
        check(
            "untracked refusal names the file",
            any("AGENTS.md" in problem for problem in surface_problems),
        )
    finally:
        gate.name_only = original_name_only

    check("pristine snapshot parses", gate.pristine_snapshot(
        "The vendored pristine QMK snapshot is named `c93ef27143`."
    ) == "c93ef27143")
    check("missing pristine snapshot refuses parsing",
          gate.pristine_snapshot("no snapshot here") is None)

    ledger = (
        "## Current Fork Edits\n\n"
        "| Files | Why | Gate |\n"
        "| --- | --- | --- |\n"
        "| `quantum/matrix.[ch]` | x | y |\n"
        "| `builddefs/common_features.mk`, `tmk_core/a.c` | x | y |\n\n"
        "## Restored\n\n"
        "`drivers/eeprom/eeprom_driver.h` is pristine.\n"
    )
    check(
        "fork table expands grouped C/header paths",
        gate.current_fork_table_paths(ledger) == {
            "quantum/matrix.c", "quantum/matrix.h",
            "builddefs/common_features.mk", "tmk_core/a.c",
        },
    )
    check(
        "fork table excludes restored prose",
        "drivers/eeprom/eeprom_driver.h"
        not in gate.current_fork_table_paths(ledger),
    )
    missing, stale = gate.fork_surface_findings(
        {"quantum/a.c", "quantum/new.c"},
        {"quantum/a.c", "quantum/old.c"},
    )
    check(
        "fork surface finds undocumented path",
        missing == {"quantum/new.c"},
    )
    check(
        "fork surface finds stale path",
        stale == {"quantum/old.c"},
    )

    original_git = gate.git
    gate.git = lambda *args: (128, "")
    try:
        failed_closed = False
        try:
            gate.name_only("diff", "--name-only")
        except RuntimeError:
            failed_closed = True
        check("failed git surface scan refuses empty answer", failed_closed)
    finally:
        gate.git = original_git


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
        doc_tool = (
            root / "keyboards" / "era" / "common" / "tools"
            / "era_doc_refs.py"
        )
        doc_tool.parent.mkdir(parents=True)
        doc_tool.write_text(
            "#!/usr/bin/env python3\nprint('temporary doc refs ok')\n",
            encoding="utf-8",
        )
        _git(root, "config", "core.hooksPath", "hooks")

        dirty = root / "dirty.txt"
        dirty.write_text("hello \n", encoding="utf-8")
        _git(root, "add", "dirty.txt", "hooks", "keyboards")
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

        partial = root / "AGENTS.md"
        partial.write_text("staged\n", encoding="utf-8")
        _git(root, "add", "AGENTS.md")
        partial.write_text("unstaged\n", encoding="utf-8")
        refused_partial = _git(
            root, "commit", "-m", "partial checked surface", check=False
        )
        partial_output = (
            (refused_partial.stdout or "") + (refused_partial.stderr or "")
        )
        check(
            "end-to-end partial checked surface refused",
            refused_partial.returncode != 0,
        )
        check(
            "end-to-end partial refusal names staged mismatch",
            "different content from what Git commits" in partial_output,
        )

        partial.write_text("staged\n", encoding="utf-8")
        _git(root, "add", "AGENTS.md")
        accepted_doc = _git(root, "commit", "-m", "complete document")
        check("end-to-end fully staged document accepted",
              accepted_doc.returncode == 0)


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
    ledger = gate.LEDGER.read_text(encoding="utf-8")
    snapshot = gate.pristine_snapshot(ledger)
    check("live ledger names pristine snapshot", snapshot is not None)
    exists = subprocess.run(
        ["git", "cat-file", "-e", f"{snapshot}^{{commit}}"], cwd=str(REPO),
        capture_output=True, check=False,
    )
    check("live pristine snapshot exists", exists.returncode == 0)
    diff = subprocess.run(
        ["git", "diff", "--cached", "--name-only", snapshot, "--",
         *gate.FORK_DIRS], cwd=str(REPO), capture_output=True, encoding="utf-8",
        errors="replace", check=False,
    )
    check("live staged fork diff succeeds", diff.returncode == 0)
    live = {line for line in diff.stdout.splitlines() if line}
    recorded = gate.current_fork_table_paths(ledger)
    missing, stale = gate.fork_surface_findings(live, recorded)
    check("live fork table has no undocumented paths", not missing)
    check("live fork table has no stale paths", not stale)
    check("Cirque ERA token is not a fork row",
          "drivers/sensors/cirque_pinnacle.c" not in recorded)


if __name__ == "__main__":
    test_wiring()
    test_units()
    test_end_to_end()
    test_live_tree()
    print("all tests passed")
