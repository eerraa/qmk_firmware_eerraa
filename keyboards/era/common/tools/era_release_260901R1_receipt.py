#!/usr/bin/env python3
"""Create and finalize fresh clean-build receipts for ERA 260901R1."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from era_release_260901R1_lib import (
    ReleaseError,
    create_receipt,
    finalize_receipt_set,
    load_inventory,
    validate_inventory_against_git,
)


def parser() -> argparse.ArgumentParser:
    root = argparse.ArgumentParser(description=__doc__)
    subcommands = root.add_subparsers(dest="command", required=True)

    targets = subcommands.add_parser("inventory-targets", help="print the ordered target/stem inventory")
    targets.add_argument("--repo", type=Path, required=True)
    targets.add_argument("--source-sha", required=True)

    create = subcommands.add_parser("create", help="turn one exact era-build manifest into one receipt")
    create.add_argument("--repo", type=Path, required=True)
    create.add_argument("--source-sha", required=True)
    create.add_argument("--manifest", type=Path, required=True)
    create.add_argument("--receipt-set-id", required=True)
    create.add_argument("--not-before-ns", type=int, required=True)
    create.add_argument("--output-dir", type=Path, required=True)
    create.add_argument("--objdump", default="arm-none-eabi-objdump")
    create.add_argument(
        "--objdump-arg",
        action="append",
        default=[],
        help="argument inserted after --objdump (repeat for an ELF-reader wrapper)",
    )

    finalize = subcommands.add_parser("finalize", help="atomically name the complete explicit 22-receipt set")
    finalize.add_argument("--repo", type=Path, required=True)
    finalize.add_argument("--source-sha", required=True)
    finalize.add_argument("--receipt-set-id", required=True)
    finalize.add_argument("--output-dir", type=Path, required=True)
    finalize.add_argument("--receipt", type=Path, action="append", required=True)
    return root


def main(argv: list[str] | None = None) -> int:
    arguments = parser().parse_args(argv)
    try:
        if arguments.command == "inventory-targets":
            inventory, _ = load_inventory(arguments.repo, arguments.source_sha)
            validate_inventory_against_git(arguments.repo, arguments.source_sha, inventory)
            for package in inventory["packages"]:
                print(f"{package['target']}\t{package['stem']}")
            return 0
        if arguments.command == "create":
            receipt = create_receipt(
                repo=arguments.repo,
                source_sha=arguments.source_sha,
                manifest_path=arguments.manifest,
                receipt_set_id=arguments.receipt_set_id,
                not_before_ns=arguments.not_before_ns,
                output_dir=arguments.output_dir,
                objdump_command=[arguments.objdump, *arguments.objdump_arg],
            )
            print(f"Receipt: {receipt}")
            return 0
        receipt_set = finalize_receipt_set(
            repo=arguments.repo,
            source_sha=arguments.source_sha,
            receipt_set_id=arguments.receipt_set_id,
            output_dir=arguments.output_dir,
            receipt_paths=arguments.receipt,
        )
        print(f"Receipt set: {receipt_set}")
        return 0
    except ReleaseError as exc:
        print(f"era-release-{arguments.command}: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
