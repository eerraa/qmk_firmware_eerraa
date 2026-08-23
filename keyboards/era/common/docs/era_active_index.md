# ERA Active Index

Status: active
Genre: entry
Canonical for: the current-state summary and the task read matrix — the router
from a task area into the smallest correct set of active documents
Read when: after `AGENTS.md`

## Current State

`keyboards/era` holds 23 boards with a `keyboard.json`, split and non-split,
over one shared common layer at `keyboards/era/common/`. The mental model and
the glossary are canonical in `contracts/era_overview.md`.

- **One runtime architecture.** Every serviced relation runs the same standing
  exchange on core1. The invariant is canonical in `contracts/era_invariants.md`;
  the routes, grants, cadences and poll period values in
  `contracts/era_route_contract.md`.
- **One load image.** Every ERA board and keymap builds the copy-to-RAM
  SRAM-resident image with the ERA matrix engine; `sirind/brick65` is
  atmega32u4 and takes none of it — a permanent exception, not a debt. The
  policy is canonical in `manuals/era_board_adoption.md`'s **Copy-To-RAM
  Policy**,
  the placement, budget and boot ordering in
  `contracts/era_sram_residency_contract.md`.
- **The firmware is finished and in use.** What a *new* change must
  demonstrate before it is believed, and every standing figure it must not
  regress, are canonical in `manuals/era_performance_gates.md`. This page does
  not restate them and carries no record of what past work ran.
- **A split pair's two halves run one identical image and are flashed
  together.** Mixed versions are not a supported configuration, and the rule
  that no code reads a format an earlier firmware stored is canonical in
  `maps/era_source_map.md`'s **Stored-Data Compatibility**.

The document set is `docs/contracts/`, `docs/maps/` and `docs/manuals/`, one
directory per genre, plus this index above them — and that is the whole of it.
**A row below names a document by its path, and the path is the genre**, which
is what makes a Change column that reaches into `manuals/` visible as the
mistake it usually is. When and how a document retires is canonical in
`AGENTS.md`'s **Evidence And Retirement**.

## Always-On Active Reads

For non-trivial ERA work, read:

1. `contracts/era_overview.md`
2. `contracts/era_invariants.md`

Then choose only the task-specific set below.

**`maps/era_walkthrough.md` is in neither list above, and both omissions are
deliberate.** `AGENTS.md`'s Startup Read Policy is where a first reader is sent
to it, and it is not always-on because a second reading buys nothing: it
defines no rule and every fact in it is canonical elsewhere. What it buys the
first time is the other axis — it is the only document organised by *path*
rather than by surface: one keyboard pass, a key on each kind of half, a config
change reaching both halves, boot to the wire opening, and suspend through
wake, each naming the file at every step. The contracts are each written about
one surface and assume the others, so a first reader who goes straight into the
matrix pays for that assumption in every row.

Everything else is conditional, and the matrix's three columns are where that
condition is written. A row's Verify column is read when the work actually
produces a build, a capture or a judgment about a figure, and not otherwise.

## Task Read Matrix

**Three columns, because there are three different reasons to open a document,
and only the first is unconditional.**

- **Change** — the contracts the task's own behaviour is defined by. Read
  before editing, always, in the order given.
- **Locate** — a lookup: which file owns this, what does this console field
  mean. Consulted mid-task, never read through.
- **Verify** — read when the change actually produces a build, a capture or an
  acceptance judgment, and not otherwise.

A row's real cost is its Change column plus whichever of the other two the work
reaches. **Reading all three of every row is the bulk-load this index
forbids** — and the reason the rows were once eight documents long is that
these three reasons used to share one list. The widest Change column in the
table is the storage row's, at three contracts on top of the always-on pair;
several rows name none at all and cost only that pair. Count a row before
opening it.

