# ERA Build And Flash

Genre: manual
Canonical for: what the configured WSL automation must provide to build this
firmware, its one build-and-gate entry point, how a built image reaches a
keyboard, and the short TOMAK_TKL hardware-acceptance handoff

**Agent and evidence builds use the configured WSL local-build automation.**
From the Windows edit tree the entry point is the host adapter's explicit
`era-build keyboard:keymap` command, invoked through WSL as shown below. It
synchronises into the WSL local filesystem before it builds. Installation and
literal machine paths remain environment and therefore live in the host adapter
(`CLAUDE.md`); the mandatory automated sequence lives here.

> **REFUSED:** run `qmk` on Windows, build on the `/mnt/` edit tree, invoke the
> internal launcher by hand, retry an automation refusal, bypass `era-build`
> with a direct compile, or substitute a different host to get past a refusal
> **WHY:** a figure from a different host or an unsynchronised tree is void, and
> the automation's refusals are stop conditions.
> **REOPENS:** this manual names a supported host other than the configured WSL
> local-build automation.

On a refusal, report the environment and stop.

## What The Build Needs

| Need | Rule |
| --- | --- |
| POSIX shell and Python 3 | Three tools in `keyboards/era/common/tools/` are bash; `era_core1_stack_walk.py` and `era_doc_refs.py` are Python 3. QMK is GNU make. A native Windows shell is not a supported host. The core1 stack figure is taken with `python3 keyboards/era/common/tools/era_core1_stack_walk.py` (`era_performance_gates.md`); the document-locatability check is run by hand. A POSIX `awk` is enough: the gates parse decimal `arm-none-eabi-size` output. |
| `qmk` CLI, its ARM toolchain first on `PATH` | A distribution toolchain that shadows `arm-none-eabi-size`/`nm` voids the gate figure. Confirm with `arm-none-eabi-gcc --version` before trusting any figure. |
| Initialised submodules | `lib/chibios` and `lib/chibios-contrib` are ERA forks; an incomplete worktree does not build. |
| Local filesystem | The gate launcher refuses a network or cross-OS mount. |

## Which Boards And Keymaps Exist

A board is a directory under `keyboards/era/` with a `keyboard.json`; its
keymaps live in that board's keymaps directory. Do not list them here:

```sh
ls keyboards/era/*/keyboard.json keyboards/era/*/*/keyboard.json
qmk list-keyboards | grep '^era/'
```

Every board carries `default` and `via`; `sirind/tomak` carries four more
layout variants. `sirind/brick65` is atmega32u4 and takes none of the
copy-to-RAM image, so the gates below do not apply to it — that exception is
canonical in `era_board_adoption.md`'s **Copy-To-RAM Policy**.

## Building

Every invocation names the intended keyboard and keymap explicitly. For
TOMAK_TKL, run this from Windows:

```powershell
wsl -d Ubuntu -e bash -lc 'era-build era/sirind/tomak:via'
```

Omitting the variant means `standard` on every keyboard. Diagnostic names are
board-independent; make preconditions refuse an incompatible selection:

```powershell
wsl -d Ubuntu -e bash -lc 'era-build era/sirind/tomak:via cause'
wsl -d Ubuntu -e bash -lc 'era-build era/sirind/tomak79h:via wire'
wsl -d Ubuntu -e bash -lc 'era-build era/sirind/tomak79h:via standard wire qwin cause'
```

`.claude/tools/era-build.sh` first runs `era-sync`, builds clean in
`~/projects/qmk_firmware_eerraa`, invokes the common gate launcher, and returns
only that invocation's manifest-declared artifacts to the Windows
`.era-artifacts/` directory. A compile without that sync is not an ERA
evidence build. Select the firmware and ELF from the `Manifest:` path the
command reports; never select an artifact by modification time. Each stem
includes the first 16 hexadecimal digits of the firmware SHA-256, so a
different uncommitted binary at the same HEAD cannot overwrite an image already
handed to a device test.

The filename contract for all twenty-five keyboards:
`<keyboard>_<keymap>_<variant>_<git10>[_dirty][_<fixed-date>]_<sha16>.<format>`,
slashes converted to underscores. RP2040 boards produce `.uf2`; atmega32u4
`sirind/brick65` produces `.hex`.

`keyboards/era/common/tools/era_qmk_build.sh` is the internal launcher, not a
second user entry point. It refuses a call without the completed sync marker,
performs the clean compile, records firmware, ELF, map, logs, hashes,
toolchain and exact command, and runs
`keyboards/era/common/tools/era_residency_gate.sh` for every UF2. Artifact and
manifest carry the selected `variant`; the recorded QMK command always contains
`-e ERA_BUILD_VARIANT=<name>`. The residency gate is ELF-only so every
copy-to-RAM board is judged by the same checks. What it checks,
and what a change may also owe, is `era_performance_gates.md`'s.

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

**A normal UF2 flash does not erase ERA NVM.** The firmware UF2 contains only
the linked firmware load range; the linker-reserved ERA NVM starts at effective
XIP `0x101E0000`, outside those UF2 blocks. A same-format upgrade therefore
mounts the existing ERA NVM image. Incompatible physical bytes are rejected
and a fresh current-format bank is constructed. Logical-layout incompatibility
remains a separate `ERA_EEPROM_RESET_KEY`/CLEAN decision.

**A VIA EEPROM CLEAN after flashing is an owner step, not a build step.** Use
it to discard a stored keymap a keycode renumbering has made wrong. What a
CLEAN boot costs is read with the wire diagnostics:
`era_performance_gates.md` says how to turn them on and
`era_capture_reading.md` says how to decode.

