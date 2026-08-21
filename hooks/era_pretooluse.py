#!/usr/bin/env python3
# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later
"""One PreToolUse gate for Claude Code, Grok Build, and Codex CLI.

Each tool's settings file only names this script. The script reads whichever
JSON shape arrived, then runs the same three checks: commit, graphify-first
read, graphify-first search. Deny is exit 2 plus stderr — the intersection
all three hosts honour. A broken gate exits 0 so it cannot wedge a session.

Codex has no Read tool; a shell `cat`/`type` of a source path is the weak
form of the read check there. apply_patch is a write and is not a read.
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
LEDGER = REPO / "keyboards/era/common/docs/manuals/era_qmk_fork_ledger.md"
DOC_REFS = REPO / "keyboards/era/common/tools/era_doc_refs.py"

EXEMPT_MARKERS = (
    "keyboards/era/common/docs/",
    "AGENTS.md",
    "CLAUDE.md",
    ".claude/",
    "graphify-out/",
    "hooks/",
    ".grok/",
    ".codex/",
)

READ_NAMES = frozenset({"read", "read_file"})
GLOB_NAMES = frozenset({"glob", "listdir", "list_dir"})
GREP_NAMES = frozenset({"grep"})
BASH_NAMES = frozenset({
    "bash", "powershell", "run_terminal_command", "shell", "local_shell",
})

COMMIT_RE = re.compile(r"(?:^|[\n;&|(])\s*git\s+(?:-\S+\s+)*commit\b")
STAGING_RE = re.compile(r"(?:^|[\n;&|(])\s*git\s+(?:-\S+\s+)*(?:add|stage|rm|mv)\b")
COMMIT_ALL_RE = re.compile(r"(?:^|\s)(?:--all|-(?!-)[A-Za-z]*a[A-Za-z]*)(?=\s|$)")
SHELL_READ_RE = re.compile(
    r"(?:^|[\n;&|(])\s*(?:cat|type|Get-Content|less|more|head|tail)(?:\.exe)?\b",
    re.I,
)
BASH_SEARCH_TOKENS = ("grep", "ripgrep", "rg ", "find ", "fd ", "ack ", "ag ")

DOC_LAYER = ("keyboards/era/common/docs/", "AGENTS.md", "CLAUDE.md",
             ".claude/rules/")
REF_SOURCE_SUFFIX = (".c", ".h", ".mk")
REF_CORE_DIRS = ("quantum/", "platforms/", "tmk_core/", "drivers/",
                 "builddefs/")
FORK_DIRS = ("quantum/", "platforms/", "drivers/")
NON_ERA_GATES = ("RP2040_BOOTLOADER_DOUBLE_TAP_RESET_NONBLOCKING",)


def _first_str(*values):
    for value in values:
        if isinstance(value, str) and value.strip():
            return value
    return ""


def _as_dict(value):
    return value if isinstance(value, dict) else {}


def parse_event(payload):
    """Turn Claude, Grok, and Codex PreToolUse JSON into one record."""
    payload = _as_dict(payload)
    inp = _as_dict(payload.get("tool_input") or payload.get("toolInput"))
    name = _first_str(
        payload.get("tool_name"),
        payload.get("toolName"),
        payload.get("tool"),
    )
    key = name.strip().lower().replace(" ", "_")
    if key in READ_NAMES:
        kind = "read"
    elif key in GLOB_NAMES:
        kind = "glob"
    elif key in GREP_NAMES:
        kind = "grep"
    elif key in BASH_NAMES:
        kind = "bash"
    else:
        kind = "other"

    command = _first_str(inp.get("command"))
    file_path = _first_str(
        inp.get("file_path"),
        inp.get("target_file"),
        inp.get("file"),
    )
    path = _first_str(
        inp.get("path"),
        inp.get("target_directory"),
    )
    pattern = _first_str(inp.get("pattern"))
    if kind == "bash" and command:
        shell_paths = shell_read_paths(command)
        if shell_paths and not file_path:
            file_path = shell_paths[0]
    probe = " ".join(part for part in (file_path, path, pattern, command) if part)
    probe = probe.replace("\\", "/")
    session_id = _first_str(
        payload.get("session_id"),
        payload.get("sessionId"),
    )
    return {
        "kind": kind,
        "tool_name": name,
        "command": command,
        "file_path": file_path,
        "path": path,
        "pattern": pattern,
        "probe": probe,
        "session_id": session_id,
        "raw": payload,
    }


def shell_read_paths(command):
    if not SHELL_READ_RE.search(command):
        return []
    paths = []
    for token in command.replace("\\", "/").split():
        cleaned = token.strip("\"'")
        if "/" in cleaned or any(
            cleaned.lower().endswith(ext)
            for ext in (".c", ".h", ".mk", ".md", ".py", ".txt", ".json")
        ):
            if cleaned not in ("/", ".", ".."):
                paths.append(cleaned)
    return paths


def is_bash_search(command):
    return any(tok in command for tok in BASH_SEARCH_TOKENS)


def classify(event):
    if COMMIT_RE.search(event["command"]):
        return "commit"
    if event["kind"] in ("read", "glob"):
        return "read"
    if event["kind"] == "bash" and shell_read_paths(event["command"]):
        return "read"
    if event["kind"] == "grep" or (
        event["kind"] == "bash" and is_bash_search(event["command"])
    ):
        return "search"
    return None


def is_exempt(probe):
    return any(marker in probe for marker in EXEMPT_MARKERS)


def claude_payload_for_graphify(event, kind):
    """graphify hook-guard still reads Claude snake_case keys only."""
    tool_input = {}
    if kind == "search":
        if event["kind"] == "grep":
            tool_input["pattern"] = event["pattern"] or "*"
            if event["path"]:
                tool_input["path"] = event["path"]
            tool_name = "Grep"
        else:
            tool_input["command"] = event["command"]
            tool_name = "Bash"
    else:
        tool_name = "Read" if event["kind"] != "glob" else "Glob"
        if event["file_path"]:
            tool_input["file_path"] = event["file_path"]
        if event["path"]:
            tool_input["path"] = event["path"]
        if event["pattern"]:
            tool_input["pattern"] = event["pattern"]
    return {
        "tool_name": tool_name,
        "tool_input": tool_input,
        "session_id": event["session_id"],
    }


def graphify_deny_reason(stdout):
    try:
        data = json.loads(stdout)
    except Exception:
        return ""
    spec = _as_dict(data.get("hookSpecificOutput"))
    if spec.get("permissionDecision") == "deny":
        return str(
            spec.get("permissionDecisionReason")
            or data.get("reason")
            or "graphify strict mode denied this read"
        )
    if data.get("decision") in ("deny", "block"):
        return str(data.get("reason") or "graphify denied this call")
    return ""


def relay_graphify(kind, event):
    args = [sys.executable, "-m", "graphify", "hook-guard", kind]
    if kind == "read":
        args.append("--strict")
    env = os.environ.copy()
    env.setdefault("CLAUDE_PROJECT_DIR", str(REPO))
    env.setdefault("GROK_WORKSPACE_ROOT", str(REPO))
    proc = subprocess.run(
        args,
        input=json.dumps(claude_payload_for_graphify(event, kind)),
        capture_output=True,
        encoding="utf-8",
        errors="replace",
        cwd=str(REPO),
        env=env,
    )
    if proc.stderr:
        sys.stderr.write(proc.stderr)
    stdout = proc.stdout or ""
    reason = graphify_deny_reason(stdout)
    if reason:
        sys.stderr.write(reason + "\n")
        return 2
    if stdout:
        sys.stdout.write(stdout)
    return 0


def run_read(event):
    if not event["probe"].strip():
        return 0
    if is_exempt(event["probe"]):
        return 0
    return relay_graphify("read", event)


def run_search(event):
    return relay_graphify("search", event)


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


def run_commit(event):
    command = event["command"]
    if STAGING_RE.search(command):
        sys.stderr.write(
            "This command stages and commits in one call, and this gate runs "
            "before the command does -- it would inspect an index that does "
            "not yet hold what you are about to commit, pass, and check "
            "nothing.\n\nRun the staging and the `git commit` as two separate "
            "calls. The gate can read the staged set in the second one.\n"
        )
        return 2

    commits_all = bool(COMMIT_ALL_RE.search(command))
    problems, notes = [], []
    check_whitespace(commits_all, problems, notes)
    changed = name_only("diff", "--cached", "--name-only")
    if commits_all:
        changed |= name_only("diff", "--name-only")
    if any(path.startswith(FORK_DIRS) for path in changed) or any(
        path.endswith("era_qmk_fork_ledger.md") for path in changed
    ):
        check_fork_ledger(problems, notes)
    if doc_layer_armed(changed):
        check_doc_layer(problems, notes)

    if problems:
        sys.stderr.write("\n\n".join(problems) + "\n")
        return 2
    if notes:
        print(
            json.dumps(
                {
                    "hookSpecificOutput": {
                        "hookEventName": "PreToolUse",
                        "additionalContext": "\n\n".join(notes),
                    }
                }
            )
        )
    return 0


def dispatch(payload):
    event = parse_event(payload)
    action = classify(event)
    if action == "commit":
        return run_commit(event)
    if action == "read":
        return run_read(event)
    if action == "search":
        return run_search(event)
    return 0


def main():
    raw = sys.stdin.read()
    payload = json.loads(raw) if raw.strip() else {}
    return dispatch(payload)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        sys.stderr.write(f"era_pretooluse.py did not run: {exc}\n")
        sys.exit(0)
