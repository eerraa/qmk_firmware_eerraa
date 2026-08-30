# ERA 문서 체계 리뉴얼 — 수행계획

작성 2026-08-30. 기준 HEAD `ca44e73562` (`work/era-nvm`). 목표·판단 근거는
`DOCS_SYSTEM.md`, **절차는 이 문서**, 문서별 재구성 명세는
`DOCS_SYSTEM_DENSITY_SPECS.md`(부록 A). 실행 세션은 이 문서를 위에서 아래로
따른다.

**이 문서의 숫자는 2026-08-30 측정값이다.** 파일 수·줄 수·dirty 목록은 실행
시 다시 재고, 다르면 트리가 맞다. 단 §0의 소유자 결정은 재측정 대상이 아니다.

---

## 0. 소유자 결정 (2026-08-30, 변경 불가)

| # | 사항 | 결정 |
| --- | --- | --- |
| 1 | 시점·브랜치 | NVM 작업이 끝난 뒤, 또는 NVM 상태를 커밋해 개발을 중단시킨 뒤, `work/era-nvm` 위에서 **깨끗한 트리**로 시작 |
| 2 | 문서 헤더 | `Genre:`·`Canonical for:`만 유지. `Status:`·`Read when:` 제거. 검사기는 두 필드의 **부재**를 검사 |
| 3 | 공통 규격 위치 | 새 소형 git 저장소 `D:\Engineering\eerraa-agent-docs` |
| 4 | 장문 문서 | **내용 재구성에 동의.** 소스로 확인 가능하거나 소유자에게 물으면 되는 서술은 제거하고 표·데이터 맵으로 |
| 5 | 커밋 메시지 | 한국어 |
| 6 | 훅 해제 | 단독 커밋 완료 `ca44e73562` (Claude/Codex/Grok 프로젝트 PreToolUse 등록 제거) |
| 7 | VIA JSON | `keyboards/era/**/keymaps/via/*-VIA.json`(26개)이 **세트로 유지되는 규칙 = 정본**. `.era-artifacts/` 내용은 삭제 가능 |
| 8 | Orca 사용자 훅 | 그대로 둔다. `~/.claude/settings.json`·`~/.grok/hooks/orca-status.json`은 건드리지 않는다 |
| 9 | commit 검사 경로 | 버전관리된 `hooks/pre-commit` + 클론당 `git config core.hooksPath hooks` (호스트별 훅 없음) |
| 10 | 보드 `readme.md` 23개 | 규격 밖. `qmk lint`가 요구하는 upstream 표면 (`lib/python/qmk/cli/lint.py:313-314`) |
| 11 | `era_doc_refs.py` 이식 | 공유 패키지 없음. 파일 머리의 설정 블록 하나로 "복사 이식" |

7번은 "저장소 안 JSON 세트가 정본이므로 대조 검사의 기준으로 쓸 수 있다"로
읽었다. 다르게 의도했다면 Phase 7을 통째로 생략한다.

---

## 1. 실측 기준선 (2026-08-30, 실행 시 재측정)

### 1.1 브랜치·트리

| 항목 | 값 |
| --- | --- |
| 작업 브랜치 | `work/era-nvm` = `work/era-dev`(7110e4590c) + NVM 4커밋 + 훅 해제 1커밋(`ca44e73562`) |
| 미커밋 (NVM 에이전트) | 16파일: `graphify-out/` 4, 문서 4(`era_host_peer_storage_contract.md` +61, `era_sram_residency_contract.md` +9, `era_performance_gates.md` +14, `era_source_map.md` +6), 소스 6, 테스트 2 |
| 링크드 worktree | `D:/Engineering/qmk_firmware_eerraa_nvm2` = `work/era-nvm2` @ 7110e4590c (옛 canon을 그대로 가짐) |
| `release/clean-repo` | 트리 ≠ HEAD 트리 (이미 무효; 리뉴얼 뒤 재구성 대상) |
| `.era-artifacts/` | 미추적, 664파일 163 MiB, `.gitignore:15` |

### 1.2 문서 세트

| 집합 | 파일 | 줄 |
| --- | --- | --- |
| 에이전트 문서 (index 1 + contracts 10 + maps 3 + manuals 7) | 21 | 9,487 (92,499 단어) |
| `docs/user/` | 3 | 971 |
| 보드 `readme.md` | 23 | 783 |
| 합계 | 47 | 11,241 |

헤더: 21/21이 4필드 보유. `Status:` 값 = `active` 20 + `active; in force.` 1.
`era_hid_report_contract.md`는 헤더 라벨만 영어이고 **본문 전체가 한국어**
(문서 자신이 "번역 필요"라고 적음). REFUSED 블록 21개(6편) — `era_invariants.md`·
`era_authority_contract.md`·`era_board_adoption.md`·`era_closed_surface_contract.md`
는 거절이 산문 형태로만 있다. `2026-` 날짜 문장 33개. 상위:
`era_capture_reading.md` 1,397 · `era_wire_contract.md` 989 ·
`era_route_contract.md` 983 · storage 617(NVM이 1,993→578로 이미 축소).

### 1.3 Graphify 발자국

| 항목 | 값 |
| --- | --- |
| tracked | `graphify-out/{graph.json 9.27 MB, GRAPH_REPORT.md, manifest.json, .graphify_labels.json}` = 9.5 MB |
| 로컬 | `graphify-out/` 3,986파일 424 MiB (날짜별 스냅샷 37개 + `cache/` + `cost.json`) |
| 설정 | `.graphifyignore` 914줄, `.gitignore:134-148` |
| live 참조 | `hooks/test_era_pretooluse.py` 16 · `hooks/era_pretooluse.py` 14 · `AGENTS.md` 14 · `CLAUDE.md` 11 · `.gitignore` 8 · `.graphifyignore` 2 · `.claude/rules/era-docs.md` 1 · **문서 세트 0** |
| 자매 | H7S는 아직 활성(SessionStart 훅 + post-commit/post-checkout + tracked 20파일); 앱은 사고 경고 2건만; 54lm20은 0 |

### 1.4 훅 현황 (ca44e73562 이후)

| 위치 | 상태 |
| --- | --- |
| `.claude/settings.json` | `permissions.deny`만 (reset --hard / checkout -- / restore 거부) |
| `.codex/`, `.grok/hooks/` | 등록 파일 삭제됨. `.grok/README.md`, `.claude/hooks/*.py` shim, `hooks/era_pretooluse.py`, `hooks/test_era_pretooluse.py`는 남아 있음(Phase 1이 정리) |
| `hooks/test_era_pretooluse.py` | `FAIL: Claude one PreToolUse group`로 실패 (예상됨) |
| commit 검사 | **어디서도 자동 실행되지 않음** — Phase 1 전까지 수동: `python keyboards/era/common/tools/era_doc_refs.py` + `git diff --check` |
| `.git/hooks`, `core.hooksPath` | 없음 / 미설정 |
| 사용자 레벨 | Orca 훅 (결정 8: 유지) |

### 1.5 자매 저장소 (규격 추출의 독자)

| 저장소 | 문서 | 검사기 | 헤더 |
| --- | --- | --- | --- |
| `the-via-eerraa` | `docs/` 7편 1,996줄 (+ADR 3) | `tests/docs-contract.test.ts` 512줄, `bun run test:p1`, CI 미실행 | `Genre`+`Canonical for`; `Status`는 ADR만; `Read when` **부재 검사** |
| `eerraa-qmk-h7s-fw` | `docs/` 7편 1,110줄 (flat) | `tools/era_doc_refs.py` 507줄 (path·symbol·comment·header·index·retired·table·menu·version), 수동 | `Genre`+`Canonical for` |
| `eerraa-54lm20-fw` | `docs/` 13편 1,426줄 | 없음 | 없음 (`docs/_archive/` 보유) |

