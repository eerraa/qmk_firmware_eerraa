#!/usr/bin/env python3
"""Verify the complete ERA 260901R1 release against Git and fresh receipts."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from era_release_260901R1_lib import ReleaseError, verify_release


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--repo", type=Path, required=True, help="clean local main checkout")
    result.add_argument("--source-sha", required=True, help="full final source SHA")
    result.add_argument("--app-repo", type=Path, required=True, help="clean the-via-eerraa checkout")
    result.add_argument("--app-head", required=True, help="explicit local app commit recorded by the packager")
    result.add_argument("--receipt-set", type=Path, required=True)
    result.add_argument("--release-dir", type=Path, required=True)
    result.add_argument(
        "--via-validator",
        type=Path,
        required=True,
        help="executable runner for the app-owned scripts/validate-external-v3.ts CLI",
    )
    result.add_argument("--objdump", default="arm-none-eabi-objdump")
    result.add_argument(
        "--objdump-arg",
        action="append",
        default=[],
        help="argument inserted after --objdump (repeat for an ELF-reader wrapper)",
    )
    return result


def main(argv: list[str] | None = None) -> int:
    arguments = parser().parse_args(argv)
    try:
        via_validator = arguments.via_validator.resolve()
        if not via_validator.is_file():
            raise ReleaseError(f"VIA validator runner is not a file: {via_validator}")
        counts = verify_release(
            repo=arguments.repo,
            source_sha=arguments.source_sha,
            app_repo=arguments.app_repo,
            app_head=arguments.app_head,
            receipt_set_path=arguments.receipt_set,
            release_dir=arguments.release_dir,
            validator_command=[str(via_validator)],
            objdump_command=[arguments.objdump, *arguments.objdump_arg],
        )
    except ReleaseError as exc:
        print(f"era-release-verify: {exc}", file=sys.stderr)
        return 1
    print(
        "Verified: "
        f"{counts['packages']} packages, {counts['via_json']} VIA JSONs, "
        f"{counts['output_files']} release files"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
