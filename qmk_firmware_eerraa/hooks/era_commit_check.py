#!/usr/bin/env python3
# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later
"""One pre-commit check for every host and the owner's terminal.

Git runs this from the component's hooks/pre-commit. The
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
STORAGE_KEY_REL = "keyboards/era/common/tools/era_storage_key.py"
STORAGE_KEY = REPO / STORAGE_KEY_REL
STOCK_VIA_REL = "keyboards/era/common/tools/era_stock_via.py"
STOCK_VIA = REPO / STOCK_VIA_REL
STAGED_PYTHON_RUNNER = (
    "import sys\n"
    "path = sys.argv[1]\n"
    "args = sys.argv[2:]\n"
    "code = sys.stdin.buffer.read().decode('utf-8')\n"
    "sys.argv = [path, *args]\n"
    "scope = {'__name__': '__main__', '__file__': path}\n"
    "exec(compile(code, path, 'exec'), scope)\n"
)

DOC_LAYER = ("keyboards/era/common/docs/", "AGENTS.md", "CLAUDE.md",
             ".claude/rules/")
REF_SOURCE_SUFFIX = (".c", ".h", ".mk")
REF_CORE_DIRS = ("quantum/", "platforms/", "tmk_core/", "drivers/",
                 "builddefs/")
FORK_DIRS = REF_CORE_DIRS
STORAGE_LAYER = ("keyboards/era/", "quantum/", "lib/python/qmk/cli/generate/")


def git(*args):
    if args[0] == "show" and args[1].startswith(":"):
        args = ("show", ":./" + args[1][1:], *args[2:])
    elif args[0] == "diff" and "--check" not in args:
        args = ("diff", "--relative", *args[1:])
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
    rc, prefix = git("rev-parse", "--show-prefix")
    if rc != 0:
        problems.append("cannot determine the staged component prefix")
        return
    rc, tree = git("write-tree", f"--prefix={prefix.strip()}")
    if rc != 0:
        problems.append("cannot read the staged component tree")
        return
    root_rc, root = git("rev-parse", "--show-toplevel")
    if root_rc != 0:
        problems.append("cannot determine the Git root")
        return
    compared = subprocess.run(
        ["git", "diff", "--name-only", snapshot, tree.strip(), "--", *FORK_DIRS],
        cwd=root.strip(), capture_output=True, encoding="utf-8", errors="replace",
    )
    rc, out = compared.returncode, compared.stdout
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



def check_doc_layer(problems, notes):
    staged = subprocess.run(
        ["git", "show", f":./{DOC_REFS_REL}"], cwd=str(REPO),
        capture_output=True,
    )
    if staged.returncode != 0:
        problems.append(
            "the staged index does not contain `era_doc_refs.py`; "
            "document references have no staged checker to validate them."
        )
        return
    proc = subprocess.run(
        [sys.executable, "-c", STAGED_PYTHON_RUNNER, str(DOC_REFS), "--index"],
        cwd=str(REPO), input=staged.stdout, capture_output=True,
    )
    output = ((proc.stdout or b"") + (proc.stderr or b"")).decode(
        "utf-8", errors="replace"
    ).strip()
    if proc.returncode == 0:
        notes.append("doc refs: " + output)
    else:
        problems.append(
            "era_doc_refs.py fails on the staged index this commit produces:\n"
            + output
            + "\nFix the references in this commit, or run it by hand to "
            "inspect: python keyboards/era/common/tools/era_doc_refs.py"
        )
    _, removed = git("diff", "--cached", "-U0", "--",
                     "keyboards/era/common/docs")
    if any(line.startswith("-") and not line.startswith("---")
           for line in removed.splitlines()):
        notes.append(
            "this commit deletes doc lines. Review unique requirements before "
            "dropping them; `python keyboards/era/common/tools/era_doc_refs.py "
            "--homeless` can surface identifiers that lost every mention, but "
            "source-owned or deliberately retired identifiers need no new home."
        )


def check_storage_layer(problems, notes):
    staged = subprocess.run(
        ["git", "show", f":./{STORAGE_KEY_REL}"], cwd=str(REPO),
        capture_output=True,
    )
    if staged.returncode != 0:
        problems.append(
            "the staged index does not contain `era_storage_key.py`; a "
            "stored-format change has no staged checker to validate it."
        )
        return
    proc = subprocess.run(
        [sys.executable, "-c", STAGED_PYTHON_RUNNER, str(STORAGE_KEY), "--index"],
        cwd=str(REPO), input=staged.stdout, capture_output=True,
    )
    output = ((proc.stdout or b"") + (proc.stderr or b"")).decode(
        "utf-8", errors="replace"
    ).strip()
    if proc.returncode == 0:
        notes.append(output)
    else:
        problems.append(
            "era_storage_key.py fails on the staged index this commit produces:\n"
            + output
            + "\nDecide the reset in this commit: move a storage key, or record "
            "why the stored bytes stay valid with "
            "`python keyboards/era/common/tools/era_storage_key.py --record \"reason\"`."
        )


def stock_via_layer_armed(changed):
    return any(
        path.startswith("keyboards/era/")
        or path in ("lib/python/qmk/info.py", "tmk_core/protocol/usb_descriptor.c")
        or path.startswith("lib/python/qmk/cli/generate/")
        for path in changed
    )


def check_stock_via_layer(problems, notes):
    staged = subprocess.run(
        ["git", "show", f":./{STOCK_VIA_REL}"], cwd=str(REPO),
        capture_output=True,
    )
    if staged.returncode != 0:
        problems.append("the staged index does not contain `" + STOCK_VIA_REL + "`")
        return
    proc = subprocess.run(
        [sys.executable, "-c", STAGED_PYTHON_RUNNER, str(STOCK_VIA), "--index"],
        cwd=str(REPO), input=staged.stdout, capture_output=True,
    )
    output = ((proc.stdout or b"") + (proc.stderr or b"")).decode(
        "utf-8", errors="replace"
    ).strip()
    if proc.returncode:
        problems.append("STOCK VIA fails on the staged index:\n" + output)
    else:
        notes.append(output)


def main():
    changed = name_only("diff", "--cached", "--name-only")
    root_changes = name_only("diff", "--no-relative", "--cached", "--name-only")
    root_entry_changed = "AGENTS.md" in root_changes
    if not changed and not root_changes:
        return 0                      # --allow-empty 등
    problems, notes = [], []
    check_whitespace(False, problems, notes)   # staged → problems, unstaged → notes
    fork_armed = fork_layer_armed(changed)
    doc_armed = root_entry_changed or doc_layer_armed(changed)
    if fork_armed:
        check_fork_ledger(problems, notes)
    if doc_armed:
        check_doc_layer(problems, notes)
    if any(path.startswith(STORAGE_LAYER) for path in changed):
        check_storage_layer(problems, notes)
    if stock_via_layer_armed(changed):
        check_stock_via_layer(problems, notes)
    if any(path.startswith("keyboards/era/") or path in ("quantum/via.h", "quantum/keycodes.h") for path in changed):
        check_wire_layer(problems, notes)
    for note in notes:
        print("note: " + note)
    if problems:
        sys.stderr.write("\n\n".join(problems) + "\n")
        return 1
    return 0


def check_wire_layer(problems, notes):
    rel = "keyboards/era/common/tools/era_wire_contract.py"
    staged = subprocess.run(
        ["git", "show", f":./{rel}"], cwd=str(REPO), capture_output=True,
    )
    if staged.returncode:
        problems.append("the staged index does not contain `" + rel + "`")
        return
    proc = subprocess.run(
        [sys.executable, "-c", STAGED_PYTHON_RUNNER, str(REPO / rel), "--index"],
        cwd=str(REPO), input=staged.stdout, capture_output=True,
    )
    output = (proc.stdout + proc.stderr).decode("utf-8", errors="replace").strip()
    if proc.returncode:
        problems.append("ERA wire contract fails on the staged index:\n" + output)
    else:
        notes.append(output)


if __name__ == "__main__":
    sys.exit(main())
