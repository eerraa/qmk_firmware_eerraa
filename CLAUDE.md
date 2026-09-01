@AGENTS.md
@keyboards/era/common/docs/era_active_index.md

## Claude Code 어댑터

`AGENTS.md` **에이전트 계층 소유**가 정한 어댑터 층: 메커니즘과 환경만.

위의 import가 `AGENTS.md` **Startup Read Policy**의 메커니즘이다. 시작 체인
전체가 세션 시작 때 결정적으로 로드되고, 세션이 읽는 것을 기억하는 데 기대지
않는다. canon은 바뀌지 않는다. 다른 도구는 정책이 적힌 대로 따른다.

### 읽기 라우팅

- `.claude/rules/*.md`는 Task Read Matrix를 경로 범위 규칙으로 거울한다: 세션이
  어떤 작업 영역의 파일을 건드리면, 맞는 규칙이 로드되고 그 영역의 정본 읽기
  집합을 이름 댄다. 권위는 `era_active_index.md`의 매트릭스다. 규칙은 거기로만
  보낸다. 매트릭스 행은 세 열이며 합치지 않는다 — **Change** / **Locate** /
  **Verify**.
- 커밋 시점 검사는 git pre-commit 훅이다 (`hooks/pre-commit` →
  `hooks/era_commit_check.py`). 클론마다 한 번
  `git config core.hooksPath hooks`(Machine setup)로 무장한다. 에이전트 문서
  층 또는 ERA 주석이 있는 `.c`/`.h`/`.mk`를 건드리는 커밋에서
  `era_doc_refs.py`를 돌리고, 다섯 QMK core 루트 또는 ledger를 건드리는
  커밋에서 fork-ledger 검사를 깨운다. 그 검사가 기계적으로 소유하는 token 표면은
  `manuals/era_qmk_fork_ledger.md`에 있다. fork 검사는 staged index를 pristine
  QMK와 비교한다. 문서 검사는 무시되지 않은 워킹 트리 전체를 재사용하므로
  스테이징하지 않은 차이나 untracked 경로가 있으면 훅이 먼저 거절한다.
  `git diff --cached --check`는 항상 돌고, 문서 줄을 지우면 `--homeless` 안내를
  출력한다. Claude에는 프로젝트 훅이 없다. `.claude/settings.json`은
  `permissions.deny`만 든다.

### Push

`permissions.deny`는 더 이상 `git push`를 나열하지 않는다 (소유자 결정
2026-07-28, 이 저장소가 실제로 원했던 첫 push에서). 그것이 기계화한 규칙은
바뀌지 않았고 `AGENTS.md`가 정본이다: 사용자가 명시적으로 요청하지 않으면 어느
브랜치도 push하지 않는다. 파괴적 복원 deny(`reset --hard`, `checkout --`,
`restore`)는 남는다.

**아래 두 문단은 이 펌웨어가 개발되는 저장소를 서술하며, 공개본 클론에는 두
브랜치가 없다.** 개발 트리의 세션이 필요해서 남겨 두었고, 다른 곳의 세션이
아무것도 가리키지 않는 명령을 돌리지 않도록 범위를 한정한다.

`main`은 정확한 원본 QMK fork point 위에 최종 관심사만 남겨 재구성한 완료
브랜치지 개발 브랜치의 머지 대상이 아니다. 개발 chronology와 추적 근거는
`work/era-nvm`에 남는다. 다음 릴리스도 완료 트리를 관심사별 커밋으로 다시
구성하며, 개발 브랜치를 `main`으로 merge해서 시행착오 히스토리를 되살리지
않는다.

`release/clean-repo`는 이전 clean-release 시도의 reference일 뿐이다. 현재
`work/era-nvm`보다 오래됐을 수 있으므로 `main`으로 승격하거나 팁 트리 동일성을
가정하지 않는다. 원본 fork boundary를 조사할 때만 다른 ancestry·tree 증거와
함께 읽는다.

### Machine setup

