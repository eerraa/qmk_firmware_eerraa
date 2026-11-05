#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Macro BEGIN/DATA/COMMIT follows real NVM completion, never a blue barrier."""
from native_sync_owner import run_owner

if __name__ == "__main__":
    run_owner("era_nvm_qmk_driver", [
        "EraNvmQmkDriver.ExplicitViaReplacementEchoesDataAndAcknowledgesOnlyDurableCommit",
        "EraNvmQmkDriver.MacroStagingPrecedesDurableSemanticPublication",
    ])
