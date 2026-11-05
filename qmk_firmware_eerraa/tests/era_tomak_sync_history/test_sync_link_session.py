#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Storage relation audits remain distinct from adopted transaction identity.

The removed presenter channel/SESSION rendezvous has no product contract.
Physical LINK lease readiness stays owned by era_split_link_lifecycle.
"""
from native_sync_owner import run_owner

if __name__ == "__main__":
    run_owner("era_host_peer_storage_news", [
        "EraHostPeerStorageNews.TransactionAdoptionCannotPretendTheNewRelationWasAudited",
        "EraHostPeerStorageNews.QueuedBootAuditDoesNotDeadlockLinkButAdmittedAuditMustDrain",
    ])
