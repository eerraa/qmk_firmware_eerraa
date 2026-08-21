#!/usr/bin/env bash
set -euo pipefail

real_qmk="${ERA_REAL_QMK_BIN:-}"
if [[ -z "$real_qmk" ]]; then
    real_qmk="$(command -v qmk || true)"
fi
if [[ -z "$real_qmk" ]]; then
    echo "era_qmk_fixed_builddate_wrapper.sh: qmk was not found" >&2
    exit 1
fi

"$real_qmk" "$@"

if [[ ${1-} != generate-version-h ]]; then
    exit 0
fi

: "${ERA_TEST_QMK_BUILDDATE:?ERA_TEST_QMK_BUILDDATE must be set for a fixed-builddate build}"
if [[ ! ${ERA_TEST_QMK_BUILDDATE} =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}-[0-9]{2}:[0-9]{2}:[0-9]{2}$ ]]; then
    echo "era_qmk_fixed_builddate_wrapper.sh: invalid build date: ${ERA_TEST_QMK_BUILDDATE}" >&2
    exit 2
fi

output=""
while (($# > 0)); do
    if [[ $1 == -o || $1 == --output ]]; then
        output=${2-}
        break
    fi
    shift
done

if [[ -z "$output" || ! -f "$output" ]]; then
    echo "era_qmk_fixed_builddate_wrapper.sh: version header output was not found" >&2
    exit 3
fi

sed -i -E "s|^#define QMK_BUILDDATE \"[^\"]+\"$|#define QMK_BUILDDATE \"${ERA_TEST_QMK_BUILDDATE}\"|" "$output"
grep -Fqx "#define QMK_BUILDDATE \"${ERA_TEST_QMK_BUILDDATE}\"" "$output"
