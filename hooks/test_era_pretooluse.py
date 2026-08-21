#!/usr/bin/env python3
# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later
"""Classify Claude / Grok / Codex hook payloads without calling graphify."""

import json
import subprocess
import sys
from pathlib import Path

import era_pretooluse as gate

REPO = Path(__file__).resolve().parent.parent
SHARED_COMMAND = (
    'python "$(git rev-parse --show-toplevel)/hooks/era_pretooluse.py"'
)

ADAPTERS = (
    (
        "Claude",
        REPO / ".claude" / "settings.json",
        {"Bash", "Grep", "PowerShell", "Read", "Glob"},
    ),
    (
        "Grok",
        REPO / ".grok" / "hooks" / "era-pretooluse.json",
        {"run_terminal_command", "read_file", "list_dir", "grep"},
    ),
    (
        "Codex",
        REPO / ".codex" / "hooks.json",
        {"Bash"},
    ),
)


def check(name, cond):
    if not cond:
        raise SystemExit(f"FAIL: {name}")
    print(f"ok  {name}")


def test_adapter_contract():
    """Every host owns mechanics only and reaches the same gate once."""
    handlers = {}
    for name, path, required_matchers in ADAPTERS:
        data = json.loads(path.read_text(encoding="utf-8"))
        groups = data.get("hooks", {}).get("PreToolUse", [])
        check(f"{name} one PreToolUse group", len(groups) == 1)
        group = groups[0]
        matcher_names = {
            part.strip() for part in group.get("matcher", "").split("|")
            if part.strip()
        }
        check(
            f"{name} native matcher coverage",
            required_matchers <= matcher_names,
        )
        group_handlers = group.get("hooks", [])
        check(f"{name} one shared handler", len(group_handlers) == 1)
        handler = group_handlers[0]
        check(f"{name} command handler", handler.get("type") == "command")
        check(f"{name} shared command", handler.get("command") == SHARED_COMMAND)
        check(f"{name} bounded timeout", handler.get("timeout") == 60)
        handlers[name] = handler

    check(
        "Codex Windows command is the shared command",
        handlers["Codex"].get("commandWindows") == SHARED_COMMAND,
    )


def test_parse_and_classify():
    claude_read = gate.parse_event({
        "tool_name": "Read",
        "tool_input": {"file_path": "keyboards/era/common/split/era_split_link.c"},
    })
    check("claude read kind", claude_read["kind"] == "read")
    check("claude read class", gate.classify(claude_read) == "read")
    check(
        "claude source not exempt",
        not gate.is_exempt(claude_read["probe"]),
    )

    grok_read = gate.parse_event({
        "toolName": "read_file",
        "toolInput": {"target_file": "keyboards/era/common/split/era_split_link.c"},
    })
    check("grok read maps file", grok_read["file_path"].endswith("era_split_link.c"))
    check("grok read class", gate.classify(grok_read) == "read")

    docs = gate.parse_event({
        "toolName": "read_file",
        "toolInput": {"target_file": "keyboards/era/common/docs/era_active_index.md"},
    })
    check("docs exempt", gate.is_exempt(docs["probe"]))

    grok_commit = gate.parse_event({
        "toolName": "run_terminal_command",
        "toolInput": {"command": "git commit -m test"},
    })
    check("grok commit class", gate.classify(grok_commit) == "commit")

    grok_stage = gate.parse_event({
        "toolName": "run_terminal_command",
        "toolInput": {"command": "git add hooks && git commit -m x"},
    })
    check("stage+commit class", gate.classify(grok_stage) == "commit")

    grok_grep = gate.parse_event({
        "toolName": "grep",
        "toolInput": {"pattern": "SCAN_DWELL", "path": "keyboards/era"},
    })
    check("grok grep class", gate.classify(grok_grep) == "search")

    claude_rg = gate.parse_event({
        "tool_name": "Bash",
        "tool_input": {"command": "rg SCAN_DWELL keyboards/era"},
    })
    check("bash rg class", gate.classify(claude_rg) == "search")

    codex_cat = gate.parse_event({
        "tool_name": "Bash",
        "tool_input": {"command": "cat keyboards/era/common/split/era_split_link.c"},
    })
    check("codex cat is read", gate.classify(codex_cat) == "read")
    check("codex cat path", "era_split_link.c" in codex_cat["file_path"])

    patch = gate.parse_event({
        "tool_name": "apply_patch",
        "tool_input": {"command": "*** Begin Patch"},
    })
    check("apply_patch not a read", gate.classify(patch) is None)

    empty = gate.parse_event({})
    check("empty is idle", gate.classify(empty) is None)


