#!/usr/bin/env python3
"""Build the deterministic, receipt-bound ERA 260901R1 release directory."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from era_release_260901R1_lib import ReleaseError, build_release


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--repo", type=Path, required=True, help="release source Git checkout")
    result.add_argument("--source-sha", required=True, help="full final source SHA")
    result.add_argument("--app-repo", type=Path, required=True, help="clean the-via-eerraa checkout")
    result.add_argument("--app-head", required=True, help="explicit local app commit used for V3 validation")
    result.add_argument("--receipt-set", type=Path, required=True, help="exact receipt-set.json from this build run")
    result.add_argument("--output-dir", type=Path, required=True, help="new directory; existing paths are refused")
    return result


def main(argv: list[str] | None = None) -> int:
    arguments = parser().parse_args(argv)
    try:
        manifest = build_release(
            repo=arguments.repo,
            source_sha=arguments.source_sha,
            app_repo=arguments.app_repo,
            app_head=arguments.app_head,
            receipt_set_path=arguments.receipt_set,
            output_dir=arguments.output_dir,
        )
    except ReleaseError as exc:
        print(f"era-release-package: {exc}", file=sys.stderr)
        return 1
    print(f"Release manifest: {manifest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
