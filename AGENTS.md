# ERA Agent Index

This is the first document agents read for ERA firmware work. It is a
read-policy and storage map, not a session plan.

ERA is one QMK firmware family over a shared common layer at
`keyboards/era/common/`: 23 boards with a `keyboard.json`, split and non-split.
The 22 RP2040 boards all take the copy-to-RAM image and the ERA matrix engine;
`sirind/brick65` (atmega32u4) is a permanent exception. The architecture model
and the glossary are canonical in
`keyboards/era/common/docs/contracts/era_overview.md`, read first.

## Agent Layer Ownership

Agent documentation has two layers, and which layer a rule lives in is not a
style preference.

**Tool-neutral canon** — the authority. Every agent, every tool.

- `AGENTS.md` — project identity, how to work on this firmware, document
  management rules, startup read policy.
- `keyboards/era/common/docs/` — `era_active_index.md`, the router into
  everything below.
- `keyboards/era/common/docs/contracts/`, `maps/`, `manuals/` — the agent
  document set, one directory per `Genre:`. The header on each document is the
  authority on its genre; the directory only groups them.
- `keyboards/era/common/docs/user/` — the user-facing firmware docs.

**Tool adapter** — mechanism only.

- `CLAUDE.md` — the Claude Code entry point. Imports `AGENTS.md`.
- `.claude/` — Claude wiring: settings, path-scoped rules, and the committed build scripts.
- `hooks/pre-commit` + `hooks/era_commit_check.py` — the one commit-time
  check, host-neutral: git runs it, so Claude, Codex, Grok and the owner's
  terminal share one path
- `hooks/test_era_commit_check.py` — its conformance test: wiring, absence of
  any host PreToolUse path, and an end-to-end refused commit
- Any equivalent entry point another tool needs.

An adapter may describe *how* a tool reaches or enforces canon; it may never
define a project rule, a contract, a read policy, or a fact about the
firmware. When an adapter and canon disagree, canon wins and the adapter is
the bug. Adding a second tool must not require editing canon — if it would,
what it needs was misfiled in canon. Environment and installation steps are
always adapter, because canon is what must be true on every machine.

Canon carries no generic coding-hygiene layer, deliberately. A portable
"do not assume / keep it simple / stay surgical" document was read at startup
for the whole project history and did not prevent the failures it described.
What changed behavior was mechanism — the commit-time checks, the
  static-capacity asserts, the source/ELF gates, and the device-evidence rule.
A rule earns a place in canon by being ERA-specific, or by being enforced by
something other than the agent remembering it.

## Working On This Firmware

### Scope

- Name the governing active document before non-trivial edits.
- Start architecture lookup from `era_active_index.md`. The read set — the
  startup chain plus the task read matrix — is canonical only there.
- **If the documents and the source disagree, stop before editing behaviour**
  and report concrete source references and options. The documents are written
  to be checkable for exactly this reason; a disagreement is a finding, not a
  detail to work around.
- If source review shows a simpler implementation that satisfies the same
  accepted goals with less state, lower wire cost, or less scan-bound work,
  propose it before implementing the heavier documented path.

### Change Rules

- For shared split behavior, prefer `split`.
- Keep hot-path decisions O(1) wherever practical.
- Do not add allocation, broad source scans, CRC construction, EEPROM/RGB/INPUT
  freshness calculation, or diagnostics snapshot construction to scan-bound
  router paths unless an active document explicitly accepts that cost.
- Keep diagnostics-only state out of release hot paths.

### Verification

Every non-trivial change states what was verified and what was not.

Which checks a change owes is canonical in
`manuals/era_performance_gates.md`, scoped to what the change touched: a
targeted build for touched source behavior, run in the supported build
environment from a tree synchronized to the change; the Source Gate for
closed-surface, retired-path and QMK core matrix changes; the Refactor
Self-Check tier for a change whose whole claim is that it touched no
behaviour. **A change that touches no source owes none of them, and saying so
is the verification statement.** The commit-time checks run from
`hooks/pre-commit`; a commit made with `--no-verify` is not a verified commit.

### Evidence And Retirement

- **Device evidence outranks source inference.** When a model of the mechanism
  and a measurement disagree, the measurement is right. Do not patch from a
  mechanism you have not observed: one guard here was built on an assumed cause
  twice, shipped inert both times, and had to be unfused before the third
  attempt found the real one. The rule runs in both directions — refusing a
  correct change by reasoning about a mechanism, without reading the paths that
  already exercise it, is the same error pointed the other way.
