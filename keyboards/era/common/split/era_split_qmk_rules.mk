# Every ERA option this file reads is declared in keyboards/era/era_build_options.mk,
# which is the one file to edit. Included here rather than by the board so this
# fragment cannot run without its declarations; a board or a profile assigning
# above the include that reads it still wins, because every line there is `?=`.
include keyboards/era/era_build_options.mk

include keyboards/era/common/system/era_common_qmk_rules.mk

# Every ERA split board is copy-to-RAM, and the bundle that makes it one lives
# in the common layer rather than here. It moved out on 2026-08-01: it was
# never split-specific, it only looked that way while every copy-to-RAM board
# was a split one. A non-split board opts in with the same include.
include keyboards/era/common/system/era_sram_resident_rules.mk


# Derived, not chosen: the replacement-storage engine is what the EEPROM sync
# feature is made of, and every board that wanted one wrote this same two-line
# derivation into its own rules.mk. One place now; a board that sets it
# explicitly still wins, because this is a `?=`.
ifeq ($(strip $(ERA_SPLIT_EEPROM_SYNC_ENABLE)), yes)
    ERA_HOST_PEER_STORAGE_V1_ENABLE ?= yes

    # The EEPROM geometry the engine is laid out in. Five preconditions, four
    # of them values, and until 2026-08-13 they lived in a tomak-named family
    # header -- so a board outside that family found them one _Static_assert at
    # a time, in a file that names none of the others. One refusal, before any
    # compile, naming the file that supplies them.
    ifneq ($(strip $(ERA_STORAGE_ADOPTION_INCLUDED)), yes)
        $(error ERA_SPLIT_EEPROM_SYNC_ENABLE requires keyboards/era/common/storage/era_storage_adoption_rules.mk, included from this board post_rules.mk above the ERA split fragment -- it supplies the 24 KiB logical EEPROM, the exact 16 KiB macro domain and the VIA magic address the storage schema is laid out in, and names the two preconditions it cannot supply)
    endif
