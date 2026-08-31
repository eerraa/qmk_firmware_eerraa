#!/usr/bin/env bash
set -euo pipefail

tool_name=era_qmk_build.sh

usage() {
    cat <<'EOF'
Usage: era_qmk_build.sh TARGET [VARIANT] [OPTIONS]

Internal launcher used by the configured host-side `era-build` automation.
TARGET is explicit and has the form keyboard:keymap, for example:

  era/sirind/tomak:via
  era/sirind/tomak:via cause

VARIANT defaults to standard for every target. The common make layer owns the
accepted names and refuses a variant whose prerequisites the target lacks.

Options:
  --build-date YYYY-MM-DD-HH:MM:SS
             override QMK_BUILDDATE through the repository-local QMK wrapper
  --artifact-dir DIR
             artifact destination (default: REPOSITORY/.era-artifacts)
  --show-options
             print the resolved ERA build options as part of the build
  --help     show this help
EOF
}

fail() {
    echo "$tool_name: $1" >&2
    exit "${2:-2}"
}

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../../../.." && pwd)"
target=""
variant=""
build_date=""
show_options="no"
artifact_dir="$repo_root/.era-artifacts"

if (($# > 0)) && [[ $1 != -* ]]; then
    target=$1
    shift
fi
if (($# > 0)) && [[ $1 != -* ]]; then
    variant=$1
    shift
fi

while (($# > 0)); do
    case "$1" in
        --build-date)
            (($# >= 2)) || fail "--build-date requires a value"
            build_date=$2
            shift 2
            ;;
        --artifact-dir)
            (($# >= 2)) || fail "--artifact-dir requires a value"
            artifact_dir=$2
            shift 2
            ;;
        --show-options)
            show_options="yes"
            shift
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            fail "unknown option: $1"
            ;;
    esac
done

[[ -n "$target" ]] || {
    usage >&2
    exit 2
}
[[ $target =~ ^era/[A-Za-z0-9_/-]+:[A-Za-z0-9_-]+$ ]] ||
    fail "invalid target (expected keyboard:keymap): $target"
keyboard=${target%:*}
keymap=${target##*:}

variant=${variant:-standard}
[[ $variant =~ ^[a-z][a-z0-9_]*$ ]] || fail "invalid variant spelling: $variant"
variant_args=(-e "ERA_BUILD_VARIANT=$variant" -e ERA_BUILD_IDENTITY_REPORT=yes)

if [[ -n "$build_date" && ! $build_date =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}-[0-9]{2}:[0-9]{2}:[0-9]{2}$ ]]; then
    fail "invalid build date: $build_date"
fi

# A successful host adapter sync is the entry condition. This prevents the
# internal launcher from silently becoming a second, stale-tree build route.
[[ ${ERA_BUILD_TREE_SYNCED:-} == yes ]] ||
    fail "invoke this launcher through the configured era-build automation" 1
[[ -n ${ERA_EDIT_TREE:-} ]] ||
    fail "ERA_EDIT_TREE is unset; the edit tree cannot be verified" 1

for command_name in qmk git make awk grep sed sort stat tee tr cp mv rm mktemp arm-none-eabi-gcc arm-none-eabi-size arm-none-eabi-nm arm-none-eabi-objcopy od sha256sum; do
    command -v "$command_name" >/dev/null 2>&1 ||
        fail "required command was not found: $command_name" 1
done

if [[ "$(uname -o 2>/dev/null)" == Msys ]]; then
    fail "MSYS2 is not supported; use the configured WSL automation" 1
fi

repo_fs="$(stat -f -c %T "$repo_root" 2>/dev/null || echo unknown)"
case "$repo_fs" in
    v9fs|9p|drvfs|cifs|smb2|smb3)
        fail "build tree is on a $repo_fs mount, not the WSL local filesystem: $repo_root" 1
        ;;
esac

mkdir -p "$artifact_dir"
artifact_dir="$(cd "$artifact_dir" && pwd)"
cd "$repo_root"

head="$(git rev-parse HEAD)"
short_head="$(git rev-parse --short=10 HEAD)"
edit_head="$(git -C "$ERA_EDIT_TREE" rev-parse HEAD 2>/dev/null)" ||
    fail "ERA_EDIT_TREE is not a Git tree: $ERA_EDIT_TREE" 1
if [[ "$edit_head" != "$head" ]]; then
    fail "build tree $head is stale against edit tree $edit_head" 1
fi

dirty="no"
dirty_suffix=""
if [[ -n $(git status --porcelain=v1 --untracked-files=all) ]]; then
    dirty="yes"
    dirty_suffix="_dirty"
fi
edit_tree_check="matched by era-sync"

date_suffix=""
if [[ -n "$build_date" ]]; then
    date_suffix="_$(printf '%s' "$build_date" | tr -d ':-')"
fi

target_stem="${keyboard//\//_}_${keymap//\//_}"
build_log_tmp="$(mktemp "$artifact_dir/.${target_stem}_${variant}_${short_head}.build.XXXXXX.log")"
gate_log_tmp=""
cleanup() {
    [[ -z "$build_log_tmp" ]] || rm -f -- "$build_log_tmp"
    [[ -z "$gate_log_tmp" ]] || rm -f -- "$gate_log_tmp"
}
trap cleanup EXIT

build_jobs="${ERA_BUILD_JOBS:-$(nproc 2>/dev/null || echo 1)}"
qmk_args=(compile -c -j "$build_jobs" -kb "$keyboard" -km "$keymap")
qmk_args+=("${variant_args[@]}")
if [[ $show_options == yes ]]; then
    qmk_args+=(-e ERA_SHOW_OPTIONS=yes)
fi
if [[ -n "$build_date" ]]; then
    qmk_args+=(-e "QMK_BIN=$script_dir/era_qmk_fixed_builddate_wrapper.sh")
fi

qmk_command=(qmk "${qmk_args[@]}")
if [[ -n "$build_date" ]]; then
    qmk_command=(env "ERA_TEST_QMK_BUILDDATE=$build_date" "${qmk_command[@]}")
fi

printf 'ERA build:'
printf ' %q' "${qmk_command[@]}"
printf '\n'
"${qmk_command[@]}" 2>&1 | tee "$build_log_tmp"

# The make layer, not this launcher, resolves the immutable tuple. Require one
# unique handshake even when GNU make restarts after regenerating dependencies;
# then use its resolved name for every downstream identity surface.
mapfile -t identity_lines < <(grep '^ERA_BUILD_IDENTITY ' "$build_log_tmp" | sort -u)
[[ ${#identity_lines[@]} -eq 1 ]] ||
    fail "build emitted ${#identity_lines[@]} distinct ERA_BUILD_IDENTITY lines; expected one" 1
identity_line=${identity_lines[0]}
resolved_variant=$(sed -n 's/^ERA_BUILD_IDENTITY variant=\([^ ]*\) tuple=.*/\1/p' <<<"$identity_line")
resolved_tuple=$(sed -n 's/^ERA_BUILD_IDENTITY variant=[^ ]* tuple=\(.*\)$/\1/p' <<<"$identity_line")
[[ $resolved_variant == "$variant" ]] ||
    fail "requested variant $variant resolved as $resolved_variant" 1
[[ $resolved_tuple =~ ^wire=(yes|no),qwin=(yes|no),phase=(yes|no),cause=(yes|no),stale=(yes|no)$ ]] ||
    fail "invalid resolved tuple for $resolved_variant: $resolved_tuple" 1

build_base="$repo_root/.build/$target_stem"
[[ -f "${build_base}.elf" ]] || fail "expected ELF was not found: ${build_base}.elf" 1

symbol_enabled() {
    local symbol=$1
    if arm-none-eabi-nm --defined-only "${build_base}.elf" | awk -v wanted="$symbol" '$NF == wanted { found=1 } END { exit found ? 0 : 1 }'; then
        printf yes
    else
        printf no
    fi
}

# Each axis has a link-visible production witness. The stale injection's
# witness is deliberately explicit; the other four are feature entry points
# that must exist for the diagnostic to work at all.
compiled_tuple="wire=$(symbol_enabled era_split_wire_diagnostics_task),qwin=$(symbol_enabled era_split_qwin_diagnostics_tick_1ms),phase=$(symbol_enabled era_pass_phase_mark),cause=$(symbol_enabled era_host_peer_storage_get_cause_timeline_snapshot),stale=$(symbol_enabled era_split_era_mirror_force_stale_compiled)"
[[ $compiled_tuple == "$resolved_tuple" ]] ||
    fail "compiled tuple $compiled_tuple disagrees with resolved tuple $resolved_tuple" 1
firmware_format=""
if [[ -f "${build_base}.uf2" ]]; then
    firmware_format=uf2
elif [[ -f "${build_base}.hex" ]]; then
    firmware_format=hex
else
    fail "expected UF2 or HEX was not found for $target" 1
fi

ram0_resident=""
ram0_free=""
vectors_gate=""
residency_gate="not applicable"
if [[ $firmware_format == uf2 ]]; then
    gate_log_tmp="$(mktemp "$artifact_dir/.${target_stem}_${variant}_${short_head}.gate.XXXXXX.log")"
    "$script_dir/era_residency_gate.sh" "${build_base}.elf" | tee "$gate_log_tmp"
    gate_output="$(<"$gate_log_tmp")"
    ram0_resident="$(awk -F= '/^ram0_resident_bytes=/ {print $2}' <<<"$gate_output")"
    ram0_free="$(awk -F= '/^ram0_free_bytes=/ {print $2}' <<<"$gate_output")"
    vectors_gate="$(awk '/^vectors_gate=/ {print; exit}' <<<"$gate_output")"
    if [[ ! $ram0_resident =~ ^[0-9]+$ || ! $ram0_free =~ ^[0-9]+$ || -z $vectors_gate ]]; then
        fail "could not read the residency figures from $gate_log_tmp" 1
    fi
    residency_gate="passed"
fi

firmware_sha256="$(sha256sum "${build_base}.${firmware_format}" | awk '{print $1}')"
elf_sha256="$(sha256sum "${build_base}.elf" | awk '{print $1}')"
artifact_id=${firmware_sha256:0:16}
stem="${target_stem}_${resolved_variant}_${short_head}${dirty_suffix}${date_suffix}_${artifact_id}"
firmware="$artifact_dir/${stem}.${firmware_format}"
elf="$artifact_dir/${stem}.elf"
build_log="$artifact_dir/${stem}.build.log"
manifest="$artifact_dir/${stem}.manifest.txt"
cp "${build_base}.${firmware_format}" "$firmware"
cp "${build_base}.elf" "$elf"
mv "$build_log_tmp" "$build_log"
build_log_tmp=""

gate_log=""
if [[ -n "$gate_log_tmp" ]]; then
    gate_log="$artifact_dir/${stem}.gate.log"
    mv "$gate_log_tmp" "$gate_log"
    gate_log_tmp=""
fi

map=""
if [[ -f "${build_base}.map" ]]; then
    map="$artifact_dir/${stem}.map"
    cp "${build_base}.map" "$map"
fi

qmk_cli_version="$(qmk --version)"
qmk_firmware_version="$(awk '/^QMK Firmware / {sub(/^QMK Firmware /, ""); print; exit}' "$build_log")"
compiler_version="$(awk '/^(arm-none-eabi-gcc|avr-gcc) / {print; exit}' "$build_log")"
if [[ -z "$compiler_version" ]]; then
    compiler_version="$(arm-none-eabi-gcc --version | sed -n '1p')"
fi
make_version="$(make --version | sed -n '1p')"

{
    printf 'requested_variant=%s\n' "$variant"
    printf 'variant=%s\n' "$resolved_variant"
    printf 'resolved_tuple=%s\n' "$resolved_tuple"
    printf 'compiled_tuple=%s\n' "$compiled_tuple"
    printf 'target=%s\n' "$target"
    printf 'git_head=%s\n' "$head"
    printf 'worktree_dirty=%s\n' "$dirty"
    printf 'edit_tree_check=%s\n' "$edit_tree_check"
    printf 'build_environment=WSL local filesystem\n'
    printf 'build_date=%s\n' "${build_date:-automatic}"
    printf 'qmk_cli_version=%s\n' "$qmk_cli_version"
    if [[ -n "$qmk_firmware_version" ]]; then
        printf 'qmk_firmware_version=%s\n' "$qmk_firmware_version"
    fi
    printf 'compiler=%s\n' "$compiler_version"
    printf 'make=%s\n' "$make_version"
    printf 'firmware=%s\n' "$firmware"
    printf 'firmware_format=%s\n' "$firmware_format"
    printf 'firmware_sha256=%s\n' "$firmware_sha256"
    printf 'artifact_id=%s\n' "$artifact_id"
    if [[ $firmware_format == uf2 ]]; then
        printf 'uf2=%s\n' "$firmware"
        printf 'uf2_sha256=%s\n' "$firmware_sha256"
    else
        printf 'hex=%s\n' "$firmware"
        printf 'hex_sha256=%s\n' "$firmware_sha256"
    fi
    printf 'elf=%s\n' "$elf"
    printf 'elf_sha256=%s\n' "$elf_sha256"
    printf 'residency_gate=%s\n' "$residency_gate"
    if [[ $residency_gate == passed ]]; then
        printf 'ram0_resident_bytes=%s\n' "$ram0_resident"
        printf 'ram0_free_bytes=%s\n' "$ram0_free"
        printf '%s\n' "$vectors_gate"
        printf 'gate_log=%s\n' "$gate_log"
    fi
    if [[ -n "$map" ]]; then
        printf 'map=%s\n' "$map"
    fi
    printf 'build_log=%s\n' "$build_log"
    printf 'command='
    printf ' %q' "${qmk_command[@]}"
    printf '\n'
} >"$manifest"

printf 'Firmware: %s\n' "$firmware"
printf 'SHA-256: %s\n' "$firmware_sha256"
printf 'Manifest: %s\n' "$manifest"
