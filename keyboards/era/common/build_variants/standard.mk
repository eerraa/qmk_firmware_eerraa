# The ordinary board/keymap configuration. `standard` is a build identity, not
# a release approval: packaging decides what is released. All diagnostics are
# stated so an environment left over from a measuring session cannot enter an
# artifact still named standard.
override ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE := no
override ERA_SPLIT_QWIN_COUNT_ONLY_ENABLE := no
override ERA_PASS_PHASE_DIAGNOSTICS_ENABLE := no
override ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE := no
override ERA_SPLIT_ERA_MIRROR_FORCE_STALE_ENABLE := no
