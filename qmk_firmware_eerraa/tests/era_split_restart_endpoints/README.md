# Production restart endpoint regression

Run `python3 tests/era_split_restart_endpoints/test_restart_endpoints.py` on the
immutable host-test mirror. The suite compiles the complete agreement owner
twice, and runs the complete production standing, responder service, responder
snapshot publisher, responder planner/sent-shadow commit, input owner and wire
codec. Endpoint clocks, transaction transport, core primitives and final act
execution are deterministic host seams; no hardware timing is claimed.

The serviced CLEAN cases use the same prepare/reset mock and verify software
receipt ordering only. Physical boot replay, checked NVM preparation and
durability are outside this fixture. Run only these cases with
`python3 tests/era_split_restart_endpoints/test_restart_endpoints.py RestartEndpoints.test_clean_actual_receipt_pipeline RestartEndpoints.test_removed_clean_snapshot_fence_is_detected`.

The scheduler's restart eligibility/apply branch and complete response commit
caller are copied from current production source by the runner. Other scheduler
families are outside this fixture; representative firmware builds cover their
integration. The ordinary authority cache and the receipt-generation callbacks
connect the real endpoint owners at the same owner/relation boundary.

Cases cover lost confirmation followed by same act/parameter retry, an IDLE
that has not yet reached the peer, a cached pre-arm IDLE answered while ARM and
IDLE are both queued, a coalesced apply whose final bytes equal the prior
snapshot, an equal-value receipt waking Core0, delayed authority cache replay,
retirement across relation rotation, snapshot-generation fence retirement, and
standalone LINK/CLEAN after unplug. Mutation negatives remove retirement,
snapshot freshness, equal-body generation refresh and authority reassertion
separately, plus the same-tuple successor fence after a completed local act
while the peer's due task is delayed. Each negative must compile, terminate
with SIGABRT and identify its expected function/assertion in stderr; a crash
or unrelated assertion is never detection evidence.

A separate role-flip case starts with a retired unconfirmed local proposal,
adopts the peer's already confirmed original deadline, then restores initiator
ownership and obtains a real standing receipt. Adoption must retire the old
wait without changing the accepted instant or erasing the live arm tuple. A
strict removal negative reproduces the prior malformed live arm.
