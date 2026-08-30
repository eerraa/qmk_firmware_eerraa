품질 기준으로 **세션 29개**다. 한 채팅에 문서 두 개를 넣지 않는다. 커밋 C 뒤에는 **열린 채팅을 전부 닫고** 04부터 새로 연다.

매 채팅: **새 Agent** → 피커 설정 → `@`로 핸드오버 파일 3개 → **공통** 붙이고 → **그 세션 본문**만 붙인다. 에이전트가 “다음 파일도 할까요” 하면 **하지 말고** 창을 닫는다.

`@` 대상

- `D:\Engineering\qmk_firmware_eerraa_handover\DOCS_SYSTEM_PLAN.md`
- `D:\Engineering\qmk_firmware_eerraa_handover\DOCS_SYSTEM_DENSITY_SPECS.md`
- `D:\Engineering\qmk_firmware_eerraa_handover\DOCS_SYSTEM.md` (29번만 필수, 나머지는 있으면 좋음)

---

## 공통 (01–29 모든 채팅 맨 위)

```text
Standing rules for this chat only. You will receive a second block that names the single allowed unit of work. Do that unit and STOP. Do not start the next unit, even if it looks small.

Read AGENTS.md then keyboards/era/common/docs/era_active_index.md. Do NOT run graphify or any graphify CLI. Locate with git grep / rg. Treat leftover "MANDATORY: graphify query" text as retired policy to delete later, not as an order.

git branch must be work/era-nvm. If git status --short is not empty with files you did not create in THIS chat for THIS unit: STOP and report. Do not commit, stash, or move anyone else's dirty files.

No git add -A or -a. No --no-verify. No push. No ~/.claude ~/.grok ~/.codex. No user-level Orca hooks. Stage by path only. Checker reads the worktree: never partial-stage a markdown file. A checker "paragraph" is a blank-line block; a whole table is one paragraph — a function claim and its `file.c` token must stay in that same block.

Korean commit messages. Title one line; body = what was removed and why, plus any MUST-SURVIVE / --homeless notes the unit requires. Last line of the body: the plan section the second block names. English in ERA agent docs. Do not edit keyboards/era C sources, board readmes, or upstream docs/.

If this unit's commit already exists on HEAD, do not redo it: print git log --oneline ca44e73562..HEAD and STOP.
```

---

## 01 — 진입만 (편집 금지)

모델: **High** · 커밋 없음

```text
Unit: Phase 0 measurement only. Zero file edits, zero commits.

Read DOCS_SYSTEM_PLAN.md §0 §1 §3.

Run §3.1 exactly. Re-measure §1 numbers that matter (status, stash, worktree list, log -3, era_doc_refs.py, agent-doc line count). Classify any dirty files: NVM content vs graphify regen vs other.

Output: pass/fail for each §3.1 check; dirty-file classification; whether the operator may paste prompt 02. If status is not empty: FAIL, and list what the NVM agent still owes. Do not rm .era-artifacts yet.
```

비어 있지 않으면 여기서 끝. NVM이 커밋한 뒤에 01을 다시 붙여라.

---

## 02 — 커밋 B (훅 이전)

모델: **High**

```text
Unit: Phase 1 commit B only. Prerequisite: prompt 01 passed on a clean tree. Last body line: DOCS_SYSTEM_PLAN §4

Read DOCS_SYSTEM_PLAN.md §2 §3.2 §3.3 §4 부록 C.

Do §3.2 artifacts rm, then §3.3 baseline (era_doc_refs.py 0; record starting line count). Then §4 in full: hooks/pre-commit mode 100755; era_commit_check.py = commit arm only from git show ca44e73562:hooks/era_pretooluse.py; test_era_commit_check.py as specified; git rm paths in §4.2; edits in §4.3. git config core.hooksPath hooks (local). Run §4.4 tests. One commit B.

Stop after printing test output, git log -1, git status.
```

---

## 03 — 커밋 C (graphify 삭제) → 모든 창 닫기

모델: **High** · 끝나면 이 창과 02 창을 닫는다

```text
Unit: Phase 2 commit C only. Prerequisite: commit B on HEAD. Last body line: DOCS_SYSTEM_PLAN §5

Read DOCS_SYSTEM_PLAN.md §5.

If git rev-parse --show-toplevel is not this firmware repo: STOP. Execute §5.1–5.4. Do not archive graphify-out. Product name must not remain in the tree: §5.5 rg -n -i --hidden --glob '!.git/**' 'graphify|graphifyy' . must be 0. Measurements named in §5.4 go in the commit BODY, not in surviving docs. Navigation section text is verbatim from §5.4.

One commit C. Print §5.5 results. Then STOP and tell the operator to close every agent session before prompt 04.
```

