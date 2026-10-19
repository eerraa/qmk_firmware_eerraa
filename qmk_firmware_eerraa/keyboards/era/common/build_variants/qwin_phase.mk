# The qwin instrument plus the pass-phase itemisation: every microsecond of a
# keyboard pass charged to one of twelve named segments, printed as ph=/us= on
# the qwin line. This is a measurement rung, never a comparison point; its cost
# is the difference against plain qwin in the same sitting.
override ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE := no
override ERA_SPLIT_QWIN_COUNT_ONLY_ENABLE := yes
override ERA_PASS_PHASE_DIAGNOSTICS_ENABLE := yes
override ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE := no
override ERA_SPLIT_ERA_MIRROR_FORCE_STALE_ENABLE := no