| Task area | Change — read first | Locate | Verify |
| --- | --- | --- | --- |
| The runtime section exchange, the `0x20`/`0x21` section masks, the INPUT or AUTHORITY section, a relation's runtime route, its poll cadence, the authority sample, responder admission under storage exclusivity, or the core1 standing exchange | `contracts/era_route_contract.md` **first** — the standing exchange grant and **One carrier for the response section set** govern the whole lane in both serviced relations — then `contracts/era_wire_contract.md` for the markers, bodies and eligibility, `contracts/era_closed_surface_contract.md`, `contracts/era_authority_contract.md` for the sample, and `contracts/era_host_peer_matrix_contract.md`, because the envelope this lane generalizes is the one that contract's source-push section rides. The generic rows below route to none of it, which is why this row exists | `maps/era_source_map.md` | `contracts/era_sram_residency_contract.md` for the core1 stack the standing loop occupies |
| authority, reducer, mode planner, session facts | `contracts/era_authority_contract.md` | `maps/era_source_map.md`, `maps/era_identifier_map.md` | — |
| the USB session, lighting sleep, or the suspend / frame-loss detectors | `contracts/era_authority_contract.md` for the reducer and the sleep predicate | `manuals/era_board_adoption.md`'s **Non-Split Board Baseline** for the detector model and the boundary the unit crosses, `maps/era_identifier_map.md`'s **USB Session** fields | `manuals/era_performance_gates.md` |
| the HID keyboard report itself — its width, the keyboard endpoint size, the boot-protocol fallback, the NKRO toggle, or the KKUK pulse that empties and restores the report buffer. **The row above is the wrong one and shares only the word USB**: that one is about the session a host opens, this one about the bytes that leave once it is open | `contracts/era_hid_report_contract.md` | `maps/era_identifier_map.md` for the VIA value id, `manuals/era_qmk_fork_ledger.md` for what each core file it names currently owes | `manuals/era_performance_gates.md` |
| wire payloads, compact IO, responder admission | `contracts/era_wire_contract.md`, `contracts/era_route_contract.md`, `contracts/era_closed_surface_contract.md` | `maps/era_source_map.md` | — |
| the agreed restart itself — the `RESTART_ARM` section, the act table, the arm, the confirmation, the commit deadline, or the degrade to acting alone | `contracts/era_wire_contract.md` **first** for the arm section and the AUTHORITY bits the answer rides, then `contracts/era_route_contract.md` for the standing exchange the arm rides and the cadence its publish-on-change obeys, then `contracts/era_closed_surface_contract.md` for what the cell is open for and what it may never carry. **Both users' rows below add to this one rather than replacing it**, and neither owns the mechanism | `maps/era_source_map.md` | `manuals/era_performance_gates.md`, whose eligibility-byte reading this surface moved once |
| the split link speed — the level, its apply, the Low boot meet, or the raise to the winner's stored level | the agreed-restart row above, then `contracts/era_route_contract.md` for the poll period the level scales and for why the convergence is a route-layer fact; the rule itself is canonical in `split/era_split_link.h`. **This is a wire row and not a storage one**: the level's region is sync-excluded and its whole content is one byte, so the storage rows below reach none of what governs it | `maps/era_source_map.md`, `maps/era_identifier_map.md` for the two SYSTEM value ids | `manuals/era_performance_gates.md` |
| HOST-PEER matrix source-push/cache/projection | `contracts/era_host_peer_matrix_contract.md`, `contracts/era_route_contract.md` | `maps/era_source_map.md` | — |
| storage protocol edit or storage failure path | `contracts/era_host_peer_storage_contract.md`, `contracts/era_wire_contract.md`, `contracts/era_route_contract.md` | `maps/era_source_map.md` | — |
| storage authority, capacity, or closed-surface impact of the above | add `contracts/era_authority_contract.md` and `contracts/era_closed_surface_contract.md` | — | `contracts/era_sram_residency_contract.md` for the static budget |
| DUAL-HOST storage, the convergence hint, or the storage news value | the two rows above, whole — the lane is one engine and DUAL-HOST adds no contract of its own | `manuals/era_capture_reading.md` for `news`/`pnews`, `arb` and `recency`, `maps/era_source_map.md` | `manuals/era_performance_gates.md` |
| the EEPROM CLEAN path, or the restart it performs | the agreed-restart row above, then `contracts/era_host_peer_storage_contract.md`'s **Why An EEPROM Clean Is An Agreed Restart**: why the restart is required at all, why **both** halves erase, why the clean needs no confirmation, and why the erase happens at the boot rather than before it are canonical only there | — | `manuals/era_performance_gates.md` |
| DUAL-HOST or closed-surface review | `contracts/era_authority_contract.md`, `contracts/era_route_contract.md`, `contracts/era_closed_surface_contract.md` | — | — |
| scheduler hot path, dirty/due, route execution | `contracts/era_route_contract.md` | `maps/era_source_map.md` | `manuals/era_performance_gates.md` |
| CORE1 ownership or backend regression review | `contracts/era_authority_contract.md`, `contracts/era_wire_contract.md`, `contracts/era_route_contract.md`, `contracts/era_sram_residency_contract.md` | `maps/era_source_map.md` | `manuals/era_performance_gates.md` |
| RAM placement, the load image, or the ERA RP2040 backend | `contracts/era_sram_residency_contract.md` | `maps/era_source_map.md` | `manuals/era_performance_gates.md`'s Layout Checks |
| boot/reset path, the pre-copy flash carve-out, the vector table, the core1 halt, or the core1 launch step | `contracts/era_sram_residency_contract.md` — which is also canonical for why the vector table and the launch step are read together — plus `contracts/era_authority_contract.md` when the work touches the launch step or `is_keyboard_master()`. `contracts/era_invariants.md` is already always-on and carries the boot-safety cluster | `maps/era_source_map.md`, plus the carve-out rules written at the selector list in `keyboards/era/ld/ERA_RP2040_SRAM_RESIDENT.ld` | `manuals/era_performance_gates.md` |
| any unintended bootloader entry, the double-tap magic word, or `bootmagic` at boot | the boot/reset row above | — | `manuals/era_performance_gates.md` |
| a non-split ERA board, or a feature that must work without the split layer | `contracts/era_sram_residency_contract.md` | `manuals/era_board_adoption.md`, whole | `manuals/era_performance_gates.md` for the copy-to-RAM gates every board runs |
| RGB / HSRSP / visual-sync / lock / indicator / time-anchor **that crosses the wire** | `contracts/era_wire_contract.md` **first** — every one of these is a response section and that contract holds the marker, the body layout and the per-relation eligibility, which is what most such questions actually turn on — then `contracts/era_authority_contract.md` for the policy gate and `contracts/era_host_peer_matrix_contract.md` | `maps/era_source_map.md`, `manuals/era_capture_reading.md` for the counters | — |
| a lock indicator or a lighting control that **does not** cross the wire — a non-split board's, or a VIA menu on either kind | — | `manuals/era_board_adoption.md`'s lighting-surface rule for which of the two homes it goes in, then `maps/era_identifier_map.md` for the value ids, which is where the one collision this surface can have is written down. The row above is the wrong one and shares only the word: nothing here is a response section | `manuals/era_performance_gates.md` when the change reaches the render pass |
| TOMAK79S/TOMAK migration | `contracts/era_sram_residency_contract.md`, `contracts/era_host_peer_storage_contract.md` | `maps/era_source_map.md` | `manuals/era_performance_gates.md` |
| a build selector, a feature enable/disable, or a new board/keymap combination | — | `manuals/era_build_options.md` first: it carries the classification rule that decides where a new option goes, every selector with its default, and the three rejected arrangements with the evidence that settled them. A new option is declared in `keyboards/era/era_build_options.mk` — three existing ones are not, and that manual names which and why; every fragment that reads one runs from a board's `post_rules.mk`, which QMK includes **after** the keymap's `rules.mk`, so `VIA_ENABLE` and every other QMK switch is visible to make. `maps/era_source_map.md` for which unit reads the option | `manuals/era_performance_gates.md` when the combination is offered as evidence |
| an edit to any QMK core file, or an upstream rebase | `contracts/era_invariants.md` bounds `quantum/matrix.c` and `quantum/matrix_common.c` and is already always-on | `manuals/era_qmk_fork_ledger.md` — the whole core-edit set, the gate each rides, the un-narrated remainder, and the re-derivation against pristine upstream. An ERA edit to a core file must appear there in the same change | `manuals/era_performance_gates.md`'s Source Gate for the matrix-file diff |
| adding a behaviour this firmware does not have yet | `manuals/era_feature_path.md` first — it defines no rule and names the document that owns each step, and the value is the order, because each step closes options in the next | the documents that row names | `manuals/era_performance_gates.md` |
| building or flashing on a machine that has not done it before | — | `manuals/era_build_and_flash.md`: what a machine must provide, the commands on any machine, and how an image reaches a keyboard. It names no machine — that is the tool adapter's, per `AGENTS.md` | `manuals/era_performance_gates.md` when the build is offered as evidence |
| any build, build experiment, or artifact offered as evidence | — | — | `manuals/era_performance_gates.md` |
| real-device capture or counter-delta review | — | `manuals/era_capture_reading.md` | `manuals/era_performance_gates.md` |
| documentation reorganization | `AGENTS.md`, `era_active_index.md` | `maps/era_source_map.md` | — |
| user-facing firmware/VIA docs | `user/readme.txt` for a one-piece board and `user/readme_split.txt` for a split one — two self-contained guides, because each ships beside one keyboard's firmware and its reader has only that keyboard. What differs is the SYNC family and the red status lights; a change to a shared feature is made in both | `user/via_keycodes.txt` | — |

## What This Repository Does Not Carry

**No document here delegates a fact to a commit, and none may.** A rule whose
reasoning is not on the page is a rule with no reasoning: if a constraint
matters, it is written where it binds, and if it is not written it is not a
constraint.

**The reason is the shipped tree, not the development one, and reading it the
other way is how a session comes to trust `git log`.** What ships is a
four-commit orphan — pristine upstream, the firmware, this document set, the
graph — and on it `git log -S` for a retired name reaches the one commit that
landed everything, which is no answer at all. The branch this work happens on
carries the full development history and answers such a query, so **a `git show`
written into a document works for its author and fails for its reader.**
Measured 2026-08-17: `git rev-list --count HEAD -- keyboards/era` reads 768 on
the working branch and 3 on `release/clean-repo`.
