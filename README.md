# EERRAA QMK

This is the Git root; the QMK firmware lives in `qmk_firmware_eerraa/`.
Agent work starts at [AGENTS.md](AGENTS.md). Product contracts and check selection
belong to the [firmware entry](qmk_firmware_eerraa/AGENTS.md).

## Fresh clone

```sh
git clone --config core.longpaths=true --branch main https://github.com/eerraa/qmk_firmware_eerraa.git
cd qmk_firmware_eerraa
git config core.hooksPath qmk_firmware_eerraa/hooks
git -c core.longpaths=true submodule update --init --recursive
```

Run Git setup from this root. Hook configuration is local and must be set once
per clone; Python 3.8+ and a Git hook shell must be on PATH. Git for Windows
provides the shell. Keep Windows clone paths short; long-path support is needed
for some dependency filenames.

## Build and checks

Run firmware commands from `qmk_firmware_eerraa/`, not the parent Git root.
The host needs Python and Git; firmware builds require a configured Linux
backend with the QMK toolchain and a local Linux filesystem.
Follow the [build and flash manual](qmk_firmware_eerraa/keyboards/era/common/docs/manuals/era_build_and_flash.md)
to create the external machine profile and run `doctor` before building.
Do not use direct Windows QMK builds or bypass the build frontend.
The Linux backend's QMK CLI must recognize firmware inside a parent Git
repository. QMK_HOME, when used, names the firmware component, not the Git root;
setting it alone does not establish CLI compatibility. Host shell exports do
not configure the isolated backend environment. Do not use `qmk setup` as a
workaround; follow the build frontend manual.

Documentation check, from the Git root:

```sh
cd qmk_firmware_eerraa
python keyboards/era/common/tools/era_doc_refs.py
```

The root `.gitmodules` owns Git dependency paths; the component copy is for
QMK-relative tooling. Local profiles, build outputs, handover backups, worktrees
and `.mixdog/` are not restored by cloning. Existing development branches may
retain an older layout; use their own AGENTS and do not merge them for setup.