def test_dispatch_routes():
    """Host payload differences stop at the shared gate's parser."""
    calls = []
    original = gate.relay_graphify

    def capture(kind, event):
        calls.append((kind, event["kind"], event["tool_name"]))
        return 17

    gate.relay_graphify = capture
    try:
        rc = gate.dispatch({
            "toolName": "read_file",
            "toolInput": {
                "target_file": "keyboards/era/common/split/era_split_link.c",
            },
        })
        check("Grok read reaches graphify", rc == 17)

        rc = gate.dispatch({
            "tool_name": "Grep",
            "tool_input": {
                "pattern": "SCAN_DWELL",
                "path": "keyboards/era",
            },
        })
        check("Claude grep reaches graphify", rc == 17)

        rc = gate.dispatch({
            "tool_name": "Bash",
            "tool_input": {
                "command": "type keyboards/era/common/split/era_split_link.c",
            },
        })
        check("Codex shell read reaches graphify", rc == 17)

        rc = gate.dispatch({
            "tool_name": "Bash",
            "tool_input": {"command": "git status"},
        })
        check("non-gated shell command stays idle", rc == 0)
    finally:
        gate.relay_graphify = original

    check(
        "dispatch routes in host order",
        calls == [
            ("read", "read", "read_file"),
            ("search", "grep", "Grep"),
            ("read", "bash", "Bash"),
        ],
    )


def test_graphify_decision_bridge():
    deny = json.dumps({
        "hookSpecificOutput": {
            "hookEventName": "PreToolUse",
            "permissionDecision": "deny",
            "permissionDecisionReason": "query the graph first",
        },
    })
    check(
        "Graphify deny reason crosses hosts",
        gate.graphify_deny_reason(deny) == "query the graph first",
    )
    check(
        "Graphify context-only output does not deny",
        gate.graphify_deny_reason(json.dumps({
            "hookSpecificOutput": {
                "hookEventName": "PreToolUse",
                "additionalContext": "graph available",
            },
        })) == "",
    )
    check(
        "malformed Graphify output does not deny",
        gate.graphify_deny_reason("x") == "",
    )


def run_script(payload):
    proc = subprocess.run(
        [sys.executable, str(REPO / "hooks" / "era_pretooluse.py")],
        input=json.dumps(payload),
        capture_output=True,
        encoding="utf-8",
        errors="replace",
        cwd=str(REPO),
    )
    return proc.returncode, proc.stdout, proc.stderr


def test_live_stdin():
    rc, _, err = run_script({
        "toolName": "run_terminal_command",
        "toolInput": {"command": "git add x && git commit -m x"},
    })
    check("stage+commit exit 2", rc == 2)
    check("stage+commit stderr", "stages and commits" in err)

    rc, _, _ = run_script({
        "toolName": "read_file",
        "toolInput": {"target_file": str(REPO / "AGENTS.md")},
    })
    check("exempt agents.md exit 0", rc == 0)

    rc, _, _ = run_script({
        "tool_name": "Read",
        "tool_input": {"file_path": "keyboards/era/common/docs/era_active_index.md"},
    })
    check("exempt docs exit 0", rc == 0)

    rc, _, _ = run_script({
        "toolName": "run_terminal_command",
        "toolInput": {"command": "git status"},
    })
    check("plain git status idle", rc == 0)

    for filename, label in (
        ("era_commit_gate.py", "old commit shim"),
        ("era_graphify_read_guard.py", "old read shim"),
    ):
        shim = subprocess.run(
            [sys.executable, str(REPO / ".claude" / "hooks" / filename)],
            input=json.dumps({
                "tool_input": {"command": "git add x && git commit -m x"},
            }),
            capture_output=True,
            encoding="utf-8",
            errors="replace",
            cwd=str(REPO),
        )
        check(f"{label} still denies", shim.returncode == 2)


if __name__ == "__main__":
    test_adapter_contract()
    test_parse_and_classify()
    test_dispatch_routes()
    test_graphify_decision_bridge()
    test_live_stdin()
    print("all tests passed")