- **Record compact signatures only.** Raw device logs and raw EEPROM bytes
  never enter a document or a commit message. This is the single statement of
  that rule; other documents rely on it rather than repeating it.
- Keep the documents focused on current contracts, maps and procedures.
- **A superseded fact is replaced, not appended beside its replacement**, and
  a closed plan is deleted rather than marked closed. Do not create a parallel
  archive tree or a struck-through entry: both are still indexed, still read
  out of context, and still carry a header that can be mistaken for current.
- What survives a close is whatever still governs behavior, and it belongs in
  the contract, map or manual that owns it.

### Commits

- Separate commits by concern: contract content, source implementation,
  diagnostics-only change, index or policy update.
- **A commit that deletes documented content carries the reasoning that
  content held**, because the deletion is otherwise the one unrecoverable move
  available.
- **Do not push unless the user explicitly asks.**

### The Final Report

A final answer states: what behaviour or document scope changed; the
verification check and its result; the commit any build evidence came from;
and what was not verified, with why.

## Document Management Rules

- Keep only document roles, read policy, and the storage map here. Do not
  store session prompts, temporary command rules, temporary plans, raw logs,
  or release notes.
- The ERA agent document set is `keyboards/era/common/docs/contracts/`,
  `maps/` and `manuals/` plus the index above them, and that is the whole set.
  When and how a document retires is **Evidence And Retirement** below.
- Do not duplicate the same fact across documents. Put each rule in its
  canonical active document and link to it elsewhere. Each document's own
  `Canonical for:` header is the authority on its scope, which is why this
  file keeps no second list of document roles to drift against it.
- If the document structure changes, update this file and
  `keyboards/era/common/docs/era_active_index.md` in the same change.

### Genre decides what kind of sentence a document may hold

Every agent document opens with two lines, `Genre:` and `Canonical for:`; the genre is one of:

- **contract** — what must be true. A rule stands until the design changes.
- **map** — where a thing lives and what a name means. A row stands until the
  code moves.
- **manual** — how to run something. A procedure stands until the instrument
  changes, and it may name the image a leg was proved on, because that is the
  leg's entry condition and not a report about a day.
- **state** — what is owed, what is waived, what a sitting measured.
- **entry** — the startup chain itself: the read policy and the router.

**There is no state document, and that is the current shape rather than an
omission.** The genre exists because a development campaign produces sentences
no other genre may hold, and this firmware's campaign is finished: what it
measured that still governs behaviour was moved into the contract or the manual
that owns it, and what it measured about particular days was deleted. A future
campaign may need one again; until then, an entry that wants to be state is a
sign the work has restarted, not a sign the set is missing a file.

The line is not the date, it is who the sentence is written for. Evidence a
rule cites to explain itself stays with the rule — an owner ruling names the
sitting it was made on, and a fixed baseline names what it was measured on.
What belongs in a state document is the *report*: what ran, what passed, what
is still owed. So a dated run report, a pass tally and a `cause_` image stamp
do not belong in a contract, a map or the entry layer. A missing or unknown
`Genre:`, an empty `Canonical for:`, or a retired
`Status:`/`Read when:` line is refused by `era_doc_refs.py`; the sentence *shapes* are not, and
nothing mechanises them now — the tripwire that did was built when this project
produced such sentences daily and retired with the campaign that produced them.
This is the rule, and a writer keeps it.

### Which layer a rule goes in is decided by who has to be looking elsewhere

Source comments and the document set are the same size — measured 2026-08-18,
the agent document set is 93,853 words and ERA's C comments 94,204, a 1:1
investment. So "put it in both" is not a rounding error and "put it in one" is
not obviously cheaper. The line:

**A constraint on one file's own contents goes in that file, and a document
names it without restating it. A constraint that binds files the editor is not
looking at goes in the contract, and the site gets a pointer.**

That is the whole rule, and it decides both directions. A cadence constant with
a `_Static_assert` beside it is the file's own business — a contract paragraph
restating the arithmetic is a worse copy that drifts, and the copy that drifts
is the document, because the compiler sits next to the other one. A rule about
which of two headers may define a name, or about what three other units do with
a mask, cannot be enforced by the file that happens to hold it and belongs in
the contract.