앱 `docs/MAP.md` §9가 이미 "네 저장소 공통 규약"을 서술한다(2필드, 장르 5종,
장르 디렉터리는 ~20편 초과 시). 이 저장소가 그 원본이고 유일하게 4필드를
유지하는 곳이다. H7S 검사기는 `FOREIGN_REPOS` 접두사(`the-via-eerraa/` 등)로
타 저장소 경로를 통과시킨다 — 이 저장소도 같은 형태를 채택한다(§6.1).

---

## 2. 실행 세션 규칙

1. 시작 시 `AGENTS.md` → `era_active_index.md`는 정상적으로 읽는다. **`graphify`
   명령은 실행하지 않는다.** 훅이나 문서가 내는 "MANDATORY: graphify query" 안내는
   제거 대상인 옛 동작이다. 위치 확인은 `git grep -n` / `rg`로 한다.
2. **같은 worktree를 다른 에이전트가 쓸 수 있다.** 스테이징은 경로 지정
   `git add <path>`만. `-a`/`-A` 금지. `git rm`은 즉시 스테이징되므로 커밋 전
   `git status --short`의 첫 열을 확인한다.
3. 커밋은 concern별 1개. 메시지는 한국어, 제목 한 줄 + 본문에 "무엇을 왜
   지웠는가". 삭제 커밋은 지운 내용이 들고 있던 판단을 본문에 보존한다.
4. 문서를 지우는 커밋마다 커밋 **전에**
   `python keyboards/era/common/tools/era_doc_refs.py --homeless`(스테이지된 diff
   기준)를 돌리고, 보고된 토큰은 승격(다른 문서로 이동)하거나 "의도적 은퇴"로
   커밋 본문에 적는다.
5. Phase 1이 끝나면 pre-commit이 검사기를 자동 실행한다. `--no-verify` 금지.
6. `push` 금지. `release/clean-repo`·`main` 갱신은 소유자 몫(Phase 8).
7. 문서에 적는 사실은 명령으로 확인한 것만. 문서 산출물(ERA 문서)은 영어,
   보고·커밋·규격 저장소는 한국어.
8. 검사기는 **index가 아니라 worktree**를 읽는다(`era_doc_refs.py`의
   `read_text()`). 부분 스테이징된 문서는 커밋하지 않는다.
9. 검사기의 "문단"은 빈 줄로 나뉜 블록이고 **표 전체가 문단 하나**다. 산문을
   표로 바꿀 때 `foo()`와 그 파일 토큰이 같은 블록(같은 표)에 남아야 한다.

---

## 3. Phase 0 — 진입 조건과 겹침 처리

### 3.1 진입 조건 (모두 충족해야 Phase 1 시작)

```bash
git branch --show-current            # work/era-nvm
git status --short                   # 비어 있어야 함
git stash list | wc -l               # 0
git worktree list                    # nvm2 worktree는 있어도 됨
git log --oneline -3                 # NVM 완료 또는 "상태 보존" 커밋이 최상단
```

- `git status --short`가 비어 있지 않으면 **중단**하고 소유자에게 보고한다.
  NVM 에이전트가 아직 작업 중이라는 뜻이다. 결정 1에 따라 NVM 에이전트가
  끝내거나, 자기 상태를 커밋하고 세션을 멈춘 뒤에만 진행한다. 이 문서의 어느
  단계도 남의 dirty 파일을 커밋·stash·이동하지 않는다.
- 겹침 분류(실행 시 다시 확인): NVM의 `graphify-out/` diff는 순수 재생성
  산출물 → Phase 2의 삭제가 대체한다. NVM의 문서 4편 diff는 실질 내용 → 먼저
  착륙해야 한다. 소스·테스트는 리뉴얼과 무관.

### 3.2 `.era-artifacts/` 정리 (결정 7)

```bash
git ls-files .era-artifacts | wc -l          # 0 (미추적 확인)
ls .era-artifacts | head                     # 내용 눈으로 확인
rm -rf .era-artifacts/* .era-artifacts/.discarded
```

지우면 다음 소스 변경의 T1/T2 before-build 기준이 없어진다. 이번 캠페인은
소스를 건드리지 않으므로 무관하고, 다음 소스 캠페인은 깨끗한 커밋에서 before를
다시 빌드한다(`era_performance_gates.md` Refactor Self-Check).

### 3.3 기준선 저장

```bash
python keyboards/era/common/tools/era_doc_refs.py    # 0건이어야 함; 아니면 먼저 보고
cat keyboards/era/common/docs/era_active_index.md keyboards/era/common/docs/contracts/*.md \
    keyboards/era/common/docs/maps/*.md keyboards/era/common/docs/manuals/*.md | wc -l   # 시작 줄 수
```

---

## 4. Phase 1 — commit 검사를 pre-commit 경로로 이전 (커밋 B)

목적: `git commit`마다 세 검사(whitespace·fork ledger·doc refs)가 **호스트와
무관하게** 자동 실행되고, 그 외 어떤 도구 호출에도 프로젝트 훅이 없다.

### 4.1 새 파일

**`hooks/pre-commit`** (실행 비트 필수 — Windows에서도 index 모드를 올린다):

```sh
#!/bin/sh
# ERA pre-commit: the static checks a commit owes (whitespace, QMK fork
# ledger, agent-document references). Activated once per clone with
#   git config core.hooksPath hooks
# Never bypass with --no-verify; fix the finding.
exec python "$(git rev-parse --show-toplevel)/hooks/era_commit_check.py"
```

```bash
git add hooks/pre-commit && git update-index --chmod=+x hooks/pre-commit
git ls-files -s hooks/pre-commit        # 100755 로 시작해야 함
```

**`hooks/era_commit_check.py`** — `git show ca44e73562:hooks/era_pretooluse.py`에서
commit arm만 옮긴다.

- 옮기는 것(본문 그대로): 상수 `REPO, LEDGER, DOC_REFS, DOC_LAYER,
  REF_SOURCE_SUFFIX, REF_CORE_DIRS, FORK_DIRS, NON_ERA_GATES`; 함수 `git,
  grep_files, name_only, mentions, ledger_records, check_whitespace,
  check_fork_ledger, doc_layer_armed, check_doc_layer`.
- 버리는 것: `parse_event, shell_read_paths, is_bash_search, classify, is_exempt,
  claude_payload_for_graphify, graphify_deny_reason, relay_graphify, run_read,
  run_search, dispatch`, `STAGING_RE`/`COMMIT_ALL_RE`와 "stages and commits in one
  call" 거절(pre-commit은 index가 완성된 뒤 실행되므로 불필요), JSON stdin 파싱,
  `hookSpecificOutput` JSON 출력.
- 새 `main()`:

```python
def main():
    changed = name_only("diff", "--cached", "--name-only")
    if not changed:
        return 0                      # --allow-empty 등
    problems, notes = [], []
    check_whitespace(False, problems, notes)   # staged → problems, unstaged → notes
    if any(p.startswith(FORK_DIRS) for p in changed) or any(
            p.endswith("era_qmk_fork_ledger.md") for p in changed):
        check_fork_ledger(problems, notes)
    if doc_layer_armed(changed):
        check_doc_layer(problems, notes)
    for note in notes:
        print("note: " + note)
    if problems:
        sys.stderr.write("\n\n".join(problems) + "\n")
        return 1
    return 0
```

`check_whitespace`의 `commits_all` 분기는 제거해도 된다(`git commit -a`도
pre-commit 시점에는 index에 반영돼 있다). docstring은 "one pre-commit check for
every host and the owner's terminal" 취지로 다시 쓰고 graphify·PreToolUse 언급을
남기지 않는다.