- Windows에서는 각 서브모듈 설정에 `core.autocrlf=true`를 두어 서브모듈이
  깨끗이 읽히게 한다. 클론 전에 `git config core.longpaths true`도 둘 만하다.
  다만 **이 트리는 그 필요를 보여 주지 않는다**: 2026-08-18 측정, 서브모듈을
  포함한 완전한 체크아웃에서 가장 긴 경로는 저장소 루트 상대 160자라 Windows
  260에 백 자 여유가 있다. 다시 구하려면
  `find . -path ./.git -prune -o -type f -print | awk '{print length($0)-2}' | sort -rn | head -1`.
  `lib/chibios-contrib/ext/mcux-sdk`를 끌어오는 `--recursive` 클론이 필요할
  법한 경우이고, 그 서브모듈은 여기서 체크아웃되지 않는다.
- `git config core.hooksPath hooks` — 클론마다 한 번, 편집 트리에서. WSL 빌드
  트리는 커밋하지 않으므로 필요 없다.

### WSL2 빌드 환경

펌웨어는 Windows가 아니라 WSL2 Ubuntu에서 빌드한다. 편집은 Windows 체크아웃
`D:\Engineering\qmk_firmware_eerraa`에서 하고, `~/projects/qmk_firmware_eerraa`의
WSL 클론은 빌드 트리뿐이다. 같은 커밋에서 바이트가 같은 UF2가 나오므로 산출물은
서로 바꿔 쓸 수 있다.

**이 절은 기계 하나를 서술하며, 스스로 설치하지 않는다.** 스크립트는
`.claude/tools/era-sync.sh`, `era-build.sh`에 버전된다. `~/bin/era-sync`,
`~/bin/era-build`는 그것으로의 심볼릭 링크다. 링크는 고의로 **편집** 트리의
복사본을 가리킨다 — `era-sync`는 빌드 트리를 리셋하고, 그 트리 안의 링크는
실행 중에 스크립트 자신을 다시 쓰게 된다. 각 파일은 기계별 경로를 맨 위 표시된
블록 하나에 들고, 그 블록과 WSL 설치가 새 기계에 필요한 전부다.

어느 것도 자동으로 설치되지 않는다. 그래서 `era-build`를 찾지 못하거나, WSL이
없거나, 여기 경로가 풀리지 않으면 **멈추고 사용자에게 무엇이 실패했는지
말한다**. `.claude/tools/`를 제자리에 복사하고 표시된 블록을 고치는 것은
사용자가 할 결정이지, 빌드로 가는 길에 밟을 단계가 아니다. 툴체인을 설치하지
말고, 무엇보다 Windows에서 빌드하는 쪽으로 떨어지지 마라 — 런처가 거절하는
실패이고, 즉흥으로 거기에 닿는 것은 같은 틀린 답을 더 천천히 얻는 것이다. 이
규칙이 섬기는 곳은 `era_performance_gates.md`다.

- `qmk` CLI는 자신의 ARM 툴체인을 `~/.local/share/qmk/bin`에 설치하고 주입한다.
  Ubuntu는 `/usr/bin`에 `arm-none-eabi-*` 13.2.1을 따로 싣고, `qmk compile`이
  아닌 모든 것 — 게이트 런처가 하는 `arm-none-eabi-size`/`nm` 호출을 포함해 —
  에서 그것을 가린다. `~/.profile`이 qmk 툴체인을 앞에 둔다.
  `arm-none-eabi-gcc --version`이 15.2.0을 보고하는지로 확인한다.
- `era-sync`가 보장하는 것은 하나다: 돌아오면 빌드 트리가 편집 트리와 같다.
  헤드가 다르면 편집 트리의 커밋으로 수렴하고, 커밋되지 않은 델타를 트리 전체로
  재생한 뒤, 두 트리의 `git status`를 비교해 차이가 있으면 실패한다 — 그
  같음이 검사이지, 파일 수가 아니다. 각각을 어떻게 하는지는
  `.claude/tools/era-sync.sh`에 주석되어 있다. 여기서 되풀이하지 않는다.
  `git remote windows`는 `/mnt/d` 체크아웃을 가리킨다.