---

## 04 — 커밋 D (헤더 + 검사기, 한 커밋)

모델: **High** · **새 채팅** (03을 재사용하지 말 것)

```text
Unit: Phase 3 commit D only. Prerequisite: B and C on HEAD; rg graphify 0. Last body line: DOCS_SYSTEM_PLAN §6

Read DOCS_SYSTEM_PLAN.md §6. If this chat still wants to run graphify: STOP (stale session).

One commit because pre-commit runs the checker on the worktree: checker change and 21 headers must land together.

Do §6.1 (settings hoist, HEADERS, FORBIDDEN_HEADERS, FOREIGN_REPOS). Strip Status/Read when via the §6.2 scratch script; do not commit the script. §6.3: add the identifier_map matrix row; verify all 21 Read-when lines against the matrix and include the comparison table in the chat report. §6.4 AGENTS.md sentences. era_doc_refs.py 0. Commit D. Stop.
```

---

## 05 — 커밋 H (규격 저장소만)

모델: **High**

```text
Unit: commit H only, in D:\Engineering\eerraa-agent-docs, BEFORE any commit E. Last body line: DOCS_SYSTEM_PLAN §7.1

Read DOCS_SYSTEM_PLAN.md §7.1 and 부록 D. Korean only in that repo.

If the repo already exists with tag v1: do not re-init; report and STOP.

Else git init, write README.md, AGENT_DOCS_CONVENTION.md (부록 D TOC, full convention text), CHANGELOG.md (one v1 entry). Write only facts you re-verify with ls/commands this chat (re-check sister-repo layout from plan §1.5; do not invent H7S/app paths). Commit + tag v1. Do not edit the QMK tree. Stop.
```

---

## 06 — 커밋 E (함정 절)

모델: **High**

```text
Unit: commit E only. Prerequisite: D on this repo, v1 tag on eerraa-agent-docs. Last body line: DOCS_SYSTEM_PLAN §7

Read DOCS_SYSTEM_PLAN.md §7.2 §7.3 and 부록 B.

Insert AGENTS.md boundary paragraph, convention v1 citation with the three declared deviations, and ## What Costs Time If You Do Not Know It. Every path, heading, and number in 부록 B must be confirmed by a command this chat (use the example commands under 부록 B; add more as needed). If a command disagrees with the draft, STOP — do not insert the stale line. CLAUDE.md WSL rsync/mtime trap, confirmed against .claude/tools/era-sync.sh. era_doc_refs.py 0 (FOREIGN_REPOS must let app-prefixed paths skip). One commit E. Stop. Do not rewrite contract bodies.
```

---

이하는 **문서 하나 = 채팅 하나 = 커밋 하나**. 4번 Extra High 묶음보다 이쪽이 품질이 좋다.

밀도 세션은 아래 **Phase 5 공통**을 세션 본문 **앞에** 붙인다.

### Phase 5 공통 (07–27)

```text
Phase 5 standing rules. Read DOCS_SYSTEM_PLAN.md §8 and the appendix-A section the unit names. Open the live file and re-map section boundaries; spec line numbers may have drifted after NVM.

§8.1 write rules in full: keep rules/REFUSED/owner decisions/device facts/fixed data/routed headings; tables for enumerations; pointers for mechanism that lives in a named source file; delete dated run reports, session narrative, meta, how-to-read, discovery stories, justification essays whose conclusion is already a rule. Prose refusals → three-line REFUSED; WHY stands alone. Dates: keep (2026-08-xx) on measurements/owner decisions that ground a rule; delete run reports. Do not change meaning. If two rules conflict or docs disagree with source: STOP and report. Function claim + file token in the same checker paragraph. Routed headings character-for-character; if you rename one, update every router in this same commit. Apply this file's §8.3 row in this commit, not later.

Before commit: python keyboards/era/common/tools/era_doc_refs.py (0 findings); --homeless on the staged diff (promote or "의도적 은퇴" in the body); git diff --check. Title: 문서: <filename> 밀도 재구성 (<n>→<m>줄). Body: MUST-SURVIVE items count/confirmed, --homeless handling, before/after lines, corrections. Last line: DOCS_SYSTEM_PLAN §8 F<n>

Do not edit a second document "for consistency" except routers required by a heading change in THIS file, or a pointer the spec says to add. If you need a sentence to move to another file, leave a pointer here and STOP after this commit so the owner file's own session can receive it.
```