**`hooks/test_era_commit_check.py`** — `hooks/test_era_pretooluse.py`를 대체.
검사 항목(각각 `ok`/`FAIL` 출력, 마지막 `all tests passed`):

1. 배선: `git ls-files -s hooks/pre-commit`가 `100755`; 파일이
   `era_commit_check.py`를 부른다; `.claude/settings.json`에 `hooks` 키 없음;
   `.codex/`, `.grok/`, `.claude/hooks/`, `hooks/era_pretooluse.py` 부재(우회
   경로 부재 검사).
2. 단위: `gate.git`를 monkeypatch해 `check_whitespace`가 staged 오류를
   `problems`에, unstaged 오류를 `notes`에 넣는지; `doc_layer_armed`가
   `AGENTS.md`·`.claude/rules/x.md`·`keyboards/era/a.c`·`quantum/b.h`에 True,
   `readme.md`·`lib/x`에 False; `ledger_records`의 세 매칭 형태.
3. 종단: 임시 디렉터리에 `git init`, `hooks/pre-commit`·`hooks/era_commit_check.py`
   복사, `git config core.hooksPath hooks`, 끝에 공백이 있는 파일을 add·commit →
   **종료 코드 ≠ 0, stderr에 `whitespace`**; 고친 뒤 commit → 0. (QMK 트리 클론
   없이, git이 훅을 실제로 부르는지·python을 찾는지·종료 코드를 존중하는지를
   증명한다. 임시 저장소에는 ledger·docs가 없으므로 그 두 검사는 무장되지 않는다.)
4. 실제 트리: `python hooks/era_commit_check.py`를 현재 index로 실행 → 종료 코드가
   `git diff --cached --check`의 결과와 일치.

실행: `python hooks/test_era_commit_check.py` — 보고에 명령과 출력을 그대로 싣는다.

### 4.2 삭제

```bash
git rm hooks/era_pretooluse.py hooks/test_era_pretooluse.py
git rm -r .claude/hooks .grok .codex           # .grok/README.md 포함
```

`.grok/README.md`의 `[compat.claude] hooks = false` 안내는 Orca 훅 때문에 사용자
레벨 `~/.grok/config.toml`에 이미 이유와 함께 있으므로 저장소에 남길 것이 없다.

### 4.3 문서·어댑터 편집 (같은 커밋)

| 파일 | 위치 | 편집 |
| --- | --- | --- |
| `CLAUDE.md` | `### Read routing` 둘째 불릿("The shared gate (`hooks/era_pretooluse.py`) runs …") | 교체: "Commit-time checks are a git pre-commit hook (`hooks/pre-commit` → `hooks/era_commit_check.py`), armed once per clone by `git config core.hooksPath hooks` (Machine setup). It runs `era_doc_refs.py` on any commit touching the agent-document layer or an ERA-commented `.c/.h/.mk`, the fork-ledger check on any commit touching `quantum/ platforms/ drivers/` or the ledger, and `git diff --cached --check` always; a deletion of doc lines prints the `--homeless` reminder. Claude has no project hooks; `.claude/settings.json` carries only `permissions.deny`." |
| `CLAUDE.md` | `### Machine setup` | 불릿 추가: "`git config core.hooksPath hooks` — once per clone, in the edit tree. The WSL build tree never commits and needs none." |
| `AGENTS.md` | `## Agent Layer Ownership` 어댑터 목록 | `hooks/era_pretooluse.py`·`hooks/test_era_pretooluse.py` 불릿 → `hooks/pre-commit` + `hooks/era_commit_check.py`("the one commit-time check, host-neutral: git runs it, so Claude, Codex, Grok and the owner's terminal share one path") + `hooks/test_era_commit_check.py`("its conformance test: wiring, absence of any host PreToolUse path, and an end-to-end refused commit"). `.grok/`·`.codex/` 불릿 삭제. "Each host MUST enter that gate through its own native project adapter…" 문단과 그 REFUSED 블록(importer) 삭제 — 호스트별 훅 배선이 사라져 결정 자체가 소멸했다; 이 판단은 커밋 본문에 적는다 |
| `AGENTS.md` | `### Verification` | 문장 추가: "The commit-time checks run from `hooks/pre-commit`; a commit made with `--no-verify` is not a verified commit." |
| `.claude/rules/era-docs.md` | "The commit gate runs the same script…" | "The pre-commit hook (`hooks/pre-commit`) runs the same script with no argument automatically, which is the locatability check." |
| `.claude/rules/era-qmk-fork.md` | "the commit gate" 2곳 | "the pre-commit check" |
| `keyboards/era/common/docs/maps/era_source_map.md` | `:336` 부근 "which the commit gate already…" | "which the pre-commit check already…" (`git grep -n 'commit gate' keyboards/era/common/docs`로 전부 찾는다) |

### 4.4 활성화·확인

```bash
git config core.hooksPath hooks            # 로컬 설정, 커밋되지 않음
git config core.hooksPath                  # hooks
python hooks/test_era_commit_check.py      # all tests passed
python keyboards/era/common/tools/era_doc_refs.py   # 0건
git add <위 파일들 경로 지정>
git commit   # pre-commit이 실제로 도는지 출력에서 확인
```

커밋 B 제목 예: `훅: commit 정적 검사를 pre-commit으로 이전, 호스트별 PreToolUse 게이트 은퇴`.
본문에 담을 판단: (1) 강제할 것은 트리의 성질이지 에이전트의 탐색 순서가
아니다; (2) Claude 훅의 입력 필터(`"if": "Bash(git commit *)"`)로도 가능하지만
Codex·Grok·터미널 커밋을 덮지 못한다; (3) importer REFUSED 블록은 배선이 사라져
소멸.

---

## 5. Phase 2 — Graphify 산출물·설정·서술 삭제 (커밋 C)

### 5.1 저장소 root 확인 후 삭제

```bash
git rev-parse --show-toplevel              # …/qmk_firmware_eerraa 여야 함 (다른 값이면 중단)
git rm -r graphify-out .graphifyignore
rm -rf graphify-out                        # 로컬 424 MiB (스냅샷·cache 포함). archive 만들지 않음
ls /d/Engineering/qmk_firmware_eerraa_nvm2/graphify-out   # tracked 사본 3~4개뿐; 그 브랜치가 이 커밋을 받을 때 사라진다
```

### 5.2 `.gitignore`

`:134-148`의 Graphify 블록(주석 3문단 + `/graphify-out/*` + `!…` 4줄) 삭제.
`.era-artifacts` 블록(`:12-15`)은 유지.

### 5.3 서술 편집

| 파일 | 위치 | 편집 |
| --- | --- | --- |
| `AGENTS.md` `:21` | Agent Layer Ownership 첫 불릿 | "…startup read policy, graphify usage policy." → "…startup read policy." |
| `AGENTS.md` `:62` | "What changed behavior was mechanism — the graphify-first hooks, …" | "…mechanism — the commit-time checks, the static-capacity asserts, the source/ELF gates, and the device-evidence rule." |
| `AGENTS.md` `:127` | Commits 불릿 "…and the graphify regeneration that follows." | "…index or policy update." |
| `AGENTS.md` `:295-318` | `## Graphify` 절 전체 | 삭제. 자리에 `## Navigation`(§5.4) |
| `CLAUDE.md` `:33-47` | `### graphify` 절 | 삭제 |
| `CLAUDE.md` `:76-84` | Machine setup의 "Install graphify before the first session…" 문단 + pip 불릿 | 삭제. 남는 것: hooksPath 불릿(Phase 1) + autocrlf/longpaths 불릿 |
| `CLAUDE.md` `:47` | "Include the graphify-first rule in every subagent prompt…" | (절 삭제에 포함) |
| `.claude/rules/era-docs.md` `:13-15` | "After doc changes: `graphify update .`…" | 삭제 |

