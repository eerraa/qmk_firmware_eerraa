@AGENTS.md
@keyboards/era/common/docs/era_active_index.md

## Claude Code adapter

The adapter layer defined in AGENTS.md "Agent Layer Ownership": mechanism and
environment only.

The imports above are the mechanism for the AGENTS.md Startup Read
Policy: the whole startup chain loads deterministically at session start
instead of relying on the session remembering to read it. Canon is unchanged;
other tools follow the policy as written.

### Read routing

- `.claude/rules/*.md` mirror the Task Read Matrix as path-scoped rules: when
  a session touches a file in a task area, the matching rule loads and names
  that area's canonical read set. The matrix in `era_active_index.md` stays
  the authority; the rules only route to it.
- Commit-time checks are a git pre-commit hook (`hooks/pre-commit` →
  `hooks/era_commit_check.py`), armed once per clone by
  `git config core.hooksPath hooks` (Machine setup). It runs `era_doc_refs.py`
  on any commit touching the agent-document layer or an ERA-commented
  `.c`/`.h`/`.mk`, the fork-ledger check on any commit touching
  `quantum/ platforms/ drivers/` or the ledger, and
  `git diff --cached --check` always; a deletion of doc lines prints the
  `--homeless` reminder. Claude has no project hooks; `.claude/settings.json`
  carries only `permissions.deny`.

### Push

`permissions.deny` no longer lists `git push` (owner decision 2026-07-28, on
the first push this repository actually wanted). The rule it mechanized is
unchanged and canonical in `AGENTS.md`: do not push either branch
unless the user explicitly requests it. The destructive-restore denies
(`reset --hard`, `checkout --`, `restore`) stay.

**The two paragraphs below describe the repository this firmware is developed
in, and a clone of the published one has neither branch.** They are kept
because a session on the development tree needs them, and scoped because a
session anywhere else would otherwise run commands that resolve to nothing.

`main` is a curated completion branch, not a merge target: its commits are
development checkpoint trees replayed in order with `read-tree`, never
hand-split diffs, so a `git merge` from a development branch is the wrong
operation there and every `main` tree equals some development commit's tree
exactly.

`release/clean-repo` is a four-commit orphan whose **tip tree must equal the
working branch's HEAD tree exactly**, so any change to the tree invalidates it
and it is rebuilt with `read-tree`/`commit-tree` rather than merged into. Check
it before believing it: `git rev-parse release/clean-repo^{tree}` against
`git rev-parse HEAD^{tree}`.

### Machine setup

- On Windows, set `core.autocrlf=true` in each submodule config so the
  submodules read clean. `git config core.longpaths true` is also worth setting
  before cloning, but **this tree does not demonstrate the need**: measured
  2026-08-18, the longest path in a complete checkout including submodules is
  160 characters relative to the repository root, which leaves a hundred
  characters of headroom against Windows' 260. Re-derive with
  `find . -path ./.git -prune -o -type f -print | awk '{print length($0)-2}' | sort -rn | head -1`.
  A `--recursive` clone that pulls `lib/chibios-contrib/ext/mcux-sdk` is the
  case that plausibly needs it, and that submodule is not checked out here.
- `git config core.hooksPath hooks` — once per clone, in the edit tree. The
  WSL build tree never commits and needs none.

### WSL2 build environment

Firmware is built in WSL2 Ubuntu, not on Windows. Editing happens in the
Windows checkout at `D:\Engineering\qmk_firmware_eerraa`; the WSL clone at
`~/projects/qmk_firmware_eerraa` is a build tree only. Both produce a
byte-identical UF2 from the same commit, so artifacts are interchangeable.

**This section describes one machine, and it does not install itself.** The
scripts are versioned at `.claude/tools/era-sync.sh` and `era-build.sh`;
`~/bin/era-sync` and `~/bin/era-build` are symlinks to them. The links point at
the **edit** tree's copy deliberately — `era-sync` resets the build tree, and a
link into that tree would have the script rewritten under itself mid-run. Each
file carries its machine-specific paths in one marked block at the top, and
that block plus a WSL install is the whole of what a new machine needs.

