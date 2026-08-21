#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: era_tomak79h_build.sh [PROFILE] [OPTIONS]

Builds the TOMAK79H VIA firmware with one versioned build profile and stores
the UF2, ELF, build log, and manifest under .era-artifacts/.

Profiles:
  release  release-like storage firmware (default)
  wire     full wire and raw-scan diagnostics
  qwin     count-only scan-rate diagnostics
  cause    wire diagnostics plus the Slice 8 cause timeline
  qwin_phase
           qwin plus the pass-phase itemisation -- a rung, outside the
           default sweep, never a comparison point

Options:
  --build-date YYYY-MM-DD-HH:MM:SS
             override QMK_BUILDDATE through the repository-local QMK wrapper
  --artifact-dir DIR
             artifact destination (default: REPOSITORY/.era-artifacts)
  --help     show this help
EOF
}

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../../../.." && pwd)"
profile="release"
build_date=""
artifact_dir="$repo_root/.era-artifacts"

if (($# > 0)) && [[ $1 != -* ]]; then
    profile=$1
    shift
fi

while (($# > 0)); do
    case "$1" in
        --build-date)
            if (($# < 2)); then
                echo "era_tomak79h_build.sh: --build-date requires a value" >&2
                exit 2
            fi
            build_date=$2
            shift 2
            ;;
        --artifact-dir)
            if (($# < 2)); then
                echo "era_tomak79h_build.sh: --artifact-dir requires a value" >&2
                exit 2
            fi
            artifact_dir=$2
            shift 2
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            echo "era_tomak79h_build.sh: unknown option: $1" >&2
            exit 2
            ;;
    esac
done

case "$profile" in
    release|wire|qwin|cause) ;;
    # Injection proof image. It goes through the launcher like any other
    # profile so the artifact carries a manifest and a distinct stem - a
    # hand-built one would land on the wire profile's name and be
    # indistinguishable from it later. `stale` is the era-mirror injection.
    # A `kill` profile once sat beside it and retired with its selector.
    stale) ;;
    # qwin plus the pass-phase itemisation. Through the launcher for the same
    # reason as `stale` -- a distinct stem and a manifest -- and outside the
    # default sweep: a rung, never a comparison point. Its whole reading is the
    # segment split, and the difference against a plain qwin window of the same
    # length is what the instrument itself costs. Performance batch 1's two
    # bisect rungs (qwin_piooff, qwin_wfeoff) were accepted here until the
    # batch closed on 2026-08-16 and retired with it.
    qwin_phase) ;;
    *)
        echo "era_tomak79h_build.sh: invalid profile: $profile" >&2
        exit 2
        ;;
esac

if [[ -n "$build_date" && ! $build_date =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}-[0-9]{2}:[0-9]{2}:[0-9]{2}$ ]]; then
    echo "era_tomak79h_build.sh: invalid build date: $build_date" >&2
    exit 2
fi

# Everything this script or the residency gate it calls actually invokes. `awk`,
# `make` and `mktemp` were missing while being used at four places here and
# inside the gate, which is the difference between a named prerequisite failure
# and an empty manifest field.
for command_name in qmk git make awk mktemp arm-none-eabi-gcc arm-none-eabi-size arm-none-eabi-nm arm-none-eabi-objcopy od sha256sum; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "era_tomak79h_build.sh: required command was not found: $command_name" >&2
        exit 1
    fi
done

# Gate builds run on a real kernel. MSYS2 emulates fork(), and that emulation
# fails silently and intermittently, so a failure under it says nothing about
# the source. The refusal is here rather than in a document because a rule the
# agent has to remember is exactly what did not hold.
if [[ "$(uname -o 2>/dev/null)" == Msys ]]; then
    cat >&2 <<'MSYS_REFUSED'
era_tomak79h_build.sh: MSYS2 is not a supported build environment.

Do not work around this. Do not run a Windows local build, and do not call
qmk compile directly to bypass this launcher: an MSYS2 artifact is not gate
evidence, and its intermittent empty-diagnostic failures are not source
defects, so acting on one produces a false result either way.

Stop and tell the user that the build reached MSYS2 instead of the configured
Linux build environment. That report is the required outcome here, not a
retry and not a fallback.
MSYS_REFUSED
    exit 1
fi

# The same refusal for the other way this goes wrong: a tree reached over a
# network or Windows mount. Building there is not slow, it is unusable -
# `make -n` alone, which only parses and stats, finishes in 3.8 s on a local
# filesystem and had not finished at 300 s over a 9p mount.
repo_fs="$(stat -f -c %T "$repo_root" 2>/dev/null || echo unknown)"
case "$repo_fs" in
    v9fs|9p|drvfs|cifs|smb2|smb3)
        cat >&2 <<MOUNT_REFUSED