### 5.4 `AGENTS.md` `## Navigation` (새 절, 옛 Graphify 자리)

```markdown
## Navigation

Structure questions are answered by the router (`era_active_index.md`) and a
source search (`git grep -n`, `rg`); there is no derived index to consult or
regenerate, and none may be reintroduced as an obligation.

> **REFUSED:** a mandatory knowledge graph, a search-before-read hook, or per-session generated context as the navigation layer.
> **WHY:** the last one cost 424 MiB of local state and 9.5 MB tracked, a process on every shell call, and ~0.7 s per query against 0.1 s for a search, while its natural-language answers spread across hundreds of nodes; the router and a search answer the same questions.
> **REOPENS:** a navigation tool whose per-call cost is below a shell search and whose answers are checked by something other than the agent reading them.
```

제품명을 쓰지 않는다 — §10의 `rg 'graphify'` 0건 검증을 통과해야 한다. 이름과
측정값 전체는 커밋 C 본문에 남긴다: 자연어 architecture 질의 2회가 231/745
노드로 퍼짐, CLI ≈0.7 s vs `rg` 0.106 s, 로컬 3,986파일 424 MiB, 앱 저장소에
75,000줄 오커밋 사고, 정확한 symbol explain의 제한적 이득은 router+검색이 대체,
모든 Bash 호출에 붙은 어댑터 비용.

### 5.5 확인

```bash
rg -n -i --hidden --glob '!.git/**' 'graphify|graphifyy' .    # 0건
git ls-files graphify-out .graphifyignore | wc -l              # 0
test ! -d graphify-out && echo gone
python keyboards/era/common/tools/era_doc_refs.py              # 0건
```

커밋 C 제목 예: `graphify: 파생 그래프·설정·graphify-first 서술 삭제`.
**이 커밋 뒤 열려 있는 모든 에이전트 세션을 재시작한다** — 옛 `AGENTS.md`를
읽은 세션은 `graphify update`를 다시 만들 수 있다.

---

## 6. Phase 3 — 헤더 축소 + 검사기 (커밋 D)

한 커밋이어야 한다: pre-commit이 검사기를 worktree에 대해 돌리므로 검사기와
21편의 헤더가 함께 바뀌어야 통과한다.

### 6.1 검사기 `keyboards/era/common/tools/era_doc_refs.py`

1. **설정 블록 hoist**(결정 11) — 파일 머리에
   `# --- repository-specific, and the only part another repository has to change ---`
   블록을 만들고 아래 상수를 모은다: `REPO`(parents 깊이), `DOC_ROOT`, `INDEX`,
   `ENTRY`, `BASES`, `GENRES`, `HEADERS`, `FORBIDDEN_HEADERS`(신설),
   `FOREIGN_REPOS`(신설), `SRC_SUFFIX`, `SKIP_PREFIX`, `CLAIM_ROOTS`,
   `CORE_COMMENT_ROOTS`, `CONST_NAME` 접두사, `TOKEN` 알파벳, `scan_set()`의
   `.claude/rules/` 경로. 동작은 바뀌지 않는다.
2. `HEADERS = ("Genre:", "Canonical for:")`,
   `FORBIDDEN_HEADERS = ("Status:", "Read when:")`. `check_headers`: 첫 14줄에
   `HEADERS` 각각 존재, `FORBIDDEN_HEADERS` 각각 **부재**(있으면
   ``FAIL: {rel}: `Status:` header is retired — the index owns when a document is read``),
   `Genre:` 값은 `GENRES`, `Canonical for:` 값이 비면 FAIL.
3. `FOREIGN_REPOS = ("the-via-eerraa/", "eerraa-qmk-h7s-fw/", "eerraa-54lm20-fw/", "eerraa-agent-docs/")`
   — `resolve()` 첫머리에서 `token.startswith(FOREIGN_REPOS)` → `"skip"`. Phase 4의
   함정 절이 앱 경로를 저장소 이름 접두사로 적기 위한 것이다.
4. docstring의 "headers" 항목을 새 규약으로 고친다.

### 6.2 21편 헤더 편집

스크래치패드에 `trim_headers.py`로 저장해 저장소 root에서 실행한다:

```python
import pathlib, re
root = pathlib.Path("keyboards/era/common/docs")
KEY = re.compile(r"^[A-Z][A-Za-z ]+:")
for p in sorted(root.rglob("*.md")):
    lines = p.read_text(encoding="utf-8").split("\n")
    out, i, in_header = [], 0, True
    while i < len(lines):
        line = lines[i]
        if in_header and i < 20 and line.startswith(("Status:", "Read when:")):
            i += 1
            while i < len(lines) and lines[i].strip() and not KEY.match(lines[i]) and not lines[i].startswith("#"):
                i += 1            # 접힌 연속행까지 제거
            continue
        if line.startswith("#") and i > 0:
            in_header = False
        out.append(line)
        i += 1
    p.write_text("\n".join(out), encoding="utf-8")
```

```bash
git diff --stat -- keyboards/era/common/docs     # 21 files, 삭제만
python keyboards/era/common/tools/era_doc_refs.py # 0건 (새 HEADERS 기준)
```

### 6.3 `Read when` 흡수 확인 (매트릭스)

제거된 `Read when` 21개를 `era_active_index.md`의 Task Read Matrix와 대조한다.
2026-08-30 판독으로 매트릭스가 덮지 않는 것 1개:

| 문서의 `Read when` | 처리 |
| --- | --- |
| `maps/era_identifier_map.md`: "naming new code, diagnostics, routes, payloads or docs, or resolving…" | 행 추가 — Task area "naming a new identifier, value id, diagnostic field or document, or resolving what a name means" · Change — · Locate `maps/era_identifier_map.md` · Verify — |
| `contracts/era_overview.md`: "first, before the invariants…" | Always-On 목록이 이미 1번 — 추가 없음 |
| 나머지 19 | 해당 행 존재. 실행 시 하나씩 대조해 표를 보고에 싣는다 |

### 6.4 `AGENTS.md` 문장

`### Genre decides what kind of sentence a document may hold` 첫 문장 "Every
agent document declares a `Genre:` beside its `Status:`, one of:" → "Every agent
document opens with two lines, `Genre:` and `Canonical for:`; the genre is one
of:". 같은 절의 "A missing or unknown `Genre:` is refused by `era_doc_refs.py`"
→ "A missing or unknown `Genre:`, an empty `Canonical for:`, or a retired
`Status:`/`Read when:` line is refused by `era_doc_refs.py`".

커밋 D 제목 예: `문서: 헤더를 Genre·Canonical for 두 줄로 축소, 검사기가 은퇴 필드의 부재를 검사`.
본문: `Status` 20/21 동일값 + 변종 1, `Read when`은 매트릭스와 같은 사실의 중복,
앱 MAP.md §9 규약과 정합, `FOREIGN_REPOS`·설정 블록은 규격 이식용.

---

## 7. Phase 4 — 규격 저장소 (커밋 H, 별도 저장소) + canon 재편 (커밋 E)

H가 E보다 먼저다: E가 규격 v1을 인용한다.

### 7.1 `D:\Engineering\eerraa-agent-docs` (커밋 H)

```bash
git init /d/Engineering/eerraa-agent-docs
# 파일: README.md (한 문단: 무엇이고 누가 읽는가), AGENT_DOCS_CONVENTION.md (부록 D 목차), CHANGELOG.md (v1 항목 하나)
git -C /d/Engineering/eerraa-agent-docs add -A && git -C /d/Engineering/eerraa-agent-docs commit -m "규격 v1: 네 저장소 공통 에이전트 문서 규약"
git -C /d/Engineering/eerraa-agent-docs tag v1
```

