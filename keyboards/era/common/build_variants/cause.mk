# Wire diagnostics plus the selector-gated storage cause timeline, indicator
# edge record and VIA dynamic-macro RAW-HID timing. The split rules refuse this
# variant unless EEPROM sync and storage V1 are present.
override ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE := yes
override ERA_SPLIT_QWIN_COUNT_ONLY_ENABLE := no
override ERA_PASS_PHASE_DIAGNOSTICS_ENABLE := no
override ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE := yes
override ERA_SPLIT_ERA_MIRROR_FORCE_STALE_ENABLE := no
