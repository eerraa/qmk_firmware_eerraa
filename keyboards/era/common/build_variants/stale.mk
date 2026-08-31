# Wire diagnostics plus the selector-gated era-boundary staleness injection.
# Proof image only, never a production image: every read of the transaction-
# engine diagnostics mirror takes its fallback, so an era block must report
# meas=0 rather than the identical-zero delta that failure produces.
override ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE := yes
override ERA_SPLIT_QWIN_COUNT_ONLY_ENABLE := no
override ERA_PASS_PHASE_DIAGNOSTICS_ENABLE := no
override ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE := no
override ERA_SPLIT_ERA_MIRROR_FORCE_STALE_ENABLE := yes
