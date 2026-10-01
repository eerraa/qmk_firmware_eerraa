# ERA Authority Contract

Genre: contract
Canonical for: local session authority, relation hold and initiator ownership,
lighting-sleep ownership, persisted sync-policy storage, matrix admission,
revalidation, and the authority/scheduler boundary.

## Local Authority Facts

One reducer owns local relation authority. First anchors:
`split/era_split_authority_reducer.c`, `system/era_usb_session.c`, and
`split/era_split_scheduler_session.c`. There is no boot master latch or
cold-boot USB master probe.

A valid local session exposes exactly one accepted state: HOST-open or no-host.
Unknown/invalid is neither. Compile-time capability and link-search facts are
not relation authority and do not become AUTHORITY-section fields.

| Close to accepted no-host | Requirement |
| --- | --- |
| sustained configured suspend | remain HOST through `ERA_SPLIT_AUTHORITY_SUSPEND_HOST_GRACE_MS` 500, then close |
| unconfigured suspend | normalize to the non-configured USB state; it is not a valid HOST suspend |
| residual loss | close when USB is not configured or SOF age reaches `ERA_SPLIT_AUTHORITY_SOF_FRESH_MS` 10 |

SOF freshness is bounded evidence. Mature stale ages MUST saturate rather than
wrap back to fresh across the raw timer. While the remote-wake ISR owns SOFRD,
polling does not also read it; returning ownership to polling after a long gap
must re-earn loss evidence. Production policy coverage:
`tests/era_usb_session_policy`.

Split role is derived from this reducer, not raw VBUS. MCU_RP split builds keep
`SPLIT_USB_DETECT`, forbid `USB_VBUS_PIN`, and retain the ERA
`is_keyboard_master_impl()` override. Otherwise QMK's weak path can disconnect
USB or derive role from VBUS. Handedness must come from the physical split-hand
source available before `split_pre_init()`.

Link-speed reconciliation MUST NOT fabricate USB authority or a synthetic HOST
hold. VIA action consumption is not a runtime or durability acknowledgement.
First anchor: `split/era_split_link.h`.

Remote wake is requested from a composed-matrix edge while the bus is suspended
and the host granted wake. It stays outside the ordinary action pipeline because
sustained suspend can close host-open and QMK can then stop forwarding
`process_record()`. First anchor: `split/era_split_keyboard.c`.

## Lighting Sleep Ownership

RGB and backlight sleep each have a switch and an input-idle timeout, the
same controls on every board and class. The RGB switch is persisted in QMK
keymap configuration; the timeouts (1..65535 seconds, zero reads as the
600-second default) and the backlight switch are the ERA block's lighting sleep
record (`features/era_lighting_sleep.c`). Default/CLEAN means both enabled at
600 seconds. Local sleep reasons are USB suspend, frame loss, and the
input-idle timeout; a switch that is off disables every reason for its output.
Sleep is an output gate: it never rewrites the lighting configuration, so a
save during sleep stores the user's lighting. Split frame loss uses
`ERA_USB_SESSION_SOF_STALE_MS` 300. Board-level surface details are owned by
`manuals/era_board_adoption.md`.

| Relation role | Lighting-sleep owner |
| --- | --- |
| `LOCAL_NO_LINK`, either DUAL-HOST role, HOST-PEER HOST | that half's local session facts |
| HOST-PEER PEER | the relation's HOST over the wire |

The non-owner does not decide the render gate. On demotion to HOST-PEER PEER,
discard the local decision and resolve lit until the new HOST publication is
accepted. On promotion to local ownership, discard the wire decision and resolve
from local facts. Owner adoption precedes receive/resolve, and a non-wire owner
must refuse a wire publication rather than cache it for a future role.

Sleep outranks presentation. STATUS or lock-indicator state may remain cached
while asleep, but does not clear the sleep gate. A HOST-PEER HOST publishes the
resolved decision, never a transient raw gate. DUAL-HOST does not transfer this
ownership. First anchors: `split/era_split_keyboard.c` and
`split/era_split_transport_scheduler.c`.