era_tomak79h_build.sh: this tree is on a $repo_fs mount, not a local filesystem.
  $repo_root

Do not work around this and do not build here anyway. The checkout the build
runs from has to be local; a mounted one is where the configured environment
is missing or its paths have moved.

Stop and tell the user which path the build reached. That report is the
required outcome, not a retry and not a fallback to a Windows build.
MOUNT_REFUSED
        exit 1
        ;;
esac

mkdir -p "$artifact_dir"
artifact_dir="$(cd "$artifact_dir" && pwd)"
cd "$repo_root"

head="$(git rev-parse HEAD)"
short_head="$(git rev-parse --short=10 HEAD)"
dirty="no"
dirty_suffix=""
if [[ -n $(git status --porcelain=v1 --untracked-files=all) ]]; then
    dirty="yes"
    dirty_suffix="_dirty"
fi

# When the edit tree is a separate checkout, this artifact is evidence for it
# only while both sit on one commit, and a stale build fails silently: it
# succeeds, passes every gate, and reports the wrong source. Point
# ERA_EDIT_TREE at that checkout to have the mismatch stop the build. An unset
# variable is recorded rather than assumed clean, so manifest silence never
# reads as verified.
if [[ -n "${ERA_EDIT_TREE:-}" ]]; then
    if ! edit_head="$(git -C "$ERA_EDIT_TREE" rev-parse HEAD 2>/dev/null)"; then
        echo "era_tomak79h_build.sh: ERA_EDIT_TREE is not a Git tree: $ERA_EDIT_TREE" >&2
        exit 1
    fi
    if [[ "$edit_head" != "$head" ]]; then
        echo "era_tomak79h_build.sh: build tree is stale against the edit tree" >&2
        echo "  build tree $head" >&2
        echo "  edit tree  $edit_head" >&2
        echo "  Synchronize first: this artifact would be evidence for the wrong commit." >&2
        exit 1
    fi
    # Equal heads are not enough. Uncommitted work in the edit tree may not
    # have reached this one, and that difference is invisible from here, so
    # the artifact is marked the same way a locally dirty build is.
    #
    # Scoped to keyboards/era, the ERA source ownership boundary, and read
    # without taking a lock. A whole-tree scan costs 44 s when the edit tree is
    # on a Windows mount and the untracked walk crosses every submodule, which
    # is four times the cost of the build it guards; this scope costs 0.6 s.
    # The accepted limit: uncommitted edit-tree work outside keyboards/era is
    # not seen here, and only the head comparison covers it.
    if [[ -n $(git -C "$ERA_EDIT_TREE" --no-optional-locks status \
                   --porcelain=v1 --untracked-files=all -- keyboards/era) ]]; then
        edit_tree_check="head matched, edit tree dirty"
        dirty="yes"
        dirty_suffix="_dirty"
    else
        edit_tree_check="matched"
    fi
else
    edit_tree_check="not checked (ERA_EDIT_TREE unset)"
fi

date_suffix=""
if [[ -n "$build_date" ]]; then
    date_suffix="_$(printf '%s' "$build_date" | tr -d ':-')"
fi

stem="era_tomak79h_${profile}_${short_head}${dirty_suffix}${date_suffix}"
build_log="$artifact_dir/${stem}.build.log"
manifest="$artifact_dir/${stem}.manifest.txt"

# Parallelism is stated here rather than left to `qmk config compile.parallel`,
# whose default of 1 is silent and cost this build 13.4 s against 3.5 s. It
# does not change the artifact: the same commit produced one UF2 hash across
# every job count tested.
build_jobs="${ERA_BUILD_JOBS:-$(nproc 2>/dev/null || echo 1)}"

qmk_args=(
    compile
    -c
    -j "$build_jobs"
    -kb era/sirind/tomak79h
    -km via
    -e "ERA_TOMAK79H_BUILD_PROFILE=$profile"
)

if [[ -n "$build_date" ]]; then
    qmk_args+=(
        -e "QMK_BIN=$script_dir/era_qmk_fixed_builddate_wrapper.sh"
    )
fi