---

## 07 — F1 capture_reading

모델: **Extra High**

```text
Unit: F1 only. keyboards/era/common/docs/manuals/era_capture_reading.md
Spec: DOCS_SYSTEM_DENSITY_SPECS.md A1. Target ~640. Residue = field decode tables.
§8.3: L577 pointer to gates "actual NVM width" is wrong — fix pointer or drop the expectation; do not invent a measurement.
Do not turn this into a tutorial. Stop after one commit.
```

---

## 08 — F2 wire_contract

모델: **Extra High**

```text
Unit: F2 only. keyboards/era/common/docs/contracts/era_wire_contract.md
Spec: A2. Target ~420. Keep REFUSED 7 verbatim. Merge push/rsp marker tables as spec says.
§8.3: L905 DUAL-HOST push "six" → seven (ERA_SPLIT_WIRE_SECTION_ELIGIBLE_DUAL_HOST_PUSH, RESTART_ARM). L573 era_split_wire_authority_equal() file → split/era_split_wire_payload.c
Stop after one commit.
```

---

## 09 — F3 route_contract

모델: **Extra High**

```text
Unit: F3 only. keyboards/era/common/docs/contracts/era_route_contract.md
Spec: A3. Target ~310. Period/constant tables. Keep REFUSED 6.
§8.3: L251-252 failure-streak induction is NOT in the storage contract — point at split/era_host_peer_storage.h (confirm live line). 1000 ms quiet: if still duplicated with storage, pointer here or stop and report; do not keep two full statements.
Stop after one commit.
```

---

## 10 — F4 performance_gates

모델: **High**

```text
Unit: F4 only. keyboards/era/common/docs/manuals/era_performance_gates.md
Spec: A5 (gates half). Target ~270. Confirm NVM hunks already landed; re-map "Focused host-test set".
§8.3: unify "Layout checks" vs "Layout Gate" with sram residency — pick the name the live source/gate scripts use; if the other file still disagrees, pointer here and leave the rename for the sram session only if the heading is routed from here. Do not edit sram in this chat.
Stop after one commit.
```

---

## 11 — F5 authority_contract

모델: **Extra High**

```text
Unit: F5 only. keyboards/era/common/docs/contracts/era_authority_contract.md
Spec: A4-b. Target ~170. Convert the 8 prose refusals to three-line REFUSED (list in A4-b MUST-SURVIVE). Keep routed headings exact: Lighting Sleep Ownership, Matrix Ready, Relation Hold, Initiator Authority, plus internal bold names listed in A4-b.
§8.3: circular ownership of in-place upgrade with era_source_map.md — make THIS file the owner (or the spec's chosen owner); do not edit source_map in this chat except if you must add a one-line pointer after you chose the owner. Prefer: authority owns the rule; source_map session will drop the duplicate.
Checker traps in A4-b: when splitting L12-73, keep file tokens on the rows that name is_keyboard_master / usb_disconnect / usb_vbus_state / split_pre_init.
Stop after one commit.
```

---

## 12 — F6 board_adoption

모델: **High**

```text
Unit: F6 only. keyboards/era/common/docs/manuals/era_board_adoption.md
Spec: A7 (adoption half). Target ~176. Convert the one prose refusal if listed.
§8.3: L213-214 "The VIA surface and its gate" — live heading is in era_build_options.md (confirm). Point; do not invent a heading here.
Keep Copy-To-RAM Policy heading exact (AGENTS.md / index route).
Stop after one commit.
```

---

## 13 — F7 build_options

모델: **High**

```text
Unit: F7 only. keyboards/era/common/docs/manuals/era_build_options.md
Spec: A5 (build_options half). Target ~260. Keep REFUSED 3 verbatim (42 lines).
§8.3: L198 — era_board_hooks.c is not the sole reader of ERA_STORAGE_QUIET_DEFER_MS; storage/era_nvm.c also reads it. Leave a command, not a uniqueness claim.
Stop after one commit.
```

---

## 14 — F8 hid_report_contract

모델: **Extra High**