**Neither layer is a trustworthy single source, which is why there are two.**
Both drift, independently and in both directions: the sweep that produced this
rule found a document stale where its comment was current and a comment stale
where its document was current, in the same header. What settles it is not care
but mechanism — on 2026-08-17, before `era_doc_refs.py` grew its source-comment
arm, six of seven `path:line` citations in ERA source comments pointed
somewhere else against zero findings in the document layer the same day. The
checked surface was clean and the unchecked one was not.

### A claim about what a function does names the file it lives in

A document that says `foo()` returns, clears, gates or holds something is
making a claim about the tree, and a claim nobody can locate is a claim nobody
re-checks. Measured 2026-08-15: the set held 41 such paragraphs naming no file
against 38 that named one, and the two worst defects the cross-document sweep
found were both of this shape — prose describing behaviour the source had
already stopped having, in one case a full day after the fix shipped.

So the paragraph carries the file. **The unit is the paragraph, not the
sentence**: a path in every sentence would cost more context than the check
saves, and a reader needs it once. Repository-relative for core files
(`quantum/keyboard.c`), common-layer-relative for ERA
(`split/era_host_peer_storage.c`). A line number is optional and rots — six of
twenty-four were pointing somewhere else when they were last checked — so add
one only where the file alone would leave a reader searching.

`era_doc_refs.py` refuses a paragraph that claims what a
resolvable function does and names no file, and refuses a cited file or line
that does not exist. What it cannot do is tell whether the *claim* is still
true; naming the file is what makes finding out cheap.

### A refusal is three lines, and it lives where the decision is made

A design that was tried and rejected is worth recording, because an agent's
default is to propose the locally obvious simplification and this project has
paid for that repeatedly. **What earns the space is the constraint, not the
story of how it was found.** The constraint changes the next decision; the
story is a second copy of a commit message.

The form is fixed so that it stays a constraint:

```text
> **REFUSED:** what was proposed
> **WHY:** the consequence that refuses it, in one sentence
> **REOPENS:** the evidence or change that would make it live again
```

Three lines, roughly thirty words, placed in the contract next to the rule it
protects — never gathered into a list of their own, because a refusal read away
from its decision is a fact nobody applies. `REOPENS` is what keeps it a rule
rather than a tombstone: a refusal with no reopening condition is either
permanent, and says so, or was never a decision.

**`WHY` has to stand on its own, because nothing stands behind it.** The block
carried a fourth line naming the commit that settled it, on the trade that one
`git show` beats loading the whole account into every session that never asks.
The trade is gone because the reader of these documents is holding the shipped
orphan, where that `git show` reaches a commit containing the whole firmware and
therefore nothing — so the consequence sentence is now the entire argument.
**A session on the development branch can run the `git show` and will get an
answer; that it works here is exactly why the line has to go.** Write it as the
thing a
reader must believe, not as a label on an argument kept elsewhere — and if it
cannot be written that way, the refusal was a preference rather than a
constraint and does not belong here.

## Startup Read Policy

Always read, in this order:

1. `AGENTS.md`
2. `keyboards/era/common/docs/era_active_index.md`

Then read only the task-specific active documents named by the active index.
**If you do not already know this firmware, read
`keyboards/era/common/docs/maps/era_walkthrough.md` once first** — it follows
five paths end to end and names the file at every step, which is what makes the
contracts decode on first contact.

Do not bulk-load all active documents. **No document delegates a fact to a
commit**, and none may: what ships is a four-commit orphan on which `git log -S`
answers nothing, whatever the working branch answers. The measurement and the
rule it serves are in `era_active_index.md`'s **What This Repository Does Not
Carry**.

The `docs/user/` tree is written for keyboard owners, not agents. Read it only
when the prompt asks for release or user documentation, and do not shorten it
to agent-context standards - it has a different reader.

## Navigation

Structure questions are answered by the router (`era_active_index.md`) and a
source search (`git grep -n`, `rg`); there is no derived index to consult or
regenerate, and none may be reintroduced as an obligation.

> **REFUSED:** a mandatory knowledge graph, a search-before-read hook, or per-session generated context as the navigation layer.
> **WHY:** the last one cost 424 MiB of local state and 9.5 MB tracked, a process on every shell call, and ~0.7 s per query against 0.1 s for a search, while its natural-language answers spread across hundreds of nodes; the router and a search answer the same questions.
> **REOPENS:** a navigation tool whose per-call cost is below a shell search and whose answers are checked by something other than the agent reading them.
