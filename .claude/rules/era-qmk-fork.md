---
paths:
  - "quantum/**"
  - "platforms/**"
  - "tmk_core/**"
  - "drivers/**"
  - "builddefs/**"
---

QMK core fork territory. The matrix row is "an edit to **any** QMK core file",
so these paths are the five roots `era_doc_refs.py` and the commit gate use and
not a sample of the forty-file fork surface — a rule that listed only the files
already edited would go quiet on the first file that is not yet one of them.

`quantum/matrix.c` / `quantum/matrix_common.c` are bound by the fork-hygiene
invariant in `contracts/era_invariants.md` (verify by diff, per the Source
Gate). Any ERA edit to a QMK core file must appear in the table in
`manuals/era_qmk_fork_ledger.md` in the same change — the commit gate enforces
this. `manuals/era_qmk_fork_ledger.md` is also where the whole surface, the
gate each edit rides, and the re-derivation against pristine upstream live.
