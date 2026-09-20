# ERA Source Map

Genre: map
Canonical for: task and source-responsibility lookup to the first implementation
owner and the contract or check that governs the change

Paths under `split/`, `storage/`, `system/`, and `features/` are relative to
`keyboards/era/common/`. This map does not describe current call flow, field
layout, or function behavior; inspect the named source for implementation facts.

| Task / responsibility | First source file / symbol | Related contract / check |
| --- | --- | --- |
| local USB authority and QMK master projection | `split/era_split_authority_reducer.c` — `is_keyboard_master_impl()`; `split/era_split_authority_reducer.h` — `era_authority_snapshot_t` | `contracts/era_authority_contract.md` |
| relation / mode planning | `split/era_split_mode_planner.h` — `era_split_mode_t`, `era_split_mode_planner_decide()` | `contracts/era_authority_contract.md`; `contracts/era_invariants.md` **Identifier Compatibility** |
| persisted EEPROM / INPUT / RGB sync policy | `split/era_split_sync_policy.c` — `ERA_SPLIT_SYNC_POLICY_DEFAULT_FLAGS` | `contracts/era_authority_contract.md` **Persisted Sync Policy** |
| split-link level and reconciliation | `split/era_split_link.c` — `era_split_link_pending_level()` | `split/era_split_link.h` **Reconciliation**; `contracts/era_route_contract.md` **Agreed Restart And Link Convergence** |
| agreed restart and act admission | `split/era_split_restart_agreement.h` | `contracts/era_route_contract.md` **Agreed Restart And Link Convergence**; `contracts/era_closed_surface_contract.md`; `tests/era_split_restart_agreement/` |
| route due/deadline and scheduler ownership | `split/era_split_transport_scheduler.c` — `era_split_transport_scheduler_transport_step()` | `contracts/era_route_contract.md`; `manuals/era_performance_gates.md` |
| wire markers, operations, section masks and eligibility | `split/era_split_wire_protocol.h` — `ERA_SPLIT_WIRE_SECTION_ELIGIBLE_*` | `contracts/era_wire_contract.md`; `contracts/era_closed_surface_contract.md` |
| compact payload admission | `split/era_split_wire_payload.c` | `contracts/era_wire_contract.md`; `contracts/era_closed_surface_contract.md` |
| RP2040 transaction backend and Core1 park/wake boundary | `split/era_split_transaction_backend_rp2040.c` — `era_split_transaction_backend_park_until()` | `contracts/era_invariants.md` **Core Lifetime And Thread Boundary**; `contracts/era_sram_residency_contract.md` |
| Core1 lifecycle / owner handoff | `split/communication_core/era_split_communication_core_lifecycle_rp2040.c` — `era_split_communication_core_request_quiesce()` | `contracts/era_invariants.md`; `contracts/era_route_contract.md`; `contracts/era_sram_residency_contract.md` |
| standing relation exchange | `split/communication_core/era_split_communication_core_standing.c` | `contracts/era_route_contract.md` **The standing exchange grant** |
| replacement storage protocol / arbitration / Apply | `split/era_host_peer_storage.c` — `era_host_peer_storage_task()` | `contracts/era_host_peer_storage_contract.md` |
| physical ERA NVM and custom EEPROM durability | `storage/era_nvm.c` — `era_nvm_replace()` | `contracts/era_host_peer_storage_contract.md`; `tests/era_nvm/`; `tests/era_nvm_qmk_driver/` |
| logical EEPROM ownership and reset/layout compatibility | `storage/era_eeprom_layout.h` — `ERA_EEPROM_RESET_KEY` | `contracts/era_host_peer_storage_contract.md` **Stored-Data Compatibility** |
| HOST-PEER matrix cache / projection | `system/era_rp2040_matrix_core.c` — `era_matrix_engine_accept_peer_snapshot()` | `contracts/era_host_peer_matrix_contract.md`; `contracts/era_route_contract.md` |
| board-neutral VIA keyboard-channel routing | `system/era_common_via.c` — `era_common_via_handle_keyboard_channel_command()` | `maps/era_identifier_map.md`; `manuals/era_board_adoption.md` |
| ERA feature lifecycle | `system/era_common_features.c` — `era_common_features_task()` | `manuals/era_feature_path.md`; feature-specific tests named there |
| local USB session / frame-loss state | `system/era_usb_session.c` — `ERA_USB_SESSION_SOF_STALE_MS` | `contracts/era_authority_contract.md` **Lighting Sleep Ownership**; `manuals/era_board_adoption.md` **Non-Split Board Baseline**; `tests/era_usb_session_policy/` |
| split board integration | `split/era_split_board.c`; `split/era_split_keyboard.c` | `manuals/era_board_adoption.md`; `contracts/era_authority_contract.md` |
| boot pre-copy halt and first Core1 launch | `system/era_boot_core1_halt.c` — `__early_init`; `split/era_split_keyboard.c` | `contracts/era_invariants.md` **Boot Safety**; `contracts/era_sram_residency_contract.md`; `manuals/era_performance_gates.md` |
| SRAM-resident load image | `keyboards/era/ld/ERA_RP2040_SRAM_RESIDENT.ld`; `system/era_sram_resident_rules.mk` | `contracts/era_sram_residency_contract.md`; `manuals/era_performance_gates.md` **Layout Checks** |
| build selector / variant ownership | `keyboards/era/era_build_options.mk`; `system/era_build_variant_rules.mk` | `manuals/era_build_options.md`; `tests/era_build_variant_rules/` |
| diagnostic field production | `split/diagnostics/era_split_wire_diagnostics.h`; `system/era_pass_phase_diagnostics.h` | `manuals/era_capture_reading.md`; `manuals/era_performance_gates.md` |
| QMK matrix fork seam | `quantum/matrix.c` | `contracts/era_invariants.md` **QMK Fork Hygiene**; `manuals/era_qmk_fork_ledger.md`; `manuals/era_performance_gates.md` **Source Gate** |

For a source-unit split/merge, wrapper, or structural refactor, read
`contracts/era_invariants.md` **Source Structure** before changing boundaries.
Per-file inventories and call graphs are intentionally source-owned rather than
copied into this map.