```text
Unit: F8 only. keyboards/era/common/docs/contracts/era_hid_report_contract.md
Spec: A7 (hid half). Target ~103. Translate the Korean body to English AND compress in this same commit. Keep REFUSED 5.
§8.3: fix citations — era_split_wire_diagnostics.c:780→ live line (was :798 at audit); features/era_kkuk.c:59 was a blank line, re-grep; "42 files vs 41" → current count from git diff --name-only c93ef27143 HEAD -- tmk_core quantum platforms drivers builddefs (plan said 32 at audit; re-count now).
Stop after one commit.
```

---

## 15 — F9 invariants

모델: **Extra High**

```text
Unit: F9 only. keyboards/era/common/docs/contracts/era_invariants.md
Spec: A4-a. Target ~120. This is always-on. One invariant = one row (invariant · enforcement · location). Convert the 6 prose refusals listed in A4-a MUST-SURVIVE.
§8.3: "three slots / 150 ms" is stale — tree has ..._RESPONDER_GENERAL_RESULT_SLOTS 4U and ..._SOURCE_RESULT_SLOTS 2U. Confirm with git grep this chat.
Checker traps: era_nvm_replace() row must cite storage/era_nvm.c; keyboard_task/matrix_task/era_common_features_task rows need a .c in the same paragraph.
Do not drop Stop Conditions. Headings may rename except as spec says. Stop after one commit.
```

---

## 16 — F10 host_peer_storage_contract

모델: **Extra High**

```text
Unit: F10 only. keyboards/era/common/docs/contracts/era_host_peer_storage_contract.md
Spec: A6-a. Target ~310. Re-map every heading against the live file after NVM landing. H7S envelope bytes (spec lines 239-248) character-for-character.
Receive the SRAM REFUSED block into Inactive-Bank Maintenance And Rotation (A6-a). Do not edit the sram file in this chat — leave it inconsistent until F11, which will replace the block with a pointer.
Keep routed headings exact: Arbitration; Diagnostics (move pending-fact composition under Diagnostics as spec); Why An EEPROM Clean Is An Agreed Restart; What The Lane Costs A Typist; Recency Layer; Relation Admission; Capacity And Publication; Source Revision And Identity.
§8.3: 1000 ms quiet — this file or route, not both; keep owner here if this is the durable rule, pointer from route already handled in F3.
Stop after one commit.
```

---

## 17 — F11 sram_residency_contract

모델: **Extra High**

```text
Unit: F11 only. keyboards/era/common/docs/contracts/era_sram_residency_contract.md
Spec: A6-b. Target ~170. Re-map after NVM. Replace the REFUSED that F10 now owns with a pointer to storage **Inactive-Bank Maintenance And Rotation**. Confirm F10 actually contains that block; if not, STOP (F10 incomplete).
§8.3: Layout Gate vs Layout Checks — match the name F4 chose; do not fork a third name.
Stop after one commit.
```

---

## 18 — F12 closed_surface_contract

모델: **Extra High**

```text
Unit: F12 only. keyboards/era/common/docs/contracts/era_closed_surface_contract.md
Spec: A7 (closed-surface half). Target ~111. Storage Lane Boundary is the unique owner — do not delete or merge it into storage. Convert the 3 optional permanent-closure refusals only if the spec marks them as convert-not-delete.
Stop after one commit.
```

---

## 19 — source_map

모델: **High**

```text
Unit: maps/era_source_map.md only. Spec: A8. Already tables. Keep REFUSED 3. Keep rows other audits named as owned here. Drop copies (absolute-value restatement; in-place upgrade — authority now owns it; replace with a pointer to authority). Do not "improve" the map into prose. Stop after one commit. Last line: DOCS_SYSTEM_PLAN §8 F13 source_map
```

---

## 20 — identifier_map

모델: **High**

```text
Unit: maps/era_identifier_map.md only. Spec: A8. Already tables. Keep table rows; delete RGB/INPUT arm explanation sentences that duplicate authority (A4-b). Phase 3 should already have the matrix row in the index; do not duplicate Read when. Stop after one commit. Last line: DOCS_SYSTEM_PLAN §8 F13 identifier_map
```

---

## 21 — walkthrough

모델: **High**

