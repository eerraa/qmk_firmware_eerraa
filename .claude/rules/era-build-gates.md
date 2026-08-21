---
paths:
  - "keyboards/era/**/*.mk"
  - "keyboards/era/**/build_profiles/**"
  - "keyboards/era/common/tools/**"
---

Build / profile / gate-launcher territory. Canonical reads:
`manuals/era_build_options.md` **first** for anything that adds, removes or
reads a selector — it carries the rule that decides where a new option goes,
every selector with its default, and the three rejected arrangements;
`manuals/era_build_and_flash.md` for what a machine must provide and the
commands, `manuals/era_performance_gates.md` for what a change owes and for
turning the console instruments on. The launcher is the only supported gate
entry point; its refusals (MSYS2, mounted tree, stale edit tree) are stop
conditions, not obstacles — report and stop, never fall back to a Windows
build (build environment mechanics: `CLAUDE.md`).