규격 문서는 한국어(자매 저장소 세 곳의 언어). 내용은 부록 D. **이 저장소에서
참이라고 확인한 것만 적는다** — 앱·H7S의 구현 위치는 §1.5의 실측을 그대로
옮기되 실행 시 `ls`로 다시 확인한다.

### 7.2 `AGENTS.md` 재편 (커밋 E)

| 위치 | 편집 |
| --- | --- |
| `## Document Management Rules` 둘째 불릿 뒤 | 경계 문단 추가: "**Everything else is not this set.** `docs/` (270 files) is upstream QMK's; `keyboards/era/**/readme.md` are the boards' QMK-facing readmes, required by `qmk lint` (`lib/python/qmk/cli/lint.py`), and carry neither a header nor an index entry; `docs/user/` is written for keyboard owners." |
| 같은 절 끝 | 규약 인용: "This set follows agent-docs convention v1 (`eerraa-agent-docs`), deviating in three declared ways: one directory per genre (21 documents, above the convention's ~20 threshold), a separate entry router (`era_active_index.md`), and English prose." |
| `## Working On This Firmware` 뒤, `## Document Management Rules` 앞 | 새 절 `## What Costs Time If You Do Not Know It` — 부록 B 초안. **각 항목의 경로·섹션명·수치를 명령으로 확인한 뒤** 넣는다 |
| `### Evidence And Retirement` | 변경 없음(`--homeless` 규칙은 `.claude/rules/era-docs.md`와 함정 절이 가리킨다) |

### 7.3 `CLAUDE.md` WSL 절 (커밋 E)

불릿 추가(어댑터 함정): "`era-sync` copies with `rsync -rlt`, which preserves
mtimes, so an incremental build can report a changed source as up to date. When a
before/after comparison is the evidence, `touch` the changed files or delete
`.build/obj_*` between the two states." (`.claude/tools/era-sync.sh:112`로
확인; 2026-08-24 관찰.)

### 7.4 확인

```bash
python keyboards/era/common/tools/era_doc_refs.py     # 0건 (FOREIGN_REPOS 덕에 앱 경로 통과)
```

커밋 E 제목 예: `canon: 함정 절 신설, 문서 경계·규약 v1 인용`.

---

## 8. Phase 5 — 밀도 재구성 (문서당 커밋 1개, F1…Fn)

결정 4의 실행. **부록 A(`DOCS_SYSTEM_DENSITY_SPECS.md`)가 문서별 명세**다:
섹션별 판정(K/T/P/D), 살아남을 것, 추정 줄 수, MUST-SURVIVE 체크리스트, 라우팅된
헤딩, 검사기 함정, 중복 소유처. 실행 세션은 그 명세를 문서마다 다시 검증한 뒤
따른다(명세는 2026-08-30 트리 기준; NVM 착륙 뒤 storage·sram·gates·source_map은
줄 번호가 밀린다).

### 8.1 쓰기 규칙 (모든 문서 공통)

1. **살아남는 문장**: 규칙·제약·불변식(MUST/never/only/refuses), `> **REFUSED:**`
   블록(세 줄 그대로), 소유자 결정(한 줄), 규칙의 근거가 되는 기기 사실(한 줄 +
   무엇을 근거 짓는지), 고정 데이터(마커·비트·오프셋·주기·예산·id·소유 파일·절차
   단계), 다른 문서·소스가 이름으로 라우팅하는 헤딩.
2. **표로 바꾸는 것**: 산문으로 쓰인 열거(섹션×관계×적격성, 필드×의미, 상태×전이,
   게이트×판정, 선택자×기본값).
3. **포인터로 바꾸는 것**: 이름 댄 소스 파일이 보여 주는 기전 서술 → 한 줄
   "X lives in `file`" 또는 표의 한 행.
4. **지우는 것**: 날짜 달린 실행 보고, 세션 서사, 집계, 문서 자신에 대한 메타
   서술, 읽는 법 조언, 결함을 찾아낸 이야기, 결론이 이미 규칙/REFUSED인 정당화
   에세이. 소유자가 물으면 답할 수 있는 것.
5. **산문 거절 → 세 줄 REFUSED**: 부록 A가 문서별로 후보를 나열한다
   (`era_authority_contract.md` 8, `era_invariants.md` 6, `era_board_adoption.md` 1,
   `era_closed_surface_contract.md` 3 선택). WHY는 홀로 서야 한다.
6. **날짜**: 규칙의 근거인 측정·소유자 결정은 `(2026-08-xx)` 형태로 남기고, 실행
   보고는 지운다. 부록 A가 33개 각각의 판정을 준다. "이력은 `git log`가
   추적한다"는 dev 브랜치에서만 참이므로, 규칙의 **이유**는 문서에 남기고
   **삭제 경위**만 커밋 본문에 둔다.
7. **의미를 바꾸지 않는다.** 압축이 규범 문장의 뜻을 바꾸는지 확신이 없으면 한 줄
   규칙으로 남긴다(제약은 남기고 산문을 자르는 쪽으로 기운다). 규칙 둘이 서로
   충돌하는 것을 발견하면 **멈추고 보고**한다 — 문서와 소스가 어긋나면 편집이
   아니라 발견이다(`AGENTS.md` Scope).
8. **함수 주장은 파일과 같은 블록에**(§2-9). 표로 쪼갤 때 부록 A "CHECKER TRAPS"의
   줄들이 첫 희생자다.
9. **라우팅된 헤딩은 글자 그대로** 유지(부록 A "ROUTED HEADINGS"). 헤딩을 바꾸면
   라우팅하는 쪽도 같은 커밋에서 바꾼다.
10. **감사가 찾은 오류는 재구성 중 고친다**(§8.3) — 별도 커밋이 아니라 그 문서의
    커밋 본문에 "정정" 항목으로 적는다.
11. 문서 하나 = 커밋 하나. 순서: 큰 것부터. 커밋 본문에 MUST-SURVIVE 체크리스트의
    확인 결과(항목 수/확인 수), `--homeless` 결과와 처리, 줄 수 전후.

### 8.2 순서와 목표 (부록 A 추정; 통과 기준이 아니라 지침)

| 순서 | 문서 | 현재 | 추정 | 비고 |
| --- | --- | --- | --- | --- |
| F1 | `manuals/era_capture_reading.md` | 1,397 | ≈640 | 잔여는 필드 디코드 표 — 디코드 매뉴얼의 본질 |
| F2 | `contracts/era_wire_contract.md` | 989 | ≈420 | push/rsp 마커 표를 하나로; REFUSED 7 |
| F3 | `contracts/era_route_contract.md` | 983 | ≈310 | 주기·상수 표; REFUSED 6 |
| F4 | `manuals/era_performance_gates.md` | 584 | ≈270 | NVM diff 착륙 후; "Layout Gate"/"Layout Checks" 이름 통일 |
| F5 | `contracts/era_authority_contract.md` | 542 | ≈170 | 산문 거절 8 → REFUSED |
| F6 | `manuals/era_board_adoption.md` | 527 | ≈176 | |
| F7 | `manuals/era_build_options.md` | 484 | ≈260 | REFUSED 3 그대로(42줄) |
| F8 | `contracts/era_hid_report_contract.md` | 404 | ≈103 | **한국어 → 영어 번역 + 압축 한 번에**; REFUSED 5 |
| F9 | `contracts/era_invariants.md` | 395 | ≈120 | always-on 문서: 불변식 = 한 행(불변식 · 강제 수단 · 위치) |
| F10 | `contracts/era_host_peer_storage_contract.md` | 610 | ≈310 | NVM 착륙 뒤 줄 번호 재확인; H7S 봉투 글자 그대로; SRAM의 REFUSED가 rotation 규칙 곁으로 옮겨 옴 |
| F11 | `contracts/era_sram_residency_contract.md` | 383 | ≈170 | REFUSED 1개를 storage 계약으로 이동(3중복 해소), 포인터만 남김 |
| F12 | `contracts/era_closed_surface_contract.md` | 273 | ≈111 | **Storage Lane Boundary**는 유일 소유 — 삭제 금지 |
| F13 | 나머지 9편 (`maps/era_source_map.md` 450 · `maps/era_identifier_map.md` 286 · `maps/era_walkthrough.md` 260 · `manuals/era_build_and_flash.md` 243 · `contracts/era_overview.md` 187 · `era_active_index.md` 136 · `manuals/era_qmk_fork_ledger.md` 127 · `manuals/era_feature_path.md` 121 · `contracts/era_host_peer_matrix_contract.md` 103) | 1,913 | §8.1 일반 규칙 | 맵 둘은 이미 표. `era_walkthrough.md`는 경로별 서사가 존재 이유이므로 압축만, 표 전환 없음 |