```text
Unit: maps/era_walkthrough.md only. Spec: A8. Existence reason = path narrative. Compress only: each step = file name + one line. NO table conversion. Keep routed mentions: (era_route_contract.md, Due/Deadline Model); What The Lane Costs A Typist; Relation Hold; Fixed Baselines — those target headings must still exist in their owner files; if missing, STOP and report. Stop after one commit. Last line: DOCS_SYSTEM_PLAN §8 F13 walkthrough
```

---

## 22 — build_and_flash

모델: **High**

```text
Unit: manuals/era_build_and_flash.md only. Spec: A8. Procedure: keep commands and expected output; delete background narrative. Keep the .era-artifacts mention. Stop after one commit. Last line: DOCS_SYSTEM_PLAN §8 F13 build_and_flash
```

---

## 23 — overview

모델: **High**

```text
Unit: contracts/era_overview.md only. Spec: A8. Always-on. Glossary already a table. Cut model prose per paragraph without dropping a defined term. Do not merge this into the index. Stop after one commit. Last line: DOCS_SYSTEM_PLAN §8 F13 overview
```

---

## 24 — active_index

모델: **Extra High**

```text
Unit: keyboards/era/common/docs/era_active_index.md only. Spec: A8: no density rewrite beyond Phase 3's added row. Do not bulk-shorten the matrix. Allowed: remove Status/Read when if somehow still present; fix rows that point at headings you know moved, only after confirming the live heading. If a Change/Locate/Verify cell would change meaning, STOP. Stop after one commit if anything changed; if nothing should change, print that and do not commit. Last line if committing: DOCS_SYSTEM_PLAN §8 F13 index
```

---

## 25 — qmk_fork_ledger

모델: **High**

```text
Unit: manuals/era_qmk_fork_ledger.md only. Spec: A8. Keep the table and the derivation paragraph (lines 10-20 at audit — rule grounds). Do not invent ledger rows. Stop after one commit. Last line: DOCS_SYSTEM_PLAN §8 F13 ledger
```

---

## 26 — feature_path

모델: **High**

```text
Unit: manuals/era_feature_path.md only. Spec: A8. Order is the value — compress, do not reorder. Stop after one commit. Last line: DOCS_SYSTEM_PLAN §8 F13 feature_path
```

---

## 27 — host_peer_matrix_contract

모델: **High**

```text
Unit: contracts/era_host_peer_matrix_contract.md only. Spec: A8. Already short. General §8.1 only. Do not fold it into invariants. Stop after one commit. Last line: DOCS_SYSTEM_PLAN §8 F13 matrix
```

---

## 28 — Phase 7 VIA 스파이크 (구현은 조건부)

모델: **Medium** · 30분 상한

```text
Unit: Phase 7 spike only. Read DOCS_SYSTEM_PLAN.md §9. Do not start Phase 8.

30-minute cap. Find whether JSON menus[].label sets map deterministically to build features using era-build --show-options <board>:via as oracle and era_build_options.mk / rules.mk / keyboard.json as static candidates. Boards: TOMAK79H, brick65, divine. All three must match or the mapping is not adopted.

If deterministic: implement check_via_menus in era_doc_refs.py as specified and commit I (Korean message, last line DOCS_SYSTEM_PLAN §9).
If not: implement nothing. Report the spike: what mapped, what did not, why. Stop.
Do not "almost" ship a heuristic.
```

---

## 29 — Phase 8 검증·반증·보고

모델: **High**

```text
Unit: Phase 8 only. No more doc rewrites unless a verification command proves a broken citation you just introduced — then one fix commit, then resume verification.

Read DOCS_SYSTEM_PLAN.md §10 and DOCS_SYSTEM.md §6.

Run every §10.1 command; paste outputs. Confirm temp-repo whitespace commit is refused (test 3 from commit B). Time the six §10.2 questions from a cold reading of the new entry points (do not use this campaign's memory as the answer). Do not rebuild release/clean-repo or main. Do not push.

Report §10.4 in full. git status empty. List follow-ups §10.3. Stop.
```

---

## 순서 한 줄

`01 측정 → (dirty면 중단) → 02 B → 03 C → 창 전부 닫기 → 04 D → 05 H → 06 E → 07…18 계약/매뉴얼 Extra High·High → 19…27 나머지 한 편씩 → 28 스파이크 → 29 보고`

막힌 세션은 **같은 번호를 다시** 붙인다. 다음 번호로 건너뛰지 마라. 16은 17보다 먼저다(REFUSED가 storage로 옮겨 간 뒤 sram이 포인터만 남긴다).