## Persisted Sync Policy

There is no DUAL-HOST parent switch and no per-relation request. Each family has
one relation-independent requested bit. Persisting a bit grants no route,
payload, or execution by itself. First anchor: `split/era_split_sync_policy.c`.

| Family | Flag | Scope | Default |
| --- | ---: | --- | --- |
| EEPROM | `0x01` | replacement-storage participation in every admitted relation; initiator off means no select, responder off means no data | ON |
| INPUT | `0x02` | DUAL-HOST INPUT-class only; disabled sender publishes neutral state and disable clears peer-derived INPUT | ON |
| RGB | `0x04` | DUAL-HOST RGB/config/visual only; HOST-PEER RGB/visual stays ungated | ON |

The split link level is not a sync-policy bit. The pair must run one link level,
while sync-policy bits are independent owner preferences; combining them would
give a pair-critical value two incompatible authorities. Link config starts at
`ERA_EEPROM_LINK_CONFIG_OFFSET` 200, occupies 4 bytes, and is sync-excluded.

The protected configuration region is
`ERA_EEPROM_PROTECTED_CONFIG_OFFSET` 176 through 255
(`ERA_EEPROM_PROTECTED_CONFIG_SIZE` 80). Nothing in it crosses as replacement
storage.

| Offset | Size | Content |
| ---: | ---: | --- |
| 176 | 24 | local sync-policy block |
| 200 | 4 | link config |
| 204 | 16 | reset guard |
| 220 | 32 | storage-recency baseline |
| 252 | 4 | protected reserve |

The local sync-policy block is a storage compatibility contract:

| Relative offset | Requirement |
| --- | --- |
| `+0` | `ERA_SPLIT_SYNC_POLICY_STORAGE_SIGNATURE` `0x504E5953` |
| `+4` | requested-flags generation, never zero |
| `+6` | EEPROM-policy generation only |
| `+8` | `ERA_SPLIT_SYNC_POLICY_STORAGE_VERSION` 5, the only accepted version |
| `+9` | requested flag mask `0x07`, all three default on |
| `+10..+23` | seven 16-bit LE divergence counters; ordinary preference persistence does not rewrite them |

There is no in-place upgrade of an earlier valid block. Invalid signature, mask,
zero generation, or version rewrites the whole 24-byte block to the current
format; ordinary reset-to-defaults writes only the preference prefix so counters
survive. Accepting an older version and partially rewriting it is forbidden
because foreign counter bytes could remain under a valid signature. A future
converter must rewrite the whole block, including counters, under a new version.
A changed default therefore rides the version byte.

Domain inventory, recency arbitration, EEPROM CLEAN, and durable replacement are
owned by `era_host_peer_storage_contract.md`. Layout constants are first anchored
in `storage/era_eeprom_layout.h` and `split/era_split_sync_storage.h`.

## Replacement Storage Authority

Storage content arbitration is owned by `era_host_peer_storage_contract.md`.
Authority here is only who may initiate: HOST-PEER PEER; DUAL-HOST Left.
Responders answer only in the admitted slot, and relation rotation rotates
storage authority with the rest of the relation.

A fixed HOST-source/PEER-target winner is forbidden. DUAL-HOST can hold durable
edits on either half, so fixed direction can overwrite newer content during
divergent-history recovery. Reopening requires a product rule that admits edits
from only one DUAL-HOST half.

## Matrix Ready

`matrix_ready` is a no-host readiness fact, not generic matrix health.

| State | Requirement |
| --- | --- |
| true | local session valid, accepted no-host, and the local HOST-PEER matrix publisher has a publishable snapshot |
| false | HOST-open, DUAL-HOST, invalid authority, or unknown authority |

Wire/session encoding must refuse `matrix_ready` without accepted no-host. First
anchors: `system/era_matrix_engine.h`, `split/era_split_scheduler_session.c`,
and `split/era_split_wire_payload.c`.

## HOST-PEER Matrix Admission