합계 추정: 감사한 12편 7,571 → ≈3,060, 나머지 9편 1,913 → ≈1,400 (일반 규칙),
즉 9,487 → **≈4,400–4,700 (약 −52 %)**. 통과 기준은 줄 수가 아니라 §8.4다.

### 8.3 감사가 찾은 정정 항목 (해당 문서 커밋에서 고친다)

| 문서 | 줄 | 사실 |
| --- | --- | --- |
| wire | 905 | DUAL-HOST push "six" → **seven** (`ERA_SPLIT_WIRE_SECTION_ELIGIBLE_DUAL_HOST_PUSH` 7 멤버, RESTART_ARM 추가 이후) |
| wire | 573 | `era_split_wire_authority_equal()` 파일 없음 → `split/era_split_wire_payload.c` |
| route | 251-252 | failure-streak 유도가 storage contract에 있다고 함 — 실제는 `split/era_host_peer_storage.h:148-149` |
| build_options | 198 | `system/era_board_hooks.c`가 `ERA_STORAGE_QUIET_DEFER_MS`의 유일 독자라는 주장 — `storage/era_nvm.c`도 읽음; 명령만 남긴다 |
| gates ↔ sram | 62 / 215,380 | "Layout checks" vs "Layout Gate" 이름 드리프트 — 하나로 |
| capture_reading | 577 | gates 문서에 "실제 NVM 폭" 측정이 있다고 기대 — 없음; 포인터 수정 또는 측정 착륙 |
| invariants | 373 | "three slots / 150 ms" — 트리는 `..._RESPONDER_GENERAL_RESULT_SLOTS 4U`, `..._SOURCE_RESULT_SLOTS 2U` |
| authority ↔ source_map | 250-251 / 427-431 | in-place upgrade 결정의 **순환 소유** — 한 곳으로 |
| board_adoption | 213-214 | "**The VIA surface and its gate** below" — 실제 헤딩은 `era_build_options.md:278` |
| hid_report | 37, 261, 167/371/399 | 인용 `era_split_wire_diagnostics.c:780`→`:798`; `features/era_kkuk.c:59`는 빈 줄; "42 files vs 41"는 현재 32(`git diff --name-only c93ef27143 HEAD -- tmk_core quantum platforms drivers builddefs`) |
| storage ↔ route | 165 / 248 | 1000 ms quiet interval 이중 서술 — 한쪽을 포인터로 |

### 8.4 문서별 통과 기준

```bash
python keyboards/era/common/tools/era_doc_refs.py           # 0건
python keyboards/era/common/tools/era_doc_refs.py --homeless # 커밋 전, 스테이지 기준; 결과 처리 후 커밋
git diff --check
```

- 부록 A MUST-SURVIVE 항목 전부가 새 본문에 있다(REFUSED 세 줄, 상수 값, 소유자
  결정, 기기 사실, 헤드라인 규칙, 절차).
- 라우팅된 헤딩이 글자 그대로 있다.
- `--homeless` 보고 토큰은 승격됐거나 커밋 본문에 "의도적 은퇴"로 적혔다.
- 줄 수가 줄었다.

---

## 9. Phase 7 (선택) — VIA JSON ↔ 빌드 기능 대조 검사 (커밋 I)

결정 7 해석 위에서만. TOMAK79H가 `MOUSE`·`NKRO`·`LINK` 없이 배포된 사고의
재발을 **이 저장소 안에서** 막는다(앱의 `FEATURE_COVERAGE`는 앱 쪽 사본).

1. **스파이크(30분 상한)**: JSON `menus[].label` → 하위 `label` 집합(2026-08-30
   TOMAK79H-L: Lighting{RGB Matrix, Badge Lighting}, FEATURE{SOCD, KKUK, DEBOUNCE,
   TAPPING, MOUSE, NKRO}, TAPDANCE{TD0..TD7}, SYSTEM{SYNC, LINK, BOOT, EEPROM})과
   보드의 기대 집합을 잇는 결정적 사상을 찾는다. 오라클은 기존 계기
   `era-build --show-options <board>:via`(선택자·값·출처 출력). 정적 유도는
   `keyboards/era/era_build_options.mk`의 `?=` 기본값 + 보드 `rules.mk`/`post_rules.mk`
   의 `=` 덮어쓰기 + `keyboard.json`의 `features` — 후보 사상: SOCD/KKUK/DEBOUNCE/
   TAPPING/MOUSE ↔ 각 `ERA_*_ENABLE`, NKRO ↔ `features.nkro`, TAPDANCE ↔
   `TAP_DANCE_ENABLE`, SYNC ↔ `ERA_SPLIT_EEPROM_SYNC_ENABLE`, LINK ↔ split, BOOT ↔
   `ERA_VIA_BOOTLOADER_ENABLE`, EEPROM ↔ `ERA_EEPROM_CLEAN_ENABLE`, Lighting ↔ 조명
   계열(`era_board_adoption.md` 조명 표). 세 보드(TOMAK79H, brick65, divine)에서
   오라클과 일치해야 채택.
2. 채택 시 `era_doc_refs.py`에 `check_via_menus` 추가(H7S `menu` 검사와 같은
   모양): 보드마다 JSON 세트(L/R 포함)의 메뉴 집합 == 기대 집합, 아니면 FAIL.
   `DOCS_SYSTEM.md` §2.2 첫 번째 열린 질문(저장소 간 정합)의 이 저장소 쪽 답이다.
3. 불일치 시: 사상이 결정적이지 않으면 **구현하지 않고** 보고에 스파이크 결과만
   싣는다.

---

## 10. Phase 8 — 검증·반증 검사·보고·후속

### 10.1 검증 명령 (전부 보고에 출력 포함)

```bash
python keyboards/era/common/tools/era_doc_refs.py              # 0건
python hooks/test_era_commit_check.py                          # all tests passed
git diff --check
rg -n -i --hidden --glob '!.git/**' 'graphify|graphifyy' .     # 0건
git ls-files graphify-out .graphifyignore .codex .grok .claude/hooks hooks/era_pretooluse.py | wc -l   # 0
git config core.hooksPath                                      # hooks
git log --oneline ca44e73562..HEAD                             # 커밋 목록
cat keyboards/era/common/docs/era_active_index.md keyboards/era/common/docs/contracts/*.md \
    keyboards/era/common/docs/maps/*.md keyboards/era/common/docs/manuals/*.md | wc -l   # 종료 줄 수 (시작 9,487)
git status --short                                             # 비어 있음
```

우회 방지 증명: 테스트 3번(임시 저장소에서 공백 오류 커밋이 거부됨)의 출력.

