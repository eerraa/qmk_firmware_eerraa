# Wire diagnostics plus the selector-gated era-boundary staleness injection.
# Proof image only, never flashed as a production build: every read of the
# transaction-engine diagnostics mirror takes its fallback, so an era block
# must report `meas=0` rather than the identical-zero delta that failure
# produces. Slice 10.6 goal 2.
ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE := yes
ERA_SPLIT_ERA_MIRROR_FORCE_STALE_ENABLE := yes
