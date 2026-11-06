#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Storage retirement runs the production input recovery/release owner."""
from native_sync_owner import run_owner

if __name__ == "__main__":
    run_owner("era_host_peer_storage_news", [
        "EraHostPeerStorageNews.FailedApplyRetiresAndRecoversLatestReleaseWithoutRetransfer",
    ])
