#!/usr/bin/env bash
# era-build - the whole edit-on-Windows, build-in-WSL loop in one command.
#
#   era-build era/sirind/tomak:via
#   era-build era/sirind/tomak:via cause
#   era-build era/sirind/tomak79h:via standard wire qwin cause
#
# The target is always explicit. Sync runs before the internal launcher, and
# only the artifacts named by this invocation are copied back to Windows.
#
# Installed as ~/bin/era-build, a symlink to this file in the EDIT tree; see
# era-sync.sh for why the link does not point into the build tree.
set -euo pipefail

# --- machine-specific, and the only part a new machine has to change --------
WIN_POSIX=/mnt/d/Engineering/qmk_firmware_eerraa
WSL="$HOME/projects/qmk_firmware_eerraa"
# ---------------------------------------------------------------------------
LAUNCHER=keyboards/era/common/tools/era_qmk_build.sh

usage() {
    cat <<'EOF'
Usage: era-build [--build-date YYYY-MM-DD-HH:MM:SS] [--show-options] TARGET [VARIANT ...]

Examples:
  era-build era/sirind/tomak:via
  era-build era/sirind/tomak:via cause
  era-build era/sirind/tomak79h:via standard wire qwin cause

An omitted variant builds standard on every target. Variant names are common
to every ERA keyboard; the make layer refuses one whose prerequisites the
selected board/keymap does not satisfy.
EOF
}

build_date=""
show_options="no"
while (($# > 0)); do
    case "$1" in
        --build-date)
            (($# >= 2)) || { echo "era-build: --build-date requires a value" >&2; exit 2; }
            build_date=$2
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
        --*)
            echo "era-build: unknown option: $1" >&2
            exit 2
            ;;
        *)
            break
            ;;
    esac
done

(($# >= 1)) || { usage >&2; exit 2; }
target=$1
shift
variants=("$@")
if [[ ! $target =~ ^era/[A-Za-z0-9_/-]+:[A-Za-z0-9_-]+$ ]]; then
    echo "era-build: invalid target (expected keyboard:keymap): $target" >&2
    exit 2
fi
if [[ -n "$build_date" && ! $build_date =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}-[0-9]{2}:[0-9]{2}:[0-9]{2}$ ]]; then
    echo "era-build: invalid build date: $build_date" >&2
    exit 2
fi

[ ${#variants[@]} -gt 0 ] || variants=(standard)
for variant in "${variants[@]}"; do
    if [[ ! $variant =~ ^[a-z][a-z0-9_]*$ ]]; then
        echo "era-build: invalid variant spelling: $variant" >&2
        exit 2
    fi
done

start=$SECONDS
era-sync
cd "$WSL"

# Take each manifest path from the launcher's own output. Globbing the
# artifact directory instead could select a previous run. Each returned name
# includes the firmware hash, but the manifest remains the authority for the
# matching ELF and logs.
runlog=$(mktemp)
trap 'rm -f "$runlog"' EXIT
labels=()
manifests=()
for variant in "${variants[@]}"; do
    labels+=("$variant")
    printf '\n=== %s %s ===\n' "$target" "$variant"
    launcher_args=("$LAUNCHER" "$target" "$variant")
    [ -n "$build_date" ] && launcher_args+=(--build-date "$build_date")
    [ "$show_options" = yes ] && launcher_args+=(--show-options)
    ERA_BUILD_TREE_SYNCED=yes bash "${launcher_args[@]}" | tee "$runlog"
    manifest=$(sed -n 's/^Manifest: //p' "$runlog" | tail -1)
    if [ -z "$manifest" ] || [ ! -f "$manifest" ]; then
        echo "era-build: launcher did not report a manifest for $target $variant" >&2
        exit 1
    fi
    manifests+=("$manifest")
done

# Copy only this invocation's declared files. Copying the whole WSL artifact
# directory can return a stale image that this run never built.
mkdir -p "$WIN_POSIX/.era-artifacts"
return_files=()
for manifest in "${manifests[@]}"; do
    return_files+=("$manifest")
    for field in firmware elf map build_log gate_log; do
        value=$(sed -n "s/^${field}=//p" "$manifest" | tail -1)
        [ -z "$value" ] || [ ! -f "$value" ] || return_files+=("$value")
    done
done
rsync -lt -- "${return_files[@]}" "$WIN_POSIX/.era-artifacts/"

printf '\n=== returned to %s ===\n' "$WIN_POSIX/.era-artifacts"
for i in "${!manifests[@]}"; do
    manifest=${manifests[$i]}
    firmware=$(sed -n 's/^firmware=//p' "$manifest" | tail -1)
    ram0_free=$(sed -n 's/^ram0_free_bytes=//p' "$manifest" | tail -1)
    printf '  %-12s dirty=%-3s edit_tree=%-28s ram0_free=%-7s %s\n' \
        "${labels[$i]}" \
        "$(sed -n 's/^worktree_dirty=//p' "$manifest")" \
        "$(sed -n 's/^edit_tree_check=//p' "$manifest")" \
        "${ram0_free:-n/a}" \
        "$(basename "$firmware")"
done
printf 'total %ss\n' "$((SECONDS - start))"
