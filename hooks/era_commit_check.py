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
LEDGER_REL = "keyboards/era/common/docs/manuals/era_qmk_fork_ledger.md"
LEDGER = REPO / LEDGER_REL
DOC_REFS_REL = "keyboards/era/common/tools/era_doc_refs.py"
DOC_REFS = REPO / DOC_REFS_REL

DOC_LAYER = ("keyboards/era/common/docs/", "AGENTS.md", "CLAUDE.md",
             ".claude/rules/")
REF_SOURCE_SUFFIX = (".c", ".h", ".mk")
REF_CORE_DIRS = ("quantum/", "platforms/", "tmk_core/", "drivers/",
                 "builddefs/")
FORK_DIRS = REF_CORE_DIRS


def git(*args):
    proc = subprocess.run(
        ["git", *args], cwd=str(REPO), capture_output=True,
        encoding="utf-8", errors="replace",
    )
    return proc.returncode, proc.stdout


def name_only(*args):
    rc, out = git(*args)
    if rc != 0:
        raise RuntimeError(
            f"git {' '.join(args)} failed (rc={rc}); refusing to treat an "
            "unmeasured surface as empty"
        )
    return {line.strip() for line in out.splitlines() if line.strip()}


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


def pristine_snapshot(ledger):
    match = re.search(
        r"vendored pristine QMK snapshot is named `([0-9a-f]{7,40})`", ledger
    )
    return match.group(1) if match else None


def current_fork_table_paths(ledger):
    """Expand the first column of Current Fork Edits, and no other prose."""
    match = re.search(
        r"^## Current Fork Edits\s*$([\s\S]*?)(?=^## |\Z)", ledger, re.M
    )
    if not match:
        return set()
    paths = set()
    for line in match.group(1).splitlines():
        if not line.startswith("|"):
            continue
        first = line.split("|", 2)[1]
        for path in re.findall(r"`([^`]+)`", first):
            if path.endswith(".[ch]"):
                stem = path[:-5]
                paths.update((stem + ".c", stem + ".h"))
            elif path.startswith(FORK_DIRS):
                paths.add(path)
    return paths


def fork_surface_findings(live, recorded):
    live, recorded = set(live), set(recorded)
    return live - recorded, recorded - live


def check_fork_ledger(problems, notes):
    rc, ledger = git("show", f":{LEDGER_REL}")
    if rc != 0:
        problems.append(
            "the staged index does not contain `era_qmk_fork_ledger.md`; "
            "the QMK fork surface has no staged authority to validate against."
        )
        return
    snapshot = pristine_snapshot(ledger)
    if snapshot is None:
        problems.append(
            "the staged `era_qmk_fork_ledger.md` does not name its vendored "
            "pristine QMK snapshot."
        )
        return
    rc, _ = git("cat-file", "-e", f"{snapshot}^{{commit}}")
    if rc != 0:
        problems.append(
            f"the staged fork ledger names `{snapshot}`, but that pristine "
            "QMK commit is unavailable in this clone."
        )
        return
    rc, out = git("diff", "--cached", "--name-only", snapshot, "--", *FORK_DIRS)
    if rc != 0:
        problems.append(
            f"cannot compare the staged QMK fork surface with `{snapshot}`. "
            "Restore the vendored pristine snapshot or correct the staged ledger."
        )
        return
    live = {line.strip() for line in out.splitlines() if line.strip()}
    recorded = current_fork_table_paths(ledger)
    if not recorded:
        problems.append(
            "the staged fork ledger has no parseable paths in its "
            "`Current Fork Edits` table."
        )
        return
    undocumented, stale = fork_surface_findings(live, recorded)
    if undocumented:
        problems.append(
            "the staged QMK core surface differs from the vendored pristine "
            "snapshot in files that the table in "
            "`era_qmk_fork_ledger.md` does not record:\n"
            + "\n".join(f"  {path}" for path in sorted(set(undocumented)))
            + "\nThat table is the full set, and a fork edit nobody records "
            "is a fork edit nobody can retire. Add the row in this commit."
        )
    if stale:
        problems.append(
            "the staged `Current Fork Edits` table records files that no "
            "longer differ from the vendored pristine snapshot:\n"
            + "\n".join(f"  {path}" for path in sorted(stale))
            + "\nRemove or reconcile the stale row in this commit."
        )
    if not undocumented and not stale:
        notes.append(
            f"QMK fork ledger: all {len(live)} staged-index difference(s) "
            f"from `{snapshot}` under the five QMK roots are recorded in "
            "`era_qmk_fork_ledger.md`."
        )


def doc_layer_armed(changed):
    return any(
        path.startswith(DOC_LAYER)
        or path in ("AGENTS.md", "CLAUDE.md")
        or path == DOC_REFS_REL
        or (path.startswith(("keyboards/era/",) + REF_CORE_DIRS)
            and path.endswith(REF_SOURCE_SUFFIX))
        for path in changed
    )


def fork_layer_armed(changed):
    return any(path.startswith(FORK_DIRS) for path in changed) or any(
        path.endswith("era_qmk_fork_ledger.md") for path in changed
    )


def checked_surface_conflicts(unstaged, untracked, fork_armed, doc_armed):
    # The fork check is index-pure. era_doc_refs.py is not: its path resolver,
    # function-owner grep, source-comment scan, submodule fallback and line
    # counts all reuse the live tree. When it is armed, only a wholly staged
    # non-ignored tree is the tree Git is about to commit.
    del fork_armed
    return set(unstaged) | set(untracked) if doc_armed else set()


def check_checked_surface_matches_index(fork_armed, doc_armed, problems):
    """Keep the working-tree document scanner honest about the commit."""
    unstaged = name_only("diff", "--name-only", "--ignore-submodules=none")
    # era_doc_refs.py intentionally includes untracked, non-ignored files, so
    # they are part of the same mismatch test even though `git diff` cannot see
    # them yet.
    untracked = name_only("ls-files", "--others", "--exclude-standard")
    conflicts = checked_surface_conflicts(
        unstaged, untracked, fork_armed, doc_armed
    )
    if not conflicts:
        return True
    problems.append(
        "the staged commit arms a working-tree check, but the checked surface "
        "also has unstaged changes:\n"
        + "\n".join(f"  {path}" for path in sorted(conflicts))
        + "\nStage the whole files or leave this surface for a later commit; "
        "the hook will not validate different content from what Git commits."
    )
    return False


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
    fork_armed = fork_layer_armed(changed)
    doc_armed = doc_layer_armed(changed)
    surface_matches = check_checked_surface_matches_index(
        fork_armed, doc_armed, problems
    )
    if surface_matches:
        if fork_armed:
            check_fork_ledger(problems, notes)
        if doc_armed:
            check_doc_layer(problems, notes)
    for note in notes:
        print("note: " + note)
    if problems:
        sys.stderr.write("\n\n".join(problems) + "\n")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
