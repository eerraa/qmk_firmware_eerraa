#!/usr/bin/env python3
# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later
"""One pre-commit check for every host and the owner's terminal.

Git runs this from hooks/pre-commit once core.hooksPath is hooks. The
checks a commit owes are whitespace, the QMK fork ledger, and
agent-document references. Never bypass with --no-verify; fix the finding.
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
LEDGER = REPO / "keyboards/era/common/docs/manuals/era_qmk_fork_ledger.md"
DOC_REFS = REPO / "keyboards/era/common/tools/era_doc_refs.py"

DOC_LAYER = ("keyboards/era/common/docs/", "AGENTS.md", "CLAUDE.md",
             ".claude/rules/")
REF_SOURCE_SUFFIX = (".c", ".h", ".mk")
REF_CORE_DIRS = ("quantum/", "platforms/", "tmk_core/", "drivers/",
                 "builddefs/")
FORK_DIRS = ("quantum/", "platforms/", "drivers/")
NON_ERA_GATES = ("RP2040_BOOTLOADER_DOUBLE_TAP_RESET_NONBLOCKING",)


def git(*args):
    proc = subprocess.run(
        ["git", *args], cwd=str(REPO), capture_output=True,
        encoding="utf-8", errors="replace",
    )
    return proc.returncode, proc.stdout


def grep_files(pattern):
    rc, out = git("grep", "-lI", pattern, "--", *FORK_DIRS)
    if rc not in (0, 1):
        raise RuntimeError(f"git grep {pattern!r} failed (rc={rc})")
    return [line.strip() for line in out.splitlines() if line.strip()]


def name_only(*args):
    _, out = git(*args)
    return {line.strip() for line in out.splitlines() if line.strip()}


def mentions(ledger, token):
    return re.search(r"(?<![\w./-])" + re.escape(token), ledger) is not None


def ledger_records(path, ledger):
    if mentions(ledger, path):
        return True
    stem, _, suffix = path.rpartition(".")
    if suffix in ("c", "h") and mentions(ledger, f"{stem}.[ch]"):
        return True
    directory, _, basename = path.rpartition("/")
    if directory and f"{directory}/" in ledger and mentions(ledger, basename):
        return True
    return False


def check_whitespace(commits_all, problems, notes):
    _, staged = git("diff", "--cached", "--check")
    if staged.strip():
        problems.append(
            "`git diff --cached --check` reports whitespace errors in the "
            "staged change:\n" + staged.rstrip()
        )
    _, tree = git("diff", "--check")
    if tree.strip():
        if commits_all:
            problems.append(
                "`git diff --check` reports whitespace errors, and this "
                "commit takes the working tree:\n" + tree.rstrip()
            )
        else:
            notes.append(
                "`git diff --check` reports whitespace errors in unstaged "
                "work. Not part of this commit, but they will block the "
                "commit that stages them:\n" + tree.rstrip()
            )


def check_fork_ledger(problems, notes):
    ledger = LEDGER.read_text(encoding="utf-8")
    undocumented = []
    live = grep_files("ERA_")
    undocumented += [path for path in live if not ledger_records(path, ledger)]
    gated = 0
    for gate in NON_ERA_GATES:
        if gate not in ledger:
            problems.append(
                f"this gate names `{gate}` as the macro that hides a QMK core "
                "fork from an `ERA_` grep, but `era_qmk_fork_ledger.md` no longer "
                "mentions it. Canon moved and the adapter did not; reconcile "
                "`NON_ERA_GATES` in this hook with the ledger."
            )
        hits = grep_files(gate)
        if not hits:
            problems.append(
                f"`era_qmk_fork_ledger.md` records `{gate}` as a live gate over a "
                "QMK core file, but no file under "
                + ", ".join(FORK_DIRS)
                + " contains it. Either the fork edit was reverted and the "
                "ledger is stale, or the gate was renamed."
            )
        gated += len(hits)
        undocumented += [path for path in hits if not ledger_records(path, ledger)]
    if undocumented:
        problems.append(
            "QMK core files carry an ERA edit that the table in "
            "`era_qmk_fork_ledger.md` does not record:\n"
            + "\n".join(f"  {path}" for path in sorted(set(undocumented)))
            + "\nThat table is the full set, and a fork edit nobody records "
            "is a fork edit nobody can retire. Add the row in this commit."
        )
    else:
        notes.append(
            f"QMK fork ledger: {len(live)} `ERA_`-visible and {gated} "
            "macro-gated core file(s); every one is recorded in "
            "`era_qmk_fork_ledger.md`."
        )


def doc_layer_armed(changed):
    return any(
        path.startswith(DOC_LAYER)
        or path in ("AGENTS.md", "CLAUDE.md")
        or (path.startswith(("keyboards/era/",) + REF_CORE_DIRS)
            and path.endswith(REF_SOURCE_SUFFIX))
        for path in changed
    )


def check_doc_layer(problems, notes):
    proc = subprocess.run(
        [sys.executable, str(DOC_REFS)], cwd=str(REPO),
        capture_output=True, encoding="utf-8", errors="replace",
    )
    output = ((proc.stdout or "") + (proc.stderr or "")).strip()
    if proc.returncode == 0:
        notes.append("doc refs: " + output)
    else:
        problems.append(
            "era_doc_refs.py fails on the tree this commit produces:\n"
            + output
            + "\nFix the references in this commit, or run it by hand to "
            "inspect: python keyboards/era/common/tools/era_doc_refs.py"
        )
    _, removed = git("diff", "--cached", "-U0", "--",
                     "keyboards/era/common/docs")
    if any(line.startswith("-") and not line.startswith("---")
           for line in removed.splitlines()):
        notes.append(
            "this commit deletes doc lines. If any deletion retires a "
            "fact rather than moving it, run the safety net first: "
            "python keyboards/era/common/tools/era_doc_refs.py --homeless "
            "(a homeless token is a promotion candidate, not a deletion "
            "candidate)."
        )


def main():
    changed = name_only("diff", "--cached", "--name-only")
    if not changed:
        return 0                      # --allow-empty 등
    problems, notes = [], []
    check_whitespace(False, problems, notes)   # staged → problems, unstaged → notes
    if any(p.startswith(FORK_DIRS) for p in changed) or any(
            p.endswith("era_qmk_fork_ledger.md") for p in changed):
        check_fork_ledger(problems, notes)
    if doc_layer_armed(changed):
        check_doc_layer(problems, notes)
    for note in notes:
        print("note: " + note)
    if problems:
        sys.stderr.write("\n\n".join(problems) + "\n")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