### 10.2 반증 검사 (새 진입점만 읽고 답하기 — 각 항목에 걸린 시간을 적는다)

| 질문 | 답이 있는 자리(설계) |
| --- | --- |
| `brick65`가 왜 다른 22종과 다른가, 결함이 아닌 이유 | `AGENTS.md` 함정 절 → `era_active_index.md` Current State → `manuals/era_board_adoption.md` **Copy-To-RAM Policy** |
| copy-to-RAM 이미지가 요구하는 것과 SRAM 예산 하한 | 함정 절(`ERA_BOARD_COMMON_ENABLE=yes` 거절, 32 KiB floor) → `contracts/era_sram_residency_contract.md` |
| split 반쪽 revision을 언제 올리고 언제 안 올리는가 | 함정 절 → `contracts/era_host_peer_storage_contract.md` **Source Revision And Identity** |
| 어느 보드에 어느 VIA 메뉴가 있는지의 정본 | 함정 절 pairs 표: 이 저장소 `keymaps/via/*-VIA.json` 세트 ↔ 앱 `FEATURE_COVERAGE` |
| 앱 저장소와 동시에 바꿔야 하는 것 | 함정 절 pairs 표 (3항목) |
| 문서를 지우기 전에 돌릴 것 | 함정 절 → `era_doc_refs.py --homeless` |

### 10.3 후속 (실행 세션 범위 밖, 보고에 명시)

- **모든 에이전트 세션 재시작**(canon이 바뀌었다). `qmk_firmware_eerraa_nvm2`
  worktree는 옛 canon이므로 그 브랜치에서 작업을 재개하려면 `work/era-nvm`을
  먼저 합친다.
- `release/clean-repo` 재구성(소유자): 트리를 건드렸으므로 `read-tree`/`commit-tree`
  로 다시 만든다(`CLAUDE.md` Push 절). `main` 체크포인트 replay도 소유자 몫.
- 자매 저장소: 앱 `docs/MAP.md` §9와 H7S `AGENTS.md`가 규격 v1을 인용하도록 하는
  것은 그쪽 세션의 일. H7S의 Graphify 퇴역은 이 저장소의 커밋 C 본문을 근거로.
- `.era-artifacts/`를 비웠으므로 다음 소스 캠페인은 before-build를 새로 만든다.

### 10.4 보고 항목 (`DOCS_SYSTEM.md` §6)

§0.1 처리(진입 조건 충족 근거) · 지운 것과 근거 / 남긴 서술과 근거 · 규격의
형태(`eerraa-agent-docs` v1, 검사기 설정 블록) · Graphify 참조·산출물·캐시 제거
확인 · 비관련 호출에 PreToolUse 없음 + commit 검사 자동 실행 증명 · 사실 표와
검사기 중 정본(§부록 D-6 원칙: 검사기가 재계산할 수 있는 수는 검사기, 나머지는
표) · 줄 수 변화 · 반증 검사 결과와 시간 · 검증 명령·출력·git 상태 · 문서-코드
불일치(§8.3)와 앱 불일치.

---

## 11. 커밋 계획 (순서대로)

| # | 제목(한국어, 예) | 포함 | 검증 |
| --- | --- | --- | --- |
| A | `어댑터: Claude·Codex·Grok 프로젝트 PreToolUse 훅 등록 해제` | **완료 `ca44e73562`** | — |
| B | `훅: commit 정적 검사를 pre-commit으로 이전, 호스트별 PreToolUse 게이트 은퇴` | `hooks/pre-commit`, `hooks/era_commit_check.py`, `hooks/test_era_commit_check.py`; 삭제 `hooks/era_pretooluse.py`, `hooks/test_era_pretooluse.py`, `.claude/hooks/`, `.grok/`, `.codex/`; `CLAUDE.md`·`AGENTS.md`·`.claude/rules/era-docs.md`·`era-qmk-fork.md`·`era_source_map.md` 문구 | 테스트 통과, `core.hooksPath` 설정 후 커밋에서 훅 실행 확인 |
| C | `graphify: 파생 그래프·설정·graphify-first 서술 삭제` | `graphify-out/`, `.graphifyignore`, `.gitignore` 블록, `AGENTS.md` Graphify→Navigation, `CLAUDE.md` graphify·install, `.claude/rules/era-docs.md` | `rg graphify` 0건; 세션 재시작 |
| D | `문서: 헤더를 Genre·Canonical for 두 줄로 축소, 검사기가 은퇴 필드의 부재를 검사` | 21편 헤더, `era_doc_refs.py`(HEADERS/FORBIDDEN/FOREIGN_REPOS/설정 블록), `AGENTS.md` genre 문장, index 행 1 | 검사기 0건 |
| H | (별도 저장소) `규격 v1: 네 저장소 공통 에이전트 문서 규약` | `eerraa-agent-docs` | tag v1 |
| E | `canon: 함정 절 신설, 문서 경계·규약 v1 인용` | `AGENTS.md` 함정 절·경계·인용, `CLAUDE.md` WSL 함정 | 검사기 0건; 반증 검사 6문 |
| F1–F13 | `문서: <파일> 밀도 재구성 (<n>→<m>줄)` | 문서 1편씩 | §8.4 |
| I | (선택) `검사기: 보드별 VIA JSON 메뉴 집합을 빌드 기능 집합과 대조` | `era_doc_refs.py` | 26 JSON 통과 |

각 커밋 본문 마지막 줄에 이 계획의 단계 번호를 적는다(예: `DOCS_SYSTEM_PLAN §5`).

---

## 12. 하지 말 것 (`DOCS_SYSTEM.md` §4 + 추가)

- 남의 미커밋 변경을 잃는 것. 진입 조건 미충족이면 시작하지 않는다.
- upstream `docs/` 270개, 보드 `readme.md` 23개, `keyboards/era/` 소스 수정.
  예외는 검사기·훅·`.mk` 아닌 어댑터 스크립트뿐.
- `graphify` 명령 실행. 그래프 archive 생성.
- 검사기가 물고 있는 계약을 문서에서 지우기; `--homeless` 토큰을 승격 없이 지우기.
- 새 문서를 만들면서 옛 문서를 남기기. 총량이 줄지 않으면 실패.
- 규칙의 **이유**를 커밋 메시지로만 남기기(출하 orphan에서는 사라진다).
- 사용자 레벨 설정(`~/.claude`, `~/.grok`, `~/.codex`) 수정.
- `--no-verify`, `git add -A`, `git commit -a`.
- 문서 안의 REFUSED 블록을 "장황하다"는 이유로 줄이기 — 세 줄은 이미 최소다.

---

## 부록 B — `AGENTS.md` 함정 절 초안 (영문; 각 줄을 명령으로 확인한 뒤 넣는다)

