# ERA Build And Flash

Genre: manual
Canonical for: what the configured Linux backend must provide to build this
firmware, its one build-and-gate entry point, how a built image reaches a
keyboard, and the short TOMAK_TKL hardware-acceptance handoff

**Evidence requires a Linux local-filesystem build tree reached through
`keyboards/era/common/tools/era_build.py` with a validated machine profile.**
Run the frontend with the host's Python and Git, from the edit worktree to
build. It selects that worktree's HEAD and uncommitted delta, regardless of
where the frontend itself lives. Windows with WSL and native Linux use the
same route; the profile alone chooses the execution environment.

> **REFUSED:** run `qmk` on Windows, build on a mounted edit tree, invoke the
> internal launcher by hand, retry an unresolved profile/backend/sync/gate
> refusal, bypass `era_build.py` with a direct compile, or substitute a
> different host to get past a refusal
> **WHY:** a figure from a different host or an unsynchronised tree is void, and
> the automation's refusals are stop conditions.
> **REOPENS:** the profile/backend passes `doctor`, or the reported source/gate
> defect is identified and corrected within the authorised task. Re-run through
> the frontend after validating the correction; do not bypass a refusal by
> changing host or disabling a check.

On a refusal, report its cause and stop builds that depend on the unresolved
condition. Investigation and authorised repair may continue. A capacity policy
change must explain its runtime requirements and retain physical-boundary,
allocator, vector and stack checks; lowering a threshold until a build passes
is not a correction.

## What The Build Needs

The host needs Python 3 and Git. The backend needs Linux, a local Linux build
filesystem, `qmk`, GNU make, Python 3, Git, rsync, and the QMK ARM toolchain.
`doctor` checks reachability, the internal launcher's command list plus Python,
Git and rsync, and the filesystem at the build root (or its nearest existing
ancestor). Missing tools and network/Windows mounts are refusals, not reasons
to fall back to the edit tree. Submodules are initialised during sync.

### Machine profile

The JSON profile is outside the repository. `ERA_BUILD_PROFILE` overrides its
location; otherwise Windows uses `%APPDATA%\era-build\profile.json`, and Linux
uses `${XDG_CONFIG_HOME:-~/.config}/era-build/profile.json`. `profile show`
prints the selected path and validated values. Missing profiles, unknown keys
and invalid values name the path, field and repair command; no path is guessed.

Schema 1 has `schema`, `backend`, and exactly the matching backend object:
`wsl` contains `distro` and `buildRoot`; `local` contains `buildRoot`.
The build root is an absolute POSIX path on the backend's local filesystem.
Choose it explicitly; commands below intentionally use a placeholder.

Windows / PowerShell:

```powershell
python keyboards/era/common/tools/era_build.py profile init --backend wsl --distro Ubuntu --build-root POSIX_PATH
python keyboards/era/common/tools/era_build.py doctor
python keyboards/era/common/tools/era_build.py profile show
```

Native Linux:

```sh
python3 keyboards/era/common/tools/era_build.py profile init --backend local --build-root POSIX_PATH
python3 keyboards/era/common/tools/era_build.py doctor
```

Moving to another machine or backend means `profile init` followed by `doctor`,
not source edits, shell-profile edits or installing wrapper symlinks. Commands
run with explicit argv, cwd and environment, never a login shell. The tool PATH
contains the backend user's `.local/bin` and `bin`, then the standard Linux
system directories; it does not inherit shell-profile PATH additions.

### Backend and worktree contract

The frontend's module docstring defines the backend interface: `connect()`
establishes the explicit Linux environment; `host_path(path)` maps a host path;
`run(argv, cwd, env, input, check)` executes without a shell and returns byte
streams; `describe()` supplies manifest environment identity. WSL translates
paths with `wslpath` and identifies itself as `wsl:DISTRO`; native Linux uses
identity paths and `local:HOSTNAME`. Each implementation also owns its
`profile_keys`, `validate_section()` and `profile_section()` for profile
validation and initialization. Adding a backend means adding and registering
one implementation, not changing sync, build or artifact-return logic.

One build tree per repository per profile lives beneath the configured build
root, named after the main checkout that owns the common Git directory. All
linked edit worktrees share it. An exclusive lock inside its artifact directory
refuses concurrent use. An interrupted run may leave the lock: confirm that no
build is running before removing that exact lock. Do not delete caches or
artifacts to work around a held lock.

An absent build tree is cloned from the common Git directory. Each sync fetches
branch refs from that directory without a persistent remote, checks that edit
HEAD is available, checks out that commit detached, initialises submodules and
resets/cleans the disposable tree while preserving `.build` and `.era-artifacts`.
Host Git enumerates the edit delta once; rsync replays only that delta without
copying drvfs executable modes or unchanged CRLF files. Effective statuses must
match (index staging and rename representations are normalised), or the build
is refused. `sync` alone prints the backend build-tree path for selftests:

