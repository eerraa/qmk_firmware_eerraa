# ERA Build And Flash

Status: active
Genre: manual
Canonical for: what a machine must provide to build this firmware, the commands
that build and gate an image on any machine, and how a built image reaches a
keyboard
Read when: building ERA firmware for the first time on a machine, or flashing
one

**This document names no machine.** Which operating system provides the shell,
where the toolchain is installed, and how an editing checkout reaches a build
tree are environment, and environment belongs to the tool adapter layer named
in `AGENTS.md`. What is here is what has to be true on every machine.

## What The Build Needs

- **A POSIX shell and Python 3.** Three of the five tools in
  `keyboards/era/common/tools/` are `#!/usr/bin/env bash` and two —
  `era_core1_stack_walk.py` and `era_doc_refs.py` — are
  `#!/usr/bin/env python3`; the QMK build is a GNU make tree. A native Windows
  shell is not a supported host for them. Python 3 is not a transitive
  dependency of the launcher, which never invokes the `.py` tools: it is
  required because the core1 stack figure is taken with
  `python3 keyboards/era/common/tools/era_core1_stack_walk.py`
  (`era_performance_gates.md`) and because the document-locatability check is
  run by hand.
  **A POSIX `awk` is enough**: the gates parse decimal `arm-none-eabi-size`
  output on purpose, so mawk and gawk agree.
- **The `qmk` CLI, with its own ARM toolchain first on `PATH`.** `qmk` installs
  and injects an `arm-none-eabi-*` set of its own. A distribution's system
  toolchain will usually shadow it for everything that is not `qmk compile` —
  including the `arm-none-eabi-size`/`nm` calls the gate tools make — and a gate
  read with a different toolchain than the build used is void. Confirm with
  `arm-none-eabi-gcc --version` before trusting any figure.
- **Initialised submodules.** `lib/chibios` and `lib/chibios-contrib` are ERA
  forks pinned to a branch of their own; an incomplete worktree does not build
  and silently shrinks the knowledge graph.
- **A local filesystem.** Building over a network or cross-OS mount is not slow
  but unusable, and the gate launcher refuses it rather than letting a build
  hang.

**The launcher's refusals are stop conditions**: on a refusal, report the
environment and stop. Do not retry, do not bypass the launcher with a direct
`qmk compile`, and do not substitute a different host to get past it.

## Which Boards And Keymaps Exist

A board is a directory under `keyboards/era/` with a `keyboard.json`, and its
keymaps are the directories inside that board's own keymaps directory. Nothing
here lists them, because a list goes stale and the tree does not:

```sh
ls keyboards/era/*/keyboard.json keyboards/era/*/*/keyboard.json
qmk list-keyboards | grep '^era/'
```

Every board carries `default` and `via`; `sirind/tomak` carries four more
layout variants. `sirind/brick65` is atmega32u4 and takes none of the
copy-to-RAM image, which is why the gates below do not apply to it — that
exception is canonical in `era_board_adoption.md`'s **Copy-To-RAM Policy**.

## Building

Any board and keymap, from the repository root:

```sh
qmk compile -kb era/sirind/tomak79h -km via
```

The artifact lands at `.build/<target>.uf2`. **Name the ELF you mean; never
pick one by time** — `ls -t .build/*.elf` picks a stale artifact whenever a
build was cached or a later target was built after the one being measured.

TOMAK79H VIA builds select a repository-owned diagnostics profile. The selector
is declared in that board's `post_rules.mk` and documented in
`era_build_options.md`; the profile files are
`keyboards/era/sirind/tomak79h/build_profiles/`; what a figure taken on one may
be compared against is `era_performance_gates.md`'s **Fixed Baselines**:

```sh
qmk compile -kb era/sirind/tomak79h -km via -e ERA_TOMAK79H_BUILD_PROFILE=wire
```

**The gate launcher is the only supported entry point for a build offered as
evidence.** It performs a clean build and preserves the UF2, ELF, log, hashes,
toolchain identity and command under `.era-artifacts/`, and it runs the three
enforced copy-to-RAM checks:

```sh
keyboards/era/common/tools/era_tomak79h_build.sh wire
```

**Every other ERA board has no launcher**, so run the same three checks by hand
on each keymap's ELF:

```sh
keyboards/era/common/tools/era_residency_gate.sh .build/<target>.elf
```

It takes an ELF and nothing else — no profile, no board name, no build — so
every board's adoption is checked by the same code rather than by the same
intention. What the three checks are, and what a change owes beyond them, is
`era_performance_gates.md`'s.

Which build selectors exist, what each costs, and where a new one is declared
are canonical in `era_build_options.md`.

## Flashing

Every ERA RP2040 board flashes as a UF2 mass-storage copy. There is no vendor
tool and no driver.

1. **Enter the bootloader.** Any of: the physical RUN double-tap; the `QK_BOOT`
   keycode; VIA's SYSTEM reset/boot control on a VIA build; or connecting the
   board with the RUN contact already shorted. The board enumerates as an
   `RPI-RP2` mass-storage device.
2. **Copy the `.uf2` onto it.** The board reboots into the new firmware by
   itself when the copy finishes.

**A split pair's two halves run one identical image and are flashed together.**
Mixed versions are not a supported configuration, and no code in this firmware
reads a format an earlier firmware stored (`era_source_map.md`'s
**Stored-Data Compatibility**). Flashing one half and not the other leaves the
pair in a state nothing is written to recover.

**A flash erases the store.** The ERA strict reset then rewrites fresh defaults
on the next boot, and both halves read all-changed at the first relation open
after it — one degraded relation open, gone from the boot after. That is the
conservative degradation working; how to read a relation open is
`era_capture_reading.md`'s.

**A VIA EEPROM CLEAN after flashing is an owner step, not a build step.** It is
the way to discard a stored keymap that a keycode renumbering has made wrong;
what a CLEAN boot costs is read with the wire diagnostics, which
`era_performance_gates.md` says how to turn on and `era_capture_reading.md`
says how to decode.
