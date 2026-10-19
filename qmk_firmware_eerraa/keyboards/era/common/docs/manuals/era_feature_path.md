# ERA Feature Path

Genre: manual
Canonical for: the decision order for adding firmware behavior that ERA does
not already have, and the document/source owner to consult at each step

This manual adds no product rule. It prevents a new behavior from choosing an
implementation surface before its requirement, compatibility boundary, build
ownership, and verification owner are known. For a new board rather than a new
behavior, use `era_board_adoption.md`.

## 1. Identify the requirement owner

Classify the behavior before editing:

| The change introduces or alters | Read first |
| --- | --- |
| persistent identifier, value id, or external compatibility name | `contracts/era_invariants.md` **Identifier Compatibility**, then `maps/era_identifier_map.md` |
| split wire marker/body/eligibility or a new cross-half state | `contracts/era_wire_contract.md` and `contracts/era_closed_surface_contract.md` |
| route grant, cadence, deadline, or scan-bound scheduling | `contracts/era_route_contract.md` |
| authority/session/lighting-sleep policy | `contracts/era_authority_contract.md` |
| durable storage layout, Apply/CLEAN, or sync semantics | `contracts/era_host_peer_storage_contract.md` |
| SRAM placement, boot-copy, or core1 residency | `contracts/era_sram_residency_contract.md` |
| board geometry or one-board hardware fact | that board's `keyboard.json` or `config.h`, then `era_board_adoption.md` if adoption rules are affected |

If none applies, the current source may already own the decision. Use
`maps/era_source_map.md` only to locate the first owner; do not invent a
contract solely to restate implementation.

## 2. Choose the implementation boundary

Trace from the requirement owner to the current source owner before adding a
new unit. Extend an existing owner when the new behavior has the same lifetime,
authority, and persistence domain.

A feature crosses the split wire only when its requirement needs cross-half
state. Then its carrier must fit the standing exchange and closed response
surface described by `contracts/era_wire_contract.md`,
`contracts/era_route_contract.md`, and
`contracts/era_closed_surface_contract.md`. A local UI or local lighting
behavior does not gain a wire section merely because a split board can expose
it.

If the change touches QMK-owned source, read `era_qmk_fork_ledger.md` before
editing and add/reconcile its current-fork row in the same change.

## 3. Decide whether it needs a build selector

Use `era_build_options.md` **Where a build option is declared**. Do not create
an `_ENABLE` variable until that classification says the behavior is a real
compile/link choice.

Live selector declarations and defaults remain in
`keyboards/era/era_build_options.mk`. Board-shipped choices remain in that
board's `post_rules.mk`. Derived facts stay beside the rule that derives them.

## 4. Give user configuration one owner

If users configure the behavior, locate the existing VIA/keyboard-channel owner
and identifier namespace through `maps/era_identifier_map.md` and
`era_board_adoption.md` **The lighting-surface rule** where lighting is
involved.

Prefer an existing persistence domain whose meaning already matches the state.
A new UI does not by itself justify a new EEPROM region or a second save path.
Storage-format changes must instead go through
`contracts/era_host_peer_storage_contract.md`.

Compile capability and runtime preference are separate decisions. A runtime
preference must not counterfeit removal of a capability that `keyboard.json`
still owns.

## 5. Re-check hot-path and ownership constraints

Use the task row in `era_active_index.md` for the affected runtime domain.
`AGENTS.md` owns the repository scan-bound hot-path rule; route/core1/storage
contracts add narrower constraints when the feature enters those paths. Keep
diagnostic or persistence work out of a scan-bound path unless the applicable
contract explicitly admits it.

## 6. Verify the affected boundary

`era_performance_gates.md` owns evidence and acceptance scope. Choose the
gate for what actually changed rather than treating a successful build as proof
of wire, storage, timing, or device behavior.

For document-only routing changes, use the document checks named by
`AGENTS.md`. For a behavior change, run the focused unit/source/device gate
the task row names. A skipped or unavailable device measurement remains
unverified.

The order above is intentional: requirement -> source boundary -> build
ownership -> user/persistence ownership -> runtime constraints -> evidence.
Each earlier decision constrains the next; none is a substitute for reading the
current source at the chosen owner.