None of it installs automatically. So when `era-build` is not found, when WSL
is not installed, or when a path here does not resolve, **stop and tell the
user which one failed**. Copying `.claude/tools/` into place and editing the
marked block is a decision for the user to make, not a step to take on the way
to a build. Do not install a toolchain, and above all do not fall back to
building on Windows — that is the failure the launcher refuses, and reaching it
by improvisation is the same wrong answer arrived at more slowly. The rule this
serves is in `era_performance_gates.md`.

- The `qmk` CLI installs and injects its own ARM toolchain at
  `~/.local/share/qmk/bin`. Ubuntu separately ships `arm-none-eabi-*` 13.2.1 in
  `/usr/bin`, which shadows it for everything that is not `qmk compile` —
  including the `arm-none-eabi-size`/`nm` calls the gate launcher makes.
  `~/.profile` puts the qmk toolchain first; confirm with
  `arm-none-eabi-gcc --version` reporting 15.2.0.
- `era-sync` guarantees one thing: after it returns, the build tree matches
  the edit tree. It converges on the edit tree's commit when the heads differ,
  replays the uncommitted delta whole-tree, and ends by comparing the two
  trees' `git status` and failing on any difference — that equality is the
  check, not the file count. How it does each of those, and why, is commented
  in `.claude/tools/era-sync.sh`; do not restate it here. `git remote windows`
  points at the `/mnt/d` checkout.
- `era-sync` copies with `rsync -rlt`, which preserves mtimes, so an
  incremental build can report a changed source as up to date. When a
  before/after comparison is the evidence, `touch` the changed files or delete
  `.build/obj_*` between the two states.
- `~/.profile` exports
  `ERA_EDIT_TREE=/mnt/d/Engineering/qmk_firmware_eerraa`. The gate launcher
  reads it to stop a build whose tree is stale against the edit tree, and
  records the outcome as `edit_tree_check=` in the manifest. The rule this
  serves is canonical in `era_performance_gates.md`; only the path is
  Claude-and-machine specific.
- `era-build` is the whole loop in one command, runnable from the Windows side.
  The `keyboard:keymap` target is mandatory, so a TOMAK_TKL request cannot
  silently select TOMAK79H:

  ```powershell
  wsl -d Ubuntu -e bash -lc 'era-build era/sirind/tomak:via'
  wsl -d Ubuntu -e bash -lc 'era-build era/sirind/tomak:via cause'
  wsl -d Ubuntu -e bash -lc 'era-build era/sirind/tomak79h:via standard wire qwin cause'
  ```

  An omitted variant means `standard` for every target. Variant names are
  board-independent; the canonical make layer refuses one the target cannot
  support. The adapter syncs, runs the gate launcher per variant, copies the artifacts back to
  the Windows `.era-artifacts/`, and prints each build's `worktree_dirty`,
  `edit_tree_check`, and free-ram0 beside its firmware name. It copies only the
  files declared by the manifests from that invocation; old WSL artifacts do
  not ride back with a new build. The sync is not optional and runs first every
  time, because skipping it is this setup's one silent failure. Four variants
  end to end take about 25 s. `.claude/tools/era-build.sh` implements this
  sequence and `keyboards/era/common/tools/era_qmk_build.sh` is its internal
  target launcher; invoking the latter directly is refused.
- Read each manifest path from the launcher's own `Manifest:` line rather than
  globbing `.era-artifacts/`. Artifact names include the first 16 hexadecimal
  digits of the firmware SHA-256, so a different dirty binary cannot overwrite
  the earlier one, but a glob can still select a previous run rather than the
  manifest returned now.
- Flashing stays on Windows: UF2 is a file copy to the RPI-RP2 mass-storage
  device.

## Compact instructions

When compacting, preserve in this order: the current task's decision blocks
and constraints from the prompt that started it; the exact position in the
work (branch, last commit hash and message, what is staged, what remains);
and any evidence or failure output not yet acted on. Do not preserve the
content of files in the repository — the agent-document layer and source
reload from disk, and the startup chain re-imports itself — and do not
preserve tool outputs already reflected in a commit. A summary that keeps
run state and drops re-readable content loses nothing.
