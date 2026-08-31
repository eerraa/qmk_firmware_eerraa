---
paths:
  - "keyboards/era/**/*.mk"
  - "keyboards/era/common/build_variants/**"
  - "keyboards/era/common/system/era_build_variant_rules.mk"
  - "keyboards/era/common/tools/**"
---

Build / variant / gate-launcher territory. Canonical reads:
`manuals/era_build_options.md` **first** for anything that adds, removes or
reads a selector — it carries the rule that decides where a new option goes,
every selector with its default, and the three rejected arrangements;
`manuals/era_build_and_flash.md` for what a machine must provide and the
commands, `manuals/era_performance_gates.md` for what a change owes and for
turning the console instruments on. `era-build keyboard:keymap` is the only
supported gate entry point; the internal launcher's refusals (missing sync,
MSYS2, mounted tree, stale edit tree) are stop conditions, not obstacles —
report and stop, never fall back to a direct or Windows build (build
environment mechanics: `CLAUDE.md`).
