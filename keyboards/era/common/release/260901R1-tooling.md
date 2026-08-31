# ERA 260901R1 Release Tooling

This directory is release input, not an ERA agent-document genre. The immutable
inventory is `260901R1.json`; it names 22 RP2040 `:via` / `standard` targets and
25 firmware-local VIA JSON files. `era/sirind/brick65` is the sole excluded
atmega32u4 board.

## Commands

After the final source is committed, local `main` is clean, and the WSL adapter
is installed, create one fresh receipt set:

```text
wsl -d Ubuntu -e bash -lc 'era-release-260901R1-build FULL_SOURCE_SHA'
```

The adapter runs the documented `era-build` path once for every target with
`--build-date 2026-09-01-00:00:00` and explicit `standard`. It consumes the one
`Manifest:` path printed by each invocation, rejects dirty/stale/wrong-target
manifests, reads `era_firmware_version` from the manifest-declared ELF, and
prints the new `receipt-set.json`. A partial directory has no receipt-set file
and cannot be packaged.

Package into a path that does not exist:

```text
python3 keyboards/era/common/tools/era_release_260901R1_package.py \
  --repo REPOSITORY \
  --source-sha FULL_SOURCE_SHA \
  --app-repo THE_VIA_EERRAA_REPOSITORY \
  --app-head EXPLICIT_CLEAN_LOCAL_APP_COMMIT \
  --receipt-set RECEIPT_SET/receipt-set.json \
  --output-dir NEW_RELEASE_DIRECTORY
```

Verify after local `main`, `origin/main`, and the annotated `260901R1` tag all
name `FULL_SOURCE_SHA`:

```text
python3 keyboards/era/common/tools/era_release_260901R1_verify.py \
  --repo REPOSITORY \
  --source-sha FULL_SOURCE_SHA \
  --app-repo THE_VIA_EERRAA_REPOSITORY \
  --app-head EXPLICIT_CLEAN_LOCAL_APP_COMMIT \
  --receipt-set RECEIPT_SET/receipt-set.json \
  --release-dir NEW_RELEASE_DIRECTORY \
  --via-validator .claude/tools/era-release-260901R1-via-validator.sh
```

The verifier invokes the externally authored app command exactly once with all
25 freshly extracted definitions. Bun is not installed on the configured WSL
host, and the app's `tsx` install carries a Windows-only optional esbuild
binary. The committed adapter therefore uses Node plus the app's pinned
pure-JavaScript TypeScript compiler to make a temporary WSL-local module, then
runs the same committed script and pinned VIA reader without writing either
checkout:

```text
node <temporary compiled scripts/validate-external-v3.ts> --format json -- <JSON paths>
```

It requires deterministic JSON output and a zero exit. Packaging and
verification both require the app checkout to be clean at the explicit local
commit, and `release-manifest.json` records that `app_head` plus the Git blob,
SHA-256, and size of the validator script. The app's `origin/main` is
deliberately not consulted: app publication is outside this release command's
authority.

The receipt creator and verifier default to the WSL toolchain's
`arm-none-eabi-objdump`; `--objdump` and repeatable `--objdump-arg` support a
reviewed wrapper without changing the witness rules.

## Fixed Artifact Shape

Each package produces `<canonical-stem>-260901R1.uf2` and the same-stem ZIP.
The ZIP is sorted, `ZIP_STORED`, has fixed 1980 metadata, and contains no
explicit directory or model-name wrapper:

```text
<canonical-stem>-260901R1.uf2
usevia.app/readme.txt
usevia.app/via_keycodes.txt
usevia.app/<official firmware-local JSON> [one, or split L and R]
```

The two text entries are generated exactly as ASCII
`Source SHA: <FULL_SOURCE_SHA>\n\n` followed by the unmodified Git-blob bytes
from that source SHA. JSON bytes also come directly from Git. The adjacent
canonical `release-manifest.json` records SHA-256, size, Git-blob and build
receipt provenance. Verification requires exact output and ZIP entry sets,
safe fresh extraction, parsed JSON, the external VIA hook, UF2 equality to the
receipt, manifest hashes, the VERSION tuple/ELF witness, and a byte-identical
rebuild.

## Focused Host Test

No keyboard compilation is performed by this test:

```text
python3 tests/era_release_260901R1/test_release.py
```