HOST-side matrix admission requires a HOST-PEER HOST relation, valid local and
peer session facts, peer accepted no-host, peer `matrix_ready`, and no
replacement-storage route exclusivity. The role term comes from the relation;
it MUST NOT be re-derived from current host-open. A suspended HOST can keep its
relation while HID emission is separately closed, so conflating the two facts
would tear down remote-wake projection during sustained suspend.

First anchors: `split/era_split_scheduler_session.c` and
`split/scheduler/era_split_transport_scheduler_responder.c`. Response-slot shape
and section eligibility remain owned by `era_route_contract.md`,
`era_wire_contract.md`, and `era_closed_surface_contract.md`.

## Relation Hold

Both halves reporting no host means the fact that assigned HOST and PEER is
temporarily absent; it does not assign opposite roles. Hold the current relation
until new USB authority or staleness resolves it.

| Local / peer accepted authority | Next relation while live |
| --- | --- |
| host / host | DUAL-HOST |
| host / no-host | HOST-PEER HOST |
| no-host / host | HOST-PEER PEER |
| no-host / no-host | hold current relation |
| either stale or unknown | reset toward `LOCAL_NO_LINK` / peer-unknown recovery |

The held relation keeps its granted standing exchange. Liveness periods and stale
threshold are owned by `era_route_contract.md`. Downstream users project from
this one authority decision rather than re-deriving roles.

## Initiator Authority

| Relation state | Initiator | Responder | Matrix route |
| --- | --- | --- | --- |
| peer unknown | Left only | Right | `SESSION_STATUS` only |
| HOST-PEER | PEER | HOST | PEER source-push only |
| DUAL-HOST | Left | Right | forbidden |
| right standalone / no peer | none | optional responder | none |

The initiator is a projection of the relation, not an independent left/right
policy. Wire availability is an additional scheduler gate, not a second
authority derivation. First anchor: `split/era_split_mode_planner.c`.

## Revalidation Authority

- `SESSION_STATUS` is discovery, bootstrap, and recovery only. Live-relation
  authority and policy generation ride the relation's AUTHORITY section.
- Relation change and staleness may require a new `SESSION_STATUS`; ordinary
  live traffic does not create a second session-fact channel.
- A pending status request is cleared only by successful `SESSION_STATUS`, not
  by a compact ACK from another route.
- Fact staleness or invalid authority payload falls back to peer-unknown
  bootstrap/backoff.
- A local HOST-open -> no-host edge forces peer-session stale recovery only when
  the peer is hosted. An unambiguously no-host peer remains the relation's
  initiator; forcing it stale would forget valid role authority and tear down a
  relation whose roles did not change.

First anchors: `split/era_split_mode_planner.c`,
`split/era_split_transport_scheduler.c`, and
`split/scheduler/era_split_transport_scheduler_routes.c`. AUTHORITY payload
shape and direction remain owned by `era_wire_contract.md`.

## Authority Scheduler Boundary

| Fact | Scheduler rule |
| --- | --- |
| SOF movement | not dirty state by itself |
| SOF loss or recovery | deadline/due work |
| reduced authority change | authority-dirty planning input |
| raw USB/SOF facts | local only; never pushed as peer authority |
| sustained suspend | cannot collapse accepted HOST before the 500 ms grace |

The authority sample is the intentional poll. Its period remains
`ERA_SPLIT_AUTHORITY_POLL_PERIOD_MS` = `ERA_SPLIT_AUTHORITY_SOF_FRESH_MS` 10;
the stopped-counter observation therefore resolves in the bounded 10-20 ms
window. First timing anchor:
`split/scheduler/era_split_transport_scheduler_internal.h`.

Event-driving that sample is forbidden while SOF loss has no sole loss
interrupt. A counter can stop without producing an edge, so an event-only design
can preserve stale HOST authority. Reopening requires a USB-stack loss event that
is itself authoritative for the stopped counter.

Split builds keep QMK's suspended-startup loop disabled so a no-local-USB PEER
continues `keyboard_task()`. First selector: `split/era_split_qmk_rules.mk`.