endif
ERA_HOST_PEER_STORAGE_V1_ENABLE ?= no
ifeq ($(strip $(ERA_HOST_PEER_STORAGE_V1_ENABLE)), yes)
    ifneq ($(strip $(ERA_SPLIT_EEPROM_SYNC_ENABLE)), yes)
        $(error ERA_HOST_PEER_STORAGE_V1_ENABLE requires ERA_SPLIT_EEPROM_SYNC_ENABLE=yes; the storage engine is the sync feature's implementation, not a separate one)
    endif
endif

# The DUAL-HOST peer's layer contribution and the one point it composes.
# This line and the selector below are one fact and belong together: the
# selector is what makes quantum/action_layer.c reach the accessor, and setting
# it without the writer fails the link on the undefined symbol rather than
# resolving to nothing. Split-only, because the relation it serves is.
SRC += keyboards/era/common/split/era_split_peer_layer.c
OPT_DEFS += -DERA_SPLIT_PEER_LAYER_MERGE_ENABLE

# The INPUT layer section is one wire byte, so an ERA split relation admits at
# most eight layers -- a wire-format fact, not a board preference. QMK derives
# layer_state_t's width from DYNAMIC_KEYMAP_LAYER_COUNT and falls back to
# sixteen bits when nothing supplies one, so a build with no dynamic keymap
# (any non-VIA keymap) used to fail a _Static_assert in era_split_peer_layer.c
# naming a layer count the board had never chosen. Declaring the requirement
# here is what makes a non-VIA ERA split build possible at all.
#
# It **could** be conditional on VIA_ENABLE since the option layer moved to the
# post_rules.mk phase, and it still must not be. This comment said the opposite
# until 2026-08-11 -- that the ordering made VIA_ENABLE unreadable here -- and
# the conclusion survives its premise: a non-VIA keymap is exactly the build
# with no DYNAMIC_KEYMAP_LAYER_COUNT, so it is the one that needs this define
# most. The bound that keeps it honest -- a layer count above eight fails loudly
# instead of being truncated by this very define -- lives beside the assert it
# protects, in era_split_peer_layer.c.
OPT_DEFS += -DLAYER_STATE_8BIT

# FA-2's cross-half tap-hold family: upstream Speculative Hold (the mod-tap
# half), the ERA layer-tap half beside it, and the split unit that owns the
# family's state and counters. One block because they are one fact — the two
# selectors make quantum/action_tapping.c reach this unit's counter sink, and
# setting either without the SRC line fails the link on the undefined symbol.
# Split-only by the same reasoning as the peer layer: the seam the family
# serves is the split one, and a non-split board keeps exact single-keyboard
# tap-hold semantics. Arming is runtime and derived (era_tapping's
# hold-on-other-key-press bridge value, default off), so compiling this in
# changes nothing on fresh defaults.
#
# The family reads the tap-hold policy the ERA tapping unit persists -- the
# judgment window is derived from hold-on-other-key-press, permissive-hold and
# retro-tapping -- and it is compiled into every ERA split build unconditionally,
# so the two do not compose. Said here because make can refuse it once, before
# any compile, naming both selectors: without this the answer is three undefined
# references to era_tapping_get_* raised at a caller that never mentions the
# selector the person turned off.
ifneq ($(strip $(ERA_TAPPING_CONFIG_ENABLE)), yes)
    $(error ERA split requires ERA_TAPPING_CONFIG_ENABLE=yes; the cross-half tap-hold judgment window is derived from the tap-hold policy that unit owns)
endif
SRC += keyboards/era/common/split/era_split_tap_activity.c
OPT_DEFS += -DERA_SPLIT_TAP_ACTIVITY_ENABLE
OPT_DEFS += -DSPECULATIVE_HOLD
OPT_DEFS += -DERA_SPECULATIVE_LAYER_ENABLE

# The split class skeleton, under the same selector the non-split one takes in
# era_common_qmk_rules.mk (which also supplies era_board_hooks.c for both
# classes). What it owns and what it deliberately does not are written at the
# top of the file; the short version is that it owns housekeeping_task_kb, so a
# split board cannot lose the double-tap window closer either.
ifeq ($(strip $(ERA_BOARD_COMMON_ENABLE)), yes)
    SRC += keyboards/era/common/split/era_split_board.c
endif

SRC += keyboards/era/common/split/era_split_keyboard.c
SRC += keyboards/era/common/split/era_split_sync_policy.c
# The agreed restart is the mechanism two halves reset together through, and it
# compiles for every split board for the same reason the link level below does:
# the wire carries its section whether or not this build has a control that can
# raise one.
SRC += keyboards/era/common/split/era_split_restart_agreement.c
# The link level is persisted state the backend and the scheduler both read, so
# it compiles for every split board -- the wire runs at the stored rate whether
# or not anything can change it. Only its VIA adapter is VIA's, on the same
# split the sync policy takes below.
SRC += keyboards/era/common/split/era_split_link.c
# The policy is persisted state the relation reads; only its VIA adapter is
# VIA's. Its single caller is era_split_keyboard_handle_via_command().
ifeq ($(strip $(VIA_ENABLE)), yes)
    SRC += keyboards/era/common/split/era_split_via_sync.c
    SRC += keyboards/era/common/split/era_split_via_link.c
endif
SRC += keyboards/era/common/split/era_split_transport.c
SRC += keyboards/era/common/split/era_split_usb_identity.c

# Core1's park instrument -- how many times and for how long core1 slept on
# WFE -- is a fact both diagnostic images want and the release image must not
# pay for (two timer reads per park). Derived here from the two selectors
# rather than declared: there is no third answer for a build to give.
ifneq ($(filter yes,$(strip $(ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE)) $(strip $(ERA_SPLIT_QWIN_COUNT_ONLY_ENABLE))),)
    OPT_DEFS += -DERA_SPLIT_CORE1_PARK_DIAGNOSTICS_ENABLE
endif

ifeq ($(strip $(ERA_SPLIT_QWIN_COUNT_ONLY_ENABLE)), yes)
    OPT_DEFS += -DERA_SPLIT_QWIN_COUNT_ONLY_ENABLE
    OPT_DEFS += -DMATRIX_SCAN_COUNT_DIAGNOSTICS_ENABLE
    OPT_DEFS += -DCONSOLE_IN_CAPACITY=16
    CONSOLE_ENABLE = yes
    SRC += keyboards/era/common/split/diagnostics/era_split_wire_diagnostics_counter.c
    SRC += keyboards/era/common/split/diagnostics/era_split_qwin_diagnostics.c
endif

# The pass-phase instrument (declared in the manifest). The twelve segments are
# marked in era_rp2040_matrix_core.c, era_split_keyboard.c and quantum/
# keyboard.c, and printed on the qwin line -- so the count-only instrument is a
# hard requirement rather than a pairing, and saying so by name here is what
# stops a profile from setting a selector whose output has nowhere to go.
ifeq ($(strip $(ERA_PASS_PHASE_DIAGNOSTICS_ENABLE)), yes)
    ifneq ($(strip $(ERA_SPLIT_QWIN_COUNT_ONLY_ENABLE)), yes)
        $(error ERA_PASS_PHASE_DIAGNOSTICS_ENABLE requires ERA_SPLIT_QWIN_COUNT_ONLY_ENABLE=yes; the qwin line is where it prints)
    endif
    OPT_DEFS += -DERA_PASS_PHASE_DIAGNOSTICS_ENABLE
    SRC += keyboards/era/common/system/era_pass_phase_diagnostics.c
endif

ifeq ($(strip $(ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE)), yes)
    OPT_DEFS += -DERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    OPT_DEFS += -DERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
    ifneq ($(strip $(ERA_SPLIT_QWIN_COUNT_ONLY_ENABLE)), yes)
        OPT_DEFS += -DMATRIX_SCAN_RAW_DIAGNOSTICS_ENABLE
    endif
    OPT_DEFS += -DCONSOLE_IN_CAPACITY=16
    CONSOLE_ENABLE = yes
    ifneq ($(strip $(ERA_SPLIT_QWIN_COUNT_ONLY_ENABLE)), yes)
        SRC += keyboards/era/common/split/diagnostics/era_split_wire_diagnostics_counter.c
    endif
endif

ifeq ($(strip $(ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE)), yes)
    ifneq ($(strip $(ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE)), yes)
        $(error ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE requires ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE=yes)
    endif
    ifneq ($(strip $(ERA_SPLIT_EEPROM_SYNC_ENABLE)), yes)
        $(error ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE requires ERA_SPLIT_EEPROM_SYNC_ENABLE=yes)
    endif
    ifneq ($(strip $(ERA_HOST_PEER_STORAGE_V1_ENABLE)), yes)
        $(error ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE requires ERA_HOST_PEER_STORAGE_V1_ENABLE=yes)
    endif
    OPT_DEFS += -DERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
endif

# Injection selector, never a production surface: it compiles the accept path
# out of the transaction-engine diagnostics mirror read so every read takes
# the fallback. Its only purpose is to prove that an era block declares a
# boundary it could not measure (`meas=0`) instead of reporting the identical
# zero that failure produces. The whole surface it reaches is diagnostics.
ifeq ($(strip $(ERA_SPLIT_ERA_MIRROR_FORCE_STALE_ENABLE)), yes)
    ifneq ($(strip $(ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE)), yes)
        $(error ERA_SPLIT_ERA_MIRROR_FORCE_STALE_ENABLE requires ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE=yes)
    endif
    OPT_DEFS += -DERA_SPLIT_ERA_MIRROR_FORCE_STALE_ENABLE
endif


# The board is actually a split one. Nothing checked this until 2026-08-11,
# because until the option layer moved to the post_rules.mk phase nothing here
# could see SPLIT_KEYBOARD -- it arrives from keyboard.json's split.enabled
# through the generated info_rules.mk. A board that includes this file without
# it would build every split unit against a QMK core that never calls the
# transport hooks.
ifneq ($(strip $(SPLIT_KEYBOARD)), yes)
    $(error ERA split rules require a split board; set split.enabled in keyboard.json, or include era_common_qmk_rules.mk instead)
endif

ifneq ($(strip $(ERA_RP2040_MATRIX_ENABLE)), yes)
    $(error ERA split transport requires ERA_RP2040_MATRIX_ENABLE; stock-compatible split matrix fallback is retired)
endif

ifneq ($(strip $(NO_USB_STARTUP_CHECK)),)
    ifneq ($(strip $(NO_USB_STARTUP_CHECK)), yes)
        $(error ERA split transport requires NO_USB_STARTUP_CHECK=yes so dynamic PEER role returns keep keyboard_task() running without local USB)
    endif
endif
# Dynamic role changes need keyboard_task() to keep running without local USB.
NO_USB_STARTUP_CHECK = yes

# The other half of that switch, and the reason these refusals exist.
#
# NO_USB_STARTUP_CHECK deletes QMK's suspend loop (chibios.c:177-198), which is
# the only caller of suspend_power_down_quantum() -- so on a split board nothing
# runs the generic lighting-sleep path, and era_split_keyboard.c's own apply is
# what turns lighting off. That apply drives RGB Matrix and nothing else.
#
# The three sirind boards are RGB Matrix only, so the gap is invisible today and
# would stay invisible on the board that first tripped it: a split ERA board
# with an OLED or a backlight would build, run, and simply never sleep that
# lighting, with no error anywhere. Refused by name instead. Adding one of these
# means extending the split arm of the sleep apply and deleting its line here,
# which is a decision someone makes on purpose rather than a behaviour someone
# discovers on a rig.
#
# Routing the split apply through suspend_power_down_quantum() to cover them was
# considered and rejected on 2026-08-11: that function calls rgb_matrix_task()
# immediately before rgb_matrix_set_suspend_state(true), and the flush that
# follows credits RGB_MATRIX_RENDER_FRAME_* to the storage-status and Caps
# indicator owners in sirind/common/tomak_common.c for a frame the same call
# then paints black.
# Corrupting the render-policy accounting to reach subsystems no split board has
# is a bad trade.
ERA_SPLIT_UNSLEPT_LIGHTING := $(strip \
    $(if $(filter yes,$(BACKLIGHT_ENABLE)),BACKLIGHT_ENABLE) \
    $(if $(filter yes,$(RGBLIGHT_ENABLE)),RGBLIGHT_ENABLE) \
    $(if $(filter yes,$(LED_MATRIX_ENABLE)),LED_MATRIX_ENABLE) \
    $(if $(filter yes,$(SLEEP_LED_ENABLE)),SLEEP_LED_ENABLE) \
    $(if $(filter yes,$(OLED_ENABLE)),OLED_ENABLE) \
    $(if $(filter yes,$(ST7565_ENABLE)),ST7565_ENABLE))
ifneq ($(ERA_SPLIT_UNSLEPT_LIGHTING),)
    $(error ERA split lighting sleep covers RGB Matrix only, so $(ERA_SPLIT_UNSLEPT_LIGHTING) would never sleep on this board -- extend the split arm of era_split_keyboard.c's sleep apply and remove the name from this check, or turn the feature off)
endif
OPT_DEFS += -DSERIAL_USART_SPEED=$(strip $(ERA_SPLIT_SERIAL_USART_SPEED))

# The seven timing knobs below are rule 2 done correctly and rule 1 left
# undone. Each default is a #ifndef in the C file that reads it, which is where
# a value's default belongs and is why make must not restate it -- listed with
# the file so the value is one hop away:
#
#   ERA_SPLIT_PEER_RESPONSE_WINDOW_MS               era_split_wire_protocol.h
#   ERA_SPLIT_SESSION_BOOTSTRAP_RESPONSE_WINDOW_MS  era_split_wire_protocol.h
#   ERA_SPLIT_SESSION_BOOTSTRAP_BACKOFF_AFTER       scheduler/..._internal.h
#   ERA_SPLIT_SESSION_BOOTSTRAP_BACKOFF_PERIOD_MS   scheduler/..._internal.h
#   ERA_SPLIT_SESSION_REFRESH_PERIOD_MS             scheduler/..._internal.h
#   ERA_SPLIT_HOST_SOURCE_RESPONSE_POLL_PERIOD_MS   scheduler/..._internal.h
#   ERA_SPLIT_SESSION_STALE_MS                      scheduler/..._internal.h
#
# None of them was declared anywhere until 2026-08-11 -- a live axis that
# `grep '?='` could not find. They carry an empty declaration in
# keyboards/era/era_build_options.mk now, so every pass-through below stays
# false and every default stays in C; what the declaration buys is only that
# the axis is on a page a person reads.

ifneq ($(strip $(ERA_SPLIT_PEER_RESPONSE_WINDOW_MS)),)
    OPT_DEFS += -DERA_SPLIT_PEER_RESPONSE_WINDOW_MS=$(strip $(ERA_SPLIT_PEER_RESPONSE_WINDOW_MS))
endif
ifneq ($(strip $(ERA_SPLIT_SESSION_BOOTSTRAP_RESPONSE_WINDOW_MS)),)
    OPT_DEFS += -DERA_SPLIT_SESSION_BOOTSTRAP_RESPONSE_WINDOW_MS=$(strip $(ERA_SPLIT_SESSION_BOOTSTRAP_RESPONSE_WINDOW_MS))
endif
ifneq ($(strip $(ERA_SPLIT_SESSION_BOOTSTRAP_BACKOFF_AFTER)),)
    OPT_DEFS += -DERA_SPLIT_SESSION_BOOTSTRAP_BACKOFF_AFTER=$(strip $(ERA_SPLIT_SESSION_BOOTSTRAP_BACKOFF_AFTER))
endif
ifneq ($(strip $(ERA_SPLIT_SESSION_BOOTSTRAP_BACKOFF_PERIOD_MS)),)
    OPT_DEFS += -DERA_SPLIT_SESSION_BOOTSTRAP_BACKOFF_PERIOD_MS=$(strip $(ERA_SPLIT_SESSION_BOOTSTRAP_BACKOFF_PERIOD_MS))
endif
ifneq ($(strip $(ERA_SPLIT_SESSION_REFRESH_PERIOD_MS)),)
    OPT_DEFS += -DERA_SPLIT_SESSION_REFRESH_PERIOD_MS=$(strip $(ERA_SPLIT_SESSION_REFRESH_PERIOD_MS))
endif
ifneq ($(strip $(ERA_SPLIT_HOST_SOURCE_RESPONSE_POLL_PERIOD_MS)),)
    OPT_DEFS += -DERA_SPLIT_HOST_SOURCE_RESPONSE_POLL_PERIOD_MS=$(strip $(ERA_SPLIT_HOST_SOURCE_RESPONSE_POLL_PERIOD_MS))
endif
ifneq ($(strip $(ERA_SPLIT_SESSION_STALE_MS)),)
    OPT_DEFS += -DERA_SPLIT_SESSION_STALE_MS=$(strip $(ERA_SPLIT_SESSION_STALE_MS))
endif

ERA_SPLIT_COMMUNICATION_CORE_LEGACY_STAGE_VARS := \
    ERA_SPLIT_COMMUNICATION_CORE_ENABLE \
    ERA_SPLIT_COMMUNICATION_CORE_SOURCE_PUSH_ENABLE \
    ERA_SPLIT_COMMUNICATION_CORE_HEARTBEAT_ENABLE \
    ERA_SPLIT_COMMUNICATION_CORE_QUEUE_ENABLE \
    ERA_SPLIT_COMMUNICATION_CORE_LIFECYCLE_ENABLE \
    ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_ENABLE
ifneq ($(strip $(foreach stage_var,$(ERA_SPLIT_COMMUNICATION_CORE_LEGACY_STAGE_VARS),$($(stage_var)))),)
    $(error Use ERA_SPLIT_COMMUNICATION_CORE_STAGE instead of individual communication-core enable variables)
endif

ERA_SPLIT_COMMUNICATION_CORE_STAGE ?= CORE1_FULL
ifneq ($(strip $(ERA_SPLIT_COMMUNICATION_CORE_STAGE)),CORE1_FULL)
    $(error ERA split communication-core stages before CORE1_FULL are retired; expected CORE1_FULL)
endif
# The stage variable above is the whole of the stage mechanism now. It used to
# emit eight macros here, and the structural slice flattened the last guard that
# read one of them, so by 2026-08-11 all eight had zero preprocessor readers in
# keyboards/, quantum/, platforms/, drivers/ and tmk_core/. They were classed as
# invariants -- an unconditional -D beside the SRC line it is one fact with --
# and that class earns its keep by failing the link when the pair is broken.
# These could not: a macro nothing reads cannot fail anything. What actually
# holds this stage is the $(error) above and the SRC block below.
SRC += keyboards/era/common/split/era_split_wire_frame.c
SRC += keyboards/era/common/split/era_split_wire_payload.c
SRC += keyboards/era/common/split/era_split_matrix_frame.c
SRC += keyboards/era/common/split/era_host_peer_matrix_link.c
SRC += keyboards/era/common/split/era_host_peer_response.c
SRC += keyboards/era/common/split/era_host_peer_source_snapshot.c
SRC += keyboards/era/common/split/era_host_peer_responder.c
SRC += keyboards/era/common/split/era_split_mode_planner.c
SRC += keyboards/era/common/split/era_split_wire_router.c
SRC += keyboards/era/common/split/era_split_scheduler_session.c
SRC += keyboards/era/common/split/era_split_transaction_backend_rp2040.c
SRC += keyboards/era/common/split/era_split_transaction_io.c
SRC += keyboards/era/common/split/era_split_transaction_engine.c
SRC += keyboards/era/common/split/era_split_responder_projection.c
SRC += keyboards/era/common/split/scheduler/era_split_transport_scheduler_routes.c
SRC += keyboards/era/common/split/scheduler/era_split_transport_scheduler_timing.c
SRC += keyboards/era/common/split/era_split_transport_scheduler.c
SRC += keyboards/era/common/split/communication_core/era_split_communication_core_lifecycle_rp2040.c
SRC += keyboards/era/common/split/communication_core/era_split_communication_core_launch_signal.c
SRC += keyboards/era/common/split/communication_core/era_split_communication_core_queue.c
SRC += keyboards/era/common/split/communication_core/era_split_communication_core_diagnostics.c
SRC += keyboards/era/common/split/communication_core/era_split_communication_core_owner.c
SRC += keyboards/era/common/split/communication_core/era_split_communication_core_host_peer_lanes.c
# The standing DUAL-HOST exchange. It carries both the Core1 service and the
# Core0 publish/read side in one unit deliberately: the two records are one
# contract, and splitting them would let a change to the layout land on one
# side only.
SRC += keyboards/era/common/split/communication_core/era_split_communication_core_standing.c
# Keep the Core1 service beside the replaced responder executor, but late-link
# the Core0 handoff so FULL-only cold state does not displace scan-bound text.
SRC += keyboards/era/common/split/communication_core/era_split_communication_core_responder_service.c
QUANTUM_LIB_SRC += keyboards/era/common/split/communication_core/era_split_communication_core_responder.c
QUANTUM_LIB_SRC += keyboards/era/common/split/scheduler/era_split_transport_scheduler_responder.c

ifeq ($(strip $(ERA_SPLIT_EEPROM_SYNC_ENABLE)), yes)
    OPT_DEFS += -DERA_SPLIT_EEPROM_SYNC_ENABLE
    SRC += keyboards/era/common/split/era_split_eeprom_sync.c
    ifeq ($(strip $(ERA_HOST_PEER_STORAGE_V1_ENABLE)), yes)
        # The engine copies VIA-owned domains -- the dynamic keymap, the layout
        # options -- and the RGB Matrix eeconfig block, so it requires both
        # features. This was an `#error` in era_host_peer_storage.c until
        # 2026-08-11, which was the only layer that could see them; make can
        # now, so it refuses once, before any compile, in the same file as the
        # selector that pulled the unit in. The sibling requirement on
        # ERA_SRAM_RESIDENT_IMAGE deliberately stays in C: that one is a
        # preprocessor define rather than a QMK feature switch, and a define is
        # best checked where defines are.
        ifneq ($(strip $(VIA_ENABLE)), yes)
            $(error ERA HOST-PEER storage requires VIA_ENABLE; it replicates VIA-owned EEPROM domains)
        endif
        ifneq ($(strip $(RGB_MATRIX_ENABLE)), yes)
            $(error ERA HOST-PEER storage requires RGB_MATRIX_ENABLE; the RGB Matrix eeconfig block is one of its portable domains)
        endif
        OPT_DEFS += -DERA_HOST_PEER_STORAGE_V1_ENABLE
        SRC += keyboards/era/common/split/era_host_peer_storage.c
        SRC += keyboards/era/common/split/communication_core/era_split_communication_core_storage.c
        SRC += keyboards/era/common/split/communication_core/era_split_communication_core_storage_service.c
    endif
endif

SRC += keyboards/era/common/split/era_split_authority_reducer.c

ifeq ($(strip $(ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE)), yes)
    # The scheduler's snapshot/baseline unit belongs here rather than beside its
    # siblings above: both of its exports are reached only from
    # era_split_wire_diagnostics.c, so a release profile compiled it only for
    # gc-sections to discard it again. Its declarations in
    # era_split_transport_scheduler.h stay ungated on purpose — a future
    # release-side caller then fails the link instead of silently pulling the
    # whole unit back into the image.
    SRC += keyboards/era/common/split/scheduler/era_split_transport_scheduler_diagnostics.c
    SRC += keyboards/era/common/split/diagnostics/era_split_transport_scheduler_role_diagnostics.c
    SRC += keyboards/era/common/split/diagnostics/era_split_wire_diagnostics.c
endif

include keyboards/era/common/split/era_split_usb_sleep_rules.mk