qmk_command=(qmk "${qmk_args[@]}")
if [[ -n "$build_date" ]]; then
    qmk_command=(env "ERA_TEST_QMK_BUILDDATE=$build_date" "${qmk_command[@]}")
fi

printf 'ERA TOMAK79H build:'
printf ' %q' "${qmk_command[@]}"
printf '\n'
"${qmk_command[@]}" 2>&1 | tee "$build_log"

build_base="$repo_root/.build/era_sirind_tomak79h_via"
if [[ ! -f "${build_base}.uf2" || ! -f "${build_base}.elf" ]]; then
    echo "era_tomak79h_build.sh: expected UF2/ELF output was not found" >&2
    exit 1
fi

uf2="$artifact_dir/${stem}.uf2"
elf="$artifact_dir/${stem}.elf"
cp "${build_base}.uf2" "$uf2"
cp "${build_base}.elf" "$elf"

map=""
if [[ -f "${build_base}.map" ]]; then
    map="$artifact_dir/${stem}.map"
    cp "${build_base}.map" "$map"
fi

# The three copy-to-RAM gates -- SRAM budget floor, allocator absence, and
# vector-table residency -- are board-agnostic and now live in one script so
# every ERA board is checked by the same code rather than only this one.
# The reason that matters is written at the top of the script.
gate_output="$("$script_dir/era_residency_gate.sh" "$elf")"
ram0_resident="$(awk -F= '/^ram0_resident_bytes=/ {print $2}' <<<"$gate_output")"
ram0_free="$(awk -F= '/^ram0_free_bytes=/ {print $2}' <<<"$gate_output")"
vectors_gate="$(grep '^vectors_gate=' <<<"$gate_output")"
# awk exits 0 on no match, so a gate whose output shape moved would leave these
# empty and the manifest would record `ram0_free_bytes=` as a figure. The gate's
# own zero-section guard covers a walk that measured nothing; this covers a
# parse that read nothing, which is the same failure one layer up.
if [[ ! $ram0_resident =~ ^[0-9]+$ || ! $ram0_free =~ ^[0-9]+$ ]]; then
    echo "era_tomak79h_build.sh: could not read the ram0 figures out of the residency gate:" >&2
    printf '%s\n' "$gate_output" >&2
    exit 1
fi

uf2_sha256="$(sha256sum "$uf2" | awk '{print $1}')"
elf_sha256="$(sha256sum "$elf" | awk '{print $1}')"
qmk_cli_version="$(qmk --version)"
qmk_firmware_version="$(awk '/^QMK Firmware / {sub(/^QMK Firmware /, ""); print; exit}' "$build_log")"
gcc_version="$(awk '/^arm-none-eabi-gcc / {print; exit}' "$build_log")"
if [[ -z "$gcc_version" ]]; then
    gcc_version="$(arm-none-eabi-gcc --version | sed -n '1p')"
fi
make_version="$(make --version | sed -n '1p')"

{
    printf 'profile=%s\n' "$profile"
    printf 'target=era/sirind/tomak79h:via\n'
    printf 'git_head=%s\n' "$head"
    printf 'worktree_dirty=%s\n' "$dirty"
    printf 'edit_tree_check=%s\n' "$edit_tree_check"
    printf 'build_date=%s\n' "${build_date:-automatic}"
    printf 'qmk_cli_version=%s\n' "$qmk_cli_version"
    printf 'qmk_firmware_version=%s\n' "$qmk_firmware_version"
    printf 'compiler=%s\n' "$gcc_version"
    printf 'make=%s\n' "$make_version"
    printf 'uf2=%s\n' "$uf2"
    printf 'uf2_sha256=%s\n' "$uf2_sha256"
    printf 'elf=%s\n' "$elf"
    printf 'elf_sha256=%s\n' "$elf_sha256"
    printf 'ram0_resident_bytes=%s\n' "$ram0_resident"
    printf 'ram0_free_bytes=%s\n' "$ram0_free"
    printf '%s\n' "$vectors_gate"
    if [[ -n "$map" ]]; then
        printf 'map=%s\n' "$map"
    fi
    printf 'build_log=%s\n' "$build_log"
    printf 'command='
    printf ' %q' "${qmk_command[@]}"
    printf '\n'
} >"$manifest"

printf 'UF2: %s\n' "$uf2"
printf 'SHA-256: %s\n' "$uf2_sha256"
printf 'Manifest: %s\n' "$manifest"
