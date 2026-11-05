VIA_ENABLE = yes
CONSOLE_ENABLE = yes
OPT_DEFS += -DERA_TOMAK_SYNC_TIMING
# Keep timing output without unrelated QMK debug strings in SRAM.
OPT_DEFS += -DNO_DEBUG
