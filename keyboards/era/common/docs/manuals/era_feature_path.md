# ERA Feature Path

Genre: manual
Canonical for: the order a new ERA feature is added in, the decision each step
makes, and what the finished feature owes before it is believed

`era_board_adoption.md` is the same document for a new *board*. This one is for
a new behaviour on the boards that exist. It defines no rule of its own: every
step names the document that owns its decision, and the value here is the
**order**, because each step's choices close options in the next.

## 1. Decide which layer owns it, before writing anything

| The behaviour is… | It lives in | Named by |
| --- | --- | --- |
| the same on every ERA board, split or not | `common/features/` or `common/system/` | `era_source_map.md` |
| a property of the split relation | `common/split/` | `era_route_contract.md`, `era_wire_contract.md` |
| shared by one product family only | that family's `_common.[ch]` | the family header's own preamble |
| geometry — a matrix, a pin, an LED range | `keyboard.json` or the board's `config.h` | `era_board_adoption.md` |

**A family unit may not reach the split class layer's internals and the class
layer may not name a family type.** That boundary is stated in each family
unit's own header.

**Nothing new goes in a board `.c`.** What differs between boards of a family
is geometry, and geometry needs no translation unit.

## 2. Decide whether it crosses the wire

If the halves must agree about it, read `era_route_contract.md` **first** — the
standing exchange grant and **One carrier for the response section set** govern
the whole lane — then `era_wire_contract.md` for the marker, the body layout
and the per-relation eligibility.

- **The response section set has one carrier.** A new fact does not get a lane;
  it gets a section on the standing answer, or it widens an existing body.
- **The section mask is a byte and it is full.** What that leaves is widening
  an existing body, and `era_closed_surface_contract.md` records what opening a
  section costs.
- **A section is latest-state, not an event stream.** It is advertised only
  while its value differs from what the wire last confirmed, and a receiver
  applies it idempotently. A feature that needs delivery counted rather than
  state converged is asking for a different mechanism.

## 3. Declare its build selector

A new option is declared in `keyboards/era/era_build_options.mk` with a
default, and every fragment that reads one runs from a board's
`post_rules.mk`. Three existing selectors are declared elsewhere for ordering
reasons and none of them is a precedent for a fourth. **The classification
rule that decides where a new option goes is canonical in
`era_build_options.md`**, along with the three arrangements that were tried
and rejected.

QMK includes `post_rules.mk` *after* the keymap's `rules.mk`, so `VIA_ENABLE`
and every other QMK switch is visible there and not in `rules.mk`. An option
line placed *below* an `include` in a board's `post_rules.mk` is read after
the fragment that would have used it, so it is silently ignored.

## 4. Give it a surface, if a user configures it

- **A VIA value id** on the board's own table (`era_board_via_get_value` /
  `era_board_via_set_value`) or on the common feature table, plus a menu entry
  in that board's VIA definition JSON. The ids themselves are
  `era_identifier_map.md`'s.
- **A keycode only when VIA cannot do it.** A keycode is a second control path
  over one state, it consumes a `QK_KB_*` slot, and renumbering that enum
  invalidates every keymap already stored on a device.
- **Persisted state** uses an existing canonical owner whenever one already fits
  the meaning. RGB Sleep is the example: the master occupies one formerly-unused
  inverted bit in QMK's 2-byte keymap config, which is already a portable split
  storage domain; TOMAK's timeout stays in its board record. Do not allocate a
  new ERA region merely because the UI is new.
- **Compile capability and runtime preference are separate layers.** If
  `keyboard.json` owns the compiled capability (for example `rgb_matrix.sleep`
  -> `RGB_MATRIX_SLEEP`), every capable board keeps that switch on. The VIA
  master may then gate the entire product behavior at runtime without pretending
  that a compile-time feature disappeared.
  A dependent control may use V3 `showIf`; hiding it is presentation, not a
  second firmware gate, and its stored value must survive while hidden.

## 5. Keep it off the hot path

`era_invariants.md` is always-on and bounds what a scan-bound path may do.
**A matrix-scan path may read cached scalars and publish bounded latches, and
may not capture EEPROM, compute a CRC, build a snapshot, enqueue, decode or
apply.** Work that must happen per millisecond rather than per pass hangs off
the board housekeeping tick in `system/era_board_hooks.c`, one call per
millisecond.

If the feature adds core1 call depth, it owes a measured stack figure — the
`_Static_assert` beside the reservation cannot report that one, and no
compile-time construct can.

## 6. Verify what the change actually touched

`era_performance_gates.md` is canonical and scoped by what was touched.

- **Any build offered as evidence** runs through the launcher, from a tree
  synchronised to the change.
- **A change that claims to touch no behaviour** owes the Refactor Self-Check
  tier its binary actually earns — T1 identical UF2, T2 identical symbols and
  sizes, T3 an explained delta. **The tier is a result, not a judgment call.**
- **A closed-surface, retired-path or QMK core matrix change** owes the Source
  Gate.
- **A change to scan-bound work, RAM placement, scheduler behaviour, core1
  ownership or storage** owes the device gate its own row in the Task Read
  Matrix names.
- **Device evidence outranks source inference**, and the reverse is also true:
  refusing a correct change by reasoning about a mechanism, without reading the
  paths that already exercise it, is the same error pointed the other way.

## 7. Land it in separated commits

Contract content, source implementation, diagnostics-only change, and index or
policy update. **A commit that deletes documented content carries the reasoning
that content held.** The whole rule set is in `AGENTS.md`'s **Commits**.
