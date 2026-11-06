#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""HOST orientations and DUAL-HOST share the production storage attempt owner."""
from native_sync_owner import run_owner

if __name__ == "__main__":
    run_owner("era_nvm_qmk_driver", [
        "EraNvmQmkDriver.AcceptedSameValueDuringRetirementAuthorizesFreshOpen",
        "EraNvmQmkDriver.TerminalResponderRejectsOldIdentityAndPinsFreshEqualOpen",
    ])