## TOMAK_TKL Hardware Acceptance Route

Build the current working tree through `era-build`, use the manifest and
SHA-256 produced by that run, and record those exact identifiers with the
device evidence. This route names no frozen commit, hash or historical
artifact.

### Exact image and definitions

Build `era/sirind/tomak:via standard` for the acceptance image. Build `wire` or
`cause` separately only when the corresponding diagnostic evidence is needed.
For every leg, use the artifact whose manifest names the current edit tree and
target; never select an artifact by modification time and never substitute a
different TOMAK-family target.

Flash the exact same selected UF2 to both halves, once each. The VIA definition is the
only side-specific file: load
`keyboards/era/sirind/tomak/keymaps/via/TOMAK-TKL-L-VIA.json` when LEFT owns
the host USB connection, or the adjacent `TOMAK-TKL-R-VIA.json` when RIGHT owns
it. Wait for the steady-red EEPROM SYNC indication to go dark before starting
a timed leg.

Use the standard VIA image for the final acceptance result. A diagnostic image
may establish timing/counter evidence but does not replace the final standard
smoke. How to bind and read `WIRE_DIAG` is in `era_capture_reading.md`.

### One-time setup

1. Export the existing VIA layout once before flashing if it must be restored
   after CLEAR. This backup is cleanup only; do not use it as an unexamined test
   input.
2. Flash both halves with the UF2 above, reconnect the normal inter-half cable,
   load the JSON for the USB-connected half, and wait for EEPROM SYNC to finish.
3. Keep the shipped layer-1 lighting keys: `Fn+A` is `RM_TOGG`, `Fn+S`/`Fn+X`
   are RGB Matrix brightness up/down, `Fn+Z` is the next effect, and `Fn+Esc`
   is `QK_BOOT`. These bindings are in
   `keyboards/era/sirind/tomak/keymaps/via/keymap.c`.
4. For the two remaining reset legs, temporarily put `QK_REBOOT` and
   `QK_CLEAR_EEPROM` on unused layer-1 keys, for example `Fn+F1` and `Fn+F2`.
   Run CLEAR last because it deliberately deletes this keymap. Do not spend a
   second setup cycle restoring it before then.
5. Export this prepared layout once as the gate LOAD, then make one known,
   harmless keymap or macro change so loading it back performs real work. Keep
   the dedicated ERA NVM timing legs from `era_performance_gates.md` separate:
   journal-room Apply, mandatory-rotation Apply and the full 16-KiB macro close
   are each measured once under a known starting state.

Use visibly different RGB markers for successive legs, for example blue/64,
red/128, green/192 and purple/96. For the TOMAK indicator use Caps Lock,
priority on, yellow and brightness 96. Write down only the chosen values and
PASS/FAIL/NOT EXERCISED; raw logs and EEPROM bytes are not test records.

### Short sequence

1. **ERA NVM Apply overlap, once.** Start the one known-different VIA layout LOAD.
   During the storage episode, tap `Fn+S` enough to leave a plainly different
   final RGB Matrix brightness, then tap the temporary `QK_REBOOT` key only if
   this leg is explicitly exercising reset overlap. PASS means one reset, no
   boot loop or bootloader entry, and the last independently committed RGB state
   is still present after boot and the following one-second quiet period. A
   steady-red SYNC lamp proves unfinished pair storage; it does not identify
   whether the current NVM call is an append or a bank rotation.
2. **SAVE then suspend.** Arrange a confirmed host USB suspend, change RGB
   Matrix to the next marker, release the final control less than 500 ms before
   USB suspend, then wake without touching Lighting. PASS means VIA reads back
   the marker and the LEDs show it. An ordinary sleep whose USB suspend time is
   unknown does not prove this leg; record NOT EXERCISED instead of repeating
   guesses.
3. **SAVE then controlled reset.** Change RGB Matrix to the next marker and tap
   the temporary `QK_REBOOT` key within 500 ms. Require exactly one reboot and
   the marker after boot. Change to another marker and repeat with `Fn+Esc`;
   after `RPI-RP2` appears, remove and restore power without copying a UF2.
   Require the same marker after normal boot.
4. **One TOMAK indicator settle.** In VIA Lighting -> Badge Lighting, select the
   indicator marker above, release the last control, wait one second, toggle
   Caps Lock, and require the chosen indicator colour and brightness. This is
   the TOMAK keyboard-channel persistence leg; do not repeat it per field.
5. **One drag only.** Drag RGB Matrix Brightness continuously for 30 seconds
   while typing on both halves, end at a recorded value, release, and wait one
   second. Require responsive input throughout and the final value, not an
   intermediate one.
6. **One final full power cycle.** Remove every USB power source from both
   halves, reconnect the normal HOST-PEER rig, wait for SYNC to go dark, and
   require the final RGB Matrix and indicator markers. This single cycle closes
   the suspend, reset, indicator and drag persistence checks together.
7. **CLEAR last.** Change one RGB value, release it, and press the temporary
   `QK_CLEAR_EEPROM` key within 500 ms. PASS is exactly one reset into defaults;
   the just-selected value must not survive, because CLEAR wins by definition.
   Restore the original VIA layout once after recording the result if the board
   must be returned to service.

TOMAK_TKL has RGB Matrix and its keyboard-channel indicator, but no RGBLight or
Backlight. Those two domain checks belong on a board that actually enables
them; looking for them on TOMAK_TKL or repeating this route cannot cover them.