```markdown
## What Costs Time If You Do Not Know It

Facts that are hard to discover by reading and expensive to learn by trial.
Each names where the rule lives; none is canonical here.

- **`sirind/brick65` takes none of the ERA layer.** It is atmega32u4 and a
  permanent exception, not a debt: `era_active_index.md` **Current State**,
  `manuals/era_board_adoption.md` **Copy-To-RAM Policy**.
- **The copy-to-RAM image refuses to build without `ERA_BOARD_COMMON_ENABLE=yes`**
  (`system/era_sram_resident_rules.mk`), and the residency gate refuses an image
  with fewer than 32768 bytes of ram0 free (`tools/era_residency_gate.sh`):
  `contracts/era_sram_residency_contract.md`.
- **Both halves run one image and are flashed together**; nothing reads an
  older firmware's stored format: `maps/era_source_map.md` **Stored-Data
  Compatibility**. When a stored-data revision moves and when it must not:
  `contracts/era_host_peer_storage_contract.md` **Source Revision And Identity**.
- **Three things change together with the VIA app (`the-via-eerraa`)**, and no
  check crosses the repositories — the pair is the check:

  | fact | this repository | the app |
  | --- | --- | --- |
  | which board has which VIA menu | `keyboards/era/**/keymaps/via/*-VIA.json` (26 files, one per half) | `the-via-eerraa/tests/era-definition.test.ts` `FEATURE_COVERAGE` |
  | a VIA label (`KKUK`, `Indicator Only`, …) | the 26 JSONs, `docs/user/readme.txt`, `docs/user/readme_split.txt`, the board `readme.md`s | `the-via-eerraa/docs/adr/0003-era-menu-help-ui.md` |
  | the durable-peer revision boundary | `split/era_host_peer_storage.c` | `the-via-eerraa/docs/adr/0001-state-sync-protocol.md` |

- **Before deleting a document line**, run
  `python keyboards/era/common/tools/era_doc_refs.py --homeless`; a homeless
  token is a promotion candidate (**Evidence And Retirement**). The checker
  reads the working tree, not the index: stage whole files.
- **A `git rm` stages immediately**, so a later scoped `git add` still commits
  it — check the first status column before committing. When another agent
  shares the worktree, stage by path, never with `-a` or `-A`.
- **History answers only on the development branch.** `git log -S` finds a
  retired name here and nothing on the shipped orphan:
  `era_active_index.md` **What This Repository Does Not Carry**.
- **`post_rules.mk` runs after the keymap's `rules.mk`**, and `-e X=y` is not
  the way to set an option — edit `keyboards/era/era_build_options.mk`:
  `manuals/era_build_options.md`.
- **A build offered as evidence** runs through `era-build keyboard:keymap` in
  WSL from a synchronized tree — never on Windows, never from a stale tree:
  `manuals/era_build_and_flash.md`, `manuals/era_performance_gates.md`.
- **The commit check is a git hook**, armed per clone by
  `git config core.hooksPath hooks`; a clone without it commits unchecked.
```

확인 명령(예): `git grep -n 'ERA_BOARD_COMMON_ENABLE' keyboards/era/common/system/era_sram_resident_rules.mk`,
`git grep -n 'floor 32768' keyboards/era/common/tools/era_residency_gate.sh`,
`grep -n '^## Source Revision And Identity' keyboards/era/common/docs/contracts/era_host_peer_storage_contract.md`,
`grep -n '^## Stored-Data Compatibility' keyboards/era/common/docs/maps/era_source_map.md`,
`git ls-files 'keyboards/era/**/keymaps/via/*-VIA.json' | wc -l` (26),
`ls /d/Engineering/the-via-eerraa/tests/era-definition.test.ts /d/Engineering/the-via-eerraa/docs/adr/000{1,3}-*.md`.

---

## 부록 C — 커밋 B 파일 골격

`hooks/pre-commit`, `hooks/era_commit_check.py`의 `main()`, 테스트 항목은 §4.1에
있다. 추가 주의:

- Windows Git Bash는 `#!/bin/sh` 훅을 자체 sh로 실행한다. `python`은 PATH의
  Windows Python(이전 PreToolUse 훅과 동일 조건).
- `core.hooksPath`는 저장소 공용 설정이라 링크드 worktree에도 적용된다. WSL 빌드
  트리는 별도 클론이며 커밋하지 않으므로 설정하지 않는다.
- `hooks/` 디렉터리에는 `pre-commit`만 훅으로 인식된다(다른 이름의 `.py`는 git이
  보지 않는다).
- 훅의 stdout(`note:` 줄)은 커밋을 실행한 에이전트의 도구 출력에 그대로 보인다 —
  옛 `additionalContext`의 대체다.

---

## 부록 D — `AGENT_DOCS_CONVENTION.md` 목차 (한국어, `eerraa-agent-docs` v1)

1. **목적과 독자** — 네 저장소(`the-via-eerraa`, `qmk_firmware_eerraa`,
   `eerraa-qmk-h7s-fw`, `eerraa-54lm20-fw`) 사이를 오가는 에이전트가 "이건 어떤
   종류의 문서고, 무엇의 원본이고, 어긋나면 무엇이 잡아 주는가"를 같은 자리에서
   찾는다. 통일이 아니라 **알아볼 수 있음**이 목표.
2. **문서 헤더** — 두 줄 `Genre:`, `Canonical for:`. `Status:`는 값이 변하는
   ADR에서만(`Proposed/Accepted/Superseded`). `Read when:`은 쓰지 않는다(진입
   색인이 소유). 근거: QMK 21편 중 20편 `active`·변종 1; `Read when`과 색인 행이
   같은 사실.
3. **장르 5종과 문장 종류** — contract(무엇이 참이어야 하는가) · map(어디에
   무엇이 있는가) · manual(어떻게 돌리는가) · state(무엇이 남았는가; 캠페인 중에만)
   · entry(진입 체인). 장르가 문장 종류를 결정한다.
4. **디렉터리** — ~20편 초과 시 장르별 디렉터리, 그 이하는 flat + 헤더 선언.
5. **진입 라우터** — `AGENTS.md`(정체·작업 규칙·읽기 정책·함정 절) + 색인. 색인의
   작업 행은 세 열: Change(편집 전 필독) / Locate(조회) / Verify(빌드·캡처·판정
   시). 모든 문서는 색인에서 도달 가능해야 한다.
6. **단일 사실 소유** — 같은 사실을 두 문서에 쓰지 않는다; 한 곳에 두고 가리킨다.
   함수가 무엇을 하는지 말하는 문단은 그 함수가 사는 파일을 댄다. 검사기가
   재계산할 수 있는 수는 검사기가 정본, 나머지는 표.
7. **거절 블록** — 세 줄 `REFUSED / WHY / REOPENS`, 결정이 내려진 자리 옆에.
8. **은퇴** — 대체된 사실은 교체, 닫힌 계획은 삭제. 아카이브 트리 없음. 삭제
   커밋이 지운 내용의 판단을 본문에 보존. 삭제 전 안전망(homeless 토큰) 실행.
9. **검사 카탈로그** — path · citation(`path:line`) · section · header · index ·
   claims(함수→파일) · source comments · constants · homeless · symbol · table ·
   menu · version · number provenance. 최소 필수 4종(path·header·index·citation).
   저장소별 구현 위치 표(QMK `keyboards/era/common/tools/era_doc_refs.py`, H7S
   `tools/era_doc_refs.py`, 앱 `tests/docs-contract.test.ts`, 54lm20 없음).
   검사기 이식: 파일 머리 설정 블록 + `FOREIGN_REPOS` 접두사.
10. **실행 경로** — 커밋마다 자동: `pre-commit`(`core.hooksPath`) 권장; 패키지
    스크립트/CI가 있으면 거기에. 일반 셸·읽기·검색 호출에 훅을 걸지 않는다.
11. **함정 절** — 제목은 "먼저 알아야 손해를 안 보는 것" / "What Costs Time If
    You Do Not Know It". 조사로 알기 어렵고 모르면 시간을 잃는 것만, 포인터형.
12. **금지** — 의무형 지식 그래프, 검색 전 훅, 세션별 생성 컨텍스트. 근거: QMK
    측정치(§5.4의 수치) + 앱 75,000줄 오커밋.
13. **저장소 간 짝 항목** — 표 형식 `사실 | 이쪽 위치 | 저쪽 위치 | 어긋나면
    잡는 것`; 교차 검사는 없으므로 짝 자체가 검사.
14. **채택 표** — 저장소 × 규격 버전 × 선언된 이탈(예: QMK — 장르 디렉터리,
    별도 색인, 영어 산문).

---

부록 A(문서별 밀도 재구성 명세)는 `DOCS_SYSTEM_DENSITY_SPECS.md`.
