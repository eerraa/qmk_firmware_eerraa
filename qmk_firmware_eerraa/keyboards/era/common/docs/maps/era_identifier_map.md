# ERA Identifier Map

Genre: map
Canonical for: identifier-namespace lookup to the first source owner and the
compatibility contract or decoding check that governs allocation

This map does not own numeric values. ERA VIA allocations are owned by
`the-via-eerraa/config/era-wire.json` under
`the-via-eerraa/docs/adr/0005-era-wire-registry.md` and consumed through
`system/era_wire.h`. Firmware enum/macro names are aliases, not allocation
owners. Split-wire ids and diagnostic field names remain in their named owners.
`keyboards/era/common/tools/era_wire_contract.py` checks official VIA name/value
sets, versions and literal redefinitions of ERA aliases; `quantum/via.h`
also expands the generated official tables as compile-time assertions.
Run `python tests/era_wire_contract/test_wire_contract.py` for its positive
inventory and deliberate negative fixtures. The commit hook checks staged blobs
when ERA sources, the generated header, via.h or keycodes.h change.
Cross-build non-reuse and the unresolved mode 5/6 question are owned by
`contracts/era_invariants.md` **Identifier Compatibility**.

## Modes

| Responsibility | First source file / symbol | Contract / check |
| --- | --- | --- |
| relation-mode identifiers | `split/era_split_mode_planner.h` — `era_split_mode_t` | `contracts/era_authority_contract.md`; `contracts/era_invariants.md` **Identifier Compatibility** |

## Routes And Route Reasons

| Responsibility | First source file / symbol | Contract / check |
| --- | --- | --- |
| route kinds and route-reason identifiers | `split/era_split_wire_router.h` | `contracts/era_route_contract.md`; decode with `manuals/era_capture_reading.md` |

## Storage Service Causes

| Responsibility | First source file / symbol | Contract / check |
| --- | --- | --- |
| storage service / pending-probe state | `split/era_host_peer_storage.c` — `probe_pending_mask` | `contracts/era_host_peer_storage_contract.md`; `contracts/era_route_contract.md` |

## Payloads

| Responsibility | First source file / symbol | Contract / check |
| --- | --- | --- |
| split wire classes, ops, section masks and response eligibility | `split/era_split_wire_protocol.h` | `contracts/era_wire_contract.md`; `contracts/era_closed_surface_contract.md` |
| storage ops, domain ids and status ids | `split/era_split_eeprom_sync.h` — `era_split_eeprom_sync_domain_t` | `contracts/era_host_peer_storage_contract.md`; `contracts/era_wire_contract.md` |

## VIA Keyboard-Channel Value IDs

| Responsibility | First source file / symbol | Contract / check |
| --- | --- | --- |
| common keyboard-channel dispatch | `system/era_common_via.c` — `era_common_via_handle_keyboard_channel_command()` | `contracts/era_invariants.md` **Identifier Compatibility** |
| common backlight / NKRO / RGB-indicator / tap-dance claims | `features/era_backlight_via.h`; `features/era_nkro_via.h`; `features/era_rgb_indicator_via.h`; `features/era_tapdance_via.c` | `manuals/era_board_adoption.md`; `contracts/era_hid_report_contract.md` where the HID report is affected |
| board-local keyboard-channel claims | `system/era_board_hooks.h` — `era_board_via_get_value()`; board-family common headers | `manuals/era_board_adoption.md`; applicable board host test |

## VIA GET_KEYBOARD_VALUE selectors

| Responsibility | First source file / symbol | Contract / check |
| --- | --- | --- |
| upstream keycodes-version selector | `quantum/via.h` — `id_keycodes_version`; `quantum/keycodes.h` — `QMK_KEYCODES_VERSION_BCD` | upstream VIA protocol; `tests/era_persistence_failures/` |
| State Sync polling selector | `system/era_state_sync.h` — `ERA_STATE_SYNC_KEYBOARD_VALUE` | `contracts/era_host_peer_storage_contract.md`; `tests/era_via_exact_ms/` |

## VIA Feature Channels

| Responsibility | First source file / symbol | Contract / check |
| --- | --- | --- |
| upstream VIA channel namespace | `quantum/via.h` | upstream VIA protocol |
| ERA system and feature-channel claims | `system/era_via_system.h`; feature `*_via.h` / `*_via.c` owners | `manuals/era_board_adoption.md`; `manuals/era_build_options.md` |

## VIA Sync Value IDs

| Responsibility | First source file / symbol | Contract / check |
| --- | --- | --- |
| EEPROM / INPUT / RGB requested-policy ids | `split/era_split_via_sync.h` | `contracts/era_authority_contract.md` **Persisted Sync Policy** |

## VIA Split Link Value IDs

| Responsibility | First source file / symbol | Contract / check |
| --- | --- | --- |
| link level, Apply and read-only link presentation | `split/era_split_via_link.h`; `split/era_split_link.c` — `era_split_link_pending_level()` | `split/era_split_link.h` **Reconciliation**; `contracts/era_route_contract.md` **Agreed Restart And Link Convergence** |

## USB Identity

| Responsibility | First source file / symbol | Contract / check |
| --- | --- | --- |
| a target's VID, PID and manufacturer string | the target's `keyboard.json` (`usb`, `manufacturer`); a split half's PID: `ERA_SPLIT_USB_IDENTITY_PID_LEFT` / `ERA_SPLIT_USB_IDENTITY_PID_RIGHT` in the board `config.h`, applied by `split/era_split_usb_identity.c` | `contracts/era_invariants.md` **Identifier Compatibility**; maker VID block and PID allocation: `the-via-eerraa/docs/adr/0004-firmware-distribution.md`; `tests/era_firmware_version/test_definitions.py` |

## USB Session

| Responsibility | First source file / symbol | Contract / check |
| --- | --- | --- |
| reducer freshness threshold | `split/era_split_authority_reducer.h` — `ERA_SPLIT_AUTHORITY_SOF_FRESH_MS` | `contracts/era_authority_contract.md` |
| frame-loss threshold / sampler ownership | `system/era_usb_session.c` — `ERA_USB_SESSION_SOF_STALE_MS` | `contracts/era_authority_contract.md` **Lighting Sleep Ownership**; `manuals/era_board_adoption.md` **Non-Split Board Baseline**; `tests/era_usb_session_policy/` |

## Diagnostic Names

| Responsibility | First source file / symbol | Contract / check |
| --- | --- | --- |
| wire/scheduler diagnostic fields | `split/diagnostics/era_split_wire_diagnostics.h`; `split/era_split_transport_scheduler_diagnostics.h` | decode and field semantics: `manuals/era_capture_reading.md` |
| pass-phase ids | `system/era_pass_phase_diagnostics.h` | `manuals/era_capture_reading.md` **The `qwin_phase` rung** |