- `era-sync`는 `rsync -rlt`로 복사하고, 이는 mtime을 보존하므로 증분 빌드가
  바뀐 소스를 최신이라고 보고할 수 있다. 전/후 비교가 증거일 때는 바뀐 파일을
  `touch`하거나 두 상태 사이에 `.build/obj_*`를 지운다.
- `~/.profile`은
  `ERA_EDIT_TREE=/mnt/d/Engineering/qmk_firmware_eerraa`를 내보낸다.
  게이트 런처는 편집 트리에 대해 낡은 트리의 빌드를 막기 위해 그것을 읽고,
  결과를 매니페스트의 `edit_tree_check=`로 기록한다. 이 규칙의 정본은
  `era_performance_gates.md`다. 경로만 Claude와 이 기계에 고유하다.
- `era-build`는 한 명령으로 루프 전체이며, Windows 쪽에서 돌릴 수 있다.
  `keyboard:keymap` 타깃은 필수라, TOMAK_TKL 요청이 조용히 TOMAK79H를 고를 수
  없다:

  ```powershell
  wsl -d Ubuntu -e bash -lc 'era-build era/sirind/tomak:via'
  wsl -d Ubuntu -e bash -lc 'era-build era/sirind/tomak:via cause'
  wsl -d Ubuntu -e bash -lc 'era-build era/sirind/tomak79h:via standard wire qwin cause'
  ```

  변종을 생략하면 모든 타깃이 `standard`다. 변종 이름은 보드에 묶이지 않고,
  정본 make 층이 타깃이 지원하지 못하는 것을 거절한다. 어댑터는 동기화하고,
  변종마다 게이트 런처를 돌리고, 산출물을 Windows `.era-artifacts/`로 복사하고,
  각 빌드의 `worktree_dirty`, `edit_tree_check`, free-ram0를 펌웨어 이름 옆에
  출력한다. 그 호출의 매니페스트가 선언한 파일만 복사한다. 옛 WSL 산출물은 새
  빌드에 실려 돌아오지 않는다. 동기화는 선택 사항이 아니고 매번 먼저 돈다.
  건너뛰는 것이 이 설정의 유일한 조용한 실패이기 때문이다. 변종 네 개를 끝까지
  돌리는 데 약 25초다. `.claude/tools/era-build.sh`가 이 순서를 구현하고
  `keyboards/era/common/tools/era_qmk_build.sh`는 내부 타깃 런처다. 후자를 직접
  호출하는 것은 거절된다.
- 매니페스트 경로는 `.era-artifacts/`를 glob하지 말고 런처 자신의 `Manifest:`
  줄에서 읽는다. 산출물 이름에 펌웨어 SHA-256의 앞 16자리(16진)가 들어가므로
  다른 dirty 바이너리가 앞선 것을 덮어쓰지 못하지만, glob은 지금 돌아온
  매니페스트가 아니라 이전 실행을 고를 수 있다.
- 플래시는 Windows에 남는다: UF2는 RPI-RP2 대용량 저장 장치로의 파일 복사다.

## 압축 지시

압축할 때 이 순서로 보존한다: 이 작업을 시작한 프롬프트의 결정 블록과 제약;
작업의 정확한 위치(브랜치, 마지막 커밋 해시와 메시지, 스테이징된 것, 남은 것);
아직 조치하지 않은 증거 또는 실패 출력. 저장소 파일의 내용은 보존하지 않는다
— 에이전트 문서 층과 소스는 디스크에서 다시 로드되고, 시작 체인은 자신을 다시
가져온다 — 이미 커밋에 반영된 도구 출력도 보존하지 않는다. 실행 상태를 남기고
다시 읽을 수 있는 내용을 버리는 요약은 잃는 것이 없다.
