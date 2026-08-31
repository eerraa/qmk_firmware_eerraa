#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"
fixture=tests/era_build_variant_rules/fixture.mk

fail() {
    echo "era-build-variant-test: $1" >&2
    exit 1
}

expect_line() {
    local label=$1 expected=$2
    shift 2
    local output
    output=$(cd "$repo_root" && "$@") || fail "$label unexpectedly failed"
    local actual
    actual=$(sed -n 's/^FIXTURE //p' <<<"$output" | tail -1)
    [[ $actual == "$expected" ]] || fail "$label: expected '$expected', got '$actual'"
}

expect_fail() {
    local label=$1
    shift
    if (cd "$repo_root" && "$@") >/dev/null 2>&1; then
        fail "$label unexpectedly succeeded"
    fi
}

declare -A tuples=(
    [standard]='wire=no,qwin=no,phase=no,cause=no,stale=no'
    [wire]='wire=yes,qwin=no,phase=no,cause=no,stale=no'
    [qwin]='wire=no,qwin=yes,phase=no,cause=no,stale=no'
    [cause]='wire=yes,qwin=no,phase=no,cause=yes,stale=no'
    [stale]='wire=yes,qwin=no,phase=no,cause=no,stale=yes'
    [qwin_phase]='wire=no,qwin=yes,phase=yes,cause=no,stale=no'
)

for variant in standard wire qwin cause stale qwin_phase; do
    expect_line "normal $variant" "variant=$variant tuple=${tuples[$variant]}" \
        make --no-print-directory -f "$fixture" "ERA_BUILD_VARIANT=$variant" print
done

# Direct option assignments, MAKEFLAGS, exported variables, and make -e all
# have higher precedence than ordinary makefile assignments. The selected
# fragment uses GNU make's `override` specifically so each attack still emits
# the canonical tuple for its name.
expect_line 'direct option overrides' "variant=cause tuple=${tuples[cause]}" \
    make --no-print-directory -f "$fixture" ERA_BUILD_VARIANT=cause \
        ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE=no ERA_SPLIT_QWIN_COUNT_ONLY_ENABLE=yes \
        ERA_PASS_PHASE_DIAGNOSTICS_ENABLE=yes ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE=no \
        ERA_SPLIT_ERA_MIRROR_FORCE_STALE_ENABLE=yes print

expect_line 'MAKEFLAGS overrides' "variant=standard tuple=${tuples[standard]}" \
    env 'MAKEFLAGS=ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE=yes ERA_SPLIT_QWIN_COUNT_ONLY_ENABLE=yes ERA_PASS_PHASE_DIAGNOSTICS_ENABLE=yes ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE=yes ERA_SPLIT_ERA_MIRROR_FORCE_STALE_ENABLE=yes' \
        make --no-print-directory -f "$fixture" ERA_BUILD_VARIANT=standard print

expect_line 'exported environment overrides' "variant=wire tuple=${tuples[wire]}" \
    env ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE=no ERA_SPLIT_QWIN_COUNT_ONLY_ENABLE=yes \
        ERA_PASS_PHASE_DIAGNOSTICS_ENABLE=yes ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE=yes \
        ERA_SPLIT_ERA_MIRROR_FORCE_STALE_ENABLE=yes \
        make --no-print-directory -f "$fixture" ERA_BUILD_VARIANT=wire print

expect_line 'make -e overrides' "variant=qwin_phase tuple=${tuples[qwin_phase]}" \
    env ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE=yes ERA_SPLIT_QWIN_COUNT_ONLY_ENABLE=no \
        ERA_PASS_PHASE_DIAGNOSTICS_ENABLE=no ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE=yes \
        ERA_SPLIT_ERA_MIRROR_FORCE_STALE_ENABLE=yes \
        make -e --no-print-directory -f "$fixture" ERA_BUILD_VARIANT=qwin_phase print

# The non-split and Brick65 rule is the same make-level boundary: standard is
# valid and firmware-inert; every diagnostic identity is refused.
expect_line 'non-split standard' "variant=standard tuple=${tuples[standard]}" \
    make --no-print-directory -f "$fixture" SPLIT_KEYBOARD=no ERA_BUILD_VARIANT=standard print
expect_fail 'non-split diagnostic variant' \
    make --no-print-directory -f "$fixture" SPLIT_KEYBOARD=no ERA_BUILD_VARIANT=wire print
expect_fail 'retired board profile' \
    make --no-print-directory -f "$fixture" ERA_TOMAK79H_BUILD_PROFILE=wire print

echo 'era-build-variant-test: all cases passed'