```sh
python3 keyboards/era/common/tools/era_build.py sync
```

Run backend selftests in that returned tree with its validated tool environment,
not on a mounted edit tree. `sync` does not retain a lock after it exits; fixture
runs needing exclusive access must arrange that separately.

Board and keymap inventory is source-owned. Use
`qmk list-keyboards | grep '^era/'` and the selected board's keymap directory
when choosing a target. Copy-to-RAM applicability and board
exceptions are canonical in `era_board_adoption.md` **Copy-To-RAM Policy**;
do not maintain a second board list here.

## Building

Every invocation names the intended keyboard and keymap explicitly. For
TOMAK_TKL, run this from the Windows edit worktree:

```powershell
python keyboards/era/common/tools/era_build.py build era/sirind/tomak:via
```

From a native Linux edit worktree:

```sh
python3 keyboards/era/common/tools/era_build.py build era/sirind/tomak:via
```

Omitting the variant means `standard` on every keyboard. Diagnostic names are
board-independent; make preconditions refuse an incompatible selection:

```powershell
python keyboards/era/common/tools/era_build.py build era/sirind/tomak:via cause
python keyboards/era/common/tools/era_build.py build era/sirind/tomak79h:via wire
python keyboards/era/common/tools/era_build.py build era/sirind/tomak79h:via standard wire qwin cause --show-options
```

The frontend owns synchronization and artifact return to the selected edit
worktree. `keyboards/era/common/tools/era_qmk_build.sh` owns the
compile invocation, manifest identity, hashes, output paths, and the call to
`keyboards/era/common/tools/era_residency_gate.sh`. This manual does not
duplicate those paths or filename formulas.

The frontend takes the manifest from the launcher's `Manifest:` line, never a
glob, and returns that manifest plus exactly its named firmware, ELF, map and
logs to the edit worktree's `.era-artifacts` after each variant. It prints
`Returned manifest:` and `Returned firmware:` with host-native paths and the
dirty/edit-tree/RAM summary. The copied manifest retains backend-origin paths;
its named files are returned beside it. Treat it as the authority for identity
and hashes, never modification time or a remembered directory. A compile that
did not pass through `era_build.py` synchronization is not ERA evidence.

Build selectors and prerequisites are canonical in `era_build_options.md`.
Gate semantics and additional validation owed by a change are in
`era_performance_gates.md`.

## Flashing

Every ERA RP2040 board flashes as a UF2 mass-storage copy. There is no vendor
tool and no driver.
Flashing is an explicit owner action, not an automatic build step.

1. **Enter the bootloader.** Any of: the physical RUN double-tap; the `QK_BOOT`
   keycode; VIA's SYSTEM reset/boot control on a VIA build; or connecting the
   board with the RUN contact already shorted. The board enumerates as an
   `RPI-RP2` mass-storage device.
2. **Copy the `.uf2` onto it.** The board reboots into the new firmware by
   itself when the copy finishes.

**A split pair's two halves run one identical image and are flashed together.**
Mixed versions are not a supported configuration; stored-data compatibility and
stale-layout rejection are canonical in `era_host_peer_storage_contract.md`
**Stored-Data Compatibility**. Flashing one half and not the other leaves the
pair in a state nothing is written to recover.

**A normal UF2 flash does not erase ERA NVM.** Physical placement and
stored-data compatibility are owned by `era_host_peer_storage_contract.md`.
A same-format upgrade therefore reuses compatible stored state; incompatible
physical state is rejected or reinitialised by the storage implementation.
Logical-layout incompatibility remains a separate CLEAN decision.

**VIA EEPROM CLEAN is an owner action, not build or flash automation.** Run it
only when the stored layout must be intentionally discarded. Its device gate is
`era_performance_gates.md`; diagnostic decoding is `era_capture_reading.md`.

## TOMAK_TKL Hardware Acceptance Route

Build the current working tree through `era_build.py`, use the manifest and
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

1. Export the existing VIA layout only if it is needed to return the board to
   service after CLEAR; it is cleanup material, not an unexamined test input.
2. Flash both halves with the same manifest-selected UF2, reconnect the normal
   inter-half cable, load the JSON for the USB-connected half, and wait for
   EEPROM SYNC to finish.
3. Use `keyboards/era/sirind/tomak/keymaps/via/keymap.c` as the authority for
   shipped bindings. Put `QK_REBOOT` and `QK_CLEAR_EEPROM` temporarily on unused
   layer-1 keys only for the reset legs, and run CLEAR last.
4. Export the prepared layout once as the gate LOAD, then make one known,
   harmless keymap or macro change so loading it back performs real work.
5. Keep the ERA NVM timing legs in `era_performance_gates.md` separate and start
   each from its stated known condition.

Record the chosen test values and PASS/FAIL/NOT EXERCISED. Raw logs and EEPROM
bytes are not test records.

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
4. **One TOMAK indicator settle.** In VIA Lighting -> Badge, select the
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
