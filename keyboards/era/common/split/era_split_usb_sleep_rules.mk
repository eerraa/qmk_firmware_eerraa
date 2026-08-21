# Every ERA option this file reads is declared in keyboards/era/era_build_options.mk,
# which is the one file to edit. Included here rather than by the board so this
# fragment cannot run without its declarations; a board or a profile assigning
# above the include that reads it still wins, because every line there is `?=`.
include keyboards/era/era_build_options.mk

# ERA split RP2040 USB sleep baseline.
#
# The RP LLD fix is intentionally exposed through a generic RP_USB_* define so
# the ChibiOS delta reads as a controller state-sync policy, not as diagnostics.


ifneq ($(strip $(RP_USB_SYNC_SUSPEND_AFTER_REMOTE_WAKEUP_SET)),)
    ERA_SPLIT_RP_USB_SLEEP_SYNC := $(strip $(RP_USB_SYNC_SUSPEND_AFTER_REMOTE_WAKEUP_SET))
endif

OPT_DEFS += -DRP_USB_SYNC_SUSPEND_AFTER_REMOTE_WAKEUP_SET=$(ERA_SPLIT_RP_USB_SLEEP_SYNC)